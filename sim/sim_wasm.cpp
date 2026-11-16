// Browser backend of the simulator: the program runs in a Web Worker (sim/web/worker.js),
// which provides these imports. Blocking calls (sleep, HTTP, files) are synchronous on the
// worker side (Atomics.wait, synchronous XMLHttpRequest), so the app keeps its blocking loops.

#include "sim_backend.h"

#include <cstdlib>
#include <cstring>

#define SIM_IMPORT(name) __attribute__((import_module("sim"), import_name(#name)))

extern "C" {
SIM_IMPORT(now_ms) double js_now_ms();
SIM_IMPORT(sleep_ms) void js_sleep_ms(int ms);
SIM_IMPORT(poll_keys) int js_poll_keys();
SIM_IMPORT(present) void js_present(const uint16_t *fb, int w, int h);
// Loads a file of the simulated SD card into result slot 0. Returns its size, or -1.
SIM_IMPORT(read_file) int js_read_file(const char *path, int pathLen);
// HTTP GET: body -> slot 0, response headers ("Name: value\n"...) -> slot 1. Returns the status.
SIM_IMPORT(http_get) int js_http_get(const char *url, int urlLen, const char *headers, int headersLen);
// Copies result slot `slot` (length given by the previous call / result_len) to dst.
SIM_IMPORT(result_len) int js_result_len(int slot);
SIM_IMPORT(result_copy) void js_result_copy(int slot, char *dst);
SIM_IMPORT(quit) void js_quit();
}

namespace {
std::string takeResult(int slot) {
    int n = js_result_len(slot);
    std::string s(n > 0 ? (size_t)n : 0, '\0');
    if (n > 0) js_result_copy(slot, &s[0]);
    return s;
}
} // namespace

uint32_t backendMillis() { return (uint32_t)js_now_ms(); }
void backendSleep(uint32_t ms) { js_sleep_ms((int)ms); }
uint32_t backendPollKeys() { return (uint32_t)js_poll_keys(); }
void backendPresent(const uint16_t *fb, int w, int h) { js_present(fb, w, h); }

bool backendReadFile(const std::string &path, std::string &out) {
    int n = js_read_file(path.data(), (int)path.size());
    if (n < 0) return false;
    out = takeResult(0);
    return true;
}

int backendHttpGet(
    const std::string &url, const std::vector<std::string> &headers, std::map<std::string, std::string> &respHeaders,
    std::string &body
) {
    std::string h;
    for (auto &line : headers) h += line + "\n";
    int code = js_http_get(url.data(), (int)url.size(), h.data(), (int)h.size());
    body = takeResult(0);
    std::string raw = takeResult(1);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        if (eol == std::string::npos) eol = raw.size();
        std::string line = raw.substr(pos, eol - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
            respHeaders[line.substr(0, colon)] = value;
        }
        pos = eol + 1;
    }
    return code;
}

void backendQuit() {
    js_quit();
    _Exit(0);
}
