#pragma once

#include "wlp2_json.h"

namespace wlp2 {

static const uint32_t HELLO_INTERVAL_MS = 300;
static const uint32_t PING_INTERVAL_MS = 1000;
static const uint32_t LINK_TIMEOUT_MS = 3000;
static const uint32_t ACK_TIMEOUT_MS = 500;
static const uint32_t BYTE_TIMEOUT_MS = 500;

static const uint32_t PROTOCOL_VERSION = 3;

using RandomFn = uint32_t (*)();
using IdChangedFn = void (*)(const char* id);

enum class Port : uint8_t { Left = 0, Right = 1, Any = 2 };
enum class LinkState : uint8_t { Down, Probing, Up };

struct Identity {
    char id[17];
    char boot[17];
    char kit[33];
    char fingerprint[17];
};

struct LinkInfo {
    LinkState state = LinkState::Down;
    char peerId[17] = {};
    char peerBoot[17] = {};
    Port peerPort = Port::Any;
    char sid[33] = {};

    bool up() const { return state == LinkState::Up; }
};

struct NetMessage {
    char src[17];
    char boot[17];
    uint32_t seq;
    char dst[17];
    uint8_t ttl;
    char kind[24];
    Port ingress;
};

class BytePort {
public:
    virtual ~BytePort() = default;
    virtual int read() = 0;
    virtual void write(const uint8_t* data, size_t len) = 0;
};

class Handler {
public:
    virtual ~Handler() = default;
    virtual void onLinkChanged(Port) {}
    virtual void onNet(const NetMessage&, const JsonReader&) {}
};

class Node {
public:
    Node(BytePort* left, BytePort* right, Handler& handler, RandomFn random,
         IdChangedFn idChanged = nullptr);

    bool begin(const Identity& identity, uint32_t now);
    void tick(uint32_t now);
    void setHandler(Handler& handler) { handler_ = &handler; }

    bool sendNet(Port port, const char* dst, const char* kind,
                 const char* bodyJson, uint32_t now);
    bool sendNet(const char* dst, const char* kind,
                 const char* bodyJson, uint32_t now);
    LinkInfo link(Port port) const;
    uint8_t connectedCount() const;
    uint32_t topoRev() const { return topoRev_; }
    const Identity& identity() const { return identity_; }
    bool configured() const { return configured_; }

private:
    struct Hello {
        char id[17];
        char boot[17];
        char nonce[17];
        char kit[33];
        char fingerprint[17];
        Port port;
    };

    struct PendingFrame {
        char data[FRAME_MAX + 1];
        uint16_t len;
        uint32_t tx;
        uint32_t lastSent;
        uint32_t notBefore;
        uint8_t retries;
    };

    enum class RxPhase : uint8_t { Idle, Json, Crc, Discard };

    struct PortState {
        BytePort* io;
        LinkState state;
        char nonce[17];
        char sid[33];
        Hello peer;
        bool peerSet;
        bool peerHello;
        bool ackValid;
        uint32_t lastHello;
        uint32_t lastPing;
        uint32_t lastActivity;
        uint32_t ping;
        uint32_t nextTx;
        uint32_t expectedTx;
        uint32_t lastTx;
        bool hasLastTx;
        PendingFrame queue[4];
        uint8_t queueCount;
        bool waitingAck;
        RxPhase rxPhase;
        char rxJson[JSON_MAX + 1];
        uint16_t rxLen;
        char rxCrc[9];
        uint8_t rxCrcLen;
        uint32_t lastRxByte;
    };

    struct Inbound {
        NetMessage message;
        char sid[33];
        char body[NET_MAX + 1];
        uint16_t bodyLen;
    };

    struct DedupEntry {
        char src[17];
        char boot[17];
        uint32_t seq;
        uint32_t seenAt;
        bool used;
    };

    static int index(Port port);
    static Port opposite(Port port);
    static const char* portName(Port port);

    void randomHex(char* out, size_t digits);
    void reassignId(uint32_t now);
    bool identityAllowed(const Hello& hello, Port localPort) const;
    bool parseHello(const JsonReader& json, Hello& hello, char* echo, size_t echoCap) const;
    void makeSid(PortState& port);
    void reset(Port which, uint32_t now, bool rotateNonce);
    void setUp(Port which, uint32_t now);
    void transmit(PortState& port);
    void emitHello(Port which);
    void emitHelloAck(Port which);
    void emitPing(Port which);
    void emitPong(Port which, uint32_t n);
    void emitLinkReply(Port which, const char* type, uint32_t tx, uint32_t retryMs = 0);
    void receiveByte(Port which, uint8_t byte, uint32_t now);
    void receiveFrame(Port which);
    void receiveNet(Port which, const JsonReader& frame, uint32_t now);
    bool duplicate(const NetMessage& message, uint32_t now) const;
    void remember(const NetMessage& message, uint32_t now);
    bool enqueueNet(Port which, const NetMessage& source, const char* body,
                    size_t bodyLen, uint32_t now);
    void removeInbound(Port which, const char* sid);
    void processInbound();
    void processPort(Port which, uint32_t now);
    void popFrame(PortState& port);

    BytePort* ports_[2];
    Handler* handler_;
    RandomFn random_;
    IdChangedFn idChanged_;
    Identity identity_ = {};
    bool configured_ = false;
    uint32_t topoRev_ = 0;
    uint32_t nextSeq_ = 1;
    PortState state_[2];
    Inbound inbound_[4];
    uint8_t inboundCount_ = 0;
    DedupEntry dedup_[64] = {};
    uint8_t dedupNext_ = 0;
    char json_[JSON_MAX + 1];
    char frame_[FRAME_MAX + 1];
};

} // namespace wlp2
