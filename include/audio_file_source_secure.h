#pragma once
#include <AudioFileSource.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <algorithm>

// ESP8266Audio 2.0 uses a plain WiFiClient even for HTTPS URLs. This source
// selects the correct client, follows the HTTPS redirect commonly used by
// stream providers, and exposes the resulting MPEG byte stream to the decoder.
class AudioFileSourceSecure : public AudioFileSource {
public:
    explicit AudioFileSourceSecure(const char* url) { open(url); }
    ~AudioFileSourceSecure() override { close(); }

    bool open(const char* url) override {
        close(); pos_ = 0;
        bool tls = strncmp(url, "https://", 8) == 0;
        if (tls) {
            secure_.setInsecure();
            client_ = &secure_;
        } else client_ = &plain_;
        http_.setUserAgent("AirPuter/1.0");
        http_.setConnectTimeout(10000); http_.setTimeout(10000);
        http_.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        if (!http_.begin(*client_, url)) return false;
        http_.addHeader("Icy-MetaData", "0");
        int code = http_.GET();
        if (code != HTTP_CODE_OK) { http_.end(); client_ = nullptr; return false; }
        open_ = true; return true;
    }
    uint32_t read(void* data, uint32_t len) override { return readInternal(data, len, false); }
    uint32_t readNonBlock(void* data, uint32_t len) override { return readInternal(data, len, true); }
    bool seek(int32_t, int) override { return false; }
    bool close() override {
        if (open_) http_.end();
        open_ = false; client_ = nullptr; return true;
    }
    bool isOpen() override { return open_ && http_.connected(); }
    uint32_t getSize() override { return 0; }
    uint32_t getPos() override { return pos_; }

private:
    uint32_t readInternal(void* data, uint32_t len, bool nonBlocking) {
        if (!open_ || !data || !http_.connected()) return 0;
        WiFiClient* stream = http_.getStreamPtr();
        uint32_t started = millis();
        int available = stream->available();
        while (!nonBlocking && available <= 0 && http_.connected() && millis() - started < 1000) {
            delay(1); available = stream->available();
        }
        if (available <= 0) return 0;
        size_t count = std::min<size_t>(len, available);
        int received = stream->read(reinterpret_cast<uint8_t*>(data), count);
        if (received <= 0) return 0;
        pos_ += received; return received;
    }

    WiFiClient plain_;
    WiFiClientSecure secure_;
    WiFiClient* client_ = nullptr;
    HTTPClient http_;
    uint32_t pos_ = 0;
    bool open_ = false;
};
