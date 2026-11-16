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
    return false;
}
