#pragma once
#include <Arduino.h>
#include "config.h"
#include "wlp2_json.h"

struct PersistState {
    uint32_t magic;
    int16_t hist[HISTORY_LEN];
    uint8_t first;
    uint8_t cur;
    uint8_t last;
    uint8_t confirmed;
    char leftWord[wlp2::TEXT_MAX + 1];
    char rightWord[wlp2::TEXT_MAX + 1];
};

#define STATE_MAGIC 0x57494832u // "WIH2"

// NVS (flash) persistence - survives power cycles.
bool storageLoadNvs(PersistState& out);
void storageSaveNvs(const PersistState& state);

// RTC memory mirror - survives deep sleep, not power loss.
bool rtcStateValid();
void rtcStateSave(const PersistState& state);
bool rtcStateLoad(PersistState& out);
