#pragma once
#include <AudioOutput.h>
#include <M5Unified.h>
#include <atomic>

class AudioOutputM5 : public AudioOutput {
public:
    explicit AudioOutputM5(m5::Speaker_Class* speaker) : speaker_(speaker) {}
    void setup() { speaker_->setBufferReleaseCallback(this, released); }
    bool begin() override { return true; }
    bool ConsumeSample(int16_t sample[2]) override {
        if (used_ >= BUFFER_SAMPLES) { flush(); return false; }
        buffers_[index_][used_++] = sample[0];
        buffers_[index_][used_++] = sample[1];
        return true;
    }
    void flush() override {
        if (!used_) return;
        busy_[index_] = true;
        if (!speaker_->playRaw(buffers_[index_], used_, hertz, true, 1, 0)) busy_[index_] = false;
        index_ = (index_ + 1) % BUFFER_COUNT; used_ = 0;
        while (busy_[index_]) vTaskDelay(1);
    }
    bool stop() override {
        flush();
        for (auto& busy : busy_) while (busy) vTaskDelay(1);
        return true;
    }
private:
    static constexpr size_t BUFFER_COUNT = 3, BUFFER_SAMPLES = 1024;
    m5::Speaker_Class* speaker_;
    int16_t buffers_[BUFFER_COUNT][BUFFER_SAMPLES] = {};
    std::atomic<bool> busy_[BUFFER_COUNT] = {{false}, {false}, {false}};
    size_t index_ = 0, used_ = 0;
    static void released(void* context, const void* data, uint8_t) {
        auto self = static_cast<AudioOutputM5*>(context);
        for (size_t i = 0; i < BUFFER_COUNT; ++i)
            if (data == self->buffers_[i]) self->busy_[i] = false;
    }
};
