#include "referential.h"

#include "core/sd_functions.h"
#include <ArduinoJson.h>
#include <globals.h>

namespace {

constexpr uint16_t kDefaultColor = 0x7BEF;     // dark grey (RGB565)
constexpr uint16_t kDefaultTextColor = 0xFFFF; // white

struct BuiltinLine {
    const char *id;
    const char *shortName;
    const char *color;
    const char *textColor;
    TransportMode mode;
};

// Colours and ids from the IDFM "referentiel-des-lignes" dataset (2026-11).
const BuiltinLine kBuiltinLines[] = {
    {"C01742", "A", "EB2132", "FFFFFF", TransportMode::Rer},
    {"C01743", "B", "5091CB", "FFFFFF", TransportMode::Rer},
    {"C01727", "C", "FFCC30", "000000", TransportMode::Rer},
    {"C01728", "D", "008B5B", "FFFFFF", TransportMode::Rer},
    {"C01729", "E", "B94E9A", "FFFFFF", TransportMode::Rer},
    {"C01371", "1",  "FFBE00", "000000", TransportMode::Metro},
    {"C01372", "2",  "0055C8", "FFFFFF", TransportMode::Metro},
    {"C01373", "3",  "6E6E00", "FFFFFF", TransportMode::Metro},
    {"C01386", "3B", "82C8E6", "000000", TransportMode::Metro},
    {"C01374", "4",  "A0006E", "FFFFFF", TransportMode::Metro},
    {"C01375", "5",  "FF5A00", "000000", TransportMode::Metro},
    {"C01376", "6",  "82DC73", "000000", TransportMode::Metro},
    {"C01377", "7",  "FF82B4", "000000", TransportMode::Metro},
    {"C01387", "7B", "82DC73", "000000", TransportMode::Metro},
    {"C01378", "8",  "D282BE", "000000", TransportMode::Metro},
    {"C01379", "9",  "D2D200", "000000", TransportMode::Metro},
    {"C01380", "10", "DC9600", "000000", TransportMode::Metro},
    {"C01381", "11", "6E491E", "FFFFFF", TransportMode::Metro},
    {"C01382", "12", "00643C", "FFFFFF", TransportMode::Metro},
    {"C01383", "13", "82C8E6", "000000", TransportMode::Metro},
    {"C01384", "14", "640082", "FFFFFF", TransportMode::Metro},
};

struct BuiltinStop {
    const char *lineId;
    const char *name;
    const char *zdaId; // zone d'arret id -> STIF:StopArea:SP:<id>:
};

// A few major stations per RER line, so the app stays usable without the SD files.
// Ids from the IDFM "arrets-lignes" / "zones-d-arrets" datasets (2026-11).
const BuiltinStop kBuiltinStops[] = {
    {"C01742", "Charles de Gaulle - Étoile", "58759" },
    {"C01742", "Auber",                      "45873" },
    {"C01742", "Châtelet - Les Halles",      "45102" },
    {"C01742", "Gare de Lyon",               "470195"},
    {"C01742", "Nation",                     "473875"},
    {"C01742", "La Défense",                 "470549"},
    {"C01743", "Aéroport Charles de Gaulle 2 (Terminal 2)", "473364"},
    {"C01743", "Gare du Nord",               "462394"},
    {"C01743", "Châtelet - Les Halles",      "45102" },
    {"C01743", "Denfert-Rochereau",          "473890"},
    {"C01743", "Massy - Palaiseau",          "58774" },
    {"C01727", "Invalides",                  "470540"},
    {"C01727", "Saint-Michel Notre-Dame",    "44877" },
    {"C01727", "Gare d'Austerlitz",          "43072" },
    {"C01727", "Versailles Château Rive Gauche", "43220"},
    {"C01728", "Gare du Nord",               "462394"},
    {"C01728", "Châtelet - Les Halles",      "45102" },
    {"C01728", "Gare de Lyon",               "470195"},
    {"C01729", "La Défense",                 "470549"},
    {"C01729", "Haussmann Saint-Lazare",     "58718" },
    {"C01729", "Magenta",                    "58572" },
};

struct BuiltinPlatformStop {
    const char *lineId;
    const char *name;
    const char *zdaId;      // zone d'arret id -> STIF:StopArea:SP:<id>:
    const char *dirs[2][2]; // {direction name, quay id -> STIF:StopPoint:Q:<id>:}, unused: {nullptr, nullptr}
};

// A few metro stations (one platform per direction), from GTFS IDFM (2026-11).
const BuiltinPlatformStop kBuiltinPlatformStops[] = {
    {"C01371", "La Défense",             "470548", {{"Château de Vincennes", "22101"}, {nullptr, nullptr}}         },
    {"C01371", "Châtelet",               "42587",  {{"Château de Vincennes", "22087"}, {"La Défense", "463160"}}   },
    {"C01371", "Gare de Lyon",           "43153",  {{"Château de Vincennes", "22083"}, {"La Défense", "463197"}}   },
    {"C01371", "Nation",                 "462358", {{"Château de Vincennes", "22080"}, {"La Défense", "462943"}}   },
    {"C01374", "Montparnasse Bienvenue", "58578",  {{"Porte de Clignancourt", "462996"}, {"Bagneux - Lucie Aubrac", "22143"}}},
    {"C01374", "Châtelet",               "42587",  {{"Porte de Clignancourt", "463158"}, {"Bagneux - Lucie Aubrac", "22092"}}},
    {"C01374", "Gare du Nord",           "462941", {{"Porte de Clignancourt", "463183"}, {"Bagneux - Lucie Aubrac", "22149"}}},
    {"C01384", "Gare de Lyon",           "43153",  {{"Saint-Denis - Pleyel", "21958"}, {"Aéroport d'Orly", "463046"}}},
    {"C01384", "Châtelet",               "42587",  {{"Saint-Denis - Pleyel", "463159"}, {"Aéroport d'Orly", "21959"}}},
    {"C01384", "Saint-Lazare",           "462374", {{"Saint-Denis - Pleyel", "21962"}, {"Aéroport d'Orly", "462972"}}},
};

const char *const kModeKeys[] = {"METRO", "RER", "TRANSILIEN", "TRAM", "BUS"};
const TransportMode kModes[] = {
    TransportMode::Metro, TransportMode::Rer, TransportMode::Transilien, TransportMode::Tram, TransportMode::Bus
};

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Opens `path` from the SD card, or from LittleFS when the SD card is absent or lacks it.
bool openReferentialFile(const String &path, File &file) {
    if (setupSdCard() && SD.exists(path)) {
        file = SD.open(path, FILE_READ);
        if (file) return true;
    }
    if (LittleFS.exists(path)) {
        file = LittleFS.open(path, FILE_READ);
        if (file) return true;
    }
    return false;
}

bool readJsonFile(const String &path, JsonDocument &doc) {
    File file;
    if (!openReferentialFile(path, file)) {
        Serial.printf("[idfm] %s not found\n", path.c_str());
        return false;
    }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[idfm] %s: JSON error %s\n", path.c_str(), err.c_str());
        return false;
    }
    return true;
}

// Buffered byte reader over a File: chunked reads (fast on the SD card), and an ArduinoJson
// "custom reader" (read() + readBytes()) so big files can be parsed one element at a time.
class BufferedReader {
public:
    explicit BufferedReader(File &file) : file_(file) {}
    int read() {
        if (pos_ >= len_) {
            len_ = file_.readBytes(buf_, sizeof(buf_));
            pos_ = 0;
            if (len_ == 0) return -1;
        }
        return (uint8_t)buf_[pos_++];
    }
    size_t readBytes(char *out, size_t n) {
        size_t k = 0;
        for (int c; k < n && (c = read()) >= 0;) out[k++] = (char)c;
        return k;
    }
    // Next non-blank character (consumed), -1 at the end of the file.
    int nextToken() {
        int c;
        do { c = read(); } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
        return c;
    }
    // Consumes the input up to and including `needle` (KMP: no false miss on partial matches).
    // Returns false when the end of the file is reached first.
    bool skipPast(const char *needle) {
        const size_t n = strlen(needle);
        if (n == 0 || n > 64) return false;
        uint8_t fail[64];
        fail[0] = 0;
        for (size_t i = 1, k = 0; i < n; i++) {
            while (k > 0 && needle[i] != needle[k]) k = fail[k - 1];
            if (needle[i] == needle[k]) k++;
            fail[i] = (uint8_t)k;
        }
        size_t matched = 0;
        for (int c; (c = read()) >= 0;) {
            while (matched > 0 && (char)c != needle[matched]) matched = fail[matched - 1];
            if ((char)c == needle[matched]) matched++;
            if (matched == n) return true;
        }
        return false;
    }

private:
    File &file_;
    char buf_[512];
    size_t len_ = 0;
    size_t pos_ = 0;
};

Line lineFromJson(JsonObjectConst obj, TransportMode mode) {
    const char *id = obj["id"] | "";
    Line line;
    line.id = id;
    line.shortName = obj["shortName"] | id;
    line.color = idfmHexToRgb565(obj["color"] | "", kDefaultColor);
    line.textColor = idfmHexToRgb565(obj["textColor"] | "", kDefaultTextColor);
    line.mode = mode;
    return line;
}

// Walks the "groups" array of /idfm/lines_bus.json one group at a time (the file holds ~1900
// lines, ~140 KB: never loaded at once). For each group, `visit(index, group)` gets the group
// parsed with `filter` and returns false to stop. Returns false when the file is missing/invalid.
template <typename Visitor>
bool forEachLineGroup(const JsonDocument &filter, Visitor visit) {
    File file;
    if (!openReferentialFile(IDFM_BUS_LINES_PATH, file)) return false;
    BufferedReader in(file);
    bool ok = in.skipPast("\"groups\":[");
    for (int index = 0; ok; index++) {
        JsonDocument group;
        DeserializationError err =
            deserializeJson(group, in, DeserializationOption::Filter(filter));
        if (err) {
            Serial.printf("[idfm] " IDFM_BUS_LINES_PATH ": JSON error %s\n", err.c_str());
            ok = false;
            break;
        }
        if (!visit(index, group.as<JsonObjectConst>())) break;
        if (in.nextToken() != ',') break; // ']' (or end of file): last group
    }
    file.close();
    return ok;
}

// Raw search of `"id":"<lineId>"` in a lines file written by gen_referential.py (compact JSON,
// flat line objects starting with "id"): no JSON parsing until the line is found.
bool findLineInFile(const char *path, const String &lineId, TransportMode defaultMode, Line &out) {
    File file;
    if (!openReferentialFile(path, file)) return false;
    BufferedReader in(file);
    const String needle = "\"id\":\"" + lineId + "\"";
    bool found = false;
    if (in.skipPast(needle.c_str())) {
        String obj = "{" + needle;
        for (int c; obj.length() < 400 && (c = in.read()) >= 0;) {
            obj += (char)c;
            if (c == '}') break;
        }
        JsonDocument doc;
        if (!deserializeJson(doc, obj)) {
            TransportMode mode = defaultMode;
            idfmParseModeKey(doc["mode"] | "", mode);
            out = lineFromJson(doc.as<JsonObjectConst>(), mode);
            found = true;
        }
    }
    file.close();
    return found;
}

void loadBuiltinLines(TransportMode mode, std::vector<Line> &out) {
    for (const BuiltinLine &b : kBuiltinLines) {
        if (b.mode != mode) continue;
        Line line;
        line.id = b.id;
        line.shortName = b.shortName;
        line.color = idfmHexToRgb565(b.color, kDefaultColor);
        line.textColor = idfmHexToRgb565(b.textColor, kDefaultTextColor);
        line.mode = b.mode;
        out.push_back(line);
    }
}

} // namespace

const char *idfmModeKey(TransportMode mode) {
    for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); i++) {
        if (kModes[i] == mode) return kModeKeys[i];
    }
    return "";
}

bool idfmParseModeKey(const char *key, TransportMode &mode) {
    if (key == nullptr) return false;
    for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); i++) {
        if (strcasecmp(key, kModeKeys[i]) == 0) {
            mode = kModes[i];
            return true;
        }
    }
    return false;
}

uint16_t idfmHexToRgb565(const char *hex, uint16_t fallback) {
    if (hex == nullptr) return fallback;
    if (*hex == '#') hex++;
    uint8_t rgb[3];
    for (int i = 0; i < 3; i++) {
        int hi = hexNibble(hex[2 * i]);
        if (hi < 0) return fallback;
        int lo = hexNibble(hex[2 * i + 1]);
        if (lo < 0) return fallback;
        rgb[i] = (uint8_t)((hi << 4) | lo);
    }
    if (hex[6] != '\0') return fallback;
    return (uint16_t)(((rgb[0] & 0xF8) << 8) | ((rgb[1] & 0xFC) << 3) | (rgb[2] >> 3));
}

bool idfmLoadLines(TransportMode mode, std::vector<Line> &out) {
    out.clear();
    JsonDocument doc;
    if (readJsonFile(IDFM_REFERENTIAL_DIR "/lines.json", doc)) {
        for (JsonObject obj : doc["lines"].as<JsonArray>()) {
            TransportMode lineMode;
            if (!idfmParseModeKey(obj["mode"] | "", lineMode) || lineMode != mode) continue;
            if (*(obj["id"] | "") == '\0') continue;
            out.push_back(lineFromJson(obj, lineMode));
        }
    }
    if (mode == TransportMode::Bus) {
        // Bus lines live in their own grouped file (streamed: ~1900 lines).
        JsonDocument filter;
        filter["lines"][0]["id"] = true;
        filter["lines"][0]["shortName"] = true;
        filter["lines"][0]["color"] = true;
        filter["lines"][0]["textColor"] = true;
        forEachLineGroup(filter, [&out](int, JsonObjectConst group) {
            for (JsonObjectConst obj : group["lines"].as<JsonArrayConst>()) {
                if (*(obj["id"] | "") != '\0') out.push_back(lineFromJson(obj, TransportMode::Bus));
            }
            return true;
        });
    }
    // A file without any line of this mode (e.g. generated for other modes only)
    // falls back to the built-in list, so the RER menu always works.
    if (!out.empty()) return true;
    loadBuiltinLines(mode, out);
    return false;
}

bool idfmLoadLineGroups(TransportMode mode, std::vector<LineGroup> &out) {
    out.clear();
    if (mode != TransportMode::Bus) return false;
    JsonDocument filter;
    filter["name"] = true;
    filter["in"] = true;
    forEachLineGroup(filter, [&out](int, JsonObjectConst group) {
        LineGroup g;
        g.name = group["name"] | "";
        g.parent = group["in"] | "";
        out.push_back(g);
        return true;
    });
    return !out.empty();
}

bool idfmLoadGroupLines(TransportMode mode, int group, std::vector<Line> &out, std::vector<String> *labels) {
    out.clear();
    if (labels) labels->clear();
    if (mode != TransportMode::Bus || group < 0) return false;
    // Groups before the wanted one are parsed with a names-only filter (their lines are skipped
    // without being stored), the wanted one in full (<= ~60 lines).
    JsonDocument skip;
    skip["name"] = true;
    bool found = false;
    File file;
    if (!openReferentialFile(IDFM_BUS_LINES_PATH, file)) return false;
    BufferedReader in(file);
    bool ok = in.skipPast("\"groups\":[");
    for (int index = 0; ok && index <= group; index++) {
        JsonDocument doc;
        DeserializationError err = index == group
                                       ? deserializeJson(doc, in)
                                       : deserializeJson(doc, in, DeserializationOption::Filter(skip));
        if (err) {
            Serial.printf("[idfm] " IDFM_BUS_LINES_PATH ": JSON error %s\n", err.c_str());
            break;
        }
        if (index == group) {
            for (JsonObjectConst obj : doc["lines"].as<JsonArrayConst>()) {
                if (*(obj["id"] | "") == '\0') continue;
                out.push_back(lineFromJson(obj, TransportMode::Bus));
                if (labels) labels->push_back(obj["label"] | "");
            }
            found = !out.empty();
            break;
        }
        ok = in.nextToken() == ',';
    }
    file.close();
    return found;
}

bool idfmLoadStops(const Line &line, std::vector<Stop> &out) {
    out.clear();
    JsonDocument doc;
    // Bus stop files live in a sub-directory (see stops_path() in scripts/gen_referential.py).
    const String dir = line.mode == TransportMode::Bus ? IDFM_REFERENTIAL_DIR "/bus" : IDFM_REFERENTIAL_DIR;
    if (readJsonFile(dir + "/stops_" + line.id + ".json", doc)) {
        JsonArray stops = doc["stops"].as<JsonArray>();
        out.reserve(stops.size());
        for (JsonObject obj : stops) {
            const char *ref = obj["ref"] | "";
            if (*ref == '\0') continue;
            Stop stop;
            stop.name = obj["name"] | ref;
            stop.monitoringRef = ref;
            JsonArray dirs = obj["dirs"].as<JsonArray>(); // metro: one entry per platform
            stop.directions.reserve(dirs.size());
            for (JsonObject d : dirs) {
                const char *dirRef = d["ref"] | "";
                if (*dirRef == '\0') continue;
                Direction dir;
                dir.name = d["name"] | "";
                dir.monitoringRef = dirRef;
                stop.directions.push_back(dir);
            }
            out.push_back(stop);
        }
        if (!out.empty()) return true;
    }
    for (const BuiltinStop &b : kBuiltinStops) {
        if (line.id != b.lineId) continue;
        Stop stop;
        stop.name = b.name;
        stop.monitoringRef = String("STIF:StopArea:SP:") + b.zdaId + ":";
        out.push_back(stop);
    }
    for (const BuiltinPlatformStop &b : kBuiltinPlatformStops) {
        if (line.id != b.lineId) continue;
        Stop stop;
        stop.name = b.name;
        stop.monitoringRef = String("STIF:StopArea:SP:") + b.zdaId + ":";
        for (const auto &d : b.dirs) {
            if (d[0] == nullptr) continue;
            Direction dir;
            dir.name = d[0];
            dir.monitoringRef = String("STIF:StopPoint:Q:") + d[1] + ":";
            stop.directions.push_back(dir);
        }
        out.push_back(stop);
    }
    return false;
}


// Streamed raw search, early exit: lines.json (~4 KB) then lines_bus.json (~140 KB), only the
// matching line object is parsed; memory use is constant. Built-in lines when the files are absent.
bool idfmFindLine(const String &lineId, Line &out) {
    if (lineId.isEmpty() || lineId.indexOf('"') >= 0) return false;
    if (findLineInFile(IDFM_REFERENTIAL_DIR "/lines.json", lineId, TransportMode::Rer, out)) return true;
    if (findLineInFile(IDFM_BUS_LINES_PATH, lineId, TransportMode::Bus, out)) return true;
    for (const BuiltinLine &b : kBuiltinLines) {
        if (lineId != b.id) continue;
        out.id = b.id;
        out.shortName = b.shortName;
        out.color = idfmHexToRgb565(b.color, kDefaultColor);
        out.textColor = idfmHexToRgb565(b.textColor, kDefaultTextColor);
        out.mode = b.mode;
        return true;
    }
    return false;
}