#include "web_serial.h"

#include <string.h>
#include <stdio.h>

void WebSerialEvents::begin(const char* device, const char* session, uint32_t now) {
    started_ = write_ && wlp2::validHex(device, 16) && wlp2::validHex(session, 16);
    if (!started_) return;
    memcpy(device_, device, sizeof(device_));
    memcpy(session_, session, sizeof(session_));
    sending_ = false;
    sequence_ = 0;
    commandLength_ = 0;
    discardCommand_ = false;
    lastReadyMs_ = now;
    emitReady();
}

void WebSerialEvents::tick(uint32_t now) {
    if (started_ && sending_) {
        emitSnapshot(); // At most one bounded frame per service call.
        return;
    }
    if (!started_ || now - lastReadyMs_ < 2000) return;
    lastReadyMs_ = now;
    // Allows a page opened long after boot to identify the firmware.
    emitReady();
}

bool WebSerialEvents::emitReady() {
    if (!started_) return false;
    memcpy(frame_, "WIH:", 4);
    wlp2::JsonWriter json(frame_ + 4, sizeof(frame_) - 5);
    json.beginObject();
    json.fieldString("type", "ready");
    json.fieldUInt("protocol", 1);
    json.fieldString("device", device_);
    json.fieldString("session", session_);
    json.fieldUInt("sentenceExport", 1);
    json.endObject();
    if (!json.finish()) return false;
    const size_t length = 4 + json.size();
    frame_[length] = '\n';
    if (canWrite_ && !canWrite_(length + 1)) return false;
    write_(frame_, length + 1);
    return true;
}

void WebSerialEvents::receive(char byte, const WordGame& game) {
    if (byte == '\n') {
        if (started_ && !discardCommand_ && commandLength_ > 4 &&
            memcmp(command_, "WIH:", 4) == 0) {
            wlp2::JsonReader json(command_ + 4, commandLength_ - 4);
            char type[24], request[17];
            uint32_t protocol = 0;
            if (json.valid() && json.getUInt("protocol", protocol) && protocol == 1 &&
                json.getString("type", type, sizeof(type)) &&
                strcmp(type, "get_sentence") == 0 &&
                json.getString("request", request, sizeof(request)) &&
                wlp2::validHex(request, 16)) {
                // A new request supersedes an old, unfinished export.
                snapshot_ = game.sentence();
                memcpy(request_, request, sizeof(request_));
                snapshotStep_ = 0;
                sending_ = true;
            }
        }
        commandLength_ = 0;
        discardCommand_ = false;
    } else if (!discardCommand_ && byte != '\r') {
        if (commandLength_ < sizeof(command_)) command_[commandLength_++] = byte;
        else { commandLength_ = 0; discardCommand_ = true; }
    }
}

bool WebSerialEvents::emitSnapshot() {
    memcpy(frame_, "WIH:", 4);
    wlp2::JsonWriter json(frame_ + 4, sizeof(frame_) - 5);
    const bool begin = snapshotStep_ == 0;
    const bool end = snapshotStep_ == snapshot_.count() + 1;
    json.beginObject();
    json.fieldString("type", begin ? "sentence_begin" : end ? "sentence_end" : "sentence_word");
    json.fieldUInt("protocol", 1);
    json.fieldString("device", device_);
    json.fieldString("session", session_);
    json.fieldString("request", request_);
    json.fieldUInt("count", snapshot_.count());
    if (!begin && !end) {
        json.fieldUInt("index", snapshotStep_ - 1);
        json.fieldString("word", snapshot_.text(snapshotStep_ - 1));
    }
    json.endObject();
    if (!json.finish()) { sending_ = false; return false; }
    const size_t length = 4 + json.size();
    frame_[length] = '\n';
    if (canWrite_ && !canWrite_(length + 1)) return false;
    write_(frame_, length + 1);
    if (end) sending_ = false;
    else ++snapshotStep_;
    return true;
}

void WebSerialEvents::observeConfirm(const WordGame& game, bool wasPicker, bool wasConfirmed) {
    // Observe the existing handler; never call a gameplay action from this module.
    if (wasPicker && !wasConfirmed && game.view() == WordGame::View::Picker && game.confirmed())
        emitConfirmed(game.currentWord());
}

void WebSerialEvents::emitConfirmed(const char* word) {
    if (!started_ || !wlp2::validText(word, strlen(word))) return;
    memcpy(frame_, "WIH:", 4);
    wlp2::JsonWriter json(frame_ + 4, sizeof(frame_) - 5);
    json.beginObject();
    json.fieldString("type", "word_confirmed");
    json.fieldUInt("protocol", 1);
    json.fieldString("device", device_);
    json.fieldString("session", session_);
    json.fieldUInt("seq", sequence_ + 1);
    json.fieldString("word", word);
    json.endObject();
    if (!json.finish()) return;
    const size_t length = 4 + json.size();
    frame_[length] = '\n';
    ++sequence_;
    write_(frame_, length + 1);
}
