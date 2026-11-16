#pragma once
// PC simulator: Wi-Fi is the PC's network connection, always considered up.
#include <Arduino.h>

class SimWiFi {
public:
    bool isConnected() const { return true; }
};
extern SimWiFi WiFi;
