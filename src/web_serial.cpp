#include "web_serial.h"

#include <string.h>
#include <stdio.h>

void WebSerialEvents::begin(const char* device, const char* session, uint32_t now) {
    started_ = write_ && wlp2::validHex(device, 16) && wlp2::validHex(session, 16);
    if (!started_) return;
    memcpy(device_, device, sizeof(device_));
    memcpy(session_, session, sizeof(session_));
    sending_ = false;
    uploading_ = false;
    sequence_ = 0;
    commandLength_ = 0;
    discardCommand_ = false;
    lastReadyMs_ = now;
    emitReady();
}

void WebSerialEvents::tick(uint32_t now) {
    if (started_ && sending_) {
        emitSnapshot();
        return;
    }
    if (!started_ || now - lastReadyMs_ < 2000) return;
    lastReadyMs_ = now;
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
                beginSnapshot(game, request, false);
            }
        }
        commandLength_ = 0;
        discardCommand_ = false;
    } else if (!discardCommand_ && byte != '\r') {
        if (commandLength_ < sizeof(command_)) command_[commandLength_++] = byte;
        else { commandLength_ = 0; discardCommand_ = true; }
    }
}

bool WebSerialEvents::beginSnapshot(const WordGame& game, const char* request, bool upload) {
    snapshot_ = game.sentence();
    memcpy(request_, request, sizeof(request_));
    snapshotStep_ = 0;
    sending_ = true;
    uploading_ = upload;
    return true;
}

bool WebSerialEvents::uploadSentence(const WordGame& game) {
    if (!started_ || sending_ || !game.hasSentence()) return false;
    char request[17];
    snprintf(request, sizeof(request), "%08lx%08lx",
             static_cast<unsigned long>(sequence_ + 1),
             static_cast<unsigned long>(sequence_ + 1));
    return beginSnapshot(game, request, true);
}

bool WebSerialEvents::emitSnapshot() {
    memcpy(frame_, "WIH:", 4);
    wlp2::JsonWriter json(frame_ + 4, sizeof(frame_) - 5);
    const bool begin = snapshotStep_ == 0;
    const bool end = snapshotStep_ == snapshot_.count() + 1;
    const char* type;
    if (uploading_) type = begin ? "upload_begin" : end ? "upload_end" : "upload_word";
    else type = begin ? "sentence_begin" : end ? "sentence_end" : "sentence_word";
    json.beginObject();
    json.fieldString("type", type);
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
    if (end) {
        sending_ = false;
        uploading_ = false;
    } else {
        ++snapshotStep_;
    }
    return true;
}

void WebSerialEvents::observeConfirm(const WordGame& game, bool wasPicker, bool wasConfirmed) {
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
