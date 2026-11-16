#include "board_bus.h"

#include "board_common.h"
#include "board_rer.h" // idfmGlcd()
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

#include <algorithm>
#include <ctype.h>
#include <string.h>

// Bus stop / shelter display (every line of the stop, one row per line + destination with the
// next 2 waiting times), see docs/reference/BUS.md. Same rendering principles as board_metro.cpp:
// GLCD 5x7 font only (x1..x4), CP437 accents via idfmGlcd(), optional full-frame PSRAM sprite,
// regions pushed for animations.
//
// Scroll semantics: scrollOffset is the index of the first ROW (line + destination group, or
// line status row) shown, clamped to [0, rows - BUS_BOARD_ROWS]. The app bounds its counter
// with departures.size() (>= number of rows), so the counter may run past the last row: the
// renderer clamps, extra encoder steps at the end of the list have no visible effect.

using namespace idfmui;

namespace {

// ---------------------------------------------------------------------------------------
// Geometry (landscape 320x170)
// ---------------------------------------------------------------------------------------
constexpr int16_t kW = 320;
constexpr int16_t kH = 170;
constexpr int16_t kHeaderH = 30;  // header band 0..29, separator at y=30
constexpr int16_t kRowsY = 31;    // first row
constexpr int16_t kRowH = 29;     // 4 rows: 31..146 (separator on the last px of each row)
constexpr uint8_t kRows = BUS_BOARD_ROWS;
constexpr int16_t kTickerY = 149; // bottom ticker band 149..169 (separator at 148)
constexpr int16_t kTickerH = kH - kTickerY;
constexpr int16_t kMargin = 5;

constexpr int16_t kPictoW = 40; // line pictogram: rectangle in the line colour
constexpr int16_t kPictoH = 21;
constexpr int16_t kDestX = kMargin + kPictoW + 6;
constexpr int16_t kColW = 50;                        // width of a waiting time column
constexpr int16_t kCol2R = kW - kMargin;             // right edge of the 2nd time
constexpr int16_t kCol1R = kCol2R - kColW - 6;       // right edge of the 1st time
constexpr int16_t kDestMaxW = kCol1R - kColW - 8 - kDestX;

constexpr uint32_t kColonMs = 500;      // half period of the clock colon blinking
constexpr uint32_t kTickerMsPerPx = 20; // 50 px/s
constexpr long kDepartedGraceS = 60;    // hide departures older than this (unless at stop)

// ---------------------------------------------------------------------------------------
// Palette (RGB565), see docs/reference/BUS.md section 4 (same family as the metro board).
// ---------------------------------------------------------------------------------------
constexpr uint16_t kBg = rgb(0x05, 0x08, 0x12);
constexpr uint16_t kHeaderBg = rgb(0x0E, 0x16, 0x30);
constexpr uint16_t kSeparator = rgb(0x2A, 0x36, 0x5C);
constexpr uint16_t kRowSep = rgb(0x1A, 0x22, 0x3C);
constexpr uint16_t kTickerBg = rgb(0x0E, 0x16, 0x30);
constexpr uint16_t kWhite = rgb(0xFF, 0xFF, 0xFF);
constexpr uint16_t kAmber = rgb(0xFF, 0xB4, 0x00);
constexpr uint16_t kGrey = rgb(0x9A, 0xA4, 0xBC);
constexpr uint16_t kRed = rgb(0xFF, 0x40, 0x40);
constexpr uint16_t kYellow = rgb(0xFF, 0xD2, 0x00);
constexpr uint16_t kNavy = rgb(0x0A, 0x16, 0x4A);
constexpr uint16_t kNeutralBg = rgb(0xC8, 0xCD, 0xD7); // unknown line colour
constexpr uint16_t kNeutralFg = rgb(0x0A, 0x16, 0x4A);

// ---------------------------------------------------------------------------------------
// Waiting time rules
// ---------------------------------------------------------------------------------------
enum class WaitKind : uint8_t { Minutes, Clock, AtStop, Approaching, Cancelled, Unknown, Departed };

time_t whenOf(const Departure &d) { return d.expected ? d.expected : d.aimed; }

WaitKind computeWait(const Departure &d, time_t now, long &minutes, time_t &when) {
    when = whenOf(d);
    minutes = 0;
    if (d.status == DepartureStatus::Cancelled) {
        if (when && (long)(when - now) < -kDepartedGraceS) return WaitKind::Departed;
        return WaitKind::Cancelled;
    }
    if (d.status == DepartureStatus::AtStop) return WaitKind::AtStop;
    if (d.status == DepartureStatus::Approaching) return WaitKind::Approaching;
    if (when == 0) return WaitKind::Unknown;
    const long diff = (long)(when - now);
    if (diff < -kDepartedGraceS) return WaitKind::Departed;
    if (diff < 60) return WaitKind::Approaching; // SIEL bus: "A l'approche" instead of "0 mn"
    if (diff / 60 >= 60) return WaitKind::Clock;
    minutes = diff / 60;
    return WaitKind::Minutes;
}

// ---------------------------------------------------------------------------------------
// Rows: one per line + destination (next 2 times), or one per line without any departure
// ---------------------------------------------------------------------------------------
struct BusRow {
    Line line;           // display info (name, colours; colours 0/0 = unknown)
    String destination;  // UTF-8, empty for a status row
    const Departure *dep[2] = {nullptr, nullptr};
    time_t first = 0;    // sort key within the line
    bool status = false; // line known but no departure: "Service terminé"...
};

// Natural order of line names: numbers by value first ("38" < "72" < "391"), then names
// starting with letters ("N11" < "N16" < "TVM"), by prefix then number.
int lineCompare(const String &a, const String &b) {
    auto split = [](const String &s, String &prefix, long &num, String &rest) {
        int i = 0;
        while (i < (int)s.length() && !isdigit((unsigned char)s[i])) i++;
        prefix = s.substring(0, i);
        int j = i;
        while (j < (int)s.length() && isdigit((unsigned char)s[j])) j++;
        num = j > i ? s.substring(i, j).toInt() : -1;
        rest = s.substring(j);
    };
    String pa, pb, ra, rb;
    long na, nb;
    split(a, pa, na, ra);
    split(b, pb, nb, rb);
    if (pa != pb) {
        if (pa.length() == 0) return -1; // plain numbers first
        if (pb.length() == 0) return 1;
        return strcmp(pa.c_str(), pb.c_str());
    }
    if (na != nb) return na < nb ? -1 : 1;
    return strcmp(ra.c_str(), rb.c_str());
}

bool findLine(const BoardData &data, const Departure &d, Line &out) {
    for (const Line &l : data.lines) {
        if (d.lineRef.length() && l.id == d.lineRef) {
            out = l;
            return true;
        }
    }
    if (d.lineRef.length() && data.line.id == d.lineRef) {
        out = data.line;
        return true;
    }
    // Fallback: PRIM public name, neutral style.
    out = Line();
    out.id = d.lineRef;
    out.shortName = d.lineName.length() ? d.lineName : String("?");
    out.color = 0;
    out.textColor = 0;
    out.mode = TransportMode::Bus;
    return false;
}

String lineKey(const Line &l) { return l.id.length() ? l.id : l.shortName; }

bool isNightLine(const Line &l) { return l.shortName.length() > 1 && (l.shortName[0] == 'N' || l.shortName[0] == 'n') && isdigit((unsigned char)l.shortName[1]); }

// Local minutes since midnight (Europe/Paris), -1 when unknown.
int localMinutes(time_t now) {
    struct tm tmLocal;
    if (now < 1600000000 || !primLocalTime(now, tmLocal)) return -1;
    return tmLocal.tm_hour * 60 + tmLocal.tm_min;
}

// Label of a line with no departure announced (SIEL bus wording).
String statusLabel(const Line &l, time_t now) {
    const int m = localMinutes(now);
    if (m < 0) return "Pas de service";
    if (isNightLine(l)) {
        if (m >= 330 && m < 720) return "Service terminé"; // Noctilien, morning
        if (m >= 720 && m < 1230) return "Service non commencé";
        return "Pas de service";
    }
    if (m >= 1230 || m < 330) return "Service terminé"; // day line, evening / night
    return "Pas de service";
}

void buildRows(const BoardData &data, time_t now, std::vector<BusRow> &rows) {
    rows.clear();
    std::vector<const Departure *> visible;
    for (const Departure &d : data.departures) {
        long m;
        time_t w;
        if (computeWait(d, now, m, w) != WaitKind::Departed) visible.push_back(&d);
    }
    std::stable_sort(visible.begin(), visible.end(), [](const Departure *a, const Departure *b) {
        const time_t wa = whenOf(*a), wb = whenOf(*b);
        if (wa == 0 || wb == 0) return wa != 0 && wb == 0;
        return wa < wb;
    });

    for (const Departure *d : visible) {
        Line l;
        findLine(data, *d, l);
        const String key = lineKey(l);
        BusRow *row = nullptr;
        for (BusRow &r : rows) {
            if (lineKey(r.line) == key && r.destination == d->destination) {
                row = &r;
                break;
            }
        }
        if (!row) {
            rows.push_back(BusRow());
            row = &rows.back();
            row->line = l;
            row->destination = d->destination;
            row->first = whenOf(*d);
        }
        if (!row->dep[0]) row->dep[0] = d;
        else if (!row->dep[1]) row->dep[1] = d;
    }

    // Lines known to serve the stop (seen in an earlier fetch) but without any departure now.
    if (data.fetchedAt) {
        auto addStatus = [&](const Line &l) {
            if (lineKey(l).length() == 0) return;
            for (const BusRow &r : rows)
                if (lineKey(r.line) == lineKey(l)) return;
            BusRow r;
            r.line = l;
            r.status = true;
            rows.push_back(r);
        };
        for (const Line &l : data.lines) addStatus(l);
        if (data.line.mode == TransportMode::Bus) addStatus(data.line);
    }

    std::stable_sort(rows.begin(), rows.end(), [](const BusRow &a, const BusRow &b) {
        const int c = lineCompare(a.line.shortName, b.line.shortName);
        if (c != 0) return c < 0;
        if (a.status != b.status) return !a.status;
        if (a.first == 0 || b.first == 0) return a.first != 0 && b.first == 0;
        return a.first < b.first;
    });
}

// ---------------------------------------------------------------------------------------
// State kept between draw and tick
// ---------------------------------------------------------------------------------------
tft_sprite *gFrame = nullptr; // full-frame sprite (PSRAM)
bool gFrameTried = false;
tft_sprite *gTickerSpr = nullptr; // ticker-only sprite, used when there is no frame sprite
bool gTickerSprTried = false;
String gTickerText; // GLCD-encoded
bool gTickerTag = false;
uint32_t gTickerStartMs = 0;
int32_t gTickerLastX = INT32_MIN;
int8_t gColonPhase = -1;
String gClock; // "HH:MM" currently drawn
long gDrawnMinute = -1;
uint8_t gScroll = 0;
size_t gRowCount = 0;

void ensureFrame() {
    if (gFrame || gFrameTried) return;
    gFrameTried = true;
#if defined(BOARD_HAS_PSRAM)
    if (!psramFound()) return;
    gFrame = new tft_sprite(&tft);
    gFrame->setColorDepth(16);
    if (gFrame->createSprite(kW, kH) == nullptr) {
        delete gFrame;
        gFrame = nullptr;
    }
#endif
}

TFT_eSPI &surface() {
    if (gFrame) return *gFrame->nativeSprite();
    return *tft.native();
}

void flushRegion(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (gFrame) gFrame->nativeSprite()->pushSprite(x, y, x, y, w, h);
}

bool colonOnNow() { return ((millis() / kColonMs) & 1) == 0; }

// ---------------------------------------------------------------------------------------
// Screen parts
// ---------------------------------------------------------------------------------------
constexpr int16_t kClockW = 5 * 12 - 2; // "HH:MM" at size 2
constexpr int16_t kClockX = kW - kMargin - kClockW;
constexpr int16_t kClockY = (kHeaderH - 14) / 2;
constexpr int16_t kModeW = 34; // "BUS" mode pictogram
constexpr int16_t kTitleX = kMargin + kModeW + 7;

void drawClock(TFT_eSPI &g, bool colonOn) {
    g.fillRect(kClockX - 1, kClockY - 1, kClockW + 2, 16, kHeaderBg);
    String c = gClock;
    if (!colonOn && c.length() == 5) c.setCharAt(2, ' ');
    drawText(g, c, kClockX, kClockY, 2, kWhite);
}

// Split `s` into two lines of at most `maxW` px each, breaking after a space or a hyphen.
bool splitTwoLines(const String &s, int32_t maxW, uint8_t size, String &a, String &b) {
    int best = -1;
    for (int i = 0; i < (int)s.length(); i++) {
        if (s[i] != ' ' && s[i] != '-') continue;
        if (textW(s.substring(0, i + (s[i] == '-' ? 1 : 0)), size) > maxW) break;
        best = i;
    }
    if (best <= 0) return false;
    a = s.substring(0, best + (s[best] == '-' ? 1 : 0));
    b = s.substring(best + 1);
    a.trim();
    b.trim();
    b = fitText(b, maxW, size);
    return b.length() > 0;
}

void drawHeader(TFT_eSPI &g, const BoardData &data, time_t now) {
    g.fillRect(0, 0, kW, kHeaderH, kHeaderBg);
    // Bus mode pictogram: white rounded square with "BUS" (stands for the IDFM bus symbol).
    const int16_t py = (kHeaderH - 22) / 2;
    g.fillSmoothRoundRect(kMargin, py, kModeW, 22, 5, kWhite, kHeaderBg);
    drawText(g, "BUS", kMargin + (kModeW - textW("BUS", 1)) / 2, py + 7, 1, kNavy);
    drawText(g, "BUS", kMargin + (kModeW - textW("BUS", 1)) / 2 + 1, py + 7, 1, kNavy);

    gClock = clockText(now);
    drawClock(g, colonOnNow());

    const int32_t maxW = kClockX - 8 - kTitleX;
    String name = idfmGlcd(data.stop.name.length() ? data.stop.name : String("Arrêt de bus"));
    String dir = idfmGlcd(data.stop.direction);
    String sub;
    if (dir.length()) sub = fitText(idfmGlcd("Direction ") + dir, maxW, 1);
    if (sub.length() == 0) {
        if (textW(name, 2) <= maxW || textW(abbreviate(name), 2) <= maxW) {
            drawText(g, fitText(name, maxW, 2), kTitleX, kClockY, 2, kWhite);
        } else {
            String a, b;
            if (splitTwoLines(abbreviate(name), maxW, 1, a, b)) {
                drawText(g, a, kTitleX, 5, 1, kWhite);
                drawText(g, b, kTitleX, 16, 1, kWhite);
            } else {
                drawText(g, fitText(name, maxW, 1), kTitleX, 11, 1, kWhite);
            }
        }
    } else {
        // Stop name (x2 when it fits, else x1) above the chosen side of the street (x1 grey).
        if (textW(name, 2) <= maxW || textW(abbreviate(name), 2) <= maxW) {
            drawText(g, fitText(name, maxW, 2), kTitleX, 2, 2, kWhite);
        } else {
            drawText(g, fitText(name, maxW, 1), kTitleX, 6, 1, kWhite);
        }
        drawText(g, sub, kTitleX, 19, 1, kGrey);
    }
    g.drawFastHLine(0, kHeaderH, kW, kSeparator);
}

// Line pictogram: rectangle in the line colour, line number centred (x2, else x1).
void drawLinePicto(TFT_eSPI &g, int32_t x, int32_t y, const Line &line) {
    uint16_t bg = line.color, fg = line.textColor;
    if (bg == 0 && fg == 0) {
        bg = kNeutralBg;
        fg = kNeutralFg;
    } else if (bg == fg) {
        fg = bg == kWhite ? rgb(0, 0, 0) : kWhite;
    }
    g.fillSmoothRoundRect(x, y, kPictoW, kPictoH, 3, bg, kBg);
    String n = idfmGlcd(line.shortName);
    if (n.length() == 0) n = "?";
    if (textW(n, 2) <= kPictoW - 4) {
        drawText(g, n, x + (kPictoW - textW(n, 2)) / 2, y + (kPictoH - 14) / 2, 2, fg);
    } else {
        n = fitText(n, kPictoW - 2, 1);
        drawText(g, n, x + (kPictoW - textW(n, 1)) / 2, y + (kPictoH - 7) / 2, 1, fg);
    }
}

// One waiting time, right-aligned on `right`, vertically centred in the row.
void drawWait(TFT_eSPI &g, const Departure &d, time_t now, int16_t right, int16_t y) {
    long minutes;
    time_t when;
    String a, b; // labels, up to 2 lines of size 1
    uint16_t color = kAmber;
    switch (computeWait(d, now, minutes, when)) {
        case WaitKind::Minutes: {
            const String num = String(minutes);
            const int16_t w = textW(num, 2) + 2 + textW("min", 1);
            const int16_t x = right - w;
            const int16_t by = y + (kRowH - 1 - 14) / 2;
            drawText(g, num, x, by, 2, kAmber);
            drawText(g, "min", x + textW(num, 2) + 2, by + 7, 1, kAmber); // same baseline
            return;
        }
        case WaitKind::Clock: {
            const String c = clockText(when);
            drawText(g, c, right - textW(c, 1), y + (kRowH - 1 - 7) / 2, 1, kAmber);
            return;
        }
        case WaitKind::Unknown: {
            drawText(g, "--", right - textW("--", 2), y + (kRowH - 1 - 14) / 2, 2, kAmber);
            return;
        }
        case WaitKind::AtStop:
            a = idfmGlcd("à");
            b = idfmGlcd("l'arrêt");
            break;
        case WaitKind::Approaching:
            a = idfmGlcd("à l'");
            b = "approche";
            break;
        case WaitKind::Cancelled:
        case WaitKind::Departed:
            a = idfmGlcd("Supprimé");
            color = kRed;
            break;
    }
    if (b.length()) {
        const int16_t x = right - std::max(textW(a, 1), textW(b, 1));
        drawText(g, a, x, y + 5, 1, color);
        drawText(g, b, x, y + 15, 1, color);
    } else {
        drawText(g, a, right - textW(a, 1), y + (kRowH - 1 - 7) / 2, 1, color);
    }
}

String destText(const BusRow &row) {
    return idfmGlcd(row.destination.length() ? row.destination : String("Terminus"));
}

// Destinations use x2 only when every destination of the page fits in x2 (uniform typography).
bool destBigFits(const BusRow &row) {
    if (row.status) return true;
    const String dest = destText(row);
    return textW(dest, 2) <= kDestMaxW || textW(abbreviate(dest), 2) <= kDestMaxW;
}

void drawRow(TFT_eSPI &g, const BusRow &row, int16_t y, time_t now, bool lastVisible, bool big) {
    g.fillRect(0, y, kW, kRowH, kBg);
    drawLinePicto(g, kMargin, y + (kRowH - 1 - kPictoH) / 2, row.line);

    if (row.status) {
        String s = idfmGlcd(statusLabel(row.line, now));
        const int32_t maxW = kCol2R - kDestX;
        if (big && textW(s, 2) <= maxW) drawText(g, s, kDestX, y + (kRowH - 1 - 14) / 2, 2, kGrey);
        else drawText(g, fitText(s, maxW, 1), kDestX, y + (kRowH - 1 - 7) / 2, 1, kGrey);
    } else {
        // Without a 2nd time, a small destination may use the 1st time column too.
        const int32_t maxW = row.dep[1] ? kDestMaxW : kCol2R - kColW - 8 - kDestX;
        const bool allCancelled = row.dep[0]->status == DepartureStatus::Cancelled &&
                                  (!row.dep[1] || row.dep[1]->status == DepartureStatus::Cancelled);
        const uint16_t color = allCancelled ? kGrey : kWhite;
        String dest = destText(row);
        String abbr = abbreviate(dest);
        String a, b;
        if (big && textW(dest, 2) <= kDestMaxW) {
            drawText(g, dest, kDestX, y + (kRowH - 1 - 14) / 2, 2, color);
        } else if (big && textW(abbr, 2) <= kDestMaxW) {
            drawText(g, abbr, kDestX, y + (kRowH - 1 - 14) / 2, 2, color);
        } else if (textW(dest, 1) <= maxW) {
            drawText(g, dest, kDestX, y + (kRowH - 1 - 7) / 2, 1, color);
        } else if (splitTwoLines(abbr, maxW, 1, a, b)) {
            drawText(g, a, kDestX, y + 5, 1, color);
            drawText(g, b, kDestX, y + 15, 1, color);
        } else {
            drawText(g, fitText(dest, maxW, 1), kDestX, y + (kRowH - 1 - 7) / 2, 1, color);
        }
        if (row.dep[1]) {
            drawWait(g, *row.dep[0], now, kCol1R, y);
            drawWait(g, *row.dep[1], now, kCol2R, y);
        } else {
            drawWait(g, *row.dep[0], now, kCol2R, y);
        }
    }
    if (!lastVisible) g.drawFastHLine(kMargin, y + kRowH - 1, kW - 2 * kMargin, kRowSep);
}

void drawScrollBar(TFT_eSPI &g, size_t total, size_t first) {
    if (total <= kRows) return;
    const int16_t h = kRows * kRowH - 2;
    const int16_t barH = std::max<int16_t>(8, h * kRows / total);
    const int16_t barY = kRowsY + 1 + (int32_t)(h - barH) * first / (total - kRows);
    g.drawFastVLine(kW - 2, kRowsY + 1, h, kRowSep);
    g.fillRect(kW - 2, barY, 2, barH, kGrey);
}

void drawCenterMessage(TFT_eSPI &g, const String &title, const String &detail) {
    const int16_t bandH = kTickerY - 1 - kRowsY;
    String t = fitText(idfmGlcd(title), kW - 2 * kMargin, 2);
    drawText(g, t, (kW - textW(t, 2)) / 2, kRowsY + bandH / 2 - 16, 2, kWhite);
    if (detail.length()) {
        String d = fitText(idfmGlcd(detail), kW - 2 * kMargin, 1);
        drawText(g, d, (kW - textW(d, 1)) / 2, kRowsY + bandH / 2 + 8, 1, kGrey);
    }
}

void buildTicker(const BoardData &data, bool showingRows) {
    String raw;
    bool tag = false;
    const String sep = "  \xE2\x80\xA2  "; // UTF-8 bullet, mapped to GLCD glyph 7
    if (!data.messages.empty()) {
        for (size_t i = 0; i < data.messages.size(); i++) {
            if (i) raw += sep;
            raw += data.messages[i];
        }
        tag = true;
    }
    if (data.error.length() && showingRows) {
        if (raw.length()) raw += sep;
        raw += "Informations non actualisées : " + data.error;
        tag = true;
    }
    if (raw.length() == 0) {
        raw = "Bus";
        if (data.stop.name.length()) raw += sep + data.stop.name;
        if (data.fetchedAt) raw += sep + "Mis à jour à " + clockText(data.fetchedAt);
    }
    String text = idfmGlcd(raw);
    if (text != gTickerText || tag != gTickerTag) {
        gTickerText = text;
        gTickerTag = tag;
        gTickerStartMs = millis();
        gTickerLastX = INT32_MIN;
    }
}

bool renderTicker(TFT_eSPI &g, int16_t oy, bool force) {
    const int16_t textX0 = gTickerTag ? 50 : kMargin;
    const int32_t tw = textW(gTickerText, 2);
    int32_t x;
    if (tw <= kW - textX0 - kMargin) {
        x = textX0; // fits: static
    } else {
        const uint32_t px = (millis() - gTickerStartMs) / kTickerMsPerPx;
        const uint32_t cycle = (uint32_t)tw + (uint32_t)(kW - textX0);
        x = kW - (int32_t)(px % cycle);
    }
    if (!force && x == gTickerLastX) return false;
    gTickerLastX = x;

    g.fillRect(0, oy, kW, kTickerH, kTickerBg);
    drawText(g, gTickerText, x, oy + 3, 2, kWhite);
    if (gTickerTag) {
        g.fillRect(0, oy, textX0 - 2, kTickerH, kTickerBg); // mask text scrolled under the tag
        g.fillRoundRect(3, oy + 3, 42, 14, 3, kYellow);
        drawText(g, "INFO", 3 + (42 - textW("INFO", 1)) / 2, oy + 6, 1, kNavy);
    }
    return true;
}

void updateTicker(bool force) {
    if (gFrame) {
        if (renderTicker(*gFrame->nativeSprite(), kTickerY, force)) flushRegion(0, kTickerY, kW, kTickerH);
        return;
    }
    if (!gTickerSpr && !gTickerSprTried) {
        gTickerSprTried = true;
        gTickerSpr = new tft_sprite(&tft); // 320x21x16bit = 13 KB
        gTickerSpr->setColorDepth(16);
        if (gTickerSpr->createSprite(kW, kTickerH) == nullptr) {
            delete gTickerSpr;
            gTickerSpr = nullptr;
        }
    }
    if (gTickerSpr) {
        if (renderTicker(*gTickerSpr->nativeSprite(), 0, force)) gTickerSpr->nativeSprite()->pushSprite(0, kTickerY);
    } else {
        renderTicker(*tft.native(), kTickerY, force); // flickers, last resort
    }
}

void updateColon(bool force) {
    const bool colon = colonOnNow();
    if (!force && (colon ? 1 : 0) == gColonPhase) return;
    gColonPhase = colon ? 1 : 0;
    drawClock(surface(), colon);
    flushRegion(kClockX - 1, kClockY - 1, kClockW + 2, 16);
}

} // namespace

// ---------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------
void busBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset) {
    ensureFrame();
    TFT_eSPI &g = surface();

    drawHeader(g, data, now);
    g.fillRect(0, kRowsY, kW, kTickerY - kRowsY, kBg);

    std::vector<BusRow> rows;
    buildRows(data, now, rows);
    gRowCount = rows.size();
    const size_t maxScroll = rows.size() > kRows ? rows.size() - kRows : 0;
    gScroll = (uint8_t)std::min<size_t>(scrollOffset, maxScroll);

    const bool anyDeparture = std::any_of(rows.begin(), rows.end(), [](const BusRow &r) { return !r.status; });
    if (anyDeparture || (!rows.empty() && data.error.length() == 0)) {
        const size_t shown = std::min<size_t>(kRows, rows.size() - gScroll);
        bool big = true;
        for (size_t i = 0; i < shown; i++) big = big && destBigFits(rows[gScroll + i]);
        for (size_t i = 0; i < shown; i++) {
            drawRow(g, rows[gScroll + i], kRowsY + i * kRowH, now, i + 1 == shown, big);
        }
        drawScrollBar(g, rows.size(), gScroll);
    } else if (data.error.length()) {
        drawCenterMessage(g, "Information indisponible", data.error);
    } else if (data.fetchedAt == 0) {
        drawCenterMessage(g, "Chargement…", "Prochains passages en cours de récupération");
    } else {
        const int m = localMinutes(now);
        if (m >= 0 && (m >= 90 && m < 330)) {
            drawCenterMessage(g, "Service terminé", "Aucun bus avant la reprise du service");
        } else {
            drawCenterMessage(g, "Aucun bus annoncé", "Pas de passage prévu pour le moment");
        }
    }
    g.drawFastHLine(0, kTickerY - 1, kW, kSeparator);

    buildTicker(data, anyDeparture);
    if (gFrame) {
        renderTicker(g, kTickerY, true);
        gFrame->nativeSprite()->pushSprite(0, 0);
    } else {
        updateTicker(true);
    }

    gColonPhase = colonOnNow() ? 1 : 0;
    gDrawnMinute = (long)(now / 60);
}

size_t busBoardRowCount() { return gRowCount; }

void busBoardTick(const BoardData &data, time_t now) {
    if ((long)(now / 60) != gDrawnMinute) {
        busBoardDraw(data, now, gScroll); // waiting times and clock changed
        return;
    }
    updateColon(false);
    updateTicker(false);
}

void busBoardRelease() {
    if (gFrame) {
        gFrame->deleteSprite();
        delete gFrame;
        gFrame = nullptr;
    }
    if (gTickerSpr) {
        gTickerSpr->deleteSprite();
        delete gTickerSpr;
        gTickerSpr = nullptr;
    }
    gFrameTried = false;
    gTickerSprTried = false;
    gTickerText = String();
    gTickerLastX = INT32_MIN;
    gColonPhase = -1;
    gClock = String();
    gDrawnMinute = -1;
    gScroll = 0;
    gRowCount = 0;
}

// ---------------------------------------------------------------------------------------
// Sample data and demo
// ---------------------------------------------------------------------------------------
static time_t busDemoNow() {
    time_t t = primNow();
    if (t) return t;
    return (time_t)1767258000 + (time_t)(millis() / 1000UL); // 2026-01-01 09:00 UTC
}

static Line busLine(const char *id, const char *name, uint16_t bg, uint16_t fg) {
    Line l;
    l.id = id;
    l.shortName = name;
    l.color = bg;
    l.textColor = fg;
    l.mode = TransportMode::Bus;
    return l;
}

static void addBus(BoardData &data, time_t t0, const char *lineRef, const char *lineName, const char *dest,
                   long inS, DepartureStatus status = DepartureStatus::OnTime) {
    Departure d;
    d.lineRef = lineRef;
    d.lineName = lineName;
    d.destination = dest;
    d.aimed = t0 + inS;
    d.expected = t0 + inS;
    d.status = status;
    data.departures.push_back(d);
}

static void sortDepartures(BoardData &data) {
    std::stable_sort(data.departures.begin(), data.departures.end(),
                     [](const Departure &a, const Departure &b) { return whenOf(a) < whenOf(b); });
}

// Hôtel de Ville (Paris), several lines. Colours: IDFM referentiel-des-lignes (colourweb_hexa).
// Line 75 is listed without colours (referential miss) and line 47 is absent from `lines`
// (fallback on the PRIM public name, neutral style).
static void busSampleAt(BoardData &out, time_t t0) {
    out = BoardData();
    out.line = busLine("C01107", "72", rgb(0xFF, 0x14, 0x00), rgb(0xFF, 0xFF, 0xFF));
    out.stop.name = "Hôtel de Ville";
    out.stop.direction = "Parc de Saint-Cloud";
    out.fetchedAt = t0;
    out.lines.push_back(out.line);
    out.lines.push_back(busLine("C01083", "38", rgb(0x00, 0x55, 0xC8), rgb(0xFF, 0xFF, 0xFF)));
    out.lines.push_back(busLine("C01103", "67", rgb(0xFF, 0x82, 0xB4), rgb(0, 0, 0)));
    out.lines.push_back(busLine("C01105", "69", rgb(0x82, 0xC8, 0xE6), rgb(0, 0, 0)));
    out.lines.push_back(busLine("C01110", "75", 0, 0));
    out.lines.push_back(busLine("C01127", "96", rgb(0xDC, 0x96, 0x00), rgb(0, 0, 0)));
    out.lines.push_back(busLine("C01415", "N11", rgb(0xFF, 0x5A, 0x00), rgb(0xFF, 0xFF, 0xFF)));

    addBus(out, t0, "C01107", "72", "Parc de Saint-Cloud", 40);
    addBus(out, t0, "C01107", "72", "Parc de Saint-Cloud", 11 * 60 + 20);
    addBus(out, t0, "C01107", "72", "Porte de Saint-Cloud", 6 * 60 + 5);
    addBus(out, t0, "C01083", "38", "Porte d'Orléans", 3 * 60 + 30);
    addBus(out, t0, "C01083", "38", "Porte d'Orléans", 9 * 60 + 50);
    addBus(out, t0, "C01088", "47", "Fort du Kremlin-Bicêtre", 7 * 60 + 15);
    addBus(out, t0, "C01088", "47", "Fort du Kremlin-Bicêtre", 19 * 60 + 40);
    addBus(out, t0, "C01103", "67", "Porte de Gentilly", 0, DepartureStatus::AtStop);
    addBus(out, t0, "C01103", "67", "Porte de Gentilly", 14 * 60 + 10);
    addBus(out, t0, "C01105", "69", "Champ de Mars", 2 * 60 + 45);
    addBus(out, t0, "C01105", "69", "Champ de Mars", 16 * 60, DepartureStatus::Cancelled);
    addBus(out, t0, "C01110", "75", "Pont Neuf", 12 * 60 + 30);
    addBus(out, t0, "C01127", "96", "Gare Montparnasse", 5 * 60 + 10);
    addBus(out, t0, "C01127", "96", "Gare Montparnasse", 24 * 60 + 5);
    addBus(out, t0, "C01127", "96", "Gare Montparnasse", 38 * 60);
    // N11 has no departure by day: shown as a status row ("Pas de service"...).
    sortDepartures(out);
    out.messages.push_back("Ligne 72 : arrêt Pont Neuf non desservi jusqu'au 30/11 en raison de travaux.");
}

void busBoardSampleData(BoardData &out) { busSampleAt(out, busDemoNow()); }

// Demo scenes (encoder scrolls the rows, then moves on to the next scene):
//   0. Hôtel de Ville sample (8 lines, fallbacks, at stop / approaching / cancelled)
//   1. suburban stop: long destinations, 3/4-char line names, clock times, unknown time
//   2. fetch error without data
//   3. stop with no departure at all
static BoardData busDemoScene(int scene, time_t t0) {
    BoardData data;
    if (scene == 0) {
        busSampleAt(data, t0);
        return data;
    }
    data.fetchedAt = t0;
    if (scene == 1) {
        data.line = busLine("C01534", "391", rgb(0x82, 0xDC, 0x73), rgb(0, 0, 0));
        data.stop.name = "Gare de Bourg-la-Reine - Place Condorcet";
        data.lines.push_back(data.line);
        data.lines.push_back(busLine("C01390", "TVM", rgb(0xA0, 0x00, 0x6E), rgb(0xFF, 0xFF, 0xFF)));
        data.lines.push_back(busLine("C01422", "N151", rgb(0x00, 0x55, 0xC8), rgb(0xFF, 0xFF, 0xFF)));
        addBus(data, t0, "C01534", "391", "Massy - Palaiseau RER", 4 * 60 + 2);
        addBus(data, t0, "C01534", "391", "Massy - Palaiseau RER", 75 * 60);
        addBus(data, t0, "C01390", "TVM", "Saint-Maur - Créteil RER", 8 * 60);
        addBus(data, t0, "C01390", "TVM", "Saint-Maur - Créteil RER", 0, DepartureStatus::Unknown);
        data.departures.back().aimed = 0;
        data.departures.back().expected = 0;
        addBus(data, t0, "C01999", "", "Sceaux - Les Blagis - Robinson RER", 9 * 60 + 30);
        addBus(data, t0, "C01999", "", "Sceaux - Les Blagis - Robinson RER", 33 * 60);
        addBus(data, t0, "C01192", "192", "Antony - Pont de Rungis - Aéroport d'Orly", 13 * 60);
        sortDepartures(data);
    } else if (scene == 2) {
        data.line = busLine("C01107", "72", rgb(0xFF, 0x14, 0x00), rgb(0xFF, 0xFF, 0xFF));
        data.stop.name = "Hôtel de Ville";
        data.fetchedAt = 0;
        data.error = "Erreur réseau (HTTP 503)";
    } else {
        data.line = busLine("C01415", "N11", rgb(0xFF, 0x5A, 0x00), rgb(0xFF, 0xFF, 0xFF));
        data.line.mode = TransportMode::Metro; // no status row: exercise the empty-board message
        data.stop.name = "Châtelet";
    }
    return data;
}

void busBoardDemo() {
    primEnsureTime(); // real clock when Wi-Fi is up (no-op otherwise)
    constexpr int kScenes = 4;
    int scene = 0;
    BoardData data = busDemoScene(scene, busDemoNow());

    uint8_t scroll = 0;
    busBoardDraw(data, busDemoNow(), scroll);

    // Ignore the click that launched the demo.
    const uint32_t start = millis();
    while (millis() - start < 300) {
        check(AnyKeyPress);
        delay(10);
    }

    while (true) {
        if (check(NextPress)) {
            if (gScroll + kRows < gRowCount) {
                scroll = gScroll + 1;
            } else {
                scene = (scene + 1) % kScenes;
                data = busDemoScene(scene, busDemoNow());
                scroll = 0;
            }
            busBoardDraw(data, busDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(PrevPress)) {
            if (gScroll > 0) {
                scroll = gScroll - 1;
            } else {
                scene = (scene + kScenes - 1) % kScenes;
                data = busDemoScene(scene, busDemoNow());
                scroll = 0;
            }
            busBoardDraw(data, busDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(AnyKeyPress)) {
            // An encoder step may land between the checks above: handle it next round.
            if (!NextPress && !PrevPress) break;
        }
        busBoardTick(data, busDemoNow());
        delay(15);
    }

    busBoardRelease();
    tft.fillScreen(bruceConfig.bgColor);
}
