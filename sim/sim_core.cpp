// Simulator core: Arduino / Bruce API used by the IDFM app, implemented on top of the platform
// backend (sim_backend.h). The app sources in app/idfm are compiled unchanged.
//
// Arguments: [--demo rer|metro|transilien|tram|bus] [--keys "s,w,n,s"] [--exit-after ms]
//   --keys: scripted encoder, n=next p=prev s=click e=back w=wait (one step every 450 ms)

#include "sim_backend.h"

#include <globals.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "../app/idfm/board_metro.h"
#include "../app/idfm/board_rer.h"
#include "../app/idfm/idfm_app.h"

#include <cctype>
#include <deque>

#include "third_party/glcdfont.c"

// ===========================================================================
// Globals expected by the app sources
// ===========================================================================
constexpr int kScreenW = 320, kScreenH = 170;
tft_logger tft(kScreenW, kScreenH);
BruceConfigSim bruceConfig;
SimSerial Serial;
SimWiFi WiFi;
SimFS SD(true), LittleFS(false);
bool wifiConnected = true;
volatile int tftWidth = kScreenW;
volatile int tftHeight = kScreenH;
volatile bool NextPress, PrevPress, SelPress, EscPress, AnyKeyPress, UpPress, DownPress;

namespace {

uint32_t g_lastPresentMs = 0;
uint32_t g_exitAfterMs = 0;
int g_menuDepth = 0;
bool g_quitRequested = false;
std::deque<char> g_script;
uint32_t g_nextScriptMs = 0;
constexpr uint32_t kScriptStepMs = 450;
constexpr uint32_t kPresentMs = 30;

void pressKey(volatile bool &flag) {
    flag = true;
    AnyKeyPress = true;
}

void applyKeys(uint32_t mask) {
    if (mask & SIM_KEY_NEXT) pressKey(NextPress);
    if (mask & SIM_KEY_PREV) pressKey(PrevPress);
    if (mask & SIM_KEY_SEL) pressKey(SelPress);
    if (mask & SIM_KEY_ESC) pressKey(EscPress);
}

void runScript() {
    if (g_script.empty() || backendMillis() < g_nextScriptMs) return;
    char k = g_script.front();
    g_script.pop_front();
    g_nextScriptMs = backendMillis() + kScriptStepMs;
    switch (k) {
        case 'n': applyKeys(SIM_KEY_NEXT); break;
        case 'p': applyKeys(SIM_KEY_PREV); break;
        case 's': applyKeys(SIM_KEY_SEL); break;
        case 'e': applyKeys(SIM_KEY_ESC); break;
        default: break; // 'w': wait one step
    }
}

void presentNow() {
    backendPresent(tft.buffer(), kScreenW, kScreenH);
    g_lastPresentMs = backendMillis();
}

// Input + periodic present, called from delay() and check().
void pump() {
    applyKeys(backendPollKeys());
    runScript();
    uint32_t now = backendMillis();
    if (now - g_lastPresentMs >= kPresentMs) presentNow();
    if (g_exitAfterMs && now >= g_exitAfterMs) {
        presentNow();
        backendQuit();
    }
}

// ===========================================================================
// Bruce-like UI helpers (approximation of Bruce's menus, the board itself is exact)
// ===========================================================================
void drawBorder() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawRoundRect(5, 5, kScreenW - 10, kScreenH - 10, 5, bruceConfig.priColor);
}

void drawTitle(const String &title, int y = 9) {
    tft.setTextSize(FM);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString(title, kScreenW / 2, y);
    tft.setTextDatum(TL_DATUM);
}

String clip(const String &s, int maxChars) {
    if ((int)s.length() <= maxChars) return s;
    return s.substring(0, maxChars - 1) + ".";
}

// Bruce submenu: title under the status bar (y=30), previous / current (large) / next option.
void drawSubmenu(std::vector<Option> &opts, int idx, const char *title) {
    drawBorder();
    drawTitle(title, 30);
    int n = (int)opts.size();
    auto label = [&](int i) { return opts[((i % n) + n) % n].label; };
    tft.setTextDatum(TC_DATUM);
    if (n > 1) {
        tft.setTextSize(FP);
        tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
        tft.drawString(clip(label(idx - 1), 48), kScreenW / 2, 50);
        tft.drawString(clip(label(idx + 1), 48), kScreenW / 2, 120);
    }
    tft.setTextSize(FM);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString(clip(label(idx), 24), kScreenW / 2, 78);
    tft.setTextDatum(TL_DATUM);
    if (opts[idx].hover) opts[idx].hover(opts[idx].hoverPointer, true);
}

// Bruce regular list: 6 visible rows in a box starting at y=20, highlighted selection.
void drawRegular(std::vector<Option> &opts, int idx, const char *title) {
    tft.fillScreen(bruceConfig.bgColor);
    if (title && *title) drawTitle(title);
    if (opts[idx].hover) opts[idx].hover(opts[idx].hoverPointer, true);
    constexpr int kRows = 6, kTop = 22, kRowH = 23;
    tft.drawRoundRect(5, 20, kScreenW - 10, kScreenH - 22, 5, bruceConfig.priColor);
    int n = (int)opts.size();
    int first = std::max(0, std::min(idx - kRows / 2, n - kRows));
    tft.setTextSize(FM);
    for (int r = 0; r < kRows && first + r < n; r++) {
        int i = first + r;
        int y = kTop + r * kRowH + 4;
        bool sel = i == idx;
        if (sel) tft.fillRoundRect(8, y - 3, kScreenW - 16, kRowH - 1, 4, bruceConfig.priColor);
        tft.setTextColor(
            sel ? bruceConfig.bgColor : bruceConfig.priColor, sel ? bruceConfig.priColor : bruceConfig.bgColor
        );
        tft.drawString(clip(opts[i].label, 24), 14, y);
    }
}

void displayBox(const String &txt, uint16_t color, bool wait);

} // namespace

// ===========================================================================
// Arduino / Bruce API implementations
// ===========================================================================
uint32_t millis() { return backendMillis(); }

void delay(uint32_t ms) {
    uint32_t end = millis() + ms;
    do {
        pump();
        uint32_t now = millis();
        if (now < end) backendSleep(std::min<uint32_t>(10, end - now));
    } while (millis() < end);
}

bool check(volatile bool &btn) {
    pump();
    if (!btn) return false;
    btn = false;
    AnyKeyPress = false;
    return true;
}

int loopOptions(std::vector<Option> &options, uint8_t menuType, const char *subText, int index, bool) {
    if (options.empty()) return -1;
    if (index < 0 || index >= (int)options.size()) index = 0;
    g_menuDepth++;
    int n = (int)options.size();
    bool redraw = true;
    int result = index;
    while (true) {
        if (redraw) {
            if (menuType == MENU_TYPE_REGULAR) drawRegular(options, index, subText);
            else drawSubmenu(options, index, subText);
            redraw = false;
        }
        if (check(NextPress)) {
            index = (index + 1) % n;
            redraw = true;
        } else if (check(PrevPress)) {
            index = (index + n - 1) % n;
            redraw = true;
        } else if (check(SelPress)) {
            result = index;
            options[index].operation();
            break;
        } else if (check(EscPress)) {
            if (g_menuDepth == 1) g_quitRequested = true;
            result = -1;
            break;
        }
        delay(10);
    }
    g_menuDepth--;
    return result;
}

void drawMainBorderWithTitle(String title, bool clear) {
    if (clear) drawBorder();
    drawTitle(title);
}

namespace {
void displayBox(const String &txt, uint16_t color, bool wait) {
    tft.fillRoundRect(20, 55, kScreenW - 40, 60, 6, bruceConfig.bgColor);
    tft.drawRoundRect(20, 55, kScreenW - 40, 60, 6, color);
    tft.setTextSize(FP);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(color, bruceConfig.bgColor);
    tft.drawString(clip(txt, 44), kScreenW / 2, 85);
    tft.setTextDatum(TL_DATUM);
    if (wait) {
        while (!check(AnyKeyPress)) delay(10);
    } else {
        delay(1000);
    }
}
} // namespace
void displayWarning(String txt, bool wait) { displayBox(txt, TFT_YELLOW, wait); }
void displayError(String txt, bool wait) { displayBox(txt, TFT_RED, wait); }
void displayInfo(String txt, bool wait) { displayBox(txt, TFT_WHITE, wait); }
void displaySuccess(String txt, bool wait) { displayBox(txt, TFT_GREEN, wait); }

// --- TFT text rendering (same rules as TFT_eSPI::drawChar for the GLCD font) ---
void TFT_eSPI::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t fg, uint32_t bg, uint8_t size) {
    if (c > 255) return;
    if (!cp437_ && c > 175) c++;
    bool fillbg = bg != fg;
    for (int i = 0; i < 6; i++) {
        uint8_t line = i == 5 ? 0 : font[c * 5 + i];
        for (int j = 0; j < 8; j++, line >>= 1) {
            if (line & 1) fillRect(x + i * size, y + j * size, size, size, fg);
            else if (fillbg) fillRect(x + i * size, y + j * size, size, size, bg);
        }
    }
}

size_t TFT_eSPI::print(const String &s) {
    for (unsigned i = 0; i < s.length(); i++) {
        char ch = s[i];
        if (ch == '\n') {
            cx_ = 0;
            cy_ += 8 * textSize_;
            continue;
        }
        if (ch == '\r') continue;
        if (wrap_ && cx_ + 6 * textSize_ > w_) {
            cx_ = 0;
            cy_ += 8 * textSize_;
        }
        drawChar(cx_, cy_, (uint8_t)ch, fg_, bg_, textSize_);
        cx_ += 6 * textSize_;
    }
    return s.length();
}

// --- file systems ---
bool SimFS::exists(const String &path) const {
    std::string unused;
    return enabled_ && backendReadFile(path.c_str(), unused);
}
File SimFS::open(const String &path, const char *) const {
    std::string data;
    if (!enabled_ || !backendReadFile(path.c_str(), data)) return File();
    return File(std::move(data));
}
bool setupSdCard() { return true; }

// --- HTTP ---
int HTTPClient::GET() {
    respHeaders_.clear();
    std::vector<std::string> headers;
    for (auto &h : reqHeaders_) headers.push_back(h.c_str());
    std::string body;
    std::map<std::string, std::string> resp;
    int code = backendHttpGet(url_.c_str(), headers, resp, body);
    for (auto &kv : resp) {
        std::string name = kv.first;
        for (auto &c : name) c = (char)tolower((unsigned char)c);
        respHeaders_[name] = kv.second;
    }
    body_.set(std::move(body));
    printf("[sim] GET %s -> %d (%d bytes)\n", url_.c_str(), code, body_.available());
    return code > 0 ? code : HTTPC_ERROR_CONNECTION_REFUSED;
}

String HTTPClient::header(const char *name) const {
    std::string n = name;
    for (auto &c : n) c = (char)tolower((unsigned char)c);
    auto it = respHeaders_.find(n);
    return it == respHeaders_.end() ? String() : String(it->second);
}

// ===========================================================================
// Entry point
// ===========================================================================
int main(int argc, char **argv) {
    std::string demo; // "", "rer", "metro", "transilien", "tram", "bus"
    bool offline = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--demo") demo = next();
        else if (a == "--offline") offline = true;
        else if (a == "--exit-after") g_exitAfterMs = (uint32_t)atoi(next().c_str());
        else if (a == "--keys") {
            for (char c : next())
                if (strchr("npsew", c)) g_script.push_back(c);
            g_nextScriptMs = backendMillis() + 800; // let the first screen draw
        }
    }

    // Live demos (real API data, offline sample data without a key), see idfmLiveDemo()
    static const std::pair<const char *, TransportMode> kDemos[] = {
        {"rer", TransportMode::Rer},   {"metro", TransportMode::Metro}, {"transilien", TransportMode::Transilien},
        {"tram", TransportMode::Tram}, {"bus", TransportMode::Bus},
    };
    bool demoRun = false;
    for (auto &d : kDemos) {
        if (demo == d.first) {
            if (offline) idfmOfflineDemo(d.second);
            else idfmLiveDemo(d.second);
            demoRun = true;
        }
    }
    if (!demoRun) {
        while (!g_quitRequested) idfm_setup();
    }
    presentNow();
    backendQuit();
}
