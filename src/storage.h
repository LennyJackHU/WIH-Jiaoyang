#pragma once
#include <Arduino.h>
#include "config.h"

struct PersistState {
    uint32_t magic; // STATE_MAGIC when the contents are valid
    int16_t hist[HISTORY_LEN];
    uint8_t first;
    uint8_t cur;
    uint8_t last;
    uint8_t confirmed;
};

#define STATE_MAGIC 0x57494831u // "WIH1"

// NVS (flash) persistence - survives power cycles.
bool storageLoadNvs(PersistState& out);
void storageSaveNvs(const PersistState& state);

// RTC memory mirror - survives deep sleep, not power loss.
bool rtcStateValid();
void rtcStateSave(const PersistState& state);
bool rtcStateLoad(PersistState& out);
