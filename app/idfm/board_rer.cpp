#include "board_rer.h"

#include "board_common.h"
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

#include <algorithm>

// Rendering notes (see docs/reference/RER.md):
// - Bruce compiles TFT_eSPI with the GLCD 5x7 font only (no GFX free fonts), so every text
//   uses font 1 scaled with setTextSize (x1 = 6x8 px cell, x2 = 12x16, x3 = 18x24).
// - The GLCD font holds the CP437 table: idfmGlcd() maps French accents onto it.
// - Drawing goes through the TFT_eSPI base class: either a full-frame 16-bit sprite (PSRAM,
//   flicker-free, regions pushed with pushSprite(tx, ty, sx, sy, sw, sh)) or, as a fallback,
//   the native TFT_eSPI behind the global `tft` (bypasses the tft_logger WebUI mirror).

using namespace idfmui;

namespace {

// ---------------------------------------------------------------------------------------
// Geometry (landscape 320x170)
// ---------------------------------------------------------------------------------------
constexpr int16_t kW = 320;
constexpr int16_t kH = 170;
constexpr int16_t kHeaderH = 26;  // header band 0..25, separator at y=26
constexpr int16_t kRowsY = 27;    // first departure row
constexpr int16_t kRowH = 40;     // 3 rows: 27..146, separator at y=147
constexpr int16_t kTickerY = 148; // bottom ticker band 148..169
constexpr int16_t kTickerH = kH - kTickerY;
constexpr int16_t kMargin = 4;

constexpr uint32_t kBlinkMs = 600;       // half period of "à quai" blinking
constexpr uint32_t kTickerMsPerPx = 20;  // 50 px/s
constexpr long kDepartedGraceS = 120;    // hide departures older than this (unless at stop)

// ---------------------------------------------------------------------------------------
// Palette (RGB565). Approximations of the real screens, see docs/reference/RER.md.
// ---------------------------------------------------------------------------------------
constexpr uint16_t kBg = rgb(0x0B, 0x1F, 0x4D);       // night blue background
constexpr uint16_t kBgAlt = rgb(0x11, 0x29, 0x5E);    // alternate row
constexpr uint16_t kHeaderBg = rgb(0x06, 0x15, 0x38); // header band
constexpr uint16_t kSeparator = rgb(0x2A, 0x45, 0x80);
constexpr uint16_t kTickerBg = rgb(0x06, 0x11, 0x2E);
constexpr uint16_t kWhite = rgb(0xFF, 0xFF, 0xFF);
constexpr uint16_t kYellow = rgb(0xFF, 0xD2, 0x00); // waiting time
constexpr uint16_t kGrey = rgb(0xA8, 0xB4, 0xCC);   // secondary text
constexpr uint16_t kOrange = rgb(0xFF, 0x91, 0x00); // delayed
constexpr uint16_t kRed = rgb(0xFF, 0x40, 0x40);    // cancelled
constexpr uint16_t kModeText = rgb(0x0B, 0x1F, 0x4D);

// ---------------------------------------------------------------------------------------
// UTF-8 helpers
// ---------------------------------------------------------------------------------------

// Decode the next code point of `s` starting at byte `i` (advanced). Invalid bytes are
// returned as-is (Latin-1 guess) so that nothing loops forever.
uint32_t nextCodepoint(const String &s, unsigned int &i) {
    const unsigned int len = s.length();
    uint8_t c = (uint8_t)s[i++];
    if (c < 0x80) return c;
    int extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07;
    } else {
        return c; // stray continuation byte or Latin-1 input
    }
    for (int k = 0; k < extra; k++) {
        if (i >= len) return '?';
        uint8_t cc = (uint8_t)s[i];
        if ((cc & 0xC0) != 0x80) return '?';
        cp = (cp << 6) | (cc & 0x3F);
        i++;
    }
    return cp;
}

// ASCII replacement of a non-ASCII code point, nullptr if unknown.
const char *asciiFor(uint32_t cp) {
    if (cp >= 0xC0 && cp <= 0xC5) return "A";
    if (cp >= 0xE0 && cp <= 0xE5) return "a";
    if (cp >= 0xC8 && cp <= 0xCB) return "E";
    if (cp >= 0xE8 && cp <= 0xEB) return "e";
    if (cp >= 0xCC && cp <= 0xCF) return "I";
    if (cp >= 0xEC && cp <= 0xEF) return "i";
    if ((cp >= 0xD2 && cp <= 0xD6) || cp == 0xD8) return "O";
    if ((cp >= 0xF2 && cp <= 0xF6) || cp == 0xF8) return "o";
    if (cp >= 0xD9 && cp <= 0xDC) return "U";
    if (cp >= 0xF9 && cp <= 0xFC) return "u";
    switch (cp) {
        case 0xC6: return "AE";
        case 0xE6: return "ae";
        case 0xC7: return "C";
        case 0xE7: return "c";
        case 0xD1: return "N";
        case 0xF1: return "n";
        case 0xDD: return "Y";
        case 0xFD:
        case 0xFF: return "y";
        case 0xDF: return "ss";
        case 0x152: return "OE";
        case 0x153: return "oe";
        case 0xA0:   // no-break space
        case 0x2009: // thin space
        case 0x202F: // narrow no-break space
            return " ";
        case 0x2018:
        case 0x2019:
        case 0x2032: return "'";
        case 0x201C:
        case 0x201D:
        case 0xAB:
        case 0xBB: return "\"";
        case 0x2010:
        case 0x2011:
        case 0x2013:
        case 0x2014:
        case 0x2212: return "-";
        case 0x2026: return "...";
        case 0x2022:
        case 0xB7: return "-";
        case 0xB0: return "o";
        case 0x20AC: return "EUR";
        default: return nullptr;
    }
}

// CP437 byte of a code point available in the GLCD font, 0 if none.
uint8_t cp437For(uint32_t cp) {
    switch (cp) {
        case 0xC7: return 0x80; // Ç
        case 0xFC: return 0x81; // ü
        case 0xE9: return 0x82; // é
        case 0xE2: return 0x83; // â
        case 0xE4: return 0x84; // ä
        case 0xE0: return 0x85; // à
        case 0xE7: return 0x87; // ç
        case 0xEA: return 0x88; // ê
        case 0xEB: return 0x89; // ë
        case 0xE8: return 0x8A; // è
        case 0xEF: return 0x8B; // ï
        case 0xEE: return 0x8C; // î
        case 0xC4: return 0x8E; // Ä
        case 0xC9: return 0x90; // É
        case 0xE6: return 0x91; // æ
        case 0xC6: return 0x92; // Æ
        case 0xF4: return 0x93; // ô
        case 0xF6: return 0x94; // ö
        case 0xFB: return 0x96; // û
        case 0xF9: return 0x97; // ù
        case 0xFF: return 0x98; // ÿ
        case 0xD6: return 0x99; // Ö
        case 0xDC: return 0x9A; // Ü
        case 0xE1: return 0xA0; // á
        case 0xED: return 0xA1; // í
        case 0xF3: return 0xA2; // ó
        case 0xFA: return 0xA3; // ú
        case 0xF1: return 0xA4; // ñ
        case 0xD1: return 0xA5; // Ñ
        case 0x2022: return 0x07; // bullet (glyph 7 of the GLCD font)
        default: return 0;
    }
}

String convert(const String &utf8, bool glcd) {
    String out;
    out.reserve(utf8.length());
    unsigned int i = 0;
    while (i < utf8.length()) {
        uint32_t cp = nextCodepoint(utf8, i);
        if (cp >= 0x20 && cp < 0x7F) {
            out += (char)cp;
            continue;
        }
        if (cp < 0x20 || cp == 0x7F) {
            if (cp == '\t' || cp == '\n' || cp == '\r') out += ' ';
            continue; // drop other control characters
        }
        if (glcd) {
            uint8_t b = cp437For(cp);
            if (b) {
                out += (char)b;
                continue;
            }
        }
        const char *rep = asciiFor(cp);
        out += rep ? rep : "?";
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------------------

// (textW, drawText, abbreviate, fitText, clockText, rgb: see board_common.h)

void lineColors(const Line &line, uint16_t &bg, uint16_t &fg) {
    if (line.color != 0 || line.textColor != 0) {
        bg = line.color;
        fg = line.textColor;
        return;
    }
    // Fallback: official IDFM colours (docs/reference/RER.md, section 3)
    fg = kWhite;
    char c = line.shortName.length() ? line.shortName[0] : '?';
    switch (c) {
        case 'A': bg = rgb(0xE3, 0x05, 0x1C); break;
        case 'B': bg = rgb(0x52, 0x91, 0xCE); break;
        case 'C':
            bg = rgb(0xFF, 0xCE, 0x00);
            fg = rgb(0, 0, 0);
            break;
        case 'D': bg = rgb(0x00, 0x81, 0x4F); break;
        case 'E': bg = rgb(0xC0, 0x41, 0x91); break;
        default: bg = kGrey; break;
    }
}

// "RER" mode cartouche + coloured line square. Total size 54x20.
void drawRerPictogram(TFT_eSPI &g, int32_t x, int32_t y, const Line &line, uint16_t surroundBg) {
    g.fillSmoothRoundRect(x, y, 30, 20, 4, kWhite, surroundBg);
    drawText(g, "RER", x + 6, y + 6, 1, kModeText);
    drawText(g, "RER", x + 7, y + 6, 1, kModeText); // fake bold

    uint16_t bg, fg;
    lineColors(line, bg, fg);
    g.fillSmoothRoundRect(x + 34, y, 20, 20, 4, bg, surroundBg);
    String letter = idfmGlcd(line.shortName);
    if (letter.length() > 1) letter = letter.substring(0, 1);
    if (letter.length() == 0) letter = "?";
    drawText(g, letter, x + 34 + 5, y + 3, 2, fg);
}

// ---------------------------------------------------------------------------------------
// Waiting time rules
// ---------------------------------------------------------------------------------------
enum class WaitKind : uint8_t { Minutes, UnderOne, Clock, AtStop, Approaching, Cancelled, Unknown, Departed };

struct WaitInfo {
    WaitKind kind = WaitKind::Unknown;
    long minutes = 0;
    time_t when = 0;
};

WaitInfo computeWait(const Departure &d, time_t now) {
    WaitInfo w;
    w.when = d.expected ? d.expected : d.aimed;
    if (d.status == DepartureStatus::Cancelled) {
        w.kind = WaitKind::Cancelled;
        return w;
    }
    if (d.status == DepartureStatus::AtStop) {
        w.kind = WaitKind::AtStop;
        return w;
    }
    if (d.status == DepartureStatus::Approaching) {
        w.kind = WaitKind::Approaching;
        return w;
    }
    if (w.when == 0) {
        w.kind = WaitKind::Unknown;
        return w;
    }
    long diff = (long)(w.when - now);
    if (diff < -kDepartedGraceS) {
        w.kind = WaitKind::Departed;
    } else if (diff < 60) {
        w.kind = WaitKind::UnderOne;
    } else if (diff / 60 < 60) {
        w.kind = WaitKind::Minutes;
        w.minutes = diff / 60; // floor, like the platform screens
    } else {
        w.kind = WaitKind::Clock;
    }
    return w;
}

// Layout of the right-hand time area of a row.
struct TimeLayout {
    String big;      // main text
    String line2;    // second line (two-line labels), may be empty
    String unit;     // small "min" after a size-3 number, may be empty
    uint8_t size = 2;
    uint16_t color = kYellow;
    bool blink = false;
    int16_t w = 0;
};

TimeLayout timeLayout(const Departure &d, time_t now) {
    TimeLayout t;
    WaitInfo w = computeWait(d, now);
    switch (w.kind) {
        case WaitKind::Minutes:
            t.big = String(w.minutes);
            t.unit = "min";
            t.size = 3;
            break;
        case WaitKind::UnderOne:
            t.big = "<1";
            t.unit = "min";
            t.size = 3;
            break;
        case WaitKind::Clock: t.big = clockText(w.when); break;
        case WaitKind::AtStop:
            t.big = idfmGlcd("à quai");
            t.blink = true;
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
            t.color = kGrey;
            break;
    }
    if (t.unit.length()) {
        t.w = textW(t.big, t.size) + 3 + textW(t.unit, 1);
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
    uint16_t bg = kBg;
};

RowSlot gRows[RER_BOARD_ROWS];
tft_sprite *gFrame = nullptr;     // full-frame sprite (PSRAM)
bool gFrameTried = false;
tft_sprite *gTickerSpr = nullptr; // ticker-only sprite, used when there is no frame sprite
bool gTickerSprTried = false;
String gTickerText;               // GLCD-encoded
bool gTickerTag = false;          // show the yellow "INFO" tag
uint32_t gTickerStartMs = 0;
int32_t gTickerLastX = INT32_MIN;
int8_t gBlinkPhase = -1;
long gDrawnMinute = -1;
uint8_t gScroll = 0;

void ensureFrame() {
    if (gFrame || gFrameTried) return;
    gFrameTried = true;
#if defined(BOARD_HAS_PSRAM)
    // 320x170x16bit = 108.8 KB: only worth it in PSRAM (TFT_eSprite uses ps_calloc there).
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

// ---------------------------------------------------------------------------------------
// Screen parts
// ---------------------------------------------------------------------------------------
void drawHeader(TFT_eSPI &g, const BoardData &data, time_t now) {
    g.fillRect(0, 0, kW, kHeaderH, kHeaderBg);
    drawRerPictogram(g, kMargin, 3, data.line, kHeaderBg);

    String clock = clockText(now);
    const int32_t clockX = kW - kMargin - textW(clock, 2);
    drawText(g, clock, clockX, 5, 2, kWhite);

    // Station name: size 2 when it fits (possibly abbreviated), else size 1.
    const int32_t nameX = kMargin + 54 + 8;
    const int32_t nameMaxW = clockX - 8 - nameX;
    String name = idfmGlcd(data.stop.name);
    String abbr = abbreviate(name);
    if (textW(name, 2) <= nameMaxW) {
        drawText(g, name, nameX, 5, 2, kWhite);
    } else if (textW(abbr, 2) <= nameMaxW) {
        drawText(g, abbr, nameX, 5, 2, kWhite);
    } else {
        drawText(g, fitText(name, nameMaxW, 1), nameX, 9, 1, kWhite);
    }
    g.drawFastHLine(0, kHeaderH, kW, kSeparator);
}

// Draw (or blank, when blinking off) the right-hand time area of a row.
void drawTimeArea(TFT_eSPI &g, const RowSlot &row, const TimeLayout &t, bool blinkOn) {
    const int16_t x0 = kW - kMargin - t.w;
    g.fillRect(x0 - 2, row.y + 1, t.w + 4, kRowH - 2, row.bg);
    if (t.blink && !blinkOn) return;

    if (t.unit.length()) {
        const int16_t by = row.y + 8; // 24 px high number, glyph body 21 px
        drawText(g, t.big, x0, by, t.size, t.color);
        drawText(g, t.unit, x0 + textW(t.big, t.size) + 3, by + 14, 1, t.color);
    } else if (t.line2.length()) {
        // Two-line label ("à l'" / "approche"): one left-aligned block reads as a single phrase.
        drawText(g, t.big, x0, row.y + 3, t.size, t.color);
        drawText(g, t.line2, x0, row.y + 21, t.size, t.color);
    } else {
        drawText(g, t.big, kW - kMargin - textW(t.big, t.size), row.y + 12, t.size, t.color);
    }
}

void drawTag(TFT_eSPI &g, int16_t &x, int16_t y, const String &label, uint16_t bg, uint16_t fg) {
    int16_t w = textW(label, 1) + 6;
    g.fillRoundRect(x, y + 3, w, 11, 2, bg);
    drawText(g, label, x + 3, y + 5, 1, fg);
    x += w + 5;
}

void drawRow(TFT_eSPI &g, const RowSlot &row, time_t now, bool blinkOn) {
    const Departure &d = row.dep;
    g.fillRect(0, row.y, kW, kRowH, row.bg);

    // Line A: mission code cartouche, status tag, platform.
    int16_t x = kMargin;
    String code = idfmGlcd(d.mission.length() ? d.mission : d.trainNumber);
    if (code.length() > 6) code = code.substring(0, 6);
    if (code.length()) drawTag(g, x, row.y, code, kWhite, row.bg);
    if (d.status == DepartureStatus::Delayed) drawTag(g, x, row.y, idfmGlcd("Retardé"), kOrange, kBg);
    if (d.platform.length()) {
        drawText(g, "Voie " + idfmGlcd(d.platform), x, row.y + 5, 1, kGrey);
    }

    // Time area first, so the destination knows how much room is left.
    TimeLayout t = timeLayout(d, now);
    drawTimeArea(g, row, t, blinkOn);

    // Line B: destination.
    const bool cancelled = d.status == DepartureStatus::Cancelled;
    int16_t destMaxW = kW - kMargin - t.w - 10 - kMargin;
    String dest = fitText(idfmGlcd(d.destination), destMaxW, 2);
    drawText(g, dest, kMargin, row.y + 20, 2, cancelled ? kGrey : kWhite);
}

void drawCenterMessage(TFT_eSPI &g, const String &title, const String &detail) {
    String t = fitText(idfmGlcd(title), kW - 2 * kMargin, 2);
    drawText(g, t, (kW - textW(t, 2)) / 2, kRowsY + 38, 2, kWhite);
    if (detail.length()) {
        String d = fitText(idfmGlcd(detail), kW - 2 * kMargin, 1);
        drawText(g, d, (kW - textW(d, 1)) / 2, kRowsY + 64, 1, kGrey);
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
        raw = "RER " + data.line.shortName;
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

// Render the ticker band. Returns false when nothing changed (no push needed).
// `oy` is the band top in the target surface coordinates.
bool renderTicker(TFT_eSPI &g, int16_t oy, bool force) {
    const int16_t textX0 = gTickerTag ? 50 : kMargin;
    const int32_t textW2 = textW(gTickerText, 2);
    int32_t x;
    if (textW2 <= kW - textX0 - kMargin) {
        x = textX0; // fits: static
    } else {
        const uint32_t px = (millis() - gTickerStartMs) / kTickerMsPerPx;
        const uint32_t cycle = (uint32_t)textW2 + (uint32_t)(kW - textX0);
        x = kW - (int32_t)(px % cycle);
    }
    if (!force && x == gTickerLastX) return false;
    gTickerLastX = x;

    g.fillRect(0, oy, kW, kTickerH, kTickerBg);
    drawText(g, gTickerText, x, oy + 3, 2, kWhite);
    if (gTickerTag) {
        g.fillRect(0, oy, textX0 - 2, kTickerH, kTickerBg); // mask text scrolled under the tag
        g.fillRoundRect(3, oy + 4, 42, 14, 3, kYellow);
        drawText(g, "INFO", 3 + (42 - textW("INFO", 1)) / 2, oy + 7, 1, kModeText);
    }
    return true;
}

// Ticker update outside of a full redraw: draw into the best available surface and push.
void updateTicker(bool force) {
    if (gFrame) {
        if (renderTicker(*gFrame->nativeSprite(), kTickerY, force)) flushRegion(0, kTickerY, kW, kTickerH);
        return;
    }
    if (!gTickerSpr && !gTickerSprTried) {
        gTickerSprTried = true;
        gTickerSpr = new tft_sprite(&tft); // 320x22x16bit = 14 KB
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
    const bool on = blinkOnNow();
    const int8_t phase = on ? 1 : 0;
    if (!force && phase == gBlinkPhase) return;
    gBlinkPhase = phase;
    TFT_eSPI &g = surface();
    for (uint8_t i = 0; i < RER_BOARD_ROWS; i++) {
        const RowSlot &row = gRows[i];
        if (!row.used) continue;
        TimeLayout t = timeLayout(row.dep, now);
        if (!t.blink) continue;
        drawTimeArea(g, row, t, on);
        const int16_t x0 = kW - kMargin - t.w - 2;
        flushRegion(x0, row.y + 1, t.w + 4, kRowH - 2);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------
String idfmAscii(const String &utf8) { return convert(utf8, false); }

String idfmGlcd(const String &utf8) { return convert(utf8, true); }

void rerBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset) {
    ensureFrame();
    TFT_eSPI &g = surface();

    drawHeader(g, data, now);
    g.fillRect(0, kRowsY, kW, kTickerY - kRowsY, kBg);

    // Departures still relevant (drop the ones that left more than kDepartedGraceS ago).
    std::vector<const Departure *> visible;
    visible.reserve(data.departures.size());
    for (const Departure &d : data.departures) {
        if (computeWait(d, now).kind != WaitKind::Departed) visible.push_back(&d);
    }
    const size_t maxScroll = visible.size() > RER_BOARD_ROWS ? visible.size() - RER_BOARD_ROWS : 0;
    gScroll = (uint8_t)std::min<size_t>(scrollOffset, maxScroll);

    const bool blinkOn = blinkOnNow();
    for (uint8_t i = 0; i < RER_BOARD_ROWS; i++) {
        RowSlot &row = gRows[i];
        row.used = false;
        size_t idx = gScroll + i;
        if (idx >= visible.size()) continue;
        row.used = true;
        row.dep = *visible[idx];
        row.y = kRowsY + i * kRowH;
        row.bg = (i & 1) ? kBgAlt : kBg;
        drawRow(g, row, now, blinkOn);
    }

    if (visible.empty()) {
        if (data.error.length()) {
            drawCenterMessage(g, "Information indisponible", data.error);
        } else if (data.fetchedAt == 0) {
            drawCenterMessage(g, "Chargement…", "Prochains passages en cours de récupération");
        } else {
            drawCenterMessage(g, "Aucun train", "Pas de départ annoncé pour le moment");
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
    gDrawnMinute = (long)(now / 60);
}

void rerBoardTick(const BoardData &data, time_t now) {
    if ((long)(now / 60) != gDrawnMinute) {
        rerBoardDraw(data, now, gScroll); // waiting times and clock changed
        return;
    }
    updateBlink(now, false);
    updateTicker(false);
}

void rerBoardRelease() {
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
    gDrawnMinute = -1;
    gScroll = 0;
}

// Demo clock: PRIM UTC reference when available, else a fixed date ticking with millis().
static time_t demoNow() {
    time_t t = primNow();
    if (t) return t;
    return (time_t)1767258000 + (time_t)(millis() / 1000UL); // 2026-01-01 09:00 UTC
}

void rerBoardDemo() {
    primEnsureTime(); // real clock when Wi-Fi is up (no-op otherwise)
    const time_t t0 = demoNow();

    BoardData data;
    data.line.id = "C01742";
    data.line.shortName = "A";
    data.line.color = rgb(0xE3, 0x05, 0x1C);
    data.line.textColor = kWhite;
    data.line.mode = TransportMode::Rer;
    data.stop.name = "Châtelet les Halles";
    data.stop.monitoringRef = "STIF:StopArea:SP:474151:";
    data.fetchedAt = t0;

    // Illustrative missions (mission/terminus pairs are not guaranteed to be real ones).
    auto add = [&](const char *mission, const char *dest, const char *platform, long inS, long delayS,
                   DepartureStatus status) {
        Departure d;
        d.lineRef = "C01742";
        d.mission = mission;
        d.trainNumber = String(mission) + "12";
        d.destination = dest;
        d.platform = platform;
        d.aimed = t0 + inS;
        d.expected = t0 + inS + delayS;
        d.status = status;
        data.departures.push_back(d);
    };
    add("NELY", "Cergy-le-Haut", "1", 0, 0, DepartureStatus::AtStop);
    add("ZEBU", "Torcy", "2", 40, 0, DepartureStatus::Approaching);
    add("ZEUS", "Boissy-Saint-Léger", "2", 200, 0, DepartureStatus::OnTime);
    add("QIKI", "Marne-la-Vallée Chessy", "2", 540, 120, DepartureStatus::Delayed);
    add("TEDI", "Saint-Germain-en-Laye", "1", 840, 0, DepartureStatus::Cancelled);
    add("XOLE", "Poissy", "1", 4500, 0, DepartureStatus::OnTime);
    data.messages.push_back("Trafic perturbé entre Nation et Vincennes en raison d'un incident technique.");
    data.messages.push_back("Pensez à valider votre titre de transport avant l'accès aux quais.");

    uint8_t scroll = 0;
    rerBoardDraw(data, demoNow(), scroll);

    // Ignore the click that launched the demo.
    const uint32_t start = millis();
    while (millis() - start < 300) {
        check(AnyKeyPress);
        delay(10);
    }

    while (true) {
        if (check(NextPress)) {
            if (scroll < data.departures.size()) scroll++;
            rerBoardDraw(data, demoNow(), scroll);
            scroll = gScroll; // clamped value
        } else if (check(PrevPress)) {
            if (scroll > 0) scroll--;
            rerBoardDraw(data, demoNow(), scroll);
        } else if (check(AnyKeyPress)) {
            break;
        }
        rerBoardTick(data, demoNow());
        delay(15);
    }

    rerBoardRelease();
    tft.fillScreen(bruceConfig.bgColor);
}
