#pragma once
// PC simulator: the parts of Bruce's globals / core API used by the IDFM app.

#include <Arduino.h>
#include <TFT_eSPI.h>

// --- display wrapper (Bruce: tft_logger / tft_sprite around TFT_eSPI) ---
class tft_logger : public TFT_eSPI {
public:
    tft_logger(int16_t w, int16_t h) : TFT_eSPI(w, h) {}
    TFT_eSPI *native() { return this; }
};
class tft_sprite {
public:
    explicit tft_sprite(TFT_eSPI *parent) : spr_(parent) {}
    void setColorDepth(int8_t d) { spr_.setColorDepth(d); }
    void *createSprite(int16_t w, int16_t h, uint8_t frames = 1) { return spr_.createSprite(w, h, frames); }
    void deleteSprite() { spr_.deleteSprite(); }
    void pushSprite(int32_t x, int32_t y) { spr_.pushSprite(x, y); }
    TFT_eSprite *nativeSprite() { return &spr_; }

private:
    TFT_eSprite spr_;
};
extern tft_logger tft;

// --- screen geometry (Bruce: platformio.ini -DLW=6, globals tftWidth/tftHeight) ---
#ifndef LW
#define LW 6
#endif
#ifndef LH
#define LH 8
#endif
extern volatile int tftWidth;
extern volatile int tftHeight;

// --- text sizes / menus ---
#define FP 1
#define FM 2
#define FG 3
#define MENU_TYPE_SUBMENU 0
#define MENU_TYPE_MAIN 1
#define MENU_TYPE_CONFIG 2
#define MENU_TYPE_REGULAR 3

struct BruceConfigSim {
    uint16_t priColor = 0xA80F; // Bruce default purple
    uint16_t secColor = 0x880F;
    uint16_t bgColor = 0x0000;
};
extern BruceConfigSim bruceConfig;

struct Option {
    String label;
    std::function<void()> operation;
    bool selected = false;
    bool enabled = true;
    bool (*hover)(void *hoverPointer, bool shouldRender);
    void *hoverPointer;
    bool hovered;

    Option(
        const char *lbl, const std::function<void()> &op, bool sel = false,
        bool (*hov)(void *, bool) = nullptr, void *ptr = nullptr, bool hvrd = false, bool en = true
    )
        : label(lbl), operation(op), selected(sel), enabled(en), hover(hov), hoverPointer(ptr), hovered(hvrd) {}
    Option(
        const String &lbl, const std::function<void()> &op, bool sel = false,
        bool (*hov)(void *, bool) = nullptr, void *ptr = nullptr, bool hvrd = false, bool en = true
    )
        : label(lbl), operation(op), selected(sel), enabled(en), hover(hov), hoverPointer(ptr), hovered(hvrd) {}
};

int loopOptions(std::vector<Option> &options, uint8_t menuType, const char *subText, int index = 0, bool interpreter = false);
inline int loopOptions(std::vector<Option> &options, int index) { return loopOptions(options, MENU_TYPE_REGULAR, "", index); }
inline int loopOptions(std::vector<Option> &options) { return loopOptions(options, MENU_TYPE_REGULAR, "", 0); }
void drawMainBorderWithTitle(String title, bool clear = true);
void displayWarning(String txt, bool waitKeyPress = false);
void displayError(String txt, bool waitKeyPress = false);
void displayInfo(String txt, bool waitKeyPress = false);
void displaySuccess(String txt, bool waitKeyPress = false);

// --- input: keyboard of the PC mapped on the encoder (see sim_platform.cpp) ---
extern volatile bool NextPress, PrevPress, SelPress, EscPress, AnyKeyPress, UpPress, DownPress;
bool check(volatile bool &btn);

// --- Wi-Fi state: the PC is always "connected" ---
extern bool wifiConnected;

// --- SD card / LittleFS mapped on a directory of the PC ---
#define FILE_READ "r"
#define FILE_WRITE "w"
// Read-only file: the whole content is loaded at open() through the simulator backend.
class File {
public:
    File() = default;
    explicit File(std::string data) : data_(std::move(data)), open_(true) {}
    explicit operator bool() const { return open_; }
    int read() { return pos_ < data_.size() ? (uint8_t)data_[pos_++] : -1; }
    size_t readBytes(char *buf, size_t n) {
        n = std::min(n, data_.size() - pos_);
        memcpy(buf, data_.data() + pos_, n);
        pos_ += n;
        return n;
    }
    int available() const { return (int)(data_.size() - pos_); }
    size_t size() const { return data_.size(); }
    void close() {
        open_ = false;
        data_.clear();
        pos_ = 0;
    }

private:
    std::string data_;
    size_t pos_ = 0;
    bool open_ = false;
};
// SD card: files served by the simulator backend. LittleFS: always empty in the simulator.
class SimFS {
public:
    explicit SimFS(bool enabled) : enabled_(enabled) {}
    bool exists(const String &path) const;
    File open(const String &path, const char *mode = FILE_READ) const;

private:
    bool enabled_;
};
extern SimFS SD, LittleFS;
bool setupSdCard();
