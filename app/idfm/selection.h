#pragma once
// Line and stop selection screens (rotary encoder + click, Bruce loopOptions).

#include "idfm_model.h"

// Lists the lines of `mode` and lets the user pick one.
// Returns false when the user goes back (Esc) or no line is available.
bool selectLine(TransportMode mode, Line &out);

// Lists the stops of `line` in route order and lets the user pick one.
// Returns false when the user goes back (Esc) or no stop is available.
bool selectStop(const Line &line, Stop &out);
