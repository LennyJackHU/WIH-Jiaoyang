#include "picker.h"
#include <esp_random.h>

int16_t Picker::randomAvoiding(int16_t a, int16_t b) {
    int16_t idx;
    do {
        idx = (int16_t)(esp_random() % WORD_COUNT);
    } while (idx == a || idx == b);
    return idx;
}

void Picker::append(int16_t idx) {
    hist[last] = idx;
    last = (uint8_t)((last + 1) % HISTORY_LEN);
    if (last == first) { // ring full: evict oldest
        first = (uint8_t)((first + 1) % HISTORY_LEN);
    }
}

void Picker::ensureNext() {
    if (!hasNext()) {
        int16_t prev = hasPrev() ? hist[(cur + HISTORY_LEN - 1) % HISTORY_LEN] : -1;
        append(randomAvoiding(hist[cur], prev));
    }
}

void Picker::init() {
    first = cur = last = 0;
    append((int16_t)(esp_random() % WORD_COUNT)); // prev
    append(randomAvoiding(hist[0], -1));          // cur
    cur = 1;
    ensureNext();
}

void Picker::stepForward() {
    ensureNext();
    cur = (uint8_t)((cur + 1) % HISTORY_LEN);
    ensureNext();
}

void Picker::stepBackward() {
    if (hasPrev()) {
        cur = (uint8_t)((cur + HISTORY_LEN - 1) % HISTORY_LEN);
    }
}

void Picker::refreshRandom() {
    int16_t prev = hasPrev() ? hist[(cur + HISTORY_LEN - 1) % HISTORY_LEN] : -1;
    hist[cur] = randomAvoiding(prev, -1);
    // drop any lookahead and regenerate it against the new current word
    last = (uint8_t)((cur + 1) % HISTORY_LEN);
    if (last == first) {
        first = (uint8_t)((first + 1) % HISTORY_LEN);
    }
    ensureNext();
}

const char* Picker::prevWord() const {
    return hasPrev() ? wordAt((uint8_t)((cur + HISTORY_LEN - 1) % HISTORY_LEN)) : nullptr;
}

const char* Picker::nextWord() const {
    return hasNext() ? wordAt((uint8_t)((cur + 1) % HISTORY_LEN)) : nullptr;
}
