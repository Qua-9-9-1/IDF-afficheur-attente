#include "board_transilien.h"

#include "board_common.h"
#include "board_rer.h" // idfmGlcd()
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

#include <algorithm>

// SNCF Transilien platform display ("tableau de voie": next trains of the platform / station,
// departure time, mission, destination, status, platform), see docs/reference/TRANSILIEN.md.
// Same rendering principles as board_rer.cpp: GLCD 5x7 font only (x1..x4), CP437 accents via
// idfmGlcd(), optional full-frame PSRAM sprite, regions pushed for animations.

using namespace idfmui;

namespace {

// ---------------------------------------------------------------------------------------
// Geometry (landscape 320x170)
// ---------------------------------------------------------------------------------------
constexpr int16_t kW = 320;
constexpr int16_t kH = 170;
constexpr int16_t kHeaderH = 26;  // header band 0..25, separator at y=26
constexpr int16_t kRowsY = 27;    // first train row
constexpr int16_t kRowH = 40;     // 3 rows: 27..146, separator at y=147
constexpr uint8_t kRows = TRANSILIEN_BOARD_ROWS;
constexpr int16_t kTickerY = 148; // bottom ticker band 148..169
constexpr int16_t kTickerH = kH - kTickerY;
constexpr int16_t kMargin = 4;

constexpr int16_t kDestX = 70;                    // destination / status column
constexpr int16_t kVoieW = 28;                    // "Voie" box
constexpr int16_t kVoieX = kW - kMargin - kVoieW; // 288
constexpr int16_t kDestMaxW = kVoieX - 6 - kDestX;
constexpr int16_t kStatusY = 24;  // status line offset inside a row
constexpr int16_t kStatusW = 100; // status area (redrawn when blinking)

constexpr uint32_t kBlinkMs = 600;      // half period of "à quai" / "à l'approche" blinking
constexpr uint32_t kTickerMsPerPx = 20; // 50 px/s
constexpr long kDepartedGraceS = 60;    // hide departures older than this (unless at stop)

// ---------------------------------------------------------------------------------------
// Palette (RGB565), see docs/reference/TRANSILIEN.md section 3.
// ---------------------------------------------------------------------------------------
constexpr uint16_t kBg = rgb(0x0A, 0x1A, 0x44);       // dark SNCF blue
constexpr uint16_t kBgAlt = rgb(0x10, 0x24, 0x55);    // alternate row
constexpr uint16_t kHeaderBg = rgb(0x05, 0x10, 0x30); // header band
constexpr uint16_t kSeparator = rgb(0x2A, 0x40, 0x78);
constexpr uint16_t kTickerBg = rgb(0x05, 0x10, 0x30);
constexpr uint16_t kWhite = rgb(0xFF, 0xFF, 0xFF);
constexpr uint16_t kYellow = rgb(0xFF, 0xD2, 0x00); // departure times
constexpr uint16_t kLightGrey = rgb(0xC8, 0xD0, 0xE0); // "à l'heure", secondary text
constexpr uint16_t kGrey = rgb(0x8C, 0x96, 0xB0);      // waiting time, train number
constexpr uint16_t kDimmed = rgb(0x6A, 0x74, 0x90);    // cancelled train (greyed out)
constexpr uint16_t kOrange = rgb(0xFF, 0x8C, 0x00);    // delay
constexpr uint16_t kRed = rgb(0xE5, 0x32, 0x32);       // cancelled
constexpr uint16_t kNavy = rgb(0x0A, 0x1A, 0x44);      // text on white

// ---------------------------------------------------------------------------------------
// Line pictogram
// ---------------------------------------------------------------------------------------
struct TransilienColor {
    char letter;
    uint8_t r, g, b;
    bool darkText;
};

// Official IDFM colours (referentiel-des-lignes, colourweb_hexa / textcolourweb_hexa, 2026-11).
constexpr TransilienColor kLineColors[] = {
    {'H', 0x84, 0x65, 0x3D, false}, {'J', 0xCE, 0xC7, 0x3D, true},  {'K', 0x9B, 0x98, 0x42, false},
    {'L', 0xC4, 0xA4, 0xCC, true},  {'N', 0x00, 0xB2, 0x97, false}, {'P', 0xF5, 0x8F, 0x53, true},
    {'R', 0xF4, 0x9F, 0xB3, true},  {'U', 0xB6, 0x13, 0x4C, false}, {'V', 0x9F, 0x98, 0x25, false},
};

void lineColors(const Line &line, uint16_t &bg, uint16_t &fg) {
    if (line.color != 0 || line.textColor != 0) {
        bg = line.color;
        fg = line.textColor;
        return;
    }
    const char c = line.shortName.length() ? (char)toupper(line.shortName[0]) : '?';
    for (const TransilienColor &lc : kLineColors) {
        if (lc.letter == c) {
            bg = rgb(lc.r, lc.g, lc.b);
            fg = lc.darkText ? rgb(0, 0, 0) : kWhite;
            return;
        }
    }
    bg = kGrey;
    fg = rgb(0, 0, 0);
}

// "Train" mode pictogram (white rounded square, front view of a train) + line letter square.
// Total size 44x20.
void drawTransilienPictogram(TFT_eSPI &g, int32_t x, int32_t y, const Line &line, uint16_t surroundBg) {
    g.fillSmoothRoundRect(x, y, 20, 20, 4, kWhite, surroundBg);
    g.fillRoundRect(x + 5, y + 3, 10, 12, 3, kNavy);      // train body
    g.fillRect(x + 7, y + 5, 6, 4, kWhite);               // windscreen
    g.fillRect(x + 7, y + 11, 2, 2, kWhite);              // headlights
    g.fillRect(x + 11, y + 11, 2, 2, kWhite);
    g.drawLine(x + 6, y + 15, x + 4, y + 17, kNavy);      // rails
    g.drawLine(x + 13, y + 15, x + 15, y + 17, kNavy);
    g.drawFastHLine(x + 4, y + 17, 12, kNavy);

    uint16_t bg, fg;
    lineColors(line, bg, fg);
    g.fillSmoothRoundRect(x + 24, y, 20, 20, 4, bg, surroundBg);
    String letter = idfmGlcd(line.shortName);
    if (letter.length() > 1) letter = letter.substring(0, 1);
    if (letter.length() == 0) letter = "?";
    drawText(g, letter, x + 24 + 5, y + 3, 2, fg);
    drawText(g, letter, x + 24 + 6, y + 3, 2, fg); // fake bold
}

// ---------------------------------------------------------------------------------------
// Departure rules
// ---------------------------------------------------------------------------------------
enum class StatusKind : uint8_t { OnTime, Delayed, Cancelled, AtStop, Approaching, Unknown, Departed };

struct DepInfo {
    StatusKind kind = StatusKind::Unknown;
    time_t when = 0;   // expected (or aimed) departure
    long waitMin = -1; // waiting time in minutes (-1: not shown)
    long delayMin = 0; // expected - aimed, minutes
};

DepInfo depInfo(const Departure &d, time_t now) {
    DepInfo i;
    i.when = d.expected ? d.expected : d.aimed;
    if (d.aimed && d.expected && d.expected > d.aimed) i.delayMin = (long)((d.expected - d.aimed + 30) / 60);
    if (d.status == DepartureStatus::Cancelled) {
        i.kind = StatusKind::Cancelled;
        return i;
    }
    if (d.status == DepartureStatus::AtStop) {
        i.kind = StatusKind::AtStop;
        return i;
    }
    if (i.when && now > 1600000000) {
        const long diff = (long)(i.when - now);
        if (diff < -kDepartedGraceS) {
            i.kind = StatusKind::Departed;
            return i;
        }
        if (diff >= 60 && diff / 60 < 60) i.waitMin = diff / 60;
    }
    if (d.status == DepartureStatus::Approaching) {
        i.kind = StatusKind::Approaching;
        i.waitMin = -1;
    } else if (d.status == DepartureStatus::Delayed || i.delayMin > 0) {
        i.kind = i.delayMin > 0 ? StatusKind::Delayed : StatusKind::OnTime;
    } else if (i.when) {
        i.kind = StatusKind::OnTime;
    }
    return i;
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

// Platform label worth showing: PRIM sends "unknown" (or similar placeholders) for SNCF trains
// whose platform is not assigned yet; the real screens then show no platform box.
String platformLabel(const Departure &d) {
    String p = d.platform;
    p.trim();
    String l = p;
    l.toLowerCase();
    if (l.length() == 0 || l == "unknown" || l == "unk" || l == "-" || l == "--" || l == "?" || l == "null") {
        return String();
    }
    return p;
}

// ---------------------------------------------------------------------------------------
// Screen parts
// ---------------------------------------------------------------------------------------
void drawHeader(TFT_eSPI &g, const BoardData &data, time_t now) {
    g.fillRect(0, 0, kW, kHeaderH, kHeaderBg);
    drawTransilienPictogram(g, kMargin, 3, data.line, kHeaderBg);

    // Clock in a white cartouche, top right.
    const String clock = clockText(now);
    const int32_t cw = textW(clock, 2) + 8;
    const int32_t cx = kW - kMargin - cw;
    g.fillSmoothRoundRect(cx, 3, cw, 20, 3, kWhite, kHeaderBg);
    drawText(g, clock, cx + 4, 6, 2, kNavy);

    // Station name: size 2 when it fits (possibly abbreviated), else size 1.
    const int32_t nameX = kMargin + 44 + 8;
    const int32_t nameMaxW = cx - 6 - nameX;
    String name = idfmGlcd(data.stop.name);
    String abbr = abbreviate(name);
    if (textW(name, 2) <= nameMaxW) {
        drawText(g, name, nameX, 6, 2, kWhite);
    } else if (textW(abbr, 2) <= nameMaxW) {
        drawText(g, abbr, nameX, 6, 2, kWhite);
    } else {
        drawText(g, fitText(name, nameMaxW, 1), nameX, 10, 1, kWhite);
    }
    g.drawFastHLine(0, kHeaderH, kW, kSeparator);
}

void drawTag(TFT_eSPI &g, int16_t x, int16_t y, const String &label, uint16_t bg, uint16_t fg) {
    const int16_t w = textW(label, 1) + 6;
    g.fillRoundRect(x, y, w, 11, 2, bg);
    drawText(g, label, x + 3, y + 2, 1, fg);
}

// Status line under the destination ("à l'heure", "retard 5 min", "supprimé", "à quai"...).
// Fills its own area, so it can be redrawn alone for blinking.
void drawStatus(TFT_eSPI &g, const RowSlot &row, const DepInfo &info, bool blinkOn) {
    const int16_t y = row.y + kStatusY;
    g.fillRect(kDestX - 1, y - 1, kStatusW + 2, 13, row.bg);
    switch (info.kind) {
        case StatusKind::OnTime: drawText(g, idfmGlcd("à l'heure"), kDestX, y + 2, 1, kLightGrey); break;
        case StatusKind::Delayed:
            drawTag(g, kDestX, y, "retard " + String(info.delayMin) + " min", kOrange, kNavy);
            break;
        case StatusKind::Cancelled: drawTag(g, kDestX, y, idfmGlcd("supprimé"), kRed, kWhite); break;
        case StatusKind::AtStop:
            if (blinkOn) drawTag(g, kDestX, y, idfmGlcd("à quai"), kYellow, kNavy);
            break;
        case StatusKind::Approaching:
            if (blinkOn) drawTag(g, kDestX, y, idfmGlcd("à l'approche"), kYellow, kNavy);
            break;
        default: break;
    }
}

bool statusBlinks(const DepInfo &info) {
    return info.kind == StatusKind::AtStop || info.kind == StatusKind::Approaching;
}

void drawVoie(TFT_eSPI &g, const RowSlot &row, const String &platform) {
    const int16_t y = row.y + 4;
    g.drawRoundRect(kVoieX, y, kVoieW, 32, 3, kWhite);
    drawText(g, "Voie", kVoieX + (kVoieW - textW("Voie", 1)) / 2, y + 3, 1, kLightGrey);
    String p = idfmGlcd(platform);
    if (textW(p, 2) <= kVoieW - 4) {
        drawText(g, p, kVoieX + (kVoieW - textW(p, 2)) / 2, y + 14, 2, kWhite);
    } else {
        p = fitText(p, kVoieW - 4, 1);
        drawText(g, p, kVoieX + (kVoieW - textW(p, 1)) / 2, y + 18, 1, kWhite);
    }
}

void drawRow(TFT_eSPI &g, const RowSlot &row, bool blinkOn, time_t now) {
    const Departure &d = row.dep;
    const DepInfo info = depInfo(d, now);
    const bool cancelled = info.kind == StatusKind::Cancelled;
    g.fillRect(0, row.y, kW, kRowH, row.bg);

    // Left column: departure time (aimed, as printed in the timetable), mission code below.
    const time_t shown = d.aimed ? d.aimed : d.expected;
    drawText(g, clockText(shown), kMargin, row.y + 5, 2, cancelled ? kDimmed : kYellow);
    String code = idfmGlcd(d.mission);
    if (code.length() > 6) code = code.substring(0, 6);
    if (code.length()) {
        const uint16_t c = cancelled ? kDimmed : kWhite;
        drawText(g, code, kMargin, row.y + kStatusY + 2, 1, c);
        drawText(g, code, kMargin + 1, row.y + kStatusY + 2, 1, c); // fake bold
    } else if (d.trainNumber.length()) {
        drawText(g, fitText(idfmGlcd(d.trainNumber), kDestX - kMargin - 4, 1), kMargin, row.y + kStatusY + 2, 1,
                 kGrey);
    }

    // Platform box (removed when the train is cancelled, like the real screens).
    const String platform = platformLabel(d);
    const bool hasVoie = platform.length() && !cancelled;
    if (hasVoie) drawVoie(g, row, platform);
    const int16_t rightEdge = hasVoie ? kVoieX - 6 : kW - kMargin;
    const int16_t maxW = rightEdge - kDestX;

    // Destination: size 2 (abbreviated if needed), else size 1, else truncated size 2.
    const uint16_t destColor = cancelled ? kDimmed : kWhite;
    String dest = idfmGlcd(d.destination.length() ? d.destination : String("Destination inconnue"));
    String abbr = abbreviate(dest);
    if (textW(dest, 2) <= maxW) {
        drawText(g, dest, kDestX, row.y + 5, 2, destColor);
    } else if (textW(abbr, 2) <= maxW) {
        drawText(g, abbr, kDestX, row.y + 5, 2, destColor);
    } else if (textW(abbr, 1) <= maxW) {
        drawText(g, abbr, kDestX, row.y + 9, 1, destColor);
    } else {
        drawText(g, fitText(dest, maxW, 2), kDestX, row.y + 5, 2, destColor);
    }

    drawStatus(g, row, info, blinkOn);

    // Waiting time, right-aligned on the status line.
    if (info.waitMin >= 0 && !cancelled) {
        const String w = "dans " + String(info.waitMin) + " min";
        const int16_t wx = rightEdge - textW(w, 1);
        if (wx > kDestX + kStatusW + 4) drawText(g, w, wx, row.y + kStatusY + 2, 1, kGrey);
    }
}

void drawCenterMessage(TFT_eSPI &g, const String &title, const String &detail) {
    String t = fitText(idfmGlcd(title), kW - 2 * kMargin, 2);
    drawText(g, t, (kW - textW(t, 2)) / 2, kRowsY + 38, 2, kWhite);
    if (detail.length()) {
        String d = fitText(idfmGlcd(detail), kW - 2 * kMargin, 1);
        drawText(g, d, (kW - textW(d, 1)) / 2, kRowsY + 64, 1, kLightGrey);
    }
}

// True during the night service break (Europe/Paris), roughly 01:30 - 04:30.
bool outOfServiceHours(time_t now) {
    struct tm tmLocal;
    if (now < 1600000000 || !primLocalTime(now, tmLocal)) return false;
    const int m = tmLocal.tm_hour * 60 + tmLocal.tm_min;
    return m >= 90 && m < 270;
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
        raw = "Ligne " + data.line.shortName;
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
        g.fillRoundRect(3, oy + 4, 42, 14, 3, kYellow);
        drawText(g, "INFO", 3 + (42 - textW("INFO", 1)) / 2, oy + 7, 1, kNavy);
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
    for (uint8_t i = 0; i < kRows; i++) {
        const RowSlot &row = gRows[i];
        if (!row.used) continue;
        const DepInfo info = depInfo(row.dep, now);
        if (!statusBlinks(info)) continue;
        drawStatus(g, row, info, on);
        flushRegion(kDestX - 1, row.y + kStatusY - 1, kStatusW + 2, 13);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------
void transilienBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset) {
    ensureFrame();
    TFT_eSPI &g = surface();

    drawHeader(g, data, now);
    g.fillRect(0, kRowsY, kW, kTickerY - kRowsY, kBg);

    std::vector<const Departure *> visible;
    visible.reserve(data.departures.size());
    for (const Departure &d : data.departures) {
        if (depInfo(d, now).kind != StatusKind::Departed) visible.push_back(&d);
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
        row.y = kRowsY + i * kRowH;
        row.bg = (i & 1) ? kBgAlt : kBg;
        drawRow(g, row, blinkOn, now);
    }

    if (visible.empty()) {
        if (data.error.length()) {
            drawCenterMessage(g, "Information indisponible", data.error);
        } else if (data.fetchedAt == 0) {
            drawCenterMessage(g, "Chargement…", "Prochains départs en cours de récupération");
        } else if (outOfServiceHours(now)) {
            drawCenterMessage(g, "Fin de service", "Pas de train avant la reprise du service");
        } else {
            drawCenterMessage(g, "Aucun train annoncé", "Pas de départ prévu pour le moment");
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

void transilienBoardTick(const BoardData &data, time_t now) {
    if ((long)(now / 60) != gDrawnMinute) {
        transilienBoardDraw(data, now, gScroll); // times, waiting times and clock changed
        return;
    }
    updateBlink(now, false);
    updateTicker(false);
}

void transilienBoardRelease() {
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

// ---------------------------------------------------------------------------------------
// Sample data and demo
// ---------------------------------------------------------------------------------------
static time_t transilienDemoNow() {
    time_t t = primNow();
    if (t) return t;
    return (time_t)1767258000 + (time_t)(millis() / 1000UL); // 2026-01-01 09:00 UTC
}

// Departure aimed at the next whole minute + inMin, delayed by delayMin.
static void addDeparture(BoardData &data, time_t t0, const char *line, const char *mission, const char *number,
                         const char *dest, const char *platform, long inS, long delayS, DepartureStatus status) {
    Departure d;
    d.lineRef = line;
    d.mission = mission;
    d.trainNumber = number;
    d.destination = dest;
    d.platform = platform;
    d.aimed = t0 + inS;
    d.expected = status == DepartureStatus::Cancelled ? 0 : t0 + inS + delayS;
    d.status = status;
    data.departures.push_back(d);
}

static void transilienScene(int scene, BoardData &out, time_t t0) {
    out = BoardData();
    out.line.mode = TransportMode::Transilien;
    out.line.color = 0; // official fallback colours
    out.line.textColor = 0;
    out.fetchedAt = t0;
    const time_t base = t0 - (t0 % 60); // timetable times on whole minutes

    switch (scene) {
        case 0:
            // Line L, Paris Saint-Lazare (missions / numbers illustrative).
            out.line.id = "C01740";
            out.line.shortName = "L";
            out.stop.name = "Paris Saint-Lazare";
            out.stop.monitoringRef = "STIF:StopArea:SP:71370:";
            addDeparture(out, base, "C01740", "PEBU", "135420", "Versailles Rive Droite", "4", 60, 0,
                         DepartureStatus::AtStop);
            addDeparture(out, base, "C01740", "NOPE", "134612", "Nanterre Université", "7", 120, 0,
                         DepartureStatus::OnTime);
            addDeparture(out, base, "C01740", "SEBO", "135622", "Saint-Nom-la-Bretèche Forêt de Marly", "5", 420,
                         300, DepartureStatus::Delayed);
            addDeparture(out, base, "C01740", "TOCA", "134734", "Cergy-le-Haut", "9", 780, 0,
                         DepartureStatus::Cancelled);
            addDeparture(out, base, "C01740", "PEBU", "135424", "Versailles Rive Droite", "4", 1260, 0,
                         DepartureStatus::OnTime);
            addDeparture(out, base, "C01740", "TOCA", "134738", "Cergy-le-Haut", "10", 1980, 0,
                         DepartureStatus::OnTime);
            addDeparture(out, base, "C01740", "SEBO", "135626", "Saint-Nom-la-Bretèche Forêt de Marly", "5", 4200,
                         0, DepartureStatus::OnTime);
            out.messages.push_back("Travaux : les week-ends de novembre, trafic interrompu entre Saint-Cloud et "
                                   "Versailles Rive Droite. Bus de remplacement.");
            out.messages.push_back("Affluence du prochain train disponible sur l'application.");
            break;
        case 1:
            // Line N, Paris Montparnasse: long destinations, unknown platform.
            out.line.id = "C01736";
            out.line.shortName = "N";
            out.stop.name = "Paris Montparnasse 3 Vaugirard";
            addDeparture(out, base, "C01736", "DIPA", "164321", "Dreux", "24", 30, 0, DepartureStatus::Approaching);
            addDeparture(out, base, "C01736", "MOGA", "164527", "Mantes-la-Jolie", "", 480, 0,
                         DepartureStatus::OnTime);
            addDeparture(out, base, "C01736", "", "164729", "Rambouillet", "21", 900, 600,
                         DepartureStatus::Delayed);
            break;
        case 2:
            out.line.id = "C01739";
            out.line.shortName = "J";
            out.stop.name = "Paris Saint-Lazare";
            out.fetchedAt = 0;
            out.error = "Erreur réseau (HTTP 503)";
            break;
        case 3:
            out.line.id = "C01730";
            out.line.shortName = "P";
            out.stop.name = "Château-Thierry";
            break;
        default:
            out.line.id = "C01737";
            out.line.shortName = "H";
            out.stop.name = "Paris Nord";
            out.fetchedAt = 0; // loading
            break;
    }
}

void transilienBoardSampleData(BoardData &out) { transilienScene(0, out, transilienDemoNow()); }

// Demo scenes (encoder scrolls the trains, then moves on to the next scene):
//   0. line L, Paris Saint-Lazare (at platform, on time, delayed, cancelled...)
//   1. line N, Montparnasse: approaching, missing platform / mission, long delay
//   2. line J: fetch error without data
//   3. line P: no train announced
//   4. line H: loading
void transilienBoardDemo() {
    primEnsureTime(); // real clock when Wi-Fi is up (no-op otherwise)
    constexpr int kScenes = 5;
    int scene = 0;
    BoardData data;
    transilienScene(scene, data, transilienDemoNow());

    uint8_t scroll = 0;
    transilienBoardDraw(data, transilienDemoNow(), scroll);

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
                transilienScene(scene, data, transilienDemoNow());
                scroll = 0;
            }
            transilienBoardDraw(data, transilienDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(PrevPress)) {
            if (gScroll > 0) {
                scroll = gScroll - 1;
            } else {
                scene = (scene + kScenes - 1) % kScenes;
                transilienScene(scene, data, transilienDemoNow());
                scroll = 0;
            }
            transilienBoardDraw(data, transilienDemoNow(), scroll);
            scroll = gScroll;
        } else if (check(AnyKeyPress)) {
            // An encoder step may land between the checks above: handle it next round.
            if (!NextPress && !PrevPress) break;
        }
        transilienBoardTick(data, transilienDemoNow());
        delay(15);
    }

    transilienBoardRelease();
    tft.fillScreen(bruceConfig.bgColor);
}
