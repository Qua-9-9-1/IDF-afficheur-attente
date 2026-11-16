#pragma once
// PC simulator: "NTP" answers with the PC clock (already NTP-synchronised by Windows).
#include <WiFiUdp.h>
#include <time.h>

class NTPClient {
public:
    NTPClient(WiFiUDP &, const char *, long = 0, unsigned long = 60000) {}
    void begin(unsigned = 0) {}
    bool update() { return true; }
    bool forceUpdate() { return true; }
    unsigned long getEpochTime() const { return (unsigned long)time(nullptr); }
    void end() {}
};
