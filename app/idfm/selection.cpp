#include "selection.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "referential.h"
#include <globals.h>

namespace {

// Display label of a transport mode (menu titles).
const char *modeLabel(TransportMode mode) {
    switch (mode) {
        case TransportMode::Metro: return "Metro";
        case TransportMode::Rer: return "RER";
        case TransportMode::Transilien: return "Transilien";
        case TransportMode::Tram: return "Tram";
        case TransportMode::Bus: return "Bus";
    }
    return "";
}

// Latin-1 supplement U+00C0..U+00FF -> ASCII (U+00C6, U+00DE, U+00DF, U+00E6, U+00FE are special-cased).
const char kLatin1Ascii[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
                            "aaaaaaaceeeeiiiidnooooo/ouuuuyty";

void appendAscii(String &out, uint32_t cp) {
    if (cp >= 0xC0 && cp <= 0xFF) {
        if (cp == 0xC6) out += "AE";
        else if (cp == 0xE6) out += "ae";
        else if (cp == 0xDF) out += "ss";
        else if (cp == 0xDE) out += "Th";
        else if (cp == 0xFE) out += "th";
        else out += kLatin1Ascii[cp - 0xC0];
        return;
    }
    switch (cp) {
        case 0x00A0: out += ' '; return;           // no-break space
        case 0x0152: out += "OE"; return;          // Œ
        case 0x0153: out += "oe"; return;          // œ
        case 0x2010:
        case 0x2011:
        case 0x2013:
        case 0x2014: out += '-'; return;           // hyphens, dashes
        case 0x2018:
        case 0x2019: out += '\''; return;          // typographic apostrophes
        case 0x201C:
        case 0x201D: out += '"'; return;
        default: out += '?'; return;
    }
}

// UTF-8 -> ASCII transliteration: Bruce's built-in fonts only cover ASCII.
// Local on purpose (board_rer.h has its own idfmAscii) to keep the modules decoupled.
String toAscii(const String &in) {
    String out;
    out.reserve(in.length());
    const uint8_t *s = reinterpret_cast<const uint8_t *>(in.c_str());
    while (*s) {
        uint8_t c = *s;
        if (c < 0x80) {
            out += (char)c;
            s++;
            continue;
        }
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        uint32_t cp = len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        bool valid = len > 1;
        for (int i = 1; i < len && valid; i++) {
            if ((s[i] & 0xC0) != 0x80) valid = false;
            else cp = (cp << 6) | (s[i] & 0x3F);
        }
        if (!valid) { // stray byte: skip it
            s++;
            continue;
        }
        appendAscii(out, cp);
        s += len;
    }
    return out;
}

// Draws the line pictogram ("RER [A]") centred in the top bar, between clock and battery.
// Must stay above y=20: MENU_TYPE_REGULAR draws its 6-row box from there on a 170 px screen.
void drawLineHeader(const Line &line) {
    const int y = 7;
    const int h = 12;
    String mode = modeLabel(line.mode);
    String name = toAscii(line.shortName);
    tft.setTextSize(1);
    int modeW = mode.length() * LW;
    int pillW = name.length() * LW + 8;
    int x = (tftWidth - modeW - 4 - pillW) / 2;

    tft.fillRect(x - 2, y, modeW + 4 + pillW + 4, h, bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString(mode, x, y + 2);
    x += modeW + 4;
    tft.fillRoundRect(x, y, pillW, h, 3, line.color);
    tft.setTextColor(line.textColor, line.color);
    tft.drawString(name, x + 4, y + 2);
}

// loopOptions hover hook: draws the header of the stop list, then lets loopOptions draw the list.
bool stopListHover(void *ptr, bool shouldRender) {
    if (shouldRender && ptr != nullptr) drawLineHeader(*static_cast<const Line *>(ptr));
    return false;
}

// Remember the last choice to reopen the menus where the user left them.
String lastLineId;
String lastStopLineId;
int lastStopIndex = 0;

} // namespace

bool selectLine(TransportMode mode, Line &out) {
    std::vector<Line> lines;
    if (!idfmLoadLines(mode, lines)) Serial.println("[idfm] lines.json missing, built-in lines used");
    if (lines.empty()) {
        displayWarning(String("No line for ") + modeLabel(mode), true);
        return false;
    }

    int start = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].id == lastLineId) start = i;
    }

    int chosen = -1;
    std::vector<Option> options;
    options.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); i++) {
        String label = String(modeLabel(mode)) + " " + toAscii(lines[i].shortName);
        options.emplace_back(label, [&chosen, i]() { chosen = (int)i; });
    }
    loopOptions(options, MENU_TYPE_SUBMENU, modeLabel(mode), start);

    if (chosen < 0) return false;
    out = lines[chosen];
    lastLineId = out.id;
    return true;
}

bool selectStop(const Line &line, Stop &out) {
    std::vector<Stop> stops;
    if (!idfmLoadStops(line, stops)) {
        Serial.printf("[idfm] stops_%s.json missing, built-in stops used\n", line.id.c_str());
    }
    if (stops.empty()) {
        displayWarning("No stop: copy /idfm to SD", true);
        return false;
    }

    int start = 0;
    if (lastStopLineId == line.id && lastStopIndex < (int)stops.size()) start = lastStopIndex;

    int chosen = -1;
    std::vector<Option> options;
    options.reserve(stops.size());
    for (size_t i = 0; i < stops.size(); i++) {
        options.emplace_back(
            toAscii(stops[i].name),
            [&chosen, i]() { chosen = (int)i; },
            false,
            stopListHover,
            const_cast<Line *>(&line)
        );
    }
    // MENU_TYPE_REGULAR: 6 visible rows with a scrolling window and marquee for long names,
    // better suited to 20-75 stops than the 3-row submenu.
    loopOptions(options, MENU_TYPE_REGULAR, "", start);

    if (chosen < 0) return false;
    out = stops[chosen];
    lastStopLineId = line.id;
    lastStopIndex = chosen;
    return true;
}
