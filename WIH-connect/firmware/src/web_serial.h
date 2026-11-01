#pragma once

#include "word_game.h"

// One-way USB-to-UART events only. WLP2 on Serial1/Serial2 is independent.
class WebSerialEvents {
public:
    typedef void (*WriteFn)(const char* data, size_t length);
    explicit WebSerialEvents(WriteFn write) : write_(write) {}

    void begin(const char* device, const char* session, uint32_t now);
    void tick(uint32_t now);
    void pressConfirm(WordGame& game, uint32_t now);

private:
    bool emit(const char* type, const char* word = nullptr);
    WriteFn write_;
    char device_[17] = {};
    char session_[17] = {};
    uint32_t sequence_ = 0;
    uint32_t lastReadyMs_ = 0;
    bool started_ = false;
    // Static instance storage, not an extra large buffer on the display stack.
    char frame_[1024] = {};
};
