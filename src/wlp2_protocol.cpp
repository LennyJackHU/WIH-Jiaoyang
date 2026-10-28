#include "wlp2_protocol.h"

#include <stdio.h>
#include <string.h>

namespace wlp2 {
namespace {

void copyText(char* out, size_t capacity, const char* value) {
    snprintf(out, capacity, "%s", value ? value : "");
}

bool validKind(const char* kind) {
    size_t len = kind ? strlen(kind) : 0;
    if (len == 0 || len >= 24) return false;
    for (size_t i = 0; i < len; ++i) {
        if (!((kind[i] >= 'A' && kind[i] <= 'Z') || kind[i] == '_')) return false;
    }
    return true;
}

int crcDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool timeReached(uint32_t now, uint32_t then, uint32_t interval) {
    return static_cast<uint32_t>(now - then) >= interval;
}

} // namespace

Node::Node(BytePort* left, BytePort* right, Handler& handler, RandomFn random)
    : ports_{left, right}, handler_(&handler), random_(random) {
    memset(state_, 0, sizeof(state_));
    for (unsigned i = 0; i < 2; ++i) {
        state_[i].io = ports_[i];
        state_[i].state = LinkState::Down;
        state_[i].rxPhase = RxPhase::Idle;
        state_[i].nextTx = 1;
        state_[i].expectedTx = 1;
    }
}

int Node::index(Port port) {
    return port == Port::Left ? 0 : port == Port::Right ? 1 : -1;
}

Port Node::opposite(Port port) {
    return port == Port::Left ? Port::Right : Port::Left;
}

const char* Node::portName(Port port) {
    return port == Port::Left ? "L" : port == Port::Right ? "R" : "?";
}

bool Node::parseRole(const char* text, Role& role) {
    if (strcmp(text, "WORD") == 0) {
        role = Role::Word;
        return true;
    }
    if (strcmp(text, "RECORDER") == 0) {
        role = Role::Recorder;
        return true;
    }
    return false;
}

const char* Node::roleName(Role role) {
    return role == Role::Recorder ? "RECORDER" : "WORD";
}

void Node::randomHex(char* out, size_t digits) {
    static const char digits16[] = "0123456789abcdef";
    for (size_t i = 0; i < digits; ++i) {
        if ((i & 7u) == 0) {
            uint32_t value = random_ ? random_() : 0;
            for (size_t j = 0; j < 8 && i + j < digits; ++j) {
                out[i + j] = digits16[(value >> (j * 4)) & 0x0F];
            }
        }
    }
    out[digits] = '\0';
}

bool Node::begin(const Identity& identity, uint32_t now) {
    configured_ = false;
    if (!random_ || !validHex(identity.id, 16) || !validHex(identity.boot, 16) ||
        !validHex(identity.kit, 32) || !validHex(identity.recorder, 16) ||
        identity.memberCount == 0 || identity.memberCount > 3 ||
        ((identity.role == Role::Recorder) != (strcmp(identity.id, identity.recorder) == 0))) {
        return false;
    }
    identity_ = identity;
    bool selfListed = false;
    bool recorderListed = false;
    for (unsigned i = 0; i < identity.memberCount; ++i) {
        if (!validHex(identity.members[i], 16)) return false;
        copyText(members_[i], sizeof(members_[i]), identity.members[i]);
        for (unsigned j = 0; j < i; ++j) {
            if (strcmp(members_[i], members_[j]) == 0) return false;
        }
        selfListed |= strcmp(identity.id, members_[i]) == 0;
        recorderListed |= strcmp(identity.recorder, members_[i]) == 0;
    }
    if (!selfListed || !recorderListed) return false;
    identity_.members[0] = members_[0];
    identity_.members[1] = members_[1];
    identity_.members[2] = members_[2];
    topoRev_ = 0;
    nextSeq_ = 1;
    inboundCount_ = 0;
    memset(dedup_, 0, sizeof(dedup_));
    dedupNext_ = 0;
    for (unsigned i = 0; i < 2; ++i) {
        PortState& port = state_[i];
        port.queueCount = 0;
        port.waitingAck = false;
        port.peerSet = false;
        port.peerHello = false;
        port.ackValid = false;
        port.state = LinkState::Down;
        port.rxPhase = RxPhase::Idle;
        randomHex(port.nonce, 16);
        reset(static_cast<Port>(i), now, false);
    }
    configured_ = true;
    emitHello(Port::Left);
    emitHello(Port::Right);
    return true;
}

bool Node::parseHello(const JsonReader& json, Hello& hello, char* echo, size_t echoCap) const {
    char port[2];
    char role[10];
    uint32_t version;
    if (!json.getUInt("v", version) || version != 2 ||
        !json.getString("id", hello.id, sizeof(hello.id)) ||
        !json.getString("boot", hello.boot, sizeof(hello.boot)) ||
        !json.getString("nonce", hello.nonce, sizeof(hello.nonce)) ||
        !json.getString("kit", hello.kit, sizeof(hello.kit)) ||
        !json.getString("recorder", hello.recorder, sizeof(hello.recorder)) ||
        !json.getString("port", port, sizeof(port)) ||
        !json.getString("role", role, sizeof(role)) || !parseRole(role, hello.role)) return false;
    if (strcmp(port, "L") == 0) hello.port = Port::Left;
    else if (strcmp(port, "R") == 0) hello.port = Port::Right;
    else return false;
    if (echoCap) {
        echo[0] = '\0';
        if (json.contains("echo") && !json.getString("echo", echo, echoCap)) return false;
    }
    return validHex(hello.id, 16) && validHex(hello.boot, 16) &&
           validHex(hello.nonce, 16) && validHex(hello.kit, 32) &&
           validHex(hello.recorder, 16);
}

bool Node::identityAllowed(const Hello& hello, Port localPort) const {
    if (!configured_ || strcmp(hello.id, identity_.id) == 0 ||
        strcmp(hello.kit, identity_.kit) != 0 ||
        strcmp(hello.recorder, identity_.recorder) != 0 ||
        hello.port != opposite(localPort) ||
        ((hello.role == Role::Recorder) != (strcmp(hello.id, identity_.recorder) == 0))) {
        return false;
    }
    for (unsigned i = 0; i < identity_.memberCount; ++i) {
        if (strcmp(hello.id, members_[i]) == 0) return true;
    }
    return false;
}

void Node::makeSid(PortState& port) {
    if (strcmp(identity_.id, port.peer.id) < 0) {
        snprintf(port.sid, sizeof(port.sid), "%s%s", port.nonce, port.peer.nonce);
    } else {
        snprintf(port.sid, sizeof(port.sid), "%s%s", port.peer.nonce, port.nonce);
    }
}

void Node::removeInbound(Port which, const char* sid) {
    uint8_t out = 0;
    for (uint8_t i = 0; i < inboundCount_; ++i) {
        if (inbound_[i].message.ingress == which && strcmp(inbound_[i].sid, sid) == 0) continue;
        if (out != i) inbound_[out] = inbound_[i];
        ++out;
    }
    inboundCount_ = out;
}

void Node::reset(Port which, uint32_t now, bool rotateNonce) {
    int i = index(which);
    if (i < 0) return;
    PortState& port = state_[i];
    bool wasUp = port.state == LinkState::Up;
    char oldSid[33];
    copyText(oldSid, sizeof(oldSid), port.sid);
    if (rotateNonce) randomHex(port.nonce, 16);
    port.state = LinkState::Probing;
    port.peerSet = false;
    port.peerHello = false;
    port.ackValid = false;
    port.sid[0] = '\0';
    port.lastHello = now - HELLO_INTERVAL_MS;
    port.lastPing = now;
    port.lastActivity = now;
    port.ping = 0;
    port.nextTx = 1;
    port.expectedTx = 1;
    port.lastTx = 0;
    port.hasLastTx = false;
    port.queueCount = 0;
    port.waitingAck = false;
    port.rxPhase = RxPhase::Idle;
    port.rxLen = 0;
    port.rxCrcLen = 0;
    port.lastRxByte = now;
    if (oldSid[0]) removeInbound(which, oldSid);
    if (wasUp) {
        ++topoRev_;
        handler_->onLinkChanged(which);
    }
}

void Node::setUp(Port which, uint32_t now) {
    PortState& port = state_[index(which)];
    if (port.state == LinkState::Up) return;
    port.state = LinkState::Up;
    makeSid(port);
    port.lastActivity = now;
    port.lastPing = now - PING_INTERVAL_MS;
    port.ping = 0;
    port.nextTx = 1;
    port.expectedTx = 1;
    port.lastTx = 0;
    port.hasLastTx = false;
    port.queueCount = 0;
    port.waitingAck = false;
    ++topoRev_;
    handler_->onLinkChanged(which);
}

void Node::transmit(PortState& port) {
    if (!port.io) return;
    size_t len;
    if (encodeFrame(json_, strlen(json_), frame_, sizeof(frame_), len)) {
        port.io->write(reinterpret_cast<const uint8_t*>(frame_), len);
    }
}

void Node::emitHello(Port which) {
    PortState& port = state_[index(which)];
    if (!port.io) return;
    JsonWriter out(json_, sizeof(json_));
    out.beginObject();
    out.fieldUInt("v", 2);
    out.fieldString("t", "HELLO");
    out.fieldString("id", identity_.id);
    out.fieldString("boot", identity_.boot);
    out.fieldString("port", portName(which));
    out.fieldString("nonce", port.nonce);
    out.fieldString("kit", identity_.kit);
    out.fieldString("recorder", identity_.recorder);
    out.fieldString("role", roleName(identity_.role));
    out.endObject();
    if (out.finish()) transmit(port);
}

void Node::emitHelloAck(Port which) {
    PortState& port = state_[index(which)];
    if (!port.io || !port.peerSet) return;
    JsonWriter out(json_, sizeof(json_));
    out.beginObject();
    out.fieldUInt("v", 2);
    out.fieldString("t", "HELLO_ACK");
    out.fieldString("id", identity_.id);
    out.fieldString("boot", identity_.boot);
    out.fieldString("port", portName(which));
    out.fieldString("nonce", port.nonce);
    out.fieldString("kit", identity_.kit);
    out.fieldString("recorder", identity_.recorder);
    out.fieldString("role", roleName(identity_.role));
    out.fieldString("echo", port.peer.nonce);
    out.endObject();
    if (out.finish()) transmit(port);
}

void Node::emitPing(Port which) {
    PortState& port = state_[index(which)];
    ++port.ping;
    if (port.ping == 0) port.ping = 1;
    JsonWriter out(json_, sizeof(json_));
    out.beginObject();
    out.fieldUInt("v", 2);
    out.fieldString("t", "PING");
    out.fieldString("sid", port.sid);
    out.fieldUInt("n", port.ping);
    out.endObject();
    if (out.finish()) transmit(port);
}

void Node::emitPong(Port which, uint32_t n) {
    PortState& port = state_[index(which)];
    JsonWriter out(json_, sizeof(json_));
    out.beginObject();
    out.fieldUInt("v", 2);
    out.fieldString("t", "PONG");
    out.fieldString("sid", port.sid);
    out.fieldUInt("n", n);
    out.endObject();
    if (out.finish()) transmit(port);
}

void Node::emitLinkReply(Port which, const char* type, uint32_t tx, uint32_t retryMs) {
    PortState& port = state_[index(which)];
    JsonWriter out(json_, sizeof(json_));
    out.beginObject();
    out.fieldUInt("v", 2);
    out.fieldString("t", type);
    out.fieldString("sid", port.sid);
    out.fieldUInt("tx", tx);
    if (retryMs) out.fieldUInt("retry_ms", retryMs);
    out.endObject();
    if (out.finish()) transmit(port);
}

LinkInfo Node::link(Port which) const {
    LinkInfo info;
    int i = index(which);
    if (i < 0) return info;
    const PortState& port = state_[i];
    info.state = port.state;
    info.peerPort = port.peer.port;
    if (port.state == LinkState::Up) {
        copyText(info.peerId, sizeof(info.peerId), port.peer.id);
        copyText(info.peerBoot, sizeof(info.peerBoot), port.peer.boot);
        copyText(info.sid, sizeof(info.sid), port.sid);
    }
    return info;
}

uint8_t Node::connectedCount() const {
    return (state_[0].state == LinkState::Up ? 1 : 0) +
           (state_[1].state == LinkState::Up ? 1 : 0);
}

void Node::receiveByte(Port which, uint8_t byte, uint32_t now) {
    PortState& port = state_[index(which)];
    if (port.rxPhase != RxPhase::Idle && timeReached(now, port.lastRxByte, BYTE_TIMEOUT_MS)) {
        port.rxPhase = RxPhase::Idle;
        port.rxLen = 0;
        port.rxCrcLen = 0;
    }
    port.lastRxByte = now;
    if (port.rxPhase == RxPhase::Crc && byte == '@') {
        port.rxPhase = RxPhase::Json;
        port.rxLen = 0;
        port.rxCrcLen = 0;
        return;
    }
    if (port.rxPhase == RxPhase::Discard) {
        if (byte == '\n') port.rxPhase = RxPhase::Idle;
        return;
    }
    if (port.rxPhase == RxPhase::Idle) {
        if (byte == '@') {
            port.rxPhase = RxPhase::Json;
            port.rxLen = 0;
            port.rxCrcLen = 0;
        }
        return;
    }
    if (port.rxPhase == RxPhase::Json) {
        if (byte == '\t') {
            port.rxJson[port.rxLen] = '\0';
            port.rxPhase = RxPhase::Crc;
            port.rxCrcLen = 0;
        } else if (byte == '\r' || byte == '\n') {
            port.rxPhase = RxPhase::Idle;
            port.rxLen = 0;
        } else if (port.rxLen >= JSON_MAX) {
            port.rxPhase = RxPhase::Discard;
            port.rxLen = 0;
        } else {
            port.rxJson[port.rxLen++] = static_cast<char>(byte);
        }
        return;
    }
    if (port.rxCrcLen < 8) {
        if (crcDigit(static_cast<char>(byte)) < 0) {
            port.rxPhase = RxPhase::Discard;
            return;
        }
        port.rxCrc[port.rxCrcLen++] = static_cast<char>(byte);
        if (port.rxCrcLen == 8) port.rxCrc[8] = '\0';
        return;
    }
    port.rxPhase = RxPhase::Idle;
    if (byte != '\n') return;
    receiveFrame(which);
}

void Node::receiveFrame(Port which) {
    PortState& port = state_[index(which)];
    uint32_t expected = 0;
    for (unsigned i = 0; i < 8; ++i) expected = (expected << 4) | crcDigit(port.rxCrc[i]);
    if (crc32(port.rxJson, port.rxLen) != expected) return;
    JsonReader json(port.rxJson, port.rxLen);
    char type[16];
    uint32_t version;
    if (!json.valid() || !json.getUInt("v", version) || version != 2 ||
        !json.getString("t", type, sizeof(type))) return;
    if (strcmp(type, "HELLO") == 0 || strcmp(type, "HELLO_ACK") == 0) {
        Hello hello;
        char echo[17];
        if (!parseHello(json, hello, echo, sizeof(echo)) || !identityAllowed(hello, which)) return;
        bool changed = !port.peerSet || strcmp(port.peer.id, hello.id) != 0 ||
                       strcmp(port.peer.boot, hello.boot) != 0 ||
                       strcmp(port.peer.nonce, hello.nonce) != 0 ||
                       strcmp(port.peer.kit, hello.kit) != 0 ||
                       strcmp(port.peer.recorder, hello.recorder) != 0 ||
                       port.peer.port != hello.port || port.peer.role != hello.role;
        if (changed) {
            reset(which, port.lastRxByte, false);
            port.peer = hello;
            port.peerSet = true;
        }
        if (strcmp(type, "HELLO") == 0) {
            port.peerHello = true;
            emitHelloAck(which);
        } else if (port.peerHello && validHex(echo, 16) && strcmp(echo, port.nonce) == 0) {
            port.ackValid = true;
            setUp(which, port.lastRxByte);
        }
        return;
    }
    if (port.state != LinkState::Up) return;
    char sid[33];
    if (!json.getString("sid", sid, sizeof(sid)) || strcmp(sid, port.sid) != 0) return;
    if (strcmp(type, "PING") == 0) {
        uint32_t n;
        if (json.getUInt("n", n)) emitPong(which, n);
    } else if (strcmp(type, "PONG") == 0) {
        uint32_t n;
        if (json.getUInt("n", n) && n == port.ping && n != 0) port.lastActivity = port.lastRxByte;
    } else if (strcmp(type, "LACK") == 0 || strcmp(type, "LBUSY") == 0) {
        uint32_t tx;
        if (!json.getUInt("tx", tx) || !port.waitingAck || port.queueCount == 0 ||
            tx != port.queue[0].tx) return;
        if (strcmp(type, "LACK") == 0) {
            popFrame(port);
        } else {
            uint32_t retry = 250;
            if (!json.getUInt("retry_ms", retry)) return;
            if (retry < 50) retry = 50;
            if (retry > 1000) retry = 1000;
            port.queue[0].notBefore = port.lastRxByte + retry;
        }
    } else if (strcmp(type, "NET") == 0) {
        receiveNet(which, json, port.lastRxByte);
    }
}

bool Node::duplicate(const NetMessage& message, uint32_t now) const {
    for (unsigned i = 0; i < 64; ++i) {
        const DedupEntry& entry = dedup_[i];
        if (entry.used && !timeReached(now, entry.seenAt, 10000) &&
            entry.seq == message.seq && strcmp(entry.src, message.src) == 0 &&
            strcmp(entry.boot, message.boot) == 0) return true;
    }
    return false;
}

void Node::remember(const NetMessage& message, uint32_t now) {
    DedupEntry& entry = dedup_[dedupNext_];
    dedupNext_ = (dedupNext_ + 1) % 64;
    copyText(entry.src, sizeof(entry.src), message.src);
    copyText(entry.boot, sizeof(entry.boot), message.boot);
    entry.seq = message.seq;
    entry.seenAt = now;
    entry.used = true;
}

void Node::receiveNet(Port which, const JsonReader& frame, uint32_t now) {
    PortState& port = state_[index(which)];
    uint32_t tx;
    JsonReader net;
    if (!frame.getUInt("tx", tx) || tx == 0 || !frame.getObject("net", net) || net.size() > NET_MAX) return;
    NetMessage message = {};
    uint32_t seq, ttl;
    JsonReader body;
    if (!net.getString("src", message.src, sizeof(message.src)) ||
        !net.getString("boot", message.boot, sizeof(message.boot)) ||
        !net.getUInt("seq", seq) || seq == 0 ||
        !net.getString("dst", message.dst, sizeof(message.dst)) ||
        !net.getUInt("ttl", ttl) || ttl < 1 || ttl > 3 ||
        !net.getString("kind", message.kind, sizeof(message.kind)) ||
        !net.getObject("body", body) || !validHex(message.src, 16) ||
        !validHex(message.boot, 16) || !validKind(message.kind) ||
        (strcmp(message.dst, "*") != 0 && !validHex(message.dst, 16))) return;
    bool sourceListed = false;
    for (unsigned i = 0; i < identity_.memberCount; ++i) {
        sourceListed |= strcmp(message.src, members_[i]) == 0;
    }
    if (!sourceListed || strcmp(message.src, identity_.id) == 0) return;
    if (port.hasLastTx && tx == port.lastTx) {
        emitLinkReply(which, "LACK", tx);
        return;
    }
    if (tx != port.expectedTx) {
        emitLinkReply(which, "LBUSY", tx, 250);
        return;
    }
    message.seq = seq;
    message.ttl = static_cast<uint8_t>(ttl);
    message.ingress = which;
    if (duplicate(message, now)) {
        emitLinkReply(which, "LACK", tx);
        port.lastTx = tx;
        port.hasLastTx = true;
        ++port.expectedTx;
        if (port.expectedTx == 0) reset(which, now, true);
        return;
    }
    bool local = strcmp(message.dst, "*") == 0 || strcmp(message.dst, identity_.id) == 0;
    bool forward = ttl > 1 && (!local || strcmp(message.dst, "*") == 0);
    Port other = opposite(which);
    forward = forward && state_[index(other)].state == LinkState::Up;
    if ((local && inboundCount_ >= 4) || (forward && state_[index(other)].queueCount >= 4)) {
        emitLinkReply(which, "LBUSY", tx, 250);
        return;
    }
    NetMessage forwarded = message;
    --forwarded.ttl;
    if (forward && !enqueueNet(other, forwarded, body.data(), body.size(), now)) {
        emitLinkReply(which, "LBUSY", tx, 250);
        return;
    }
    if (local) {
        Inbound& inbound = inbound_[inboundCount_++];
        inbound.message = message;
        copyText(inbound.sid, sizeof(inbound.sid), port.sid);
        memcpy(inbound.body, body.data(), body.size());
        inbound.body[body.size()] = '\0';
        inbound.bodyLen = static_cast<uint16_t>(body.size());
    }
    remember(message, now);
    emitLinkReply(which, "LACK", tx);
    port.lastTx = tx;
    port.hasLastTx = true;
    ++port.expectedTx;
    if (port.expectedTx == 0) reset(which, now, true);
}

bool Node::enqueueNet(Port which, const NetMessage& source, const char* body,
                      size_t bodyLen, uint32_t now) {
    int i = index(which);
    if (i < 0 || !body || bodyLen > NET_MAX || !validKind(source.kind) ||
        source.ttl < 1 || source.ttl > 3) return false;
    PortState& port = state_[i];
    if (port.state != LinkState::Up || port.queueCount >= 4) return false;
    if (port.nextTx == 0) {
        reset(which, now, true);
        return false;
    }
    JsonReader bodyCheck(body, bodyLen);
    if (!bodyCheck.valid()) return false;
    uint32_t tx = port.nextTx;
    JsonWriter out(json_, sizeof(json_));
    out.beginObject();
    out.fieldUInt("v", 2);
    out.fieldString("t", "NET");
    out.fieldString("sid", port.sid);
    out.fieldUInt("tx", tx);
    out.beginObject("net");
    out.fieldString("src", source.src);
    out.fieldString("boot", source.boot);
    out.fieldUInt("seq", source.seq);
    out.fieldString("dst", source.dst);
    out.fieldUInt("ttl", source.ttl);
    out.fieldString("kind", source.kind);
    out.fieldRaw("body", body, bodyLen);
    out.endObject();
    out.endObject();
    if (!out.finish()) return false;
    JsonReader complete(json_, out.size());
    JsonReader net;
    if (!complete.getObject("net", net) || net.size() > NET_MAX) return false;
    PendingFrame& pending = port.queue[port.queueCount];
    size_t frameLen;
    if (!encodeFrame(json_, out.size(), pending.data, sizeof(pending.data), frameLen)) return false;
    pending.len = static_cast<uint16_t>(frameLen);
    pending.tx = tx;
    pending.lastSent = now;
    pending.notBefore = now;
    pending.retries = 0;
    ++port.nextTx;
    ++port.queueCount;
    return true;
}

bool Node::sendNet(Port which, const char* dst, const char* kind,
                   const char* bodyJson, uint32_t now) {
    if (!bodyJson || !dst || (strcmp(dst, "*") != 0 && !validHex(dst, 16)) ||
        !validKind(kind)) return false;
    if (which == Port::Any) return sendNet(dst, kind, bodyJson, now);
    if (index(which) < 0) return false;
    if (nextSeq_ == 0) {
        randomHex(identity_.boot, 16);
        nextSeq_ = 1;
        reset(Port::Left, now, true);
        reset(Port::Right, now, true);
        return false;
    }
    NetMessage message = {};
    copyText(message.src, sizeof(message.src), identity_.id);
    copyText(message.boot, sizeof(message.boot), identity_.boot);
    copyText(message.dst, sizeof(message.dst), dst);
    copyText(message.kind, sizeof(message.kind), kind);
    message.seq = nextSeq_++;
    message.ttl = 3;
    message.ingress = Port::Any;
    return enqueueNet(which, message, bodyJson, strlen(bodyJson), now);
}

bool Node::sendNet(const char* dst, const char* kind,
                   const char* bodyJson, uint32_t now) {
    if (!bodyJson || !dst || (strcmp(dst, "*") != 0 && !validHex(dst, 16)) ||
        !validKind(kind)) return false;
    int direct = -1;
    bool any = false;
    for (unsigned i = 0; i < 2; ++i) {
        if (state_[i].state != LinkState::Up) continue;
        if (strcmp(state_[i].peer.id, dst) == 0) direct = i;
        if (direct < 0 || direct == static_cast<int>(i)) any = true;
    }
    if (!any) return false;
    if (nextSeq_ == 0) {
        randomHex(identity_.boot, 16);
        nextSeq_ = 1;
        reset(Port::Left, now, true);
        reset(Port::Right, now, true);
        return false;
    }
    NetMessage message = {};
    copyText(message.src, sizeof(message.src), identity_.id);
    copyText(message.boot, sizeof(message.boot), identity_.boot);
    copyText(message.dst, sizeof(message.dst), dst);
    copyText(message.kind, sizeof(message.kind), kind);
    message.seq = nextSeq_++;
    message.ttl = 3;
    message.ingress = Port::Any;
    bool sent = false;
    for (unsigned i = 0; i < 2; ++i) {
        if (state_[i].state != LinkState::Up || (direct >= 0 && direct != static_cast<int>(i))) continue;
        sent = enqueueNet(static_cast<Port>(i), message, bodyJson, strlen(bodyJson), now) || sent;
    }
    return sent;
}

void Node::popFrame(PortState& port) {
    if (port.queueCount == 0) return;
    --port.queueCount;
    for (unsigned i = 0; i < port.queueCount; ++i) port.queue[i] = port.queue[i + 1];
    port.waitingAck = false;
}

void Node::processInbound() {
    while (inboundCount_) {
        Inbound inbound = inbound_[0];
        --inboundCount_;
        for (unsigned i = 0; i < inboundCount_; ++i) inbound_[i] = inbound_[i + 1];
        JsonReader body(inbound.body, inbound.bodyLen);
        handler_->onNet(inbound.message, body);
    }
}

void Node::processPort(Port which, uint32_t now) {
    int i = index(which);
    PortState& port = state_[i];
    if (!port.io) return;
    if (port.rxPhase != RxPhase::Idle && timeReached(now, port.lastRxByte, BYTE_TIMEOUT_MS)) {
        port.rxPhase = RxPhase::Idle;
        port.rxLen = 0;
        port.rxCrcLen = 0;
    }
    unsigned budget = 8192;
    while (budget--) {
        int value = port.io->read();
        if (value < 0) break;
        receiveByte(which, static_cast<uint8_t>(value), now);
    }
    if (port.state == LinkState::Up && timeReached(now, port.lastActivity, LINK_TIMEOUT_MS)) {
        reset(which, now, true);
    }
    if (timeReached(now, port.lastHello, HELLO_INTERVAL_MS)) {
        port.lastHello = now;
        emitHello(which);
    }
    if (port.state == LinkState::Up && timeReached(now, port.lastPing, PING_INTERVAL_MS)) {
        port.lastPing = now;
        emitPing(which);
    }
    if (port.state != LinkState::Up || port.queueCount == 0) return;
    PendingFrame& pending = port.queue[0];
    if (!port.waitingAck) {
        port.io->write(reinterpret_cast<const uint8_t*>(pending.data), pending.len);
        port.waitingAck = true;
        pending.lastSent = now;
        pending.retries = 0;
        return;
    }
    uint32_t interval = pending.retries < 3 ? ACK_TIMEOUT_MS : 1000;
    if (timeReached(now, pending.lastSent, interval) &&
        static_cast<int32_t>(now - pending.notBefore) >= 0) {
        port.io->write(reinterpret_cast<const uint8_t*>(pending.data), pending.len);
        pending.lastSent = now;
        if (pending.retries < 3) ++pending.retries;
    }
}

void Node::tick(uint32_t now) {
    if (!configured_) return;
    processPort(Port::Left, now);
    processPort(Port::Right, now);
    processInbound();
}

} // namespace wlp2
