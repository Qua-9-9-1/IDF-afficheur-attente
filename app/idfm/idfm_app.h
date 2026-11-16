#pragma once

#include "idfm_model.h"

// Entry point of the IDFM waiting-time board app, called from Bruce's "Others" menu.
void idfm_setup();

// Live demo of a mode: a preset line / stop with real API data, or the offline sample data
// when there is no API key or the preset is not in the referential.
void idfmLiveDemo(TransportMode mode);
