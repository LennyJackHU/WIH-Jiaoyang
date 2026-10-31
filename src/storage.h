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
    char leftWord[wlp2::TEXT_MAX + 1];
    char rightWord[wlp2::TEXT_MAX + 1];
    char leftSource[17];
    char rightSource[17];
    uint8_t receiptCount;
    WordGame::ReceiptState receipts[8];
};

#define STATE_MAGIC 0x57494833u // "WIH3"

// NVS (flash) persistence - survives power cycles.
bool storageLoadNvs(PersistState& out);
bool storageSaveNvs(const PersistState& state);

// RTC memory mirror - survives deep sleep, not power loss.
bool rtcStateValid();
void rtcStateSave(const PersistState& state);
bool rtcStateLoad(PersistState& out);
