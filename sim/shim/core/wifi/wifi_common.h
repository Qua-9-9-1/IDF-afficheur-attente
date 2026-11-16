#pragma once
// PC simulator: the PC network is always up
#include <globals.h>
enum wifi_mode_t { WIFI_MODE_STA };
inline bool wifiConnectMenu(wifi_mode_t = WIFI_MODE_STA) { wifiConnected = true; return true; }
