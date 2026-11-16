#pragma once
// Line / stop referential of the IDFM board app.
//
// Files are produced offline by scripts/gen_referential.py and copied to the SD card
// (LittleFS is used as a fallback location, same paths):
//   /idfm/lines.json           {"v":1,"lines":[{"id":"C01742","shortName":"A","mode":"RER",
//                                               "color":"EB2132","textColor":"FFFFFF"}, ...]}
//   /idfm/stops_<lineId>.json  {"v":1,"line":"C01742",
//                               "stops":[{"name":"Châtelet - Les Halles",
//                                         "ref":"STIF:StopArea:SP:45102:"}, ...]}
// Metro stops also carry their directions, one per platform (-> Stop::directions):
//     {"name":"Châtelet","ref":"STIF:StopArea:SP:42587:",
//      "dirs":[{"name":"Porte de Clignancourt","ref":"STIF:StopPoint:Q:463158:"},
//              {"name":"Bagneux - Lucie Aubrac","ref":"STIF:StopPoint:Q:22092:"}]}
// Stops are in route order (main route end to end, then each extra branch).
// See docs/REFERENTIEL.md.

#include "idfm_model.h"
#include <Arduino.h>
#include <vector>

#define IDFM_REFERENTIAL_DIR "/idfm"

// Mode key used in lines.json ("METRO", "RER", "TRANSILIEN", "TRAM", "BUS").
const char *idfmModeKey(TransportMode mode);
bool idfmParseModeKey(const char *key, TransportMode &mode);

// "EB2132" or "#EB2132" -> RGB565. Returns fallback when the string is not a valid colour.
uint16_t idfmHexToRgb565(const char *hex, uint16_t fallback);

// Fills `out` with the lines of `mode`, in file order.
// When /idfm/lines.json is missing or unreadable, built-in lines are used (RER A-E, metro 1-14).
// Returns true when the data comes from a file, false when the built-in fallback was used.
bool idfmLoadLines(TransportMode mode, std::vector<Line> &out);

// Fills `out` with the stops of `line`, in route order, from /idfm/stops_<line.id>.json.
// Each stop's monitoringRef is its stop area; metro stops also fill `directions` (platform refs),
// to be chosen with selectDirection(). When the file is missing, a few major built-in stations
// are used (RER A-E, metro 1, 4, 14).
// Returns true when the data comes from a file, false when the built-in fallback was used
// (in that case `out` may be empty).
bool idfmLoadStops(const Line &line, std::vector<Stop> &out);

// Looks up one line by its bare id ("C01742") in the referential, any mode (used to name and
// colour the lines of multi-line boards). Must stay cheap on the device even with ~1500 bus
// lines (streamed/filtered parsing). Returns false if unknown.
bool idfmFindLine(const String &lineId, Line &out);
