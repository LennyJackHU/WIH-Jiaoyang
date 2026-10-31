#pragma once

#include <stdint.h>
#include "config.h"
#include "words.h"

using PickerRandomFn = uint32_t (*)();

class Picker {
public:
    explicit Picker(PickerRandomFn random) : random_(random) {}

    int16_t hist[HISTORY_LEN];
    uint8_t first = 0;
    uint8_t cur = 0;
    uint8_t last = 0;

    void init();
    void stepForward();
    void stepBackward();
    void refreshRandom();

    const char* currentWord() const { return wordAt(cur); }
    const char* prevWord() const;
    const char* nextWord() const;
    int16_t currentIndex() const { return hist[cur]; }

private:
    const char* wordAt(uint8_t pos) const { return WORDS[hist[pos]]; }
    bool hasPrev() const { return cur != first; }
    bool hasNext() const { return (uint8_t)((cur + 1) % HISTORY_LEN) != last; }
    void append(int16_t idx);
    void ensureNext();
    int16_t randomAvoiding(int16_t a, int16_t b);

    PickerRandomFn random_;
};
