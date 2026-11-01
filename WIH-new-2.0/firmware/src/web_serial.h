#pragma once

#include "word_game.h"

// USB-to-UART events and requested sentence snapshots. WLP2 is independent.
class WebSerialEvents {
public:
    typedef void (*WriteFn)(const char* data, size_t length);
    typedef bool (*CanWriteFn)(size_t length);
    explicit WebSerialEvents(WriteFn write, CanWriteFn canWrite = nullptr)
        : write_(write), canWrite_(canWrite) {}

    void begin(const char* device, const char* session, uint32_t now);
    void tick(uint32_t now);
    void receive(char byte, const WordGame& game);
    void observeConfirm(const WordGame& game, bool wasPicker, bool wasConfirmed);

private:
    bool emitReady();
    bool emitSnapshot();
    void emitConfirmed(const char* word);
    WriteFn write_;
    CanWriteFn canWrite_;
    Sentence snapshot_; // Frozen at request time; never copy large state on stack.
    char request_[17] = {};
    char command_[160] = {};
    size_t commandLength_ = 0;
    bool discardCommand_ = false;
    bool sending_ = false;
    uint32_t sequence_ = 0;
    uint8_t snapshotStep_ = 0;
    char device_[17] = {};
    char session_[17] = {};
    uint32_t lastReadyMs_ = 0;
    bool started_ = false;
    // Static instance storage, not an extra large buffer on the display stack.
    char frame_[1024] = {};
};
