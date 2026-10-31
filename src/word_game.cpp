#include "word_game.h"

#include <stdio.h>
#include <string.h>

using namespace wlp2;

namespace {

int portIndex(Port port) {
    return port == Port::Left ? 0 : 1;
}

const char* sideName(Port port) {
    return port == Port::Left ? "L" : "R";
}

void copyValue(char* out, size_t capacity, const char* value) {
    snprintf(out, capacity, "%s", value ? value : "");
}

void copyWord(char* out, const char* value) {
    if (value && *value && validText(value, strlen(value))) copyValue(out, TEXT_MAX + 1, value);
    else out[0] = '\0';
}

bool elapsed(uint32_t now, uint32_t start, uint32_t duration) {
    return static_cast<uint32_t>(now - start) >= duration;
}

} // namespace

WordGame::WordGame(Node& node, PickerRandomFn pickerRandom, RandomFn requestRandom)
    : node_(node), requestRandom_(requestRandom), picker_(pickerRandom) {}

bool WordGame::begin(const Identity& identity, uint32_t now) {
    now_ = now;
    node_.setHandler(*this);
    bool configured = node_.begin(identity, now);
    picker_.init();
    confirmed_ = false;
    view_ = View::Picker;
    status_ = Status::None;
    leftWord_[0] = rightWord_[0] = '\0';
    leftSource_[0] = rightSource_[0] = '\0';
    wordRev_ = 1;
    uiRevision_ = 0;
    receivedTransfers_ = 0;
    statusUntil_ = 0;
    saveRequested_ = false;
    displayRequested_ = true;
    pending_ = false;
    pendingReq_[0] = pendingDst_[0] = pendingText_[0] = '\0';
    nextOfferMs_ = 0;
    for (unsigned i = 0; i < 2; ++i) {
        remotes_[i] = Remote();
        remotes_[i].lastQuery = now - 500;
    }
    for (auto& receipt : receipts_) receipt = Receipt();
    nextReceiptOrder_ = 1;
    donePending_ = false;
    doneReq_[0] = doneDst_[0] = '\0';
    nextDoneMs_ = 0;
    return configured;
}

void WordGame::makeRequest(char* out) {
    for (unsigned i = 0; i < 4; ++i) {
        snprintf(out + i * 8, 9, "%08lx",
                 static_cast<unsigned long>(requestRandom_ ? requestRandom_() : 0));
    }
}

void WordGame::changed() {
    ++uiRevision_;
    displayRequested_ = true;
}

void WordGame::setStatus(Status status) {
    if (status_ == status) return;
    status_ = status;
    changed();
}

const char* WordGame::statusText() const {
    switch (status_) {
        case Status::NoLink: return "NO LINK";
        case Status::TwoLinks: return "CHOOSE ONE LINK";
        case Status::Sending: return "SENDING / WAIT";
        case Status::ReceivedLeft: return "RECEIVED FROM L";
        case Status::ReceivedRight: return "RECEIVED FROM R";
        case Status::Sent: return "SENT - NEW WORD";
        case Status::SlotBusy: return "SIDE BUSY - B CLEARS";
        case Status::Invalid: return "TRANSFER REJECTED";
        default: return "";
    }
}

void WordGame::clearSentence() {
    if (!hasSentence() && view_ == View::Picker) return;
    leftWord_[0] = rightWord_[0] = '\0';
    leftSource_[0] = rightSource_[0] = '\0';
    view_ = View::Picker;
    saveRequested_ = true;
    changed();
}

bool WordGame::directPeer(const char* id, Port& port) const {
    for (unsigned i = 0; i < 2; ++i) {
        Port candidate = static_cast<Port>(i);
        LinkInfo link = node_.link(candidate);
        if (link.up() && strcmp(link.peerId, id) == 0) {
            port = candidate;
            return true;
        }
    }
    return false;
}

bool WordGame::directMessage(const NetMessage& message) const {
    LinkInfo link = node_.link(message.ingress);
    return link.up() && strcmp(link.peerId, message.src) == 0 &&
           strcmp(link.peerBoot, message.boot) == 0;
}

void WordGame::localWordChanged() {
    ++wordRev_;
    char body[320];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldUInt("rev", wordRev_);
    out.fieldString("text", currentWord());
    out.endObject();
    if (out.finish()) node_.sendNet("*", "STATE_EVT", body, now_);
    changed();
}

void WordGame::pressConfirm(uint32_t now) {
    now_ = now;
    if (pending_) return;
    if (view_ == View::Sentence) clearSentence();
    if (!confirmed_) {
        confirmed_ = true;
        statusUntil_ = 0;
        setStatus(Status::None);
        saveRequested_ = true;
        changed();
        return;
    }
    if (node_.connectedCount() == 0) {
        setStatus(Status::NoLink);
        return;
    }
    if (node_.connectedCount() != 1) {
        setStatus(Status::TwoLinks);
        return;
    }
    Port port = node_.link(Port::Left).up() ? Port::Left : Port::Right;
    LinkInfo peer = node_.link(port);
    pending_ = true;
    makeRequest(pendingReq_);
    copyValue(pendingDst_, sizeof(pendingDst_), peer.peerId);
    copyValue(pendingText_, sizeof(pendingText_), currentWord());
    nextOfferMs_ = now;
    statusUntil_ = 0;
    setStatus(Status::Sending);
    tryOffer(now);
}

void WordGame::pressRefresh(uint32_t now) {
    now_ = now;
    if (pending_) {
        pending_ = false;
        confirmed_ = false;
        pendingReq_[0] = '\0';
        setStatus(Status::None);
        saveRequested_ = true;
        changed();
        return;
    }
    if (view_ == View::Sentence) {
        clearSentence();
        setStatus(Status::None);
        return;
    }
    if (confirmed_) {
        confirmed_ = false;
        setStatus(Status::None);
        saveRequested_ = true;
        changed();
        return;
    }
    picker_.refreshRandom();
    statusUntil_ = 0;
    setStatus(Status::None);
    localWordChanged();
}

void WordGame::turnEncoder(int steps, uint32_t now) {
    now_ = now;
    if (steps == 0) return;
    bool needsSave = confirmed_ || pending_;
    if (view_ == View::Sentence) clearSentence();
    if (pending_) {
        pending_ = false;
        pendingReq_[0] = '\0';
    }
    confirmed_ = false;
    if (needsSave) saveRequested_ = true;
    for (int i = 0; i < steps; ++i) picker_.stepBackward();
    for (int i = 0; i > steps; --i) picker_.stepForward();
    statusUntil_ = 0;
    setStatus(Status::None);
    localWordChanged();
}

void WordGame::tryOffer(uint32_t now) {
    if (!pending_ || static_cast<int32_t>(now - nextOfferMs_) < 0) return;
    Port port;
    if (!directPeer(pendingDst_, port)) return;
    char body[512];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldString("req", pendingReq_);
    out.fieldString("text", pendingText_);
    out.endObject();
    if (!out.finish()) return;
    bool sent = node_.sendNet(port, pendingDst_, "WORD_OFFER", body, now);
    nextOfferMs_ = now + (sent ? 700 : 250);
}

void WordGame::sendError(const NetMessage& message, const char* req, const char* code) {
    char body[128];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldString("req", req);
    out.fieldString("code", code);
    out.endObject();
    if (out.finish()) node_.sendNet(message.ingress, message.src, "ERROR", body, now_);
}

bool WordGame::migrateReceipt(Receipt& receipt, Port side) {
    if (receipt.side == side) return true;
    char* oldWord = receipt.side == Port::Left ? leftWord_ : rightWord_;
    char* oldSource = receipt.side == Port::Left ? leftSource_ : rightSource_;
    char* newWord = side == Port::Left ? leftWord_ : rightWord_;
    char* newSource = side == Port::Left ? leftSource_ : rightSource_;
    if (*newWord && (!*newSource || strcmp(newSource, receipt.src) != 0)) return false;
    if (strcmp(oldWord, receipt.text) == 0) {
        oldWord[0] = '\0';
        oldSource[0] = '\0';
    }
    copyValue(newWord, TEXT_MAX + 1, receipt.text);
    copyValue(newSource, 17, receipt.src);
    receipt.side = side;
    view_ = View::Sentence;
    saveRequested_ = true;
    changed();
    return true;
}

void WordGame::receiveOffer(const NetMessage& message, const JsonReader& body) {
    char req[33];
    char text[TEXT_MAX + 1];
    if (!body.getString("req", req, sizeof(req)) || !validHex(req, 32) ||
        !body.getText("text", text, sizeof(text)) || !directMessage(message)) return;
    if (strcmp(message.dst, node_.identity().id) != 0) {
        sendError(message, req, "INVALID");
        return;
    }
    for (auto& existing : receipts_) {
        if (!existing.used || strcmp(existing.src, message.src) != 0 ||
            strcmp(existing.boot, message.boot) != 0 || strcmp(existing.req, req) != 0) continue;
        if (strcmp(existing.text, text) != 0) {
            sendError(message, req, "INVALID");
            return;
        }
        if (!migrateReceipt(existing, message.ingress)) {
            sendError(message, req, "BUSY");
            setStatus(Status::SlotBusy);
            return;
        }
        existing.ackPending = true;
        return;
    }
    Port side = message.ingress;
    char* word = side == Port::Left ? leftWord_ : rightWord_;
    char* source = side == Port::Left ? leftSource_ : rightSource_;
    if (*word && (!*source || strcmp(source, message.src) != 0)) {
        sendError(message, req, "BUSY");
        setStatus(Status::SlotBusy);
        return;
    }
    Receipt* receipt = nullptr;
    for (auto& candidate : receipts_) {
        if (!candidate.used) {
            receipt = &candidate;
            break;
        }
    }
    if (!receipt) {
        for (auto& candidate : receipts_) {
            if (candidate.completed && (!receipt || candidate.order < receipt->order)) {
                receipt = &candidate;
            }
        }
    }
    if (!receipt) {
        sendError(message, req, "BUSY");
        setStatus(Status::SlotBusy);
        return;
    }
    *receipt = Receipt();
    receipt->used = true;
    receipt->ackPending = true;
    receipt->side = side;
    receipt->order = nextReceiptOrder_++;
    copyValue(receipt->src, sizeof(receipt->src), message.src);
    copyValue(receipt->boot, sizeof(receipt->boot), message.boot);
    copyValue(receipt->req, sizeof(receipt->req), req);
    copyValue(receipt->text, sizeof(receipt->text), text);
    copyValue(word, TEXT_MAX + 1, text);
    copyValue(source, 17, message.src);
    view_ = View::Sentence;
    ++receivedTransfers_;
    saveRequested_ = true;
    statusUntil_ = now_ + 1800;
    setStatus(side == Port::Left ? Status::ReceivedLeft : Status::ReceivedRight);
    changed();
}

void WordGame::completeOffer(const NetMessage& message, const JsonReader& body) {
    char req[33];
    if (!body.getString("req", req, sizeof(req)) || !validHex(req, 32) ||
        !directMessage(message) || strcmp(message.dst, node_.identity().id) != 0) return;
    for (auto& receipt : receipts_) {
        if (!receipt.used || receipt.completed || strcmp(receipt.src, message.src) != 0 ||
            strcmp(receipt.boot, message.boot) != 0 || strcmp(receipt.req, req) != 0) continue;
        receipt.completed = true;
        saveRequested_ = true;
        return;
    }
}

void WordGame::queueDone(const char* dst, const char* req) {
    if (donePending_) return;
    donePending_ = true;
    copyValue(doneDst_, sizeof(doneDst_), dst);
    copyValue(doneReq_, sizeof(doneReq_), req);
    nextDoneMs_ = now_;
    tryDone(now_);
}

void WordGame::tryDone(uint32_t now) {
    if (!donePending_ || static_cast<int32_t>(now - nextDoneMs_) < 0) return;
    Port port;
    if (!directPeer(doneDst_, port)) return;
    char body[96];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldString("req", doneReq_);
    out.endObject();
    if (out.finish() && node_.sendNet(port, doneDst_, "WORD_DONE", body, now)) {
        donePending_ = false;
    } else {
        nextDoneMs_ = now + 250;
    }
}

void WordGame::receiveAccept(const NetMessage& message, const JsonReader& body) {
    char req[33];
    char side[2];
    if (!pending_ || !body.getString("req", req, sizeof(req)) ||
        !body.getString("side", side, sizeof(side)) || strcmp(req, pendingReq_) != 0 ||
        strcmp(message.src, pendingDst_) != 0 || !directMessage(message)) return;
    Port expected = message.ingress == Port::Left ? Port::Right : Port::Left;
    if (strcmp(side, sideName(expected)) != 0) return;
    queueDone(message.src, req);
    pending_ = false;
    pendingReq_[0] = '\0';
    confirmed_ = false;
    view_ = hasSentence() ? View::Sentence : View::Picker;
    picker_.refreshRandom();
    saveRequested_ = true;
    localWordChanged();
    statusUntil_ = now_ + 1800;
    setStatus(Status::Sent);
}

void WordGame::writeNeighbor(JsonWriter& out, const char* key, Port port) const {
    LinkInfo link = node_.link(port);
    if (!link.up()) {
        out.fieldNull(key);
        return;
    }
    out.beginObject(key);
    out.fieldString("id", link.peerId);
    out.fieldString("boot", link.peerBoot);
    out.fieldString("port", sideName(link.peerPort));
    out.fieldString("sid", link.sid);
    out.endObject();
}

void WordGame::sendNodeInfo(const NetMessage& message, const char* req) {
    char body[1024];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldString("req", req);
    out.fieldUInt("rev", wordRev_);
    out.fieldString("text", currentWord());
    out.fieldUInt("topo_rev", node_.topoRev());
    writeNeighbor(out, "L", Port::Left);
    writeNeighbor(out, "R", Port::Right);
    out.endObject();
    if (out.finish()) node_.sendNet(message.ingress, message.src, "NODE_INFO", body, now_);
}

void WordGame::queryRemote(Port port, uint32_t now) {
    LinkInfo link = node_.link(port);
    if (!link.up()) return;
    Remote& remote = remotes_[portIndex(port)];
    if (!elapsed(now, remote.lastQuery, 500)) return;
    makeRequest(remote.queryReq);
    char body[80];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldString("req", remote.queryReq);
    out.endObject();
    if (out.finish() && node_.sendNet(port, link.peerId, "NODE_GET", body, now)) {
        remote.lastQuery = now;
    }
}

bool WordGame::readNeighbor(const JsonReader& body, const char* key,
                            NeighborSnapshot& neighbor) const {
    neighbor = NeighborSnapshot();
    if (body.isNull(key)) return true;
    JsonReader object;
    if (!body.getObject(key, object)) return false;
    if (!object.getString("id", neighbor.id, sizeof(neighbor.id)) ||
        !object.getString("boot", neighbor.boot, sizeof(neighbor.boot)) ||
        !object.getString("port", neighbor.port, sizeof(neighbor.port)) ||
        !object.getString("sid", neighbor.sid, sizeof(neighbor.sid)) ||
        !validHex(neighbor.id, 16) || !validHex(neighbor.boot, 16) ||
        !validHex(neighbor.sid, 32) ||
        (strcmp(neighbor.port, "L") != 0 && strcmp(neighbor.port, "R") != 0)) return false;
    neighbor.present = true;
    return true;
}

void WordGame::receiveNodeInfo(const NetMessage& message, const JsonReader& body) {
    if (!directMessage(message)) return;
    Remote& remote = remotes_[portIndex(message.ingress)];
    char req[33];
    char text[TEXT_MAX + 1];
    uint32_t rev, topoRev;
    NeighborSnapshot left, right;
    if (!body.getString("req", req, sizeof(req)) || strcmp(req, remote.queryReq) != 0 ||
        !body.getUInt("rev", rev) || !body.getText("text", text, sizeof(text)) ||
        !body.getUInt("topo_rev", topoRev) || !readNeighbor(body, "L", left) ||
        !readNeighbor(body, "R", right)) return;
    bool wasReady = remote.snapshot.valid && remote.snapshot.orderValid;
    copyValue(remote.snapshot.id, sizeof(remote.snapshot.id), message.src);
    copyValue(remote.snapshot.boot, sizeof(remote.snapshot.boot), message.boot);
    copyValue(remote.snapshot.text, sizeof(remote.snapshot.text), text);
    remote.snapshot.rev = rev;
    remote.snapshot.topoRev = topoRev;
    remote.snapshot.left = left;
    remote.snapshot.right = right;
    remote.snapshot.valid = true;
    LinkInfo link = node_.link(message.ingress);
    const NeighborSnapshot& edge = message.ingress == Port::Right ? left : right;
    remote.snapshot.orderValid = edge.present &&
        strcmp(edge.id, node_.identity().id) == 0 &&
        strcmp(edge.boot, node_.identity().boot) == 0 &&
        strcmp(edge.port, sideName(message.ingress)) == 0 && strcmp(edge.sid, link.sid) == 0;
    if (!wasReady && remote.snapshot.orderValid) changed();
}

void WordGame::receiveState(const NetMessage& message, const JsonReader& body) {
    if (!directMessage(message)) return;
    Remote& remote = remotes_[portIndex(message.ingress)];
    if (!remote.snapshot.valid) return;
    uint32_t rev;
    char text[TEXT_MAX + 1];
    if (!body.getUInt("rev", rev) || !body.getText("text", text, sizeof(text))) return;
    remote.snapshot.rev = rev;
    copyValue(remote.snapshot.text, sizeof(remote.snapshot.text), text);
}

void WordGame::onLinkChanged(Port port) {
    Remote& remote = remotes_[portIndex(port)];
    remote = Remote();
    remote.lastQuery = now_ - 500;
    changed();
    char body[48];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldUInt("topo_rev", node_.topoRev());
    out.endObject();
    if (out.finish()) node_.sendNet("*", "TOPO_EVT", body, now_);
}

void WordGame::onNet(const NetMessage& message, const JsonReader& body) {
    if (strcmp(message.kind, "NODE_GET") == 0) {
        char req[33];
        if (body.getString("req", req, sizeof(req)) && validHex(req, 32)) {
            sendNodeInfo(message, req);
        }
    } else if (strcmp(message.kind, "NODE_INFO") == 0) {
        receiveNodeInfo(message, body);
    } else if (strcmp(message.kind, "STATE_EVT") == 0) {
        receiveState(message, body);
    } else if (strcmp(message.kind, "TOPO_EVT") == 0) {
        if (directMessage(message)) remotes_[portIndex(message.ingress)].lastQuery = now_ - 500;
    } else if (strcmp(message.kind, "WORD_OFFER") == 0) {
        receiveOffer(message, body);
    } else if (strcmp(message.kind, "WORD_ACCEPT") == 0) {
        receiveAccept(message, body);
    } else if (strcmp(message.kind, "WORD_DONE") == 0) {
        completeOffer(message, body);
    } else if (strcmp(message.kind, "ERROR") == 0) {
        char req[33], code[24];
        if (!pending_ || !body.getString("req", req, sizeof(req)) ||
            !body.getString("code", code, sizeof(code)) || strcmp(req, pendingReq_) != 0 ||
            strcmp(message.src, pendingDst_) != 0 ||
            strcmp(message.dst, node_.identity().id) != 0 || !directMessage(message)) return;
        if (strcmp(code, "BUSY") == 0) {
            nextOfferMs_ = now_ + 1000;
            setStatus(Status::SlotBusy);
        } else if (strcmp(code, "INVALID") == 0 || strcmp(code, "UNSUPPORTED") == 0) {
            pending_ = false;
            setStatus(Status::Invalid);
        }
    } else {
        char req[33];
        if (body.getString("req", req, sizeof(req)) && validHex(req, 32)) {
            sendError(message, req, "UNSUPPORTED");
        }
    }
}

void WordGame::flushReceipts(uint32_t now) {
    for (auto& receipt : receipts_) {
        if (!receipt.used || !receipt.ackPending) continue;
        Port port;
        if (!directPeer(receipt.src, port)) continue;
        char body[96];
        JsonWriter out(body, sizeof(body));
        out.beginObject();
        out.fieldString("req", receipt.req);
        out.fieldString("side", sideName(receipt.side));
        out.endObject();
        if (out.finish() && node_.sendNet(port, receipt.src, "WORD_ACCEPT", body, now)) {
            receipt.ackPending = false;
        }
    }
}

void WordGame::tick(uint32_t now) {
    now_ = now;
    node_.tick(now);
    queryRemote(Port::Left, now);
    queryRemote(Port::Right, now);
    tryOffer(now);
    flushReceipts(now);
    tryDone(now);
    if (statusUntil_ && static_cast<int32_t>(now - statusUntil_) >= 0) {
        statusUntil_ = 0;
        setStatus(Status::None);
    }
}

uint8_t WordGame::participantCount() const {
    return 1 + node_.connectedCount();
}

const WordGame::RemoteSnapshot& WordGame::remote(Port port) const {
    static const RemoteSnapshot empty;
    if (port == Port::Any) return empty;
    return remotes_[portIndex(port)].snapshot;
}

void WordGame::restore(bool confirmed, const char* leftWord, const char* rightWord,
                       const char* leftSource, const char* rightSource,
                       const ReceiptState* receipts, uint8_t receiptCount) {
    confirmed_ = confirmed;
    pending_ = false;
    pendingReq_[0] = '\0';
    donePending_ = false;
    copyWord(leftWord_, leftWord);
    copyWord(rightWord_, rightWord);
    copyValue(leftSource_, sizeof(leftSource_), leftWord_[0] ? leftSource : "");
    copyValue(rightSource_, sizeof(rightSource_), rightWord_[0] ? rightSource : "");
    for (auto& receipt : receipts_) receipt = Receipt();
    nextReceiptOrder_ = 1;
    if (receipts) {
        if (receiptCount > 8) receiptCount = 8;
        for (uint8_t i = 0; i < receiptCount; ++i) {
            Receipt& receipt = receipts_[i];
            receipt.used = true;
            receipt.completed = receipts[i].completed != 0;
            receipt.side = receipts[i].side == 0 ? Port::Left : Port::Right;
            receipt.order = receipts[i].order;
            copyValue(receipt.src, sizeof(receipt.src), receipts[i].src);
            copyValue(receipt.boot, sizeof(receipt.boot), receipts[i].boot);
            copyValue(receipt.req, sizeof(receipt.req), receipts[i].req);
            copyValue(receipt.text, sizeof(receipt.text), receipts[i].text);
            if (receipt.order >= nextReceiptOrder_) nextReceiptOrder_ = receipt.order + 1;
        }
    }
    view_ = hasSentence() ? View::Sentence : View::Picker;
    status_ = Status::None;
    statusUntil_ = 0;
    saveRequested_ = false;
    changed();
}

uint8_t WordGame::exportReceipts(ReceiptState* out, uint8_t capacity) const {
    if (!out) return 0;
    uint8_t count = 0;
    for (const auto& receipt : receipts_) {
        if (!receipt.used || count >= capacity) continue;
        out[count] = {};
        copyValue(out[count].src, sizeof(out[count].src), receipt.src);
        copyValue(out[count].boot, sizeof(out[count].boot), receipt.boot);
        copyValue(out[count].req, sizeof(out[count].req), receipt.req);
        copyValue(out[count].text, sizeof(out[count].text), receipt.text);
        out[count].side = receipt.side == Port::Left ? 0 : 1;
        out[count].completed = receipt.completed ? 1 : 0;
        out[count].order = receipt.order;
        ++count;
    }
    return count;
}

bool WordGame::consumeSaveRequest() {
    bool requested = saveRequested_;
    saveRequested_ = false;
    return requested;
}

bool WordGame::consumeDisplayRequest() {
    bool requested = displayRequested_;
    displayRequested_ = false;
    return requested;
}
