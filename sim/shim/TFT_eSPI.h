#pragma once
// PC simulator: subset of TFT_eSPI drawing into an RGB565 framebuffer.
// Text uses the same GLCD 5x7 font and rules as Bruce's TFT_eSPI (font 1, no cp437 fix).

#include <Arduino.h>

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_YELLOW 0xFFE0
#define TFT_PURPLE 0x780F
#define TFT_DARKGREY 0x7BEF

class TFT_eSPI {
public:
    TFT_eSPI(int16_t w = 0, int16_t h = 0) { resize(w, h); }
    virtual ~TFT_eSPI() = default;

    int16_t width() const { return w_; }
    int16_t height() const { return h_; }
    uint16_t *buffer() { return buf_.data(); }

    // --- primitives ---
    void drawPixel(int32_t x, int32_t y, uint32_t c) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        buf_[(size_t)y * w_ + x] = (uint16_t)c;
    }
    uint16_t readPixel(int32_t x, int32_t y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
        return buf_[(size_t)y * w_ + x];
    }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
        for (int32_t j = std::max<int32_t>(0, y); j < std::min<int32_t>(h_, y + h); j++)
            for (int32_t i = std::max<int32_t>(0, x); i < std::min<int32_t>(w_, x + w); i++)
                buf_[(size_t)j * w_ + i] = (uint16_t)c;
    }
    void fillScreen(uint32_t c) { fillRect(0, 0, w_, h_, c); }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { fillRect(x, y, w, 1, c); }
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { fillRect(x, y, 1, h, c); }
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
        int32_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int32_t err = dx + dy;
        while (true) {
            drawPixel(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int32_t e2 = 2 * err;
            if (e2 >= dy) err += dy, x0 += sx;
            if (e2 <= dx) err += dx, y0 += sy;
        }
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
        drawFastHLine(x, y, w, c);
        drawFastHLine(x, y + h - 1, w, c);
        drawFastVLine(x, y, h, c);
        drawFastVLine(x + w - 1, y, h, c);
    }
    // Horizontal inset of row j (0..h-1) of a rounded rectangle of radius r.
    static int32_t roundInset(int32_t j, int32_t h, int32_t r) {
        int32_t dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        if (dy <= 0) return 0;
        double d = dy - 0.5;
        return r - (int32_t)floor(sqrt(std::max(0.0, (double)r * r - d * d)) + 0.5);
    }
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
        r = std::min<int32_t>(r, std::min(w, h) / 2);
        for (int32_t j = 0; j < h; j++) {
            int32_t in = roundInset(j, h, r);
            fillRect(x + in, y + j, w - 2 * in, 1, c);
        }
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
        r = std::min<int32_t>(r, std::min(w, h) / 2);
        for (int32_t j = 0; j < h; j++) {
            int32_t in = roundInset(j, h, r);
            if (j == 0 || j == h - 1) {
                fillRect(x + in, y + j, w - 2 * in, 1, c);
                continue;
            }
            // extend each side towards the neighbour row with the larger inset: keeps the curve closed
            int32_t hi = std::max(roundInset(j - 1, h, r), roundInset(j + 1, h, r));
            int32_t span = std::max<int32_t>(1, hi - in);
            fillRect(x + in, y + j, span, 1, c);
            fillRect(x + w - in - span, y + j, span, 1, c);
        }
    }
    void fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c, uint32_t = 0x00FFFFFF) {
        fillRoundRect(x, y, w, h, r, c);
    }
    void fillCircle(int32_t cx, int32_t cy, int32_t r, uint32_t c) {
        for (int32_t dy = -r; dy <= r; dy++) {
            int32_t dx = (int32_t)sqrt((double)r * r - (double)dy * dy);
            drawFastHLine(cx - dx, cy + dy, 2 * dx + 1, c);
        }
    }

    // --- text (GLCD font 1 only) ---
    void setTextFont(uint8_t) {}
    void setFreeFont(const void *) {}
    void setTextSize(uint8_t s) { textSize_ = s ? s : 1; }
    void setTextColor(uint16_t fg) { fg_ = bg_ = fg; }
    void setTextColor(uint16_t fg, uint16_t bg, bool = false) { fg_ = fg, bg_ = bg; }
    void setTextDatum(uint8_t d) { datum_ = d; }
    void setTextWrap(bool w, bool = false) { wrap_ = w; }
    void setCursor(int16_t x, int16_t y) { cx_ = x, cy_ = y; }
    int16_t getCursorX() const { return cx_; }
    int16_t getCursorY() const { return cy_; }
    void cp437(bool b) { cp437_ = b; }
    int16_t textWidth(const String &s) const { return (int16_t)(s.length() * 6 * textSize_); }
    int16_t fontHeight() const { return (int16_t)(8 * textSize_); }

    void drawChar(int32_t x, int32_t y, uint16_t c, uint32_t fg, uint32_t bg, uint8_t size);
    int16_t drawString(const String &s, int32_t x, int32_t y) {
        int32_t w = textWidth(s), h = fontHeight();
        switch (datum_) {
            case TC_DATUM: x -= w / 2; break;
            case TR_DATUM: x -= w; break;
            case ML_DATUM: y -= h / 2; break;
            case MC_DATUM: x -= w / 2, y -= h / 2; break;
            case MR_DATUM: x -= w, y -= h / 2; break;
            case BL_DATUM: y -= h; break;
            case BC_DATUM: x -= w / 2, y -= h; break;
            case BR_DATUM: x -= w, y -= h; break;
        }
        for (unsigned i = 0; i < s.length(); i++) drawChar(x + (int32_t)i * 6 * textSize_, y, (uint8_t)s[i], fg_, bg_, textSize_);
        return (int16_t)w;
    }
    int16_t drawString(const char *s, int32_t x, int32_t y) { return drawString(String(s), x, y); }
    int16_t drawCentreString(const String &s, int32_t x, int32_t y, uint8_t) {
        uint8_t d = datum_;
        datum_ = TC_DATUM;
        int16_t w = drawString(s, x, y);
        datum_ = d;
        return w;
    }
    size_t print(const String &s);
    size_t print(const char *s) { return print(String(s)); }
    size_t print(char c) { return print(String(c)); }
    size_t print(int v) { return print(String(v)); }
    size_t print(unsigned v) { return print(String(v)); }
    size_t print(long v) { return print(String(v)); }
    size_t print(unsigned long v) { return print(String(v)); }
    size_t print(double v, int d = 2) { return print(String(v, (unsigned)d)); }
    size_t println(const String &s = String()) { return print(s + "\n"); }
    size_t println(const char *s) { return println(String(s)); }

protected:
    void resize(int16_t w, int16_t h) {
        w_ = w, h_ = h;
        buf_.assign((size_t)std::max<int>(0, w) * std::max<int>(0, h), 0);
    }
    int16_t w_ = 0, h_ = 0;
    std::vector<uint16_t> buf_;
    uint16_t fg_ = TFT_WHITE, bg_ = TFT_WHITE;
    uint8_t textSize_ = 1, datum_ = TL_DATUM;
    int16_t cx_ = 0, cy_ = 0;
    bool wrap_ = true, cp437_ = false;
};

class TFT_eSprite : public TFT_eSPI {
public:
    explicit TFT_eSprite(TFT_eSPI *parent) : parent_(parent) {}
    void setColorDepth(int8_t) {}
    void *createSprite(int16_t w, int16_t h, uint8_t = 1) {
        resize(w, h);
        return buf_.data();
    }
    void deleteSprite() { resize(0, 0); }
    bool created() const { return !buf_.empty(); }
    void pushSprite(int32_t x, int32_t y) { pushSprite(x, y, 0, 0, w_, h_); }
    // Push the (sx, sy, sw, sh) region of the sprite at (tx, ty) on the parent.
    void pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
        if (!parent_) return;
        for (int32_t j = 0; j < sh; j++)
            for (int32_t i = 0; i < sw; i++) parent_->drawPixel(tx + i, ty + j, readPixel(sx + i, sy + j));
    }

private:
    TFT_eSPI *parent_;
};
