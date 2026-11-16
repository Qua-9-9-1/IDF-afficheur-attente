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
// Metro, tram and bus stops also carry their directions, one per platform / physical stop
// (-> Stop::directions):
//     {"name":"Châtelet","ref":"STIF:StopArea:SP:42587:",
//      "dirs":[{"name":"Porte de Clignancourt","ref":"STIF:StopPoint:Q:463158:"},
//              {"name":"Bagneux - Lucie Aubrac","ref":"STIF:StopPoint:Q:22092:"}]}
// Stops are in route order (main route end to end, then each extra branch).
// Bus lines (~1900) are not in lines.json but in their own file, split into selection groups
// of <= ~60 lines ("in" = parent menu entry, "" at the top level; "label" = optional menu label
// for homonym lines):
//   /idfm/lines_bus.json       {"v":1,"mode":"BUS","groups":[{"name":"20-99","in":"",
//                                "lines":[{"id":"C01107","shortName":"72","color":"FF1400",
//                                          "textColor":"FFFFFF"}, ...]},
//                               {"name":"1100-1199 Vexin","in":"1000-1999","lines":[...]}, ...]}
// See docs/REFERENTIEL.md.

#include "idfm_model.h"
#include <Arduino.h>
#include <vector>

#define IDFM_REFERENTIAL_DIR "/idfm"
#define IDFM_BUS_LINES_PATH IDFM_REFERENTIAL_DIR "/lines_bus.json"

// A selection group of lines (bus only): "20-99", "1100-1199 Vexin" under parent "1000-1999"...
struct LineGroup {
    String name;
    String parent; // parent menu entry, empty for a top-level group
};

// Mode key used in lines.json ("METRO", "RER", "TRANSILIEN", "TRAM", "BUS").
const char *idfmModeKey(TransportMode mode);
bool idfmParseModeKey(const char *key, TransportMode &mode);

// "EB2132" or "#EB2132" -> RGB565. Returns fallback when the string is not a valid colour.
uint16_t idfmHexToRgb565(const char *hex, uint16_t fallback);

// Fills `out` with the lines of `mode`, in file order (bus: every group of lines_bus.json,
// streamed — prefer idfmLoadLineGroups() + idfmLoadGroupLines() for menus).
// When /idfm/lines.json is missing or unreadable, built-in lines are used (RER A-E, metro 1-14).
// Returns true when the data comes from a file, false when the built-in fallback was used.
bool idfmLoadLines(TransportMode mode, std::vector<Line> &out);

// Selection groups of `mode`, in menu order (bus only). Returns false (and an empty `out`) when
// the mode is not grouped or lines_bus.json is missing: list idfmLoadLines() directly then.
bool idfmLoadLineGroups(TransportMode mode, std::vector<LineGroup> &out);

// Lines of group `group` (index in idfmLoadLineGroups()). `labels` (optional) receives one menu
// label per line, empty when the short name is enough. Returns false when nothing was found.
bool idfmLoadGroupLines(TransportMode mode, int group, std::vector<Line> &out,
                        std::vector<String> *labels = nullptr);

// Fills `out` with the stops of `line`, in route order, from /idfm/stops_<line.id>.json.
// Each stop's monitoringRef is its stop area; metro / tram / bus stops also fill `directions`
// (platform or physical stop refs),
// to be chosen with selectDirection(). When the file is missing, a few major built-in stations
// are used (RER A-E, metro 1, 4, 14).
// Returns true when the data comes from a file, false when the built-in fallback was used
// (in that case `out` may be empty).
bool idfmLoadStops(const Line &line, std::vector<Stop> &out);

// Looks up one line by its bare id ("C01742") in the referential, any mode (used to name and
// colour the lines of multi-line boards). Cheap: streamed raw search with early exit in
// lines.json then lines_bus.json, only the matching entry is parsed. Returns false if unknown.
bool idfmFindLine(const String &lineId, Line &out);
