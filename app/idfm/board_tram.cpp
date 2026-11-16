#include "board_tram.h"

#include "board_common.h"
#include "board_rer.h" // idfmGlcd()
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

#include <algorithm>
#include <climits>

// Ile-de-France tram stop display (one platform = one direction, next 2 trams), see
// docs/reference/TRAM.md. Same rendering principles as board_metro.cpp: GLCD 5x7 font only
// (x1..x4), CP437 accents via idfmGlcd(), optional full-frame PSRAM sprite, regions pushed for
// animations. Tram specifics: IDFM tram pictograms (white plate between two horizontal bars),
// "Prochain tram" / "Tram suivant", "à l'approche" below one minute.

using namespace idfmui;

namespace {

// ---------------------------------------------------------------------------------------
// Geometry (landscape 320x170)
// ---------------------------------------------------------------------------------------
constexpr int16_t kW = 320;
constexpr int16_t kH = 170;
constexpr int16_t kHeaderH = 40;  // header band 0..39, separator at y=40
constexpr int16_t kRowsY = 41;    // first tram row
constexpr int16_t kRowH = 53;     // 2 rows: 41..93 | separator 94 | 95..147
constexpr uint8_t kRows = TRAM_BOARD_ROWS;
constexpr int16_t kTickerY = 149; // bottom ticker band 149..169 (separator at 148)
constexpr int16_t kTickerH = kH - kTickerY;
constexpr int16_t kMargin = 5;
constexpr int16_t kPictoH = 28;   // height of the mode and line pictograms
constexpr int16_t kPictoY = (kHeaderH - kPictoH) / 2;
constexpr int16_t kBarH = 4;      // horizontal bars of the IDFM tram pictograms

constexpr uint32_t kBlinkMs = 500;      // half period of "à l'approche" blinking
constexpr uint32_t kColonMs = 500;      // half period of the clock colon blinking
constexpr uint32_t kTickerMsPerPx = 20; // 50 px/s
constexpr long kDepartedGraceS = 60;    // hide departures older than this (unless at stop)

// ---------------------------------------------------------------------------------------
// Palette (RGB565), see docs/reference/TRAM.md section 4.
// ---------------------------------------------------------------------------------------
constexpr uint16_t kBg = rgb(0x06, 0x09, 0x0E);       // near-black screen
constexpr uint16_t kHeaderBg = rgb(0x1A, 0x22, 0x2C); // IDFM dark slate (#25303B), darkened
constexpr uint16_t kSeparator = rgb(0x3A, 0x46, 0x56);
constexpr uint16_t kTickerBg = rgb(0x1A, 0x22, 0x2C);
constexpr uint16_t kWhite = rgb(0xFF, 0xFF, 0xFF);
constexpr uint16_t kSlate = rgb(0x25, 0x30, 0x3B);    // IDFM pictogram ink
constexpr uint16_t kAmber = rgb(0xFF, 0xB4, 0x00);    // waiting times (RATP SIEL heritage)
constexpr uint16_t kGrey = rgb(0x9C, 0xA6, 0xB4);     // secondary text
constexpr uint16_t kRed = rgb(0xFF, 0x40, 0x40);      // cancelled
constexpr uint16_t kYellow = rgb(0xFF, 0xD2, 0x00);   // INFO tag

// ---------------------------------------------------------------------------------------
// Pictograms
// ---------------------------------------------------------------------------------------
struct TramColor {
    const char *name;
    uint8_t r, g, b;
};

// Official IDFM colours (referentiel-des-lignes, colourweb_hexa, transportmode = tram).
constexpr TramColor kTramColors[] = {
    {"T1", 0x00, 0x55, 0xC8},  {"T2", 0xA0, 0x00, 0x6E},  {"T3A", 0xFF, 0x5A, 0x00},
    {"T3B", 0x00, 0x64, 0x3C}, {"T4", 0xDC, 0x96, 0x00},  {"T5", 0x64, 0x00, 0x82},
    {"T6", 0xFF, 0x00, 0x00},  {"T7", 0x6E, 0x49, 0x1E},  {"T8", 0x6E, 0x6E, 0x00},
    {"T9", 0x3C, 0x91, 0xDC},  {"T10", 0x6E, 0x6E, 0x00}, {"T11", 0xFF, 0x5A, 0x00},
    {"T12", 0xA5, 0x00, 0x34}, {"T13", 0x8D, 0x65, 0x3D}, {"T14", 0x00, 0xA0, 0x92},
};

// "3a" -> "T3a", "t11" -> "T11": tram line names always start with a capital T.
String tramName(const String &shortName) {
    String n = shortName;
    n.trim();
    if (n.length() == 0) return "T";
    if (n[0] == 't') n.setCharAt(0, 'T');
    if (n[0] != 'T') n = "T" + n;
    return n;
}

uint16_t lineColor(const Line &line) {
    if (line.color != 0 || line.textColor != 0) return line.color;
    String n = tramName(line.shortName);
    n.toUpperCase();
    for (const TramColor &c : kTramColors) {
        if (n == c.name) return rgb(c.r, c.g, c.b);
    }
    return kGrey;
}

// White plate between two rounded bars, like the IDFM tram pictograms (mode and lines).
void drawPlate(TFT_eSPI &g, int32_t x, int32_t y, int32_t w, uint16_t bar, uint16_t surroundBg) {
    g.fillRect(x, y + 2, w, kPictoH - 4, kWhite);
    g.fillSmoothRoundRect(x, y, w, kBarH, 2, bar, surroundBg);
    g.fillSmoothRoundRect(x, y + kPictoH - kBarH, w, kBarH, 2, bar, surroundBg);
}

// Tram mode pictogram: slate bars and a simplified tram silhouette (pantograph, body with
// three windows, rail).
constexpr int16_t kModeW = kPictoH;
void drawModePicto(TFT_eSPI &g, int32_t x, int32_t y, uint16_t surroundBg) {
    drawPlate(g, x, y, kModeW, kSlate, surroundBg);
    g.drawLine(x + 9, y + 11, x + 13, y + 7, kSlate); // pantograph arm
    g.drawLine(x + 10, y + 11, x + 14, y + 7, kSlate);
    g.drawFastHLine(x + 11, y + 6, 7, kSlate);       // pantograph head
    g.fillRoundRect(x + 4, y + 11, 20, 8, 3, kSlate); // body, rounded front
    for (int i = 0; i < 3; i++) g.fillRect(x + 7 + i * 5, y + 13, 3, 3, kWhite); // windows
    g.fillRect(x + 3, y + 20, 22, 2, kSlate);         // rail
}

// Line pictogram: line name in slate on the white plate, bars in the line colour.
int32_t linePictoW(const Line &line) {
    String n = idfmGlcd(tramName(line.shortName));
    if (n.length() > 3) n = n.substring(0, 3);
    return std::max<int32_t>(kPictoH, textW(n, 2) + 9);
}

void drawLinePicto(TFT_eSPI &g, int32_t x, int32_t y, const Line &line, uint16_t surroundBg) {
    String n = idfmGlcd(tramName(line.shortName));
    if (n.length() > 3) n = n.substring(0, 3);
    const int32_t w = linePictoW(line);
    drawPlate(g, x, y, w, lineColor(line), surroundBg);
    const int32_t tw = textW(n, 2) + 1; // fake bold adds one column
    const int32_t tx = x + (w - tw) / 2;
    const int32_t ty = y + (kPictoH - 14) / 2;
    drawText(g, n, tx, ty, 2, kSlate);
    drawText(g, n, tx + 1, ty, 2, kSlate);
}

// ---------------------------------------------------------------------------------------
// Waiting time rules
// ---------------------------------------------------------------------------------------
enum class WaitKind : uint8_t { Minutes, Clock, AtStop, Approaching, Cancelled, Unknown, Departed };

WaitKind computeWait(const Departure &d, time_t now, long &minutes, time_t &when) {
    when = d.expected ? d.expected : d.aimed;
    minutes = 0;
    if (d.status == DepartureStatus::Cancelled) return WaitKind::Cancelled;
    if (d.status == DepartureStatus::AtStop) return WaitKind::AtStop;
    if (d.status == DepartureStatus::Approaching) return WaitKind::Approaching;
    if (when == 0) return WaitKind::Unknown;
    const long diff = (long)(when - now);
    if (diff < -kDepartedGraceS) return WaitKind::Departed;
    if (diff < 60) return WaitKind::Approaching; // RATP tram/bus: "à l'approche" instead of 0 min
    if (diff / 60 >= 60) return WaitKind::Clock;
    minutes = diff / 60;
    return WaitKind::Minutes;
}

struct TimeLayout {
    String big;   // main text
    String line2; // second line (two-line labels), may be empty
    String unit;  // "min" after a size-4 number, may be empty
    uint8_t size = 2;
    uint16_t color = kAmber;
    bool blink = false;
    int16_t w = 0;
};

TimeLayout timeLayout(const Departure &d, time_t now) {
    TimeLayout t;
    long minutes;
    time_t when;
    switch (computeWait(d, now, minutes, when)) {
        case WaitKind::Minutes:
            t.big = String(minutes);
            t.unit = "min";
            t.size = 4;
            break;
        case WaitKind::Clock:
            t.big = clockText(when);
            t.size = 3;
            break;
        case WaitKind::AtStop:
            t.big = idfmGlcd("à quai");
            t.size = 3;
            break;
        case WaitKind::Approaching:
            t.big = idfmGlcd("à l'");
            t.line2 = "approche";
            t.blink = true;
            break;
        case WaitKind::Cancelled:
            t.big = idfmGlcd("Supprimé");
            t.color = kRed;
            break;
        case WaitKind::Unknown:
        case WaitKind::Departed:
            t.big = "--";
            t.size = 4;
            break;
    }
    if (t.unit.length()) {
        t.w = textW(t.big, t.size) + 5 + textW(t.unit, 2);
    } else {
        t.w = std::max(textW(t.big, t.size), textW(t.line2, t.size));
    }
    return t;
}

// ---------------------------------------------------------------------------------------
// State kept between draw and tick
// ---------------------------------------------------------------------------------------
struct RowSlot {
    bool used = false;
    Departure dep;
    int16_t y = 0;
};

RowSlot gRows[kRows];
tft_sprite *gFrame = nullptr; // full-frame sprite (PSRAM)
bool gFrameTried = false;
tft_sprite *gTickerSpr = nullptr; // ticker-only sprite, used when there is no frame sprite
bool gTickerSprTried = false;
String gTickerText; // GLCD-encoded
bool gTickerTag = false;
uint32_t gTickerStartMs = 0;
int32_t gTickerLastX = INT32_MIN;
int8_t gBlinkPhase = -1;
int8_t gColonPhase = -1;
String gClock; // "HH:MM" currently drawn
long gDrawnMinute = -1;
uint8_t gScroll = 0;

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

bool blinkOnNow() { return ((millis() / kBlinkMs) & 1) == 0; }
bool colonOnNow() { return ((millis() / kColonMs) & 1) == 0; }

// ---------------------------------------------------------------------------------------
// Screen parts
// ---------------------------------------------------------------------------------------
constexpr int16_t kClockW = 5 * 12 - 2; // "HH:MM" at size 2
constexpr int16_t kClockX = kW - kMargin - kClockW;
constexpr int16_t kClockY = 4;

void drawClock(TFT_eSPI &g, bool colonOn) {
    g.fillRect(kClockX - 1, kClockY - 1, kClockW + 2, 16, kHeaderBg);
    String c = gClock;
    if (!colonOn && c.length() == 5) c.setCharAt(2, ' ');
    drawText(g, c, kClockX, kClockY, 2, kWhite);
}

// fitText() without a doubled dot when the cut lands after an initial ("G. Pompidou").
String fitName(const String &s, int32_t maxW, uint8_t size) {
    String r = fitText(s, maxW, size);
    if (r.endsWith("..")) r.remove(r.length() - 1);
    return r;
}

// Split `s` into two lines of at most `maxW` px each, breaking after a space or a hyphen
// (the hyphen stays on the first line). Line 2 is shortened with fitText() if needed.
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
    b = fitName(b, maxW, size);
    return b.length() > 0;
}

void drawHeader(TFT_eSPI &g, const BoardData &data, time_t now) {
    g.fillRect(0, 0, kW, kHeaderH, kHeaderBg);
    drawModePicto(g, kMargin, kPictoY, kHeaderBg);
    const int32_t lineX = kMargin + kModeW + 4;
    drawLinePicto(g, lineX, kPictoY, data.line, kHeaderBg);
    const int32_t titleX = lineX + linePictoW(data.line) + 8;

    gClock = clockText(now);
    drawClock(g, colonOnNow());

    // "Direction" (small, next to the clock) above the terminus name, which may use the
    // whole width under the clock line.
    const int32_t maxW = kW - kMargin - titleX;
    String dir = idfmGlcd(data.stop.direction);
    const bool hasDir = dir.length() > 0;
    if (!hasDir) dir = idfmGlcd(data.stop.name); // no direction: show the stop
    String abbr = abbreviate(dir);
    const int dash = abbr.indexOf(" - ");
    if (dash > 0 && textW(abbr, 2) > maxW && textW(abbr.substring(0, dash), 2) <= maxW) {
        abbr = abbr.substring(0, dash); // main part of "Name - complement"
    }
    if (hasDir) drawText(g, "Direction", titleX, 7, 1, kGrey);
    if (textW(dir, 2) <= maxW) {
        drawText(g, dir, titleX, 21, 2, kWhite);
    } else if (textW(abbr, 2) <= maxW) {
        drawText(g, abbr, titleX, 21, 2, kWhite);
    } else if (textW(abbr, 1) <= maxW) {
        drawText(g, abbr, titleX, 25, 1, kWhite);
    } else {
        String a, b;
        if (splitTwoLines(abbr, maxW, 1, a, b)) {
            drawText(g, a, titleX, 20, 1, kWhite);
            drawText(g, b, titleX, 30, 1, kWhite);
        } else {
            drawText(g, fitName(abbr, maxW, 1), titleX, 25, 1, kWhite);
        }
    }
    g.drawFastHLine(0, kHeaderH, kW, kSeparator);
}

void drawTimeArea(TFT_eSPI &g, const RowSlot &row, const TimeLayout &t, bool blinkOn) {
    const int16_t x0 = kW - kMargin - t.w;
    g.fillRect(x0 - 2, row.y + 1, t.w + 4, kRowH - 2, kBg);
    if (t.blink && !blinkOn) return;

    if (t.unit.length()) {
        const int16_t by = row.y + (kRowH - 28) / 2; // size-4 glyph body: 28 px
        drawText(g, t.big, x0, by, t.size, t.color);
        drawText(g, t.unit, x0 + textW(t.big, t.size) + 5, by + 14, 2, t.color); // same baseline
    } else if (t.line2.length()) {
        drawText(g, t.big, x0, row.y + 9, t.size, t.color);
        drawText(g, t.line2, x0, row.y + 28, t.size, t.color);
    } else {
        const int16_t h = 7 * t.size;
        drawText(g, t.big, kW - kMargin - textW(t.big, t.size), row.y + (kRowH - h) / 2, t.size, t.color);
    }
}

String rankLabel(size_t idx) {
    if (idx == 0) return "Prochain tram";
    if (idx == 1) return "Tram suivant";
    return String(idx + 1) + "e tram";
}

void drawRow(TFT_eSPI &g, const RowSlot &row, size_t idx, time_t now, bool blinkOn) {
    const Departure &d = row.dep;
    g.fillRect(0, row.y, kW, kRowH, kBg);

    TimeLayout t = timeLayout(d, now);
    drawTimeArea(g, row, t, blinkOn);

    const int16_t maxW = kW - kMargin - t.w - 10 - kMargin;
    const uint16_t color = d.status == DepartureStatus::Cancelled ? kGrey : kWhite;
    // Terminus of this tram: differs from the platform direction for short workings.
    String dest = idfmGlcd(d.destination.length() ? d.destination : String("Terminus"));
    String abbr = abbreviate(dest);
    // Long PRIM names carry a " - " complement ("Pont du Garigliano - Hôpital Européen
    // G. Pompidou", "Porte de Versailles - Parc des Expositions"): keep the main part only.
    const int dash = dest.indexOf(" - ");
    if (dash > 0 && textW(dest, 2) > maxW && textW(abbr, 2) > maxW) {
        dest = dest.substring(0, dash);
        abbr = abbreviate(dest);
    }
    String a, b;
    if (textW(dest, 2) <= maxW || textW(abbr, 2) <= maxW) {
        drawText(g, rankLabel(idx), kMargin, row.y + 9, 1, kGrey);
        drawText(g, textW(dest, 2) <= maxW ? dest : abbr, kMargin, row.y + 22, 2, color);
    } else if (splitTwoLines(abbr, maxW, 2, a, b)) {
        drawText(g, a, kMargin, row.y + 9, 2, color);
        drawText(g, b, kMargin, row.y + 28, 2, color);
    } else {
        drawText(g, rankLabel(idx), kMargin, row.y + 9, 1, kGrey);
        drawText(g, fitName(dest, maxW, 2), kMargin, row.y + 22, 2, color);
    }
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

// True between the end and the restart of service (Europe/Paris), roughly 01:00 - 05:00.
bool outOfServiceHours(time_t now) {
    struct tm tmLocal;
    if (now < 1600000000 || !primLocalTime(now, tmLocal)) return false;
    const int m = tmLocal.tm_hour * 60 + tmLocal.tm_min;
    return m >= 60 && m < 300;
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
        raw = "Tramway " + tramName(data.line.shortName);
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
        drawText(g, "INFO", 3 + (42 - textW("INFO", 1)) / 2, oy + 6, 1, kSlate);
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

void updateBlink(time_t now, bool force) {
    TFT_eSPI &g = surface();
    const bool colon = colonOnNow();
    if (force || (colon ? 1 : 0) != gColonPhase) {
        gColonPhase = colon ? 1 : 0;
        drawClock(g, colon);
        flushRegion(kClockX - 1, kClockY - 1, kClockW + 2, 16);
    }

    const bool on = blinkOnNow();
    const int8_t phase = on ? 1 : 0;
    if (!force && phase == gBlinkPhase) return;
    gBlinkPhase = phase;
    for (uint8_t i = 0; i < kRows; i++) {
        const RowSlot &row = gRows[i];
        if (!row.used) continue;
        TimeLayout t = timeLayout(row.dep, now);
        if (!t.blink) continue;
        drawTimeArea(g, row, t, on);
        flushRegion(kW - kMargin - t.w - 2, row.y + 1, t.w + 4, kRowH - 2);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------
void tramBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset) {
    ensureFrame();
    TFT_eSPI &g = surface();

    drawHeader(g, data, now);
    g.fillRect(0, kRowsY, kW, kTickerY - kRowsY, kBg);

    std::vector<const Departure *> visible;
    visible.reserve(data.departures.size());
    for (const Departure &d : data.departures) {
        long m;
        time_t w;
        if (computeWait(d, now, m, w) != WaitKind::Departed) visible.push_back(&d);
    }
    const size_t maxScroll = visible.size() > kRows ? visible.size() - kRows : 0;
    gScroll = (uint8_t)std::min<size_t>(scrollOffset, maxScroll);

    const bool blinkOn = blinkOnNow();
    for (uint8_t i = 0; i < kRows; i++) {
        RowSlot &row = gRows[i];
        row.used = false;
        const size_t idx = gScroll + i;
        if (idx >= visible.size()) continue;
        row.used = true;
        row.dep = *visible[idx];
        row.y = kRowsY + i * (kRowH + 1);
        drawRow(g, row, idx, now, blinkOn);
    }
    if (visible.size() > 1) g.drawFastHLine(kMargin, kRowsY + kRowH, kW - 2 * kMargin, kSeparator);

    if (visible.empty()) {
        if (data.error.length()) {
            drawCenterMessage(g, "Information indisponible", data.error);
        } else if (data.fetchedAt == 0) {
            drawCenterMessage(g, "Chargement…", "Prochains passages en cours de récupération");
        } else if (outOfServiceHours(now)) {
            drawCenterMessage(g, "Service terminé", "Pas de tram avant la reprise du service");
        } else {
            drawCenterMessage(g, "Aucun tram annoncé", "Pas de passage prévu pour le moment");
        }
    }
    g.drawFastHLine(0, kTickerY - 1, kW, kSeparator);

    buildTicker(data, !visible.empty());
    if (gFrame) {
        renderTicker(g, kTickerY, true);
        gFrame->nativeSprite()->pushSprite(0, 0);
    } else {
        updateTicker(true);
    }

    gBlinkPhase = blinkOn ? 1 : 0;
    gColonPhase = colonOnNow() ? 1 : 0;
    gDrawnMinute = (long)(now / 60);
}

void tramBoardTick(const BoardData &data, time_t now) {
    if ((long)(now / 60) != gDrawnMinute) {
        tramBoardDraw(data, now, gScroll); // waiting times and clock changed
        return;
    }
    updateBlink(now, false);
    updateTicker(false);
}

void tramBoardRelease() {
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
    for (RowSlot &row : gRows) row = RowSlot();
    gTickerText = String();
    gTickerLastX = INT32_MIN;
    gBlinkPhase = -1;
    gColonPhase = -1;
    gClock = String();
    gDrawnMinute = -1;
    gScroll = 0;
}

// ---------------------------------------------------------------------------------------
// Sample data and demo
// ---------------------------------------------------------------------------------------
static time_t tramDemoNow() {
    time_t t = primNow();
    if (t) return t;
    return (time_t)1767258000 + (time_t)(millis() / 1000UL); // 2026-01-01 09:00 UTC
}

// Demo scenes (encoder scrolls the trams, then moves on to the next scene):
//   0. T3a, Porte d'Italie -> Pont du Garigliano (sample data: varied statuses)
//   1. T11 (3-character name), at the platform
//   2. T1, long direction and per-tram destinations (short workings)
//   3. T6: fetch error without data
//   4. T13: no tram announced / service ended
static BoardData tramDemoScene(int scene, time_t t0) {
    BoardData data;
    data.line.mode = TransportMode::Tram;
    data.line.color = 0; // official fallback colours
    data.line.textColor = 0;
    data.fetchedAt = t0;

    auto add = [&](const char *dest, long inS, DepartureStatus status) {
        Departure d;
        d.destination = dest;
        d.aimed = inS == LONG_MIN ? 0 : t0 + inS;
        d.expected = d.aimed;
        d.status = status;
        data.departures.push_back(d);
    };

    switch (scene) {
        case 0:
            data.line.id = "C01391";
            data.line.shortName = "T3a";
            data.stop.name = "Porte d'Italie";
            data.stop.direction = "Pont du Garigliano";
            add("Pont du Garigliano", 30, DepartureStatus::Approaching);
            add("Pont du Garigliano", 250, DepartureStatus::OnTime);
            add("Porte de Versailles - Parc des Expositions", 610, DepartureStatus::Delayed);
            add("Pont du Garigliano", 900, DepartureStatus::Cancelled);
            add("Pont du Garigliano", 1260, DepartureStatus::OnTime);
            add("Pont du Garigliano", LONG_MIN, DepartureStatus::Unknown);
            add("Pont du Garigliano", 3900, DepartureStatus::OnTime);
            data.messages.push_back("T3a : travaux entre Stade Charléty et Porte d'Ivry, "
                                    "trafic interrompu le soir à partir de 22h.");
            break;
        case 1:
            data.line.id = "C01999";
            data.line.shortName = "T11";
            data.stop.name = "Épinay-sur-Seine";
            data.stop.direction = "Le Bourget";
            add("Le Bourget", 0, DepartureStatus::AtStop);
            add("Le Bourget", 540, DepartureStatus::OnTime);
            break;
        case 2:
            data.line.id = "C01389";
            data.line.shortName = "T1";
            data.stop.name = "Saint-Denis Basilique";
            data.stop.direction = "Asnières-Gennevilliers Les Courtilles";
            add("Gare de Saint-Denis", 70, DepartureStatus::OnTime);
            add("Asnières-Gennevilliers Les Courtilles", 330, DepartureStatus::OnTime);
            add("Asnières-Gennevilliers Les Courtilles", 720, DepartureStatus::OnTime);
            break;
        case 3:
            data.line.id = "C01794";
            data.line.shortName = "T6";
            data.stop.name = "Vélizy 2";
            data.stop.direction = "Viroflay-Rive-Droite";
            data.fetchedAt = 0;
            data.error = "Erreur réseau (HTTP 503)";
            break;
        default:
            data.line.id = "C02344";
            data.line.shortName = "T13";
            data.stop.name = "Saint-Germain-en-Laye";
            data.stop.direction = "Saint-Cyr-l'École";
            break;
    }
    return data;
}

void tramBoardSampleData(BoardData &out) { out = tramDemoScene(0, tramDemoNow()); }

void tramBoardDemo() {
    primEnsureTime(); // real clock when Wi-Fi is up (no-op otherwise)
    constexpr int kScenes = 5;
    int scene = 0;
    BoardData data = tramDemoScene(scene, tramDemoNow());

    uint8_t scroll = 0;
    tramBoardDraw(data, tramDemoNow(), scroll);

    // Ignore the click that launched the demo.
    const uint32_t start = millis();
    while (millis() - start < 300) {
        check(AnyKeyPress);
        delay(10);
    }

    while (true) {
        if (check(NextPress)) {
            if (gScroll + kRows < data.departures.size()) {
                scroll = gScroll + 1;
            } else {
                scene = (scene + 1) % kScenes;
                data = tramDemoScene(scene, tramDemoNow());
                scroll = 0;
            }
            tramBoardDraw(data, tramDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(PrevPress)) {
            if (gScroll > 0) {
                scroll = gScroll - 1;
            } else {
                scene = (scene + kScenes - 1) % kScenes;
                data = tramDemoScene(scene, tramDemoNow());
                scroll = 0;
            }
            tramBoardDraw(data, tramDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(AnyKeyPress)) {
            // An encoder step may land between the checks above: handle it next round.
            if (!NextPress && !PrevPress) break;
        }
        tramBoardTick(data, tramDemoNow());
        delay(15);
    }

    tramBoardRelease();
    tft.fillScreen(bruceConfig.bgColor);
}
