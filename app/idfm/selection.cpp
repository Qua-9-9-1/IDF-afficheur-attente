#include "selection.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "referential.h"
#include <globals.h>

#include <algorithm>
#include <string.h>

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

// What the user is choosing in the stop list, shown next to the line pictogram.
const char *stopKindLabel(TransportMode mode) {
    switch (mode) {
        case TransportMode::Rer:
        case TransportMode::Transilien: return "Gare de depart";
        case TransportMode::Bus: return "Arret de depart";
        default: return "Station de depart";
    }
}

// Draws the line pictogram ("RER [A]") centred in the top bar, between clock and battery,
// optionally followed by `label` ("Gare de depart"...). With a label the mode name is left out
// to keep the header narrow (the pictogram already identifies the line).
// Must stay above y=20: MENU_TYPE_REGULAR draws its 6-row box from there on a 170 px screen.
void drawLineHeader(const Line &line, const char *label = nullptr) {
    const int y = 7;
    const int h = 12;
    String mode = label ? String() : String(modeLabel(line.mode));
    String text = label ? String(label) : String();
    String name = toAscii(line.shortName);
    tft.setTextSize(1);
    int modeW = mode.length() ? mode.length() * LW + 4 : 0;
    int pillW = name.length() * LW + 8;
    int textW = text.length() ? 4 + text.length() * LW : 0;
    int x = (tftWidth - modeW - pillW - textW) / 2;

    tft.fillRect(x - 2, y, modeW + pillW + textW + 4, h, bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    if (modeW) tft.drawString(mode, x, y + 2);
    x += modeW;
    // RER: square-ish pictogram; metro: round roundel (pill for "3B", "10"...).
    tft.fillRoundRect(x, y, pillW, h, line.mode == TransportMode::Metro ? h / 2 : 3, line.color);
    tft.setTextColor(line.textColor, line.color);
    tft.drawString(name, x + 4, y + 2);
    if (textW) {
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.drawString(text, x + pillW + 4, y + 2);
    }
}

// loopOptions hover hook: draws the line header (pointer = the Line), then lets loopOptions draw
// the menu. Used by the line menu (header of the hovered line).
bool lineHeaderHover(void *ptr, bool shouldRender) {
    if (shouldRender && ptr != nullptr) drawLineHeader(*static_cast<const Line *>(ptr));
    return false;
}

// Header of the stop and direction lists: line pictogram + what is being chosen.
struct LabeledHeader {
    const Line *line;
    const char *label;
};
bool labeledHeaderHover(void *ptr, bool shouldRender) {
    if (shouldRender && ptr != nullptr) {
        const LabeledHeader *h = static_cast<const LabeledHeader *>(ptr);
        drawLineHeader(*h->line, h->label);
    }
    return false;
}

// Sort key of a stop name: ASCII, lower case (alphabetical order ignoring accents and case).
String sortKey(const String &name) {
    String k = toAscii(name);
    k.toLowerCase();
    return k;
}

// Remember the last choice to reopen the menus where the user left them.
String lastLineId;
String lastStopLineId;
String lastStopName; // by name: the list is sorted alphabetically, not in route order
String lastDirectionKey;  // line id + stop name of the last direction choice
String lastDirectionName;

} // namespace

// Grouped line selection (bus, ~1900 lines): top-level groups ("20-99", "100-149", "1000-1999",
// "Noctilien", "Autres"...) -> sub-groups when any ("1100-1199 Vexin") -> lines of the group.
// Esc goes up one level; reopens directly on the last group's line list.
int lastGroup = -1;
TransportMode lastGroupMode = TransportMode::Metro;
int lastTopIndex = 0;
int lastSubIndex = 0;

bool selectGroupedLine(TransportMode mode, const std::vector<LineGroup> &groups, Line &out) {
    // Top-level entries: a group without parent, or a parent shared by several groups.
    std::vector<String> tops;
    std::vector<int> topGroup; // group index, -1 for a parent entry
    for (size_t i = 0; i < groups.size(); i++) {
        const String &label = groups[i].parent.isEmpty() ? groups[i].name : groups[i].parent;
        bool seen = false;
        for (const String &t : tops) seen = seen || t == label;
        if (seen) continue;
        tops.push_back(label);
        topGroup.push_back(groups[i].parent.isEmpty() ? (int)i : -1);
    }

    int group = (lastGroupMode == mode && lastGroup < (int)groups.size()) ? lastGroup : -1;
    String parent = group >= 0 ? groups[group].parent : String();
    const String title = modeLabel(mode);
    while (true) {
        if (group < 0 && parent.isEmpty()) { // level 1: top-level entries
            int chosen = -1;
            std::vector<Option> options;
            options.reserve(tops.size());
            for (size_t i = 0; i < tops.size(); i++) {
                String label = toAscii(tops[i]);
                if (topGroup[i] < 0) label += " >";
                options.emplace_back(label, [&chosen, i]() { chosen = (int)i; });
            }
            loopOptions(options, MENU_TYPE_REGULAR, title.c_str(),
                        lastTopIndex < (int)tops.size() ? lastTopIndex : 0);
            if (chosen < 0) { // leaving the mode: next time, start from the top-level list
                lastGroup = -1;
                return false;
            }
            lastTopIndex = chosen;
            if (topGroup[chosen] >= 0) group = topGroup[chosen];
            else parent = tops[chosen];
            continue;
        }
        if (group < 0) { // level 2: groups of `parent`
            std::vector<int> subs;
            for (size_t i = 0; i < groups.size(); i++) {
                if (groups[i].parent == parent) subs.push_back((int)i);
            }
            int chosen = -1;
            std::vector<Option> options;
            options.reserve(subs.size());
            int start = 0;
            for (size_t i = 0; i < subs.size(); i++) {
                if (subs[i] == lastGroup) start = (int)i;
                options.emplace_back(toAscii(groups[subs[i]].name), [&chosen, i]() { chosen = (int)i; });
            }
            loopOptions(options, MENU_TYPE_REGULAR, toAscii(parent).c_str(), start);
            if (chosen < 0) {
                parent = "";
                continue;
            }
            group = subs[chosen];
            continue;
        }
        // level 3: lines of `group`
        std::vector<Line> lines;
        std::vector<String> labels;
        if (!idfmLoadGroupLines(mode, group, lines, &labels) || lines.empty()) {
            displayWarning("No line in this group", true);
            group = -1;
            continue;
        }
        int start = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            if (lines[i].id == lastLineId) start = (int)i;
        }
        int chosen = -1;
        std::vector<Option> options;
        options.reserve(lines.size());
        for (size_t i = 0; i < lines.size(); i++) {
            String label = title + " " + toAscii(labels[i].isEmpty() ? lines[i].shortName : labels[i]);
            options.emplace_back(label, [&chosen, i]() { chosen = (int)i; }, false, lineHeaderHover, &lines[i]);
        }
        loopOptions(options, MENU_TYPE_REGULAR, "", start);
        if (chosen < 0) { // back to the group list this group came from
            lastGroup = group;
            lastGroupMode = mode;
            group = -1;
            continue;
        }
        out = lines[chosen];
        lastLineId = out.id;
        lastGroup = group;
        lastGroupMode = mode;
        return true;
    }
}

bool selectLine(TransportMode mode, Line &out) {
    std::vector<LineGroup> groups;
    if (idfmLoadLineGroups(mode, groups)) return selectGroupedLine(mode, groups, out);

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
        // "RER A", "Metro 1", "Metro 3B"... with the line pictogram of the hovered line in the top bar.
        String label = String(modeLabel(mode)) + " " + toAscii(lines[i].shortName);
        options.emplace_back(label, [&chosen, i]() { chosen = (int)i; }, false, lineHeaderHover, &lines[i]);
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

    // Alphabetical order (accents and case ignored): easier to find a stop with the encoder
    // than the route order.
    std::vector<String> keys;
    keys.reserve(stops.size());
    for (const Stop &s : stops) keys.push_back(sortKey(s.name));
    std::vector<size_t> order(stops.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&keys](size_t a, size_t b) {
        return strcmp(keys[a].c_str(), keys[b].c_str()) < 0;
    });

    int start = 0;
    if (lastStopLineId == line.id) {
        for (size_t i = 0; i < order.size(); i++) {
            if (stops[order[i]].name == lastStopName) start = (int)i;
        }
    }

    LabeledHeader header = {&line, stopKindLabel(line.mode)};
    int chosen = -1;
    std::vector<Option> options;
    options.reserve(stops.size());
    for (size_t i = 0; i < order.size(); i++) {
        const size_t idx = order[i];
        options.emplace_back(
            toAscii(stops[idx].name), [&chosen, idx]() { chosen = (int)idx; }, false, labeledHeaderHover, &header
        );
    }
    // MENU_TYPE_REGULAR: 6 visible rows with a scrolling window and marquee for long names,
    // better suited to 20-75 stops than the 3-row submenu.
    loopOptions(options, MENU_TYPE_REGULAR, "", start);

    if (chosen < 0) return false;
    out = stops[chosen];
    lastStopLineId = line.id;
    lastStopName = out.name;
    return true;
}

bool selectDirection(const Line &line, Stop &stop) {
    if (stop.directions.empty()) return true; // stop area display (RER): nothing to choose
    int chosen = 0;
    if (stop.directions.size() > 1) {
        const String key = line.id + "|" + stop.name;
        int start = 0;
        for (size_t i = 0; i < stop.directions.size(); i++) {
            const String &name = stop.directions[i].name;
            if (name == stop.direction || (key == lastDirectionKey && name == lastDirectionName)) start = i;
        }

        chosen = -1;
        LabeledHeader header = {&line, "Direction"};
        std::vector<Option> options;
        options.reserve(stop.directions.size());
        for (size_t i = 0; i < stop.directions.size(); i++) {
            options.emplace_back(
                "Direction " + toAscii(stop.directions[i].name),
                [&chosen, i]() { chosen = (int)i; },
                false,
                labeledHeaderHover,
                &header
            );
        }
        // Regular list: branched lines have long labels ("Direction Saint-Denis - Universite /
        // Asnieres - Gennevilliers - Les Courtilles"), scrolled by the marquee of this menu type.
        loopOptions(options, MENU_TYPE_REGULAR, "", start);
        if (chosen < 0) return false;
        lastDirectionKey = key;
        lastDirectionName = stop.directions[chosen].name;
    }
    stop.direction = stop.directions[chosen].name;
    stop.monitoringRef = stop.directions[chosen].monitoringRef;
    return true;
}
