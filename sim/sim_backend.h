#pragma once
// Platform backend of the simulator. sim_core.cpp implements the Arduino/Bruce layer on top of
// these few functions; sim_wasm.cpp implements them for the browser (WebAssembly + Web Worker).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

uint32_t backendMillis();
void backendSleep(uint32_t ms);
// Returns a bitmask of the keys pressed since the last call (SIM_KEY_*).
uint32_t backendPollKeys();
// Sends the RGB565 framebuffer to the display.
void backendPresent(const uint16_t *fb, int w, int h);
// Reads a whole file of the simulated SD card ("/idfm/lines.json"). False if missing.
bool backendReadFile(const std::string &path, std::string &out);
// Blocking HTTP GET. Returns the status code (<= 0 on network failure).
int backendHttpGet(
    const std::string &url, const std::vector<std::string> &headers, std::map<std::string, std::string> &respHeaders,
    std::string &body
);
[[noreturn]] void backendQuit();

enum : uint32_t { SIM_KEY_NEXT = 1, SIM_KEY_PREV = 2, SIM_KEY_SEL = 4, SIM_KEY_ESC = 8 };
