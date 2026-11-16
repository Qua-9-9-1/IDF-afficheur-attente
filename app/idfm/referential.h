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
// When /idfm/lines.json is missing or unreadable, built-in lines are used (RER A-E only).
// Returns true when the data comes from a file, false when the built-in fallback was used.
bool idfmLoadLines(TransportMode mode, std::vector<Line> &out);

// Fills `out` with the stops of `line`, in route order, from /idfm/stops_<line.id>.json.
// When the file is missing, a few major built-in stations are used (RER only).
// Returns true when the data comes from a file, false when the built-in fallback was used
// (in that case `out` may be empty).
bool idfmLoadStops(const Line &line, std::vector<Stop> &out);
