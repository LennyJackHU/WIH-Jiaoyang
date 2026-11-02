#pragma once
#include <Arduino.h>
#include "config.h"
#include "word_game.h"

struct PersistState {
    uint32_t magic;
    int16_t hist[HISTORY_LEN];
    uint8_t first;
    uint8_t cur;
    uint8_t last;
    uint8_t confirmed;
    uint8_t sentenceCount;
    uint8_t sentenceCursor;
    char sentenceWords[Sentence::MAX_WORDS][wlp2::TEXT_MAX + 1];
    char sentenceSources[Sentence::MAX_WORDS][17];
    uint8_t receiptCount;
    WordGame::ReceiptState receipts[8];
    uint8_t standalone;
    uint8_t insertPosition;
};

#define STATE_MAGIC 0x57494835u // "WIH5"

// NVS (flash) persistence - survives power cycles.
bool storageLoadNvs(PersistState& out);
bool storageSaveNvs(const PersistState& state);
bool storageLoadNodeId(char* out, size_t capacity);
bool storageSaveNodeId(const char* id);
