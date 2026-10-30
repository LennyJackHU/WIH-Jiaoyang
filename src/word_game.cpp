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

bool elapsed(uint32_t now, uint32_t start, uint32_t duration) {
    return static_cast<uint32_t>(now - start) >= duration;
}

bool restorableText(const char* text) {
    return text && validText(text, strlen(text));
}

bool restorableReceipt(const WordGame::ReceiptState& state) {
    return state.src[16] == '\0' && state.boot[16] == '\0' && state.req[32] == '\0' &&
           state.text[TEXT_MAX] == '\0' && validHex(state.src, 16) &&
           validHex(state.boot, 16) && validHex(state.req, 32) &&
           validText(state.text, strlen(state.text)) && state.side <= 1 &&
           state.position <= Sentence::MAX_WORDS && state.completed <= 1 && state.order != 0;
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
    sentence_.clear();
    editMode_ = SentenceEdit::Navigate;
    insertAfter_ = false;
    wordRev_ = 1;
    uiRevision_ = 0;
    receivedTransfers_ = 0;
    statusUntil_ = 0;
    saveRequested_ = false;
    displayRequested_ = true;
    pending_ = false;
    pendingReq_[0] = pendingDst_[0] = pendingText_[0] = '\0';
    pendingPositioned_ = false;
    pendingPosition_ = 0;
    nextOfferMs_ = 0;
    pullPending_ = false;
    pullReq_[0] = pullDst_[0] = '\0';
    pullPosition_ = 0;
    nextPullMs_ = 0;
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
        case Status::SlotBusy: return "SENTENCE FULL/BUSY";
        case Status::Invalid: return "TRANSFER REJECTED";
        default: return "";
    }
}

void WordGame::clearSentence() {
    if (!hasSentence() && view_ == View::Picker) return;
    sentence_.clear();
    view_ = View::Picker;
    editMode_ = SentenceEdit::Navigate;
    insertAfter_ = false;
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

void WordGame::startOffer(const char* dst, const char* req, const char* text,
                          bool positioned, uint8_t position, uint32_t now) {
    pending_ = true;
    copyValue(pendingReq_, sizeof(pendingReq_), req);
    copyValue(pendingDst_, sizeof(pendingDst_), dst);
    copyValue(pendingText_, sizeof(pendingText_), text);
    pendingPositioned_ = positioned;
    pendingPosition_ = position;
    nextOfferMs_ = now;
    statusUntil_ = 0;
    setStatus(Status::Sending);
    tryOffer(now);
}

void WordGame::startPull(Port port, uint32_t now) {
    LinkInfo peer = node_.link(port);
    pullPending_ = true;
    makeRequest(pullReq_);
    copyValue(pullDst_, sizeof(pullDst_), peer.peerId);
    pullPosition_ = sentence_.empty() ? 0 : sentence_.cursor() + (insertAfter_ ? 1 : 0);
    nextPullMs_ = now;
    statusUntil_ = 0;
    setStatus(Status::Sending);
    tryPull(now);
}

void WordGame::clearPull() {
    pullPending_ = false;
    pullReq_[0] = pullDst_[0] = '\0';
    pullPosition_ = 0;
    nextPullMs_ = 0;
}

void WordGame::pressConfirm(uint32_t now) {
    now_ = now;
    if (pending_ || pullPending_) return;
    if (view_ == View::Sentence) {
        if (editMode_ == SentenceEdit::DeleteConfirm) {
            if (sentence_.empty()) return;
            sentence_.remove(sentence_.cursor());
            editMode_ = SentenceEdit::Navigate;
            saveRequested_ = true;
            setStatus(Status::None);
            changed();
            return;
        }
        if (editMode_ == SentenceEdit::InsertSide) {
            if (sentence_.count() >= Sentence::MAX_WORDS) {
                editMode_ = SentenceEdit::Navigate;
                setStatus(Status::SlotBusy);
                changed();
                return;
            }
            if (node_.connectedCount() != 1) {
                editMode_ = SentenceEdit::Navigate;
                setStatus(Status::NoLink);
                changed();
                return;
            }
            Port port = node_.link(Port::Left).up() ? Port::Left : Port::Right;
            startPull(port, now);
            return;
        }
        if (node_.connectedCount() == 0) {
            if (sentence_.empty()) return;
            editMode_ = SentenceEdit::DeleteConfirm;
            setStatus(Status::None);
            changed();
            return;
        }
        if (node_.connectedCount() != 1) {
            setStatus(Status::TwoLinks);
            return;
        }
        Port port = node_.link(Port::Left).up() ? Port::Left : Port::Right;
        const RemoteSnapshot& snapshot = remote(port);
        if (!snapshot.valid || !snapshot.orderValid) {
            setStatus(Status::NoLink);
            return;
        }
        if (sentence_.count() >= Sentence::MAX_WORDS) {
            setStatus(Status::SlotBusy);
            return;
        }
        editMode_ = SentenceEdit::InsertSide;
        setStatus(Status::None);
        changed();
        return;
    }
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
    char req[33];
    makeRequest(req);
    startOffer(peer.peerId, req, currentWord(), false, 0, now);
}

void WordGame::pressRefresh(uint32_t now) {
    now_ = now;
    if (pullPending_) {
        clearPull();
        editMode_ = SentenceEdit::Navigate;
        insertAfter_ = false;
        setStatus(pending_ ? Status::Sending : Status::None);
        changed();
        return;
    }
    if (pending_) {
        pending_ = false;
        confirmed_ = false;
        pendingReq_[0] = '\0';
        pendingPositioned_ = false;
        pendingPosition_ = 0;
        setStatus(Status::None);
        saveRequested_ = true;
        changed();
        return;
    }
    if (view_ == View::Sentence) {
        if (editMode_ == SentenceEdit::DeleteConfirm) {
            editMode_ = SentenceEdit::Navigate;
            setStatus(Status::None);
            changed();
        } else if (editMode_ == SentenceEdit::InsertSide) {
            insertAfter_ = !insertAfter_;
            setStatus(Status::None);
            changed();
        } else {
            clearSentence();
            setStatus(Status::None);
        }
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
    if (view_ == View::Sentence) {
        if (editMode_ == SentenceEdit::DeleteConfirm || sentence_.empty()) return;
        uint8_t previous = sentence_.cursor();
        sentence_.moveCursor(steps);
        if (sentence_.cursor() != previous) changed();
        if (!pending_ && !pullPending_) {
            statusUntil_ = 0;
            setStatus(Status::None);
        }
        return;
    }
    bool needsSave = confirmed_ || pending_;
    if (pending_) {
        pending_ = false;
        pendingReq_[0] = '\0';
        pendingPositioned_ = false;
        pendingPosition_ = 0;
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
    if (pendingPositioned_) out.fieldUInt("pos", pendingPosition_);
    out.endObject();
    if (!out.finish()) return;
    bool sent = node_.sendNet(port, pendingDst_, "WORD_OFFER", body, now);
    nextOfferMs_ = now + (sent ? 700 : 250);
}

void WordGame::tryPull(uint32_t now) {
    if (!pullPending_ || static_cast<int32_t>(now - nextPullMs_) < 0) return;
    Port port;
    if (!directPeer(pullDst_, port)) return;
    char body[128];
    JsonWriter out(body, sizeof(body));
    out.beginObject();
    out.fieldString("req", pullReq_);
    out.fieldUInt("pos", pullPosition_);
    out.endObject();
    if (!out.finish()) return;
    bool sent = node_.sendNet(port, pullDst_, "WORD_PULL", body, now);
    nextPullMs_ = now + (sent ? 700 : 250);
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

void WordGame::receivePull(const NetMessage& message, const JsonReader& body) {
    char req[33];
    uint32_t position;
    if (!body.getString("req", req, sizeof(req)) || !validHex(req, 32) ||
        !directMessage(message)) return;
    if (strcmp(message.dst, node_.identity().id) != 0 ||
        !body.getUInt("pos", position) || position > Sentence::MAX_WORDS) {
        sendError(message, req, "INVALID");
        return;
    }
    if (pending_ && strcmp(pendingReq_, req) == 0 && strcmp(pendingDst_, message.src) == 0) {
        if (!pendingPositioned_ || pendingPosition_ != position) {
            sendError(message, req, "INVALID");
            return;
        }
        tryOffer(now_);
        return;
    }
    if (pending_) {
        sendError(message, req, "BUSY");
        return;
    }
    startOffer(message.src, req, currentWord(), true, static_cast<uint8_t>(position), now_);
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
    bool positioned = body.contains("pos");
    uint32_t requestedPosition = 0;
    if (positioned && !body.getUInt("pos", requestedPosition)) {
        sendError(message, req, "INVALID");
        return;
    }
    for (auto& existing : receipts_) {
        if (!existing.used || strcmp(existing.src, message.src) != 0 ||
            strcmp(existing.boot, message.boot) != 0 || strcmp(existing.req, req) != 0) continue;
        if (strcmp(existing.text, text) != 0 || existing.positioned != positioned ||
            (positioned && existing.position != requestedPosition)) {
            sendError(message, req, "INVALID");
            return;
        }
        if (existing.side != message.ingress) {
            existing.side = message.ingress;
            saveRequested_ = true;
        }
        existing.ackPending = true;
        return;
    }
    uint8_t position = positioned ? static_cast<uint8_t>(requestedPosition) :
        (message.ingress == Port::Left ? 0 : sentence_.count());
    if (sentence_.count() >= Sentence::MAX_WORDS || requestedPosition > sentence_.count()) {
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
    if (!sentence_.insert(position, text, message.src)) {
        sendError(message, req, "BUSY");
        setStatus(Status::SlotBusy);
        return;
    }
    *receipt = Receipt();
    receipt->used = true;
    receipt->ackPending = true;
    receipt->side = message.ingress;
    receipt->positioned = positioned;
    receipt->position = position;
    receipt->order = nextReceiptOrder_++;
    copyValue(receipt->src, sizeof(receipt->src), message.src);
    copyValue(receipt->boot, sizeof(receipt->boot), message.boot);
    copyValue(receipt->req, sizeof(receipt->req), req);
    copyValue(receipt->text, sizeof(receipt->text), text);
    if (pullPending_ && positioned && strcmp(pullReq_, req) == 0 &&
        strcmp(pullDst_, message.src) == 0 && pullPosition_ == position) clearPull();
    view_ = View::Sentence;
    editMode_ = SentenceEdit::Navigate;
    ++receivedTransfers_;
    saveRequested_ = true;
    statusUntil_ = now_ + 1800;
    setStatus(message.ingress == Port::Left ? Status::ReceivedLeft : Status::ReceivedRight);
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
    pendingPositioned_ = false;
    pendingPosition_ = 0;
    confirmed_ = false;
    view_ = hasSentence() ? View::Sentence : View::Picker;
    if (view_ == View::Picker) editMode_ = SentenceEdit::Navigate;
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
    bool wordChanged = !remote.snapshot.valid || remote.snapshot.rev != rev ||
                       strcmp(remote.snapshot.text, text) != 0;
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
    if (wordChanged || (!wasReady && remote.snapshot.orderValid)) changed();
}

void WordGame::receiveState(const NetMessage& message, const JsonReader& body) {
    if (!directMessage(message)) return;
    Remote& remote = remotes_[portIndex(message.ingress)];
    if (!remote.snapshot.valid) return;
    uint32_t rev;
    char text[TEXT_MAX + 1];
    if (!body.getUInt("rev", rev) || !body.getText("text", text, sizeof(text))) return;
    if (remote.snapshot.rev == rev && strcmp(remote.snapshot.text, text) == 0) return;
    remote.snapshot.rev = rev;
    copyValue(remote.snapshot.text, sizeof(remote.snapshot.text), text);
    changed();
}

void WordGame::onLinkChanged(Port port) {
    Remote& remote = remotes_[portIndex(port)];
    remote = Remote();
    remote.lastQuery = now_ - 500;
    if (editMode_ == SentenceEdit::InsertSide && node_.connectedCount() != 1) {
        editMode_ = SentenceEdit::Navigate;
        insertAfter_ = false;
    }
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
    } else if (strcmp(message.kind, "WORD_PULL") == 0) {
        receivePull(message, body);
    } else if (strcmp(message.kind, "WORD_OFFER") == 0) {
        receiveOffer(message, body);
    } else if (strcmp(message.kind, "WORD_ACCEPT") == 0) {
        receiveAccept(message, body);
    } else if (strcmp(message.kind, "WORD_DONE") == 0) {
        completeOffer(message, body);
    } else if (strcmp(message.kind, "ERROR") == 0) {
        char req[33], code[24];
        if (!body.getString("req", req, sizeof(req)) ||
            !body.getString("code", code, sizeof(code)) ||
            strcmp(message.dst, node_.identity().id) != 0 || !directMessage(message)) return;
        bool offer = pending_ && strcmp(req, pendingReq_) == 0 &&
                     strcmp(message.src, pendingDst_) == 0;
        bool pull = pullPending_ && strcmp(req, pullReq_) == 0 &&
                    strcmp(message.src, pullDst_) == 0;
        if (!offer && !pull) return;
        if (strcmp(code, "BUSY") == 0) {
            if (offer) nextOfferMs_ = now_ + 1000;
            else nextPullMs_ = now_ + 1000;
            setStatus(Status::SlotBusy);
        } else if (strcmp(code, "INVALID") == 0 || strcmp(code, "UNSUPPORTED") == 0) {
            if (offer) {
                pending_ = false;
                pendingReq_[0] = '\0';
                pendingPositioned_ = false;
                pendingPosition_ = 0;
            } else {
                clearPull();
                editMode_ = SentenceEdit::Navigate;
                insertAfter_ = false;
            }
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
    tryPull(now);
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

void WordGame::restoreReceipts(const ReceiptState* states, uint8_t receiptCount) {
    for (auto& receipt : receipts_) receipt = Receipt();
    nextReceiptOrder_ = 1;
    if (!states) return;
    if (receiptCount > 8) receiptCount = 8;
    for (uint8_t i = 0; i < receiptCount; ++i) {
        if (!restorableReceipt(states[i])) continue;
        Receipt& receipt = receipts_[i];
        receipt.used = true;
        receipt.completed = states[i].completed != 0;
        receipt.side = states[i].side == 0 ? Port::Left : Port::Right;
        receipt.positioned = states[i].positioned;
        receipt.position = states[i].position;
        receipt.order = states[i].order;
        copyValue(receipt.src, sizeof(receipt.src), states[i].src);
        copyValue(receipt.boot, sizeof(receipt.boot), states[i].boot);
        copyValue(receipt.req, sizeof(receipt.req), states[i].req);
        copyValue(receipt.text, sizeof(receipt.text), states[i].text);
        if (receipt.order >= nextReceiptOrder_) nextReceiptOrder_ = receipt.order + 1;
    }
}

void WordGame::restoreSentence(const Sentence& value, bool confirmed,
                               const ReceiptState* receipts, uint8_t receiptCount) {
    Sentence copy = value;
    confirmed_ = confirmed;
    pending_ = false;
    pendingReq_[0] = '\0';
    pendingPositioned_ = false;
    pendingPosition_ = 0;
    clearPull();
    donePending_ = false;
    doneReq_[0] = doneDst_[0] = '\0';
    sentence_.clear();
    for (uint8_t i = 0; i < copy.count(); ++i) {
        const char* source = copy.source(i);
        sentence_.insert(sentence_.count(), copy.text(i),
                         !source[0] || validHex(source, 16) ? source : "");
    }
    sentence_.setCursor(copy.cursor());
    restoreReceipts(receipts, receiptCount);
    view_ = hasSentence() ? View::Sentence : View::Picker;
    editMode_ = SentenceEdit::Navigate;
    insertAfter_ = false;
    status_ = Status::None;
    statusUntil_ = 0;
    saveRequested_ = false;
    changed();
}

void WordGame::restore(bool confirmed, const char* leftWord, const char* rightWord,
                       const char* leftSource, const char* rightSource,
                       const ReceiptState* receipts, uint8_t receiptCount) {
    Sentence restored;
    if (restorableText(leftWord)) restored.insert(restored.count(), leftWord, leftSource);
    if (restorableText(rightWord)) restored.insert(restored.count(), rightWord, rightSource);
    restored.setCursor(0);
    restoreSentence(restored, confirmed, receipts, receiptCount);
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
        out[count].positioned = receipt.positioned;
        out[count].position = receipt.position;
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
