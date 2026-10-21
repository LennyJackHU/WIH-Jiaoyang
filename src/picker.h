#pragma once
#include <Arduino.h>
#include "config.h"
#include "words.h"

// Random word picker with a short ring history.
// Stepping forward at the newest end generates a fresh random word;
// stepping backward replays history. A one-slot lookahead ("next") is
// always kept so the three-line display is fully populated.
class Picker {
public:
    // Ring positions: entries live in [first, last), cur points at the
    // current (center) word. All indices are modulo HISTORY_LEN.
    int16_t hist[HISTORY_LEN];
    uint8_t first = 0;
    uint8_t cur = 0;
    uint8_t last = 0; // one past the newest entry

    void init();                    // cold start: seed prev/cur/next
    void stepForward();             // next candidate
    void stepBackward();            // back into history
    void refreshRandom();           // regenerate cur and next (button B)

    const char* currentWord() const { return wordAt(cur); }
    // nullptr when the slot does not exist (e.g. stepped to oldest).
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
};
