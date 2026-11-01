#include "web_serial.h"

#include <string.h>

void WebSerialEvents::begin(const char* device, const char* session, uint32_t now) {
    started_ = write_ && wlp2::validHex(device, 16) && wlp2::validHex(session, 16);
    if (!started_) return;
    memcpy(device_, device, sizeof(device_));
    memcpy(session_, session, sizeof(session_));
    sequence_ = 0;
    lastReadyMs_ = now;
    emit("ready");
}

void WebSerialEvents::tick(uint32_t now) {
    if (!started_ || now - lastReadyMs_ < 2000) return;
    lastReadyMs_ = now;
    // Allows a page opened long after boot to identify the firmware.
    emit("ready");
}

void WebSerialEvents::pressConfirm(WordGame& game, uint32_t now) {
    const bool wasPicker = game.view() == WordGame::View::Picker;
    const bool wasConfirmed = game.confirmed();
    game.pressConfirm(now);
    // Second A transfers a word; sentence editing and NVS restoration are not
    // new local word selections. Never publish those as another confirmed word.
    if (wasPicker && !wasConfirmed &&
        game.view() == WordGame::View::Picker && game.confirmed()) {
        emit("word_confirmed", game.currentWord());
    }
}

bool WebSerialEvents::emit(const char* type, const char* word) {
    if (!started_) return false;
    if (word && !wlp2::validText(word, strlen(word))) return false;
    memcpy(frame_, "WIH:", 4);
    wlp2::JsonWriter json(frame_ + 4, sizeof(frame_) - 5);
    json.beginObject();
    json.fieldString("type", type);
    json.fieldUInt("protocol", 1);
    json.fieldString("device", device_);
    json.fieldString("session", session_);
    if (word) {
        json.fieldString("word", word);
        json.fieldUInt("seq", sequence_ + 1);
    }
    json.endObject();
    if (!json.finish()) return false;
    if (word) ++sequence_;
    const size_t length = 4 + json.size();
    frame_[length] = '\n';
    // No Serial.flush(): do not wait for the whole frame to drain here.
    write_(frame_, length + 1);
    return true;
}
