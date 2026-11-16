#pragma once
// PC simulator: minimal Arduino core (String, timing, Serial) for the IDFM app sources.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <time.h>
#include <vector>

#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define DEC 10
#define HEX 16

class String {
public:
    String() = default;
    String(const char *s) : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(int v, int base = DEC) : s_(fmtInt((long long)v, base)) {}
    String(unsigned int v, int base = DEC) : s_(fmtUInt((unsigned long long)v, base)) {}
    String(long v, int base = DEC) : s_(fmtInt((long long)v, base)) {}
    String(unsigned long v, int base = DEC) : s_(fmtUInt((unsigned long long)v, base)) {}
    String(long long v, int base = DEC) : s_(fmtInt(v, base)) {}
    String(unsigned long long v, int base = DEC) : s_(fmtUInt(v, base)) {}
    String(float v, unsigned int decimals = 2) : s_(fmtDouble(v, decimals)) {}
    String(double v, unsigned int decimals = 2) : s_(fmtDouble(v, decimals)) {}

    String &operator=(const char *s) {
        s_ = s ? s : "";
        return *this;
    }

    unsigned int length() const { return (unsigned int)s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    const char *c_str() const { return s_.c_str(); }
    void reserve(unsigned int n) { s_.reserve(n); }

    char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    char &operator[](unsigned int i) { return s_[i]; }
    char charAt(unsigned int i) const { return (*this)[i]; }
    void setCharAt(unsigned int i, char c) {
        if (i < s_.size()) s_[i] = c;
    }

    bool concat(const char *s) {
        if (s) s_ += s;
        return true;
    }
    bool concat(const String &s) {
        s_ += s.s_;
        return true;
    }
    bool concat(char c) {
        s_ += c;
        return true;
    }
    String &operator+=(const String &o) {
        s_ += o.s_;
        return *this;
    }
    String &operator+=(const char *o) {
        if (o) s_ += o;
        return *this;
    }
    String &operator+=(char c) {
        s_ += c;
        return *this;
    }
    String &operator+=(int v) { return *this += String(v); }
    String &operator+=(unsigned int v) { return *this += String(v); }
    String &operator+=(long v) { return *this += String(v); }
    String &operator+=(unsigned long v) { return *this += String(v); }

    friend String operator+(const String &a, const String &b) { return String(a.s_ + b.s_); }
    friend String operator+(const String &a, const char *b) { return String(a.s_ + (b ? b : "")); }
    friend String operator+(const char *a, const String &b) { return String(std::string(a ? a : "") + b.s_); }
    friend String operator+(const String &a, char b) { return String(a.s_ + b); }
    friend String operator+(const String &a, int b) { return a + String(b); }
    friend String operator+(const String &a, unsigned int b) { return a + String(b); }
    friend String operator+(const String &a, long b) { return a + String(b); }
    friend String operator+(const String &a, unsigned long b) { return a + String(b); }

    bool operator==(const String &o) const { return s_ == o.s_; }
    bool operator==(const char *o) const { return s_ == (o ? o : ""); }
    bool operator!=(const String &o) const { return s_ != o.s_; }
    bool operator!=(const char *o) const { return !(*this == o); }
    bool operator<(const String &o) const { return s_ < o.s_; }
    bool equals(const String &o) const { return s_ == o.s_; }
    bool equalsIgnoreCase(const String &o) const {
        if (s_.size() != o.s_.size()) return false;
        for (size_t i = 0; i < s_.size(); i++)
            if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
        return true;
    }

    int indexOf(char c, unsigned int from = 0) const { return pos(s_.find(c, from)); }
    int indexOf(const String &s, unsigned int from = 0) const { return pos(s_.find(s.s_, from)); }
    int lastIndexOf(char c) const { return pos(s_.rfind(c)); }
    int lastIndexOf(const String &s) const { return pos(s_.rfind(s.s_)); }
    bool startsWith(const String &p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
    bool endsWith(const String &p) const {
        return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
    }

    String substring(unsigned int from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
    String substring(unsigned int from, unsigned int to) const {
        if (from > to) std::swap(from, to);
        if (from >= s_.size()) return String();
        return String(s_.substr(from, std::min<size_t>(to, s_.size()) - from));
    }
    void remove(unsigned int idx) {
        if (idx < s_.size()) s_.erase(idx);
    }
    void remove(unsigned int idx, unsigned int count) {
        if (idx < s_.size()) s_.erase(idx, count);
    }
    void replace(const String &from, const String &to) {
        if (from.s_.empty()) return;
        size_t p = 0;
        while ((p = s_.find(from.s_, p)) != std::string::npos) {
            s_.replace(p, from.s_.size(), to.s_);
            p += to.s_.size();
        }
    }
    void replace(char from, char to) { std::replace(s_.begin(), s_.end(), from, to); }
    void trim() {
        size_t b = 0, e = s_.size();
        while (b < e && isspace((unsigned char)s_[b])) b++;
        while (e > b && isspace((unsigned char)s_[e - 1])) e--;
        s_ = s_.substr(b, e - b);
    }
    void toUpperCase() {
        for (auto &c : s_) c = (char)toupper((unsigned char)c);
    }
    void toLowerCase() {
        for (auto &c : s_) c = (char)tolower((unsigned char)c);
    }
    long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
    float toFloat() const { return strtof(s_.c_str(), nullptr); }

    const std::string &str() const { return s_; }

private:
    static int pos(size_t p) { return p == std::string::npos ? -1 : (int)p; }
    static std::string fmtUInt(unsigned long long v, int base) {
        if (base == 10) return std::to_string(v);
        static const char *digits = "0123456789ABCDEF";
        std::string out;
        do {
            out.insert(out.begin(), digits[v % base]);
            v /= base;
        } while (v);
        return out;
    }
    static std::string fmtInt(long long v, int base) {
        if (v < 0 && base == 10) return "-" + fmtUInt((unsigned long long)(-v), base);
        return fmtUInt((unsigned long long)v, base);
    }
    static std::string fmtDouble(double v, unsigned int decimals) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", (int)decimals, v);
        return buf;
    }
    std::string s_;
};

// --- timing (implemented in sim_platform.cpp; delay() also pumps the window) ---
uint32_t millis();
void delay(uint32_t ms);
inline void yield() {}

// --- Serial: goes to stdout ---
class SimSerial {
public:
    void begin(unsigned long) {}
    void print(const String &s) { fputs(s.c_str(), stdout); }
    void print(const char *s) { fputs(s, stdout); }
    void println(const String &s = String()) {
        fputs(s.c_str(), stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
    void println(const char *s) { println(String(s)); }
    int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
        va_list ap;
        va_start(ap, fmt);
        int n = vprintf(fmt, ap);
        va_end(ap);
        fflush(stdout);
        return n;
    }
};
extern SimSerial Serial;

#define log_w(fmt, ...) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#define log_e(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#define log_d(fmt, ...) ((void)0)

inline bool psramFound() { return false; } // the simulator draws like a board without PSRAM

// --- Stream / Client: byte sources (HTTP body) ---
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int read(uint8_t *buf, size_t n) {
        size_t i = 0;
        for (; i < n; i++) {
            int c = read();
            if (c < 0) break;
            buf[i] = (uint8_t)c;
        }
        return (int)i;
    }
    size_t readBytes(uint8_t *buf, size_t n) { return (size_t)std::max(0, read(buf, n)); }
    size_t readBytes(char *buf, size_t n) { return readBytes((uint8_t *)buf, n); }
};
class Client : public Stream {};
