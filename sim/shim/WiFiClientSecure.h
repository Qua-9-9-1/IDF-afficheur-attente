#pragma once
// PC simulator: TLS is handled by curl (see HTTPClient.h); this is only a placeholder.
#include <Arduino.h>

class WiFiClientSecure : public Client {
public:
    void setInsecure() {}
    int available() override { return 0; }
    int read() override { return -1; }
};
