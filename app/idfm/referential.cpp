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
            const char *id = obj["id"] | "";
            if (*id == '\0') continue;
            Line line;
            line.id = id;
            line.shortName = obj["shortName"] | id;
            line.color = idfmHexToRgb565(obj["color"] | "", kDefaultColor);
            line.textColor = idfmHexToRgb565(obj["textColor"] | "", kDefaultTextColor);
            line.mode = lineMode;
            out.push_back(line);
        }
        // A file without any line of this mode (e.g. generated for other modes only)
        // falls back to the built-in list, so the RER menu always works.
        if (!out.empty()) return true;
    }
    loadBuiltinLines(mode, out);
    return false;
}

bool idfmLoadStops(const Line &line, std::vector<Stop> &out) {
    out.clear();
    JsonDocument doc;
    if (readJsonFile(String(IDFM_REFERENTIAL_DIR "/stops_") + line.id + ".json", doc)) {
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
