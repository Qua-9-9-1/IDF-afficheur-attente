#pragma once
// Line, stop and direction selection screens (rotary encoder + click, Bruce loopOptions).

#include "idfm_model.h"

// Lists the lines of `mode` and lets the user pick one.
// Returns false when the user goes back (Esc) or no line is available.
bool selectLine(TransportMode mode, Line &out);

// Lists the stops of `line` in route order and lets the user pick one.
// Returns false when the user goes back (Esc) or no stop is available.
bool selectStop(const Line &line, Stop &out);

// Lets the user pick the direction (= platform) of `stop` among stop.directions ("Direction
// La Défense"...), then sets stop.direction and stop.monitoringRef (platform ref) accordingly.
// No UI when there is nothing to choose: returns true, `stop` unchanged when directions is empty
// (RER: stop area ref), the single direction applied when there is only one (e.g. at a terminus).
// Returns false when the user goes back (Esc).
bool selectDirection(const Line &line, Stop &stop);
