#pragma once
// PC simulator: HTTPClient on top of the simulator backend (browser: local proxy server).

#include <Arduino.h>
#include <map>

#define HTTP_CODE_OK 200
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)

// In-memory response body exposed as a Client stream.
class MemoryClient : public Client {
public:
    void set(std::string data) {
        data_ = std::move(data);
        pos_ = 0;
    }
    int available() override { return (int)(data_.size() - pos_); }
    int read() override { return pos_ < data_.size() ? (uint8_t)data_[pos_++] : -1; }
    int read(uint8_t *buf, size_t n) override {
        size_t k = std::min(n, data_.size() - pos_);
        memcpy(buf, data_.data() + pos_, k);
        pos_ += k;
        return (int)k;
    }

private:
    std::string data_;
    size_t pos_ = 0;
};

class HTTPClient {
public:
    void useHTTP10(bool) {}
    void setReuse(bool) {}
    void setConnectTimeout(int32_t) {}
    void setTimeout(uint16_t) {}
    bool begin(Client &, const String &url) { return begin(url); }
    bool begin(const String &url) {
        url_ = url;
        reqHeaders_.clear();
        return url.startsWith("http");
    }
    void addHeader(const String &name, const String &value) { reqHeaders_.push_back(name + ": " + value); }
    void collectHeaders(const char *[], size_t) {} // every response header is kept
    int GET();
    String header(const char *name) const;
    Client &getStream() { return body_; }
    String getString() {
        String s;
        int c;
        while ((c = body_.read()) >= 0) s += (char)c;
        return s;
    }
    void end() {}

private:
    String url_;
    std::vector<String> reqHeaders_;
    std::map<std::string, std::string> respHeaders_; // lower-case names
    MemoryClient body_;
};
