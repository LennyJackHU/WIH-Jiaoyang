#pragma once

#include <stdint.h>
#include "wlp2_json.h"

enum class SentenceEdit : uint8_t { Navigate, DeleteConfirm, InsertSide };

class Sentence {
public:
    static const uint8_t MAX_WORDS = 12;

    bool empty() const { return count_ == 0; }
    uint8_t count() const { return count_; }
    const char* text(uint8_t index) const;
    const char* source(uint8_t index) const;

    bool insert(uint8_t index, const char* text, const char* source);
    bool remove(uint8_t index);
    void clear();

    uint8_t cursor() const { return cursor_; }
    void moveCursor(int steps);
    void setCursor(uint8_t index);

private:
    char words_[MAX_WORDS][wlp2::TEXT_MAX + 1] = {};
    char sources_[MAX_WORDS][17] = {};
    uint8_t count_ = 0;
    uint8_t cursor_ = 0;
};
