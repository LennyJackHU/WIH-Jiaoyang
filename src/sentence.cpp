#include "sentence.h"

#include <stdio.h>
#include <string.h>

const char* Sentence::text(uint8_t index) const {
    return index < count_ ? words_[index] : "";
}

const char* Sentence::source(uint8_t index) const {
    return index < count_ ? sources_[index] : "";
}

bool Sentence::insert(uint8_t index, const char* text, const char* source) {
    if (count_ >= MAX_WORDS || index > count_ || !text ||
        !wlp2::validText(text, strlen(text))) return false;
    for (uint8_t i = count_; i > index; --i) {
        memcpy(words_[i], words_[i - 1], sizeof(words_[i]));
        memcpy(sources_[i], sources_[i - 1], sizeof(sources_[i]));
    }
    snprintf(words_[index], sizeof(words_[index]), "%s", text);
    snprintf(sources_[index], sizeof(sources_[index]), "%s", source ? source : "");
    ++count_;
    cursor_ = index;
    return true;
}

bool Sentence::remove(uint8_t index) {
    if (index >= count_) return false;
    for (uint8_t i = index; i + 1 < count_; ++i) {
        memcpy(words_[i], words_[i + 1], sizeof(words_[i]));
        memcpy(sources_[i], sources_[i + 1], sizeof(sources_[i]));
    }
    --count_;
    words_[count_][0] = '\0';
    sources_[count_][0] = '\0';
    cursor_ = count_ == 0 ? 0 : (index < count_ ? index : count_ - 1);
    return true;
}

void Sentence::clear() {
    for (uint8_t i = 0; i < count_; ++i) {
        words_[i][0] = '\0';
        sources_[i][0] = '\0';
    }
    count_ = 0;
    cursor_ = 0;
}

void Sentence::moveCursor(int steps) {
    if (count_ == 0) {
        cursor_ = 0;
        return;
    }
    int next = cursor_ + steps % count_;
    if (next < 0) next += count_;
    else if (next >= count_) next -= count_;
    cursor_ = static_cast<uint8_t>(next);
}

void Sentence::setCursor(uint8_t index) {
    cursor_ = count_ == 0 ? 0 : (index < count_ ? index : count_ - 1);
}
