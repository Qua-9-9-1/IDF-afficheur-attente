#include "board_metro.h"

#include "board_common.h"
#include "board_rer.h" // idfmGlcd()
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

#include <algorithm>

// Paris metro platform display ("Direction X", next 2 trains), see docs/reference/METRO.md.
// Same rendering principles as board_rer.cpp: GLCD 5x7 font only (x1..x4), CP437 accents via
// idfmGlcd(), optional full-frame PSRAM sprite, regions pushed for animations.

using namespace idfmui;

namespace {

// ---------------------------------------------------------------------------------------
// Geometry (landscape 320x170)
// ---------------------------------------------------------------------------------------
constexpr int16_t kW = 320;
constexpr int16_t kH = 170;
constexpr int16_t kHeaderH = 36;  // header band 0..35, separator at y=36
constexpr int16_t kRowsY = 37;    // first train row
constexpr int16_t kRowH = 55;     // 2 rows: 37..91 | separator 92 | 93..147
constexpr uint8_t kRows = 2;      // the real screens show the next 2 trains
constexpr int16_t kTickerY = 149; // bottom ticker band 149..169 (separator at 148)
constexpr int16_t kTickerH = kH - kTickerY;
constexpr int16_t kMargin = 5;
constexpr int16_t kPictoD = 28;   // diameter of the "M" and line roundels
constexpr int16_t kTitleX = kMargin + 2 * kPictoD + 4 + 8;

constexpr uint32_t kBlinkMs = 500;      // half period of "à l'approche" blinking
constexpr uint32_t kColonMs = 500;      // half period of the clock colon blinking
constexpr uint32_t kTickerMsPerPx = 20; // 50 px/s
constexpr long kDepartedGraceS = 60;    // hide departures older than this (unless at stop)

// ---------------------------------------------------------------------------------------
// Palette (RGB565), see docs/reference/METRO.md section 4.
// ---------------------------------------------------------------------------------------
constexpr uint16_t kBg = rgb(0x05, 0x08, 0x12);       // near-black screen
constexpr uint16_t kHeaderBg = rgb(0x0E, 0x16, 0x30); // dark navy header
constexpr uint16_t kSeparator = rgb(0x2A, 0x36, 0x5C);
constexpr uint16_t kTickerBg = rgb(0x0E, 0x16, 0x30);
constexpr uint16_t kWhite = rgb(0xFF, 0xFF, 0xFF);
constexpr uint16_t kAmber = rgb(0xFF, 0xB4, 0x00);   // waiting times (SIEL heritage)
constexpr uint16_t kGrey = rgb(0x9A, 0xA4, 0xBC);    // secondary text
constexpr uint16_t kRed = rgb(0xFF, 0x40, 0x40);     // cancelled
constexpr uint16_t kYellow = rgb(0xFF, 0xD2, 0x00);  // INFO tag
constexpr uint16_t kNavy = rgb(0x0A, 0x16, 0x4A);    // text on white pictograms

// ---------------------------------------------------------------------------------------
// Line pictogram
// ---------------------------------------------------------------------------------------
struct MetroColor {
    const char *name;
    uint8_t r, g, b;
    bool darkText;
};

// Official IDFM colours (referentiel-des-lignes, colourweb_hexa / textcolourweb_hexa).
constexpr MetroColor kMetroColors[] = {
    {"1", 0xFF, 0xBE, 0x00, true},  {"2", 0x00, 0x55, 0xC8, false}, {"3", 0x6E, 0x6E, 0x00, false},
    {"3B", 0x82, 0xC8, 0xE6, true}, {"4", 0xA0, 0x00, 0x6E, false}, {"5", 0xFF, 0x5A, 0x00, true},
    {"6", 0x82, 0xDC, 0x73, true},  {"7", 0xFF, 0x82, 0xB4, true},  {"7B", 0x82, 0xDC, 0x73, true},
    {"8", 0xD2, 0x82, 0xBE, true},  {"9", 0xD2, 0xD2, 0x00, true},  {"10", 0xDC, 0x96, 0x00, true},
    {"11", 0x6E, 0x49, 0x1E, false}, {"12", 0x00, 0x64, 0x3C, false}, {"13", 0x82, 0xC8, 0xE6, true},
    {"14", 0x64, 0x00, 0x82, false}, {"15", 0xAA, 0xAA, 0xAA, true}, {"18", 0x00, 0xA0, 0x92, false},
};

void lineColors(const Line &line, uint16_t &bg, uint16_t &fg) {
    if (line.color != 0 || line.textColor != 0) {
        bg = line.color;
        fg = line.textColor;
        return;
    }
    String n = line.shortName;
    n.toUpperCase();
    if (n.endsWith("BIS")) n = n.substring(0, n.length() - 3) + "B";
    for (const MetroColor &c : kMetroColors) {
        if (n == c.name) {
            bg = rgb(c.r, c.g, c.b);
            fg = c.darkText ? rgb(0, 0, 0) : kWhite;
            return;
        }
    }
    bg = kGrey;
    fg = rgb(0, 0, 0);
}

// Metro mode roundel ("M") + line roundel, each kPictoD wide, 4 px apart.
void drawMetroPictogram(TFT_eSPI &g, int32_t x, int32_t y, const Line &line, uint16_t surroundBg) {
    const int32_t r = kPictoD / 2;
    // Circles as fully rounded squares (fillSmoothRoundRect: same anti-aliasing as the RER board).
    g.fillSmoothRoundRect(x, y, kPictoD, kPictoD, r, kWhite, surroundBg);
    drawText(g, "M", x + r - 6, y + r - 7, 2, kNavy);
    drawText(g, "M", x + r - 5, y + r - 7, 2, kNavy); // fake bold

    uint16_t bg, fg;
    lineColors(line, bg, fg);
    const int32_t lx = x + kPictoD + 4;
    g.fillSmoothRoundRect(lx, y, kPictoD, kPictoD, r, bg, surroundBg);
    String num = idfmGlcd(line.shortName);
    if (num.length() > 2) num = num.substring(0, 2);
    if (num.length() == 0) num = "?";
    const int32_t w = textW(num, 2);
    drawText(g, num, lx + r - w / 2, y + r - 7, 2, fg);
    if (num.length() == 1) drawText(g, num, lx + r - w / 2 + 1, y + r - 7, 2, fg); // bold when room
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
    if (diff < 0) return WaitKind::Minutes; // "0 min" while the train is due
    if (diff / 60 >= 60) return WaitKind::Clock;
    minutes = diff / 60; // floor, like the SIEL ("00" = train due)
    return WaitKind::Minutes;
}

struct TimeLayout {
    String big;      // main text
    String line2;    // second line (two-line labels), may be empty
    String unit;     // "min" after a size-4 number, may be empty
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
            t.big = "--"; // SIEL shows "--" when the waiting time is unknown
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
constexpr int16_t kClockY = 11;

// Clock with a blinking colon (recent SIEL versions). Drawn as two halves so the digits stay
// put when the colon is hidden.
void drawClock(TFT_eSPI &g, bool colonOn) {
    g.fillRect(kClockX - 1, kClockY - 1, kClockW + 2, 18, kHeaderBg);
    String c = gClock;
    if (!colonOn && c.length() == 5) c.setCharAt(2, ' ');
    drawText(g, c, kClockX, kClockY, 2, kWhite);
}

// Split `s` into two lines of at most `maxW` px each, breaking after a space or a hyphen
// (the hyphen stays on the first line). Picks the latest break that keeps line 1 fitting;
// line 2 is shortened with fitText() if needed.
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
    b = fitText(b, maxW, size); // second line truncated if still too long
    return b.length() > 0;
}

void drawHeader(TFT_eSPI &g, const BoardData &data, time_t now) {
    g.fillRect(0, 0, kW, kHeaderH, kHeaderBg);
    drawMetroPictogram(g, kMargin, (kHeaderH - kPictoD) / 2, data.line, kHeaderBg);

    gClock = clockText(now);
    drawClock(g, colonOnNow());

    // "Direction" (small) above the terminus name (large), like the enamelled plate.
    const int32_t maxW = kClockX - 8 - kTitleX;
    String dir = idfmGlcd(data.stop.direction);
    if (dir.length() == 0) dir = idfmGlcd(data.stop.name); // no direction: show the station
    String abbr = abbreviate(dir);
    if (data.stop.direction.length()) drawText(g, "Direction", kTitleX, 4, 1, kGrey);
    if (textW(dir, 2) <= maxW) {
        drawText(g, dir, kTitleX, 15, 2, kWhite);
    } else if (textW(abbr, 2) <= maxW) {
        drawText(g, abbr, kTitleX, 15, 2, kWhite);
    } else if (textW(dir, 1) <= maxW) {
        drawText(g, dir, kTitleX, 19, 1, kWhite);
    } else {
        // Two lines of size 1 under "Direction" (e.g. both branches of line 13).
        String a, b;
        if (splitTwoLines(abbr, maxW, 1, a, b)) {
            drawText(g, a, kTitleX, 15, 1, kWhite);
            drawText(g, b, kTitleX, 25, 1, kWhite);
        } else {
            drawText(g, fitText(abbr, maxW, 1), kTitleX, 19, 1, kWhite);
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
        drawText(g, t.big, x0, row.y + 10, t.size, t.color);
        drawText(g, t.line2, x0, row.y + 29, t.size, t.color);
    } else {
        const int16_t h = 7 * t.size;
        drawText(g, t.big, kW - kMargin - textW(t.big, t.size), row.y + (kRowH - h) / 2, t.size, t.color);
    }
}

String rankLabel(size_t idx) {
    if (idx == 0) return "Prochain train";
    if (idx == 1) return "Train suivant";
    return String(idx + 1) + "e train";
}

void drawRow(TFT_eSPI &g, const RowSlot &row, size_t idx, time_t now, bool blinkOn) {
    const Departure &d = row.dep;
    g.fillRect(0, row.y, kW, kRowH, kBg);

    TimeLayout t = timeLayout(d, now);
    drawTimeArea(g, row, t, blinkOn);

    const int16_t maxW = kW - kMargin - t.w - 10 - kMargin;
    const uint16_t color = d.status == DepartureStatus::Cancelled ? kGrey : kWhite;
    // Terminus of this train: differs from the platform direction on branched lines (7, 13).
    String dest = idfmGlcd(d.destination.length() ? d.destination : String("Terminus"));
    String abbr = abbreviate(dest);
    String a, b;
    if (textW(dest, 2) <= maxW || textW(abbr, 2) <= maxW) {
        drawText(g, rankLabel(idx), kMargin, row.y + 10, 1, kGrey);
        drawText(g, textW(dest, 2) <= maxW ? dest : abbr, kMargin, row.y + 23, 2, color);
    } else if (splitTwoLines(abbr, maxW, 2, a, b)) {
        // Long terminus: two lines of size 2 instead of the rank label.
        drawText(g, a, kMargin, row.y + 10, 2, color);
        drawText(g, b, kMargin, row.y + 29, 2, color);
    } else {
        drawText(g, rankLabel(idx), kMargin, row.y + 10, 1, kGrey);
        drawText(g, fitText(dest, maxW, 2), kMargin, row.y + 23, 2, color);
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

// True between the end and the restart of service (Europe/Paris), roughly 01:15 - 05:30.
bool outOfServiceHours(time_t now) {
    struct tm tmLocal;
    if (now < 1600000000 || !primLocalTime(now, tmLocal)) return false;
    const int m = tmLocal.tm_hour * 60 + tmLocal.tm_min;
    return m >= 75 && m < 330;
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
        raw = "Métro " + data.line.shortName;
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

void updateBlink(time_t now, bool force) {
    TFT_eSPI &g = surface();
    const bool colon = colonOnNow();
    if (force || (colon ? 1 : 0) != gColonPhase) {
        gColonPhase = colon ? 1 : 0;
        drawClock(g, colon);
        flushRegion(kClockX - 1, kClockY - 1, kClockW + 2, 18);
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
void metroBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset) {
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
            drawCenterMessage(g, "Service terminé", "Pas de train avant la reprise du service");
        } else {
            drawCenterMessage(g, "Aucun train annoncé", "Pas de passage prévu pour le moment");
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

void metroBoardTick(const BoardData &data, time_t now) {
    if ((long)(now / 60) != gDrawnMinute) {
        metroBoardDraw(data, now, gScroll); // waiting times and clock changed
        return;
    }
    updateBlink(now, false);
    updateTicker(false);
}

void metroBoardRelease() {
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
// Demo
// ---------------------------------------------------------------------------------------
static time_t metroDemoNow() {
    time_t t = primNow();
    if (t) return t;
    return (time_t)1767258000 + (time_t)(millis() / 1000UL); // 2026-01-01 09:00 UTC
}

// Demo scenes (encoder scrolls the trains, then moves on to the next scene):
//   0. line 1, Châtelet -> La Défense (approaching, minutes, cancelled, clock time)
//   1. line 13 branch platform: destination differs per train
//   2. line 3bis near the terminus: a single train, at the platform
//   3. line 14: fetch error without data
//   4. line 7bis: no train announced
static BoardData metroDemoScene(int scene, time_t t0) {
    BoardData data;
    data.line.mode = TransportMode::Metro;
    data.line.color = 0; // official fallback colours
    data.line.textColor = 0;
    data.fetchedAt = t0;

    auto add = [&](const char *dest, long inS, DepartureStatus status) {
        Departure d;
        d.destination = dest;
        d.aimed = t0 + inS;
        d.expected = t0 + inS;
        d.status = status;
        data.departures.push_back(d);
    };

    switch (scene) {
        case 0:
            data.line.id = "C01371";
            data.line.shortName = "1";
            data.stop.name = "Châtelet";
            data.stop.direction = "La Défense";
            add("La Défense", 20, DepartureStatus::Approaching);
            add("La Défense", 190, DepartureStatus::OnTime);
            add("La Défense", 760, DepartureStatus::OnTime);
            add("La Défense", 1000, DepartureStatus::Cancelled);
            add("La Défense", 3900, DepartureStatus::OnTime);
            data.messages.push_back("Trafic perturbé sur la ligne 1 entre Nation et Château de Vincennes : "
                                    "incident technique.");
            break;
        case 1:
            data.line.id = "C01383";
            data.line.shortName = "13";
            data.stop.name = "La Fourche";
            data.stop.direction = "Les Courtilles / Saint-Denis Université";
            add("Saint-Denis Université", 0, DepartureStatus::AtStop);
            add("Asnières-Gennevilliers Les Courtilles", 140, DepartureStatus::OnTime);
            add("Saint-Denis Université", 560, DepartureStatus::OnTime);
            break;
        case 2:
            data.line.id = "C01386";
            data.line.shortName = "3B";
            data.stop.name = "Pelleport";
            data.stop.direction = "Gambetta";
            add("Gambetta", 0, DepartureStatus::AtStop);
            break;
        case 3:
            data.line.id = "C01384";
            data.line.shortName = "14";
            data.stop.name = "Saint-Lazare";
            data.stop.direction = "Aéroport d'Orly";
            data.fetchedAt = 0;
            data.error = "Erreur réseau (HTTP 503)";
            break;
        default:
            data.line.id = "C01387";
            data.line.shortName = "7B";
            data.stop.name = "Botzaris";
            data.stop.direction = "Pré-Saint-Gervais";
            break;
    }
    return data;
}

void metroBoardDemo() {
    primEnsureTime(); // real clock when Wi-Fi is up (no-op otherwise)
    constexpr int kScenes = 5;
    int scene = 0;
    BoardData data = metroDemoScene(scene, metroDemoNow());

    uint8_t scroll = 0;
    metroBoardDraw(data, metroDemoNow(), scroll);

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
                data = metroDemoScene(scene, metroDemoNow());
                scroll = 0;
            }
            metroBoardDraw(data, metroDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(PrevPress)) {
            if (gScroll > 0) {
                scroll = gScroll - 1;
            } else {
                scene = (scene + kScenes - 1) % kScenes;
                data = metroDemoScene(scene, metroDemoNow());
                scroll = 0;
            }
            metroBoardDraw(data, metroDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(AnyKeyPress)) {
            // An encoder step may land between the checks above: handle it next round.
            if (!NextPress && !PrevPress) break;
        }
        metroBoardTick(data, metroDemoNow());
        delay(15);
    }

    metroBoardRelease();
    tft.fillScreen(bruceConfig.bgColor);
}
