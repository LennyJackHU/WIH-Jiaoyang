#include <unity.h>

#include <deque>
#include <string>
#include <vector>
#include <string.h>

#include "wlp2_protocol.h"

using namespace wlp2;

namespace {

uint32_t rngA = 0x10203040;
uint32_t rngB = 0x50607080;
uint32_t rngC = 0x90abcdef;

uint32_t next(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
uint32_t randomA() { return next(rngA); }
uint32_t randomB() { return next(rngB); }
uint32_t randomC() { return next(rngC); }

class TestPort : public BytePort {
public:
    TestPort* peer = nullptr;
    bool online = true;
    bool txOnline = true;
    bool dropNextLack = false;
    bool corruptNextNet = false;
    bool duplicateNextNet = false;
    bool captureNextNet = false;
    bool captureNextPong = false;
    std::vector<uint8_t> captured;
    std::deque<uint8_t> incoming;

    int read() override {
        if (incoming.empty()) return -1;
        uint8_t value = incoming.front();
        incoming.pop_front();
        return value;
    }

    void write(const uint8_t* data, size_t len) override {
        if (!online || !txOnline || !peer || !peer->online) return;
        std::string frame(reinterpret_cast<const char*>(data), len);
        bool net = frame.find("\"t\":\"NET\"") != std::string::npos;
        bool pong = frame.find("\"t\":\"PONG\"") != std::string::npos;
        bool lack = frame.find("\"t\":\"LACK\"") != std::string::npos;
        if (pong && captureNextPong) {
            captureNextPong = false;
            captured.assign(data, data + len);
            return;
        }
        if (lack && dropNextLack) {
            dropNextLack = false;
            return;
        }
        if (net && captureNextNet) {
            captureNextNet = false;
            captured.assign(data, data + len);
            return;
        }
        std::vector<uint8_t> output(data, data + len);
        if (net && corruptNextNet) {
            corruptNextNet = false;
            output[1] ^= 1;
        }
        peer->incoming.insert(peer->incoming.end(), output.begin(), output.end());
        if (net && duplicateNextNet) {
            duplicateNextNet = false;
            peer->incoming.insert(peer->incoming.end(), output.begin(), output.end());
        }
    }
};

class Recorder : public Handler {
public:
    std::vector<NetMessage> messages;
    std::vector<std::string> bodies;

    void onNet(const NetMessage& message, const JsonReader& body) override {
        messages.push_back(message);
        bodies.emplace_back(body.data(), body.size());
    }
};

struct Device {
    TestPort left;
    TestPort right;
    Recorder recorder;
    Node node;
    explicit Device(RandomFn random) : node(&left, &right, recorder, random) {}
};

Identity identity(const char* id, const char* boot, Role role = Role::Word) {
    static const char* members[] = {
        "0000000000000001", "0000000000000002", "0000000000000003"
    };
    Identity value = {};
    snprintf(value.id, sizeof(value.id), "%s", id);
    snprintf(value.boot, sizeof(value.boot), "%s", boot);
    snprintf(value.kit, sizeof(value.kit), "%s", "0123456789abcdef0123456789abcdef");
    snprintf(value.recorder, sizeof(value.recorder), "%s", "0000000000000003");
    value.role = role;
    for (unsigned i = 0; i < 3; ++i) value.members[i] = members[i];
    value.memberCount = 3;
    return value;
}

Identity identityFor(unsigned number) {
    if (number == 1) return identity("0000000000000001", "00000000000000a1");
    if (number == 2) return identity("0000000000000002", "00000000000000b1");
    return identity("0000000000000003", "00000000000000c1", Role::Recorder);
}

void connect(Device& a, Device& b) {
    a.right.peer = &b.left;
    b.left.peer = &a.right;
    a.right.online = b.left.online = true;
    a.right.txOnline = b.left.txOnline = true;
}

void connectRight(Device& a, Device& b) {
    a.right.peer = &b.left;
    b.left.peer = &a.right;
    a.right.online = b.left.online = true;
    a.right.txOnline = b.left.txOnline = true;
}

void disconnect(Device& a, Device& b) {
    a.right.online = b.left.online = false;
}

void run(Device& a, Device& b, uint32_t& now, uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 5) {
        now += 5;
        a.node.tick(now);
        b.node.tick(now);
    }
}

void run(Device& a, Device& b, Device& c, uint32_t& now, uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 5) {
        now += 5;
        a.node.tick(now);
        b.node.tick(now);
        c.node.tick(now);
    }
}

void beginPair(Device& a, Device& b, uint32_t& now) {
    Identity ai = identityFor(1);
    Identity bi = identityFor(2);
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
}

void test_repeated_hello_and_frame_recovery() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 900);
    char sid[33];
    snprintf(sid, sizeof(sid), "%s", a.node.link(Port::Right).sid);
    uint32_t topo = a.node.topoRev();
    run(a, b, now, 700);
    TEST_ASSERT_EQUAL_STRING(sid, a.node.link(Port::Right).sid);
    TEST_ASSERT_EQUAL_UINT32(topo, a.node.topoRev());

    const char* body = "{\"req\":\"123456789abcdef0123456789abcdef0\"}";
    a.right.corruptNextNet = true;
    TEST_ASSERT_TRUE(a.node.sendNet("0000000000000002", "NODE_GET", body, now));
    run(a, b, now, 1200);
    TEST_ASSERT_EQUAL(1, b.recorder.messages.size());

    b.left.dropNextLack = true;
    TEST_ASSERT_TRUE(a.node.sendNet("0000000000000002", "NODE_GET", body, now));
    run(a, b, now, 1200);
    TEST_ASSERT_EQUAL(2, b.recorder.messages.size());

    a.right.duplicateNextNet = true;
    TEST_ASSERT_TRUE(a.node.sendNet("0000000000000002", "NODE_GET", body, now));
    run(a, b, now, 700);
    TEST_ASSERT_EQUAL(3, b.recorder.messages.size());
}

void test_failed_send_preserves_next_tx() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 900);
    std::string oversized = "{\"text\":\"" + std::string(1900, 'x') + "\"}";
    TEST_ASSERT_FALSE(a.node.sendNet("0000000000000002", "NODE_GET", oversized.c_str(), now));
    const char* body = "{\"req\":\"123456789abcdef0123456789abcdef0\"}";
    TEST_ASSERT_TRUE(a.node.sendNet("0000000000000002", "NODE_GET", body, now));
    run(a, b, now, 150);
    TEST_ASSERT_EQUAL(1, b.recorder.messages.size());
    TEST_ASSERT_EQUAL_STRING(body, b.recorder.bodies[0].c_str());
}

void test_one_way_failure_and_stale_session() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 900);
    a.right.captureNextNet = true;
    const char* body = "{\"req\":\"123456789abcdef0123456789abcdef0\"}";
    TEST_ASSERT_TRUE(a.node.sendNet("0000000000000002", "NODE_GET", body, now));
    run(a, b, now, 100);
    TEST_ASSERT_FALSE(a.right.captured.empty());
    TEST_ASSERT_EQUAL(0, b.recorder.messages.size());
    a.right.txOnline = false;
    run(a, b, now, 3300);
    TEST_ASSERT_FALSE(a.node.link(Port::Right).up());
    run(a, b, now, 3300);
    TEST_ASSERT_FALSE(b.node.link(Port::Left).up());
    a.right.txOnline = true;
    run(a, b, now, 1000);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    b.left.incoming.insert(b.left.incoming.end(), a.right.captured.begin(), a.right.captured.end());
    b.node.tick(now);
    TEST_ASSERT_EQUAL(0, b.recorder.messages.size());
}

void test_three_node_forward_and_reorder() {
    Device a(randomA);
    Device b(randomB);
    Device c(randomC);
    connect(a, b);
    connectRight(b, c);
    uint32_t now = 0;
    Identity ai = identityFor(1), bi = identityFor(2), ci = identityFor(3);
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    TEST_ASSERT_TRUE(c.node.begin(ci, now));
    run(a, b, c, now, 1400);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(c.node.link(Port::Left).up());
    TEST_ASSERT_EQUAL_STRING(a.node.link(Port::Right).sid, b.node.link(Port::Left).sid);
    TEST_ASSERT_EQUAL_STRING(b.node.link(Port::Right).sid, c.node.link(Port::Left).sid);

    const char* body = "{\"req\":\"123456789abcdef0123456789abcdef0\"}";
    TEST_ASSERT_TRUE(a.node.sendNet(ci.id, "NODE_GET", body, now));
    run(a, b, c, now, 500);
    TEST_ASSERT_EQUAL(1, c.recorder.messages.size());
    TEST_ASSERT_EQUAL(0, b.recorder.messages.size());
    TEST_ASSERT_EQUAL_STRING(ai.id, c.recorder.messages[0].src);
    TEST_ASSERT_EQUAL_UINT8(2, c.recorder.messages[0].ttl);
    TEST_ASSERT_TRUE(c.node.sendNet(ai.id, "NODE_INFO", body, now));
    run(a, b, c, now, 500);
    TEST_ASSERT_EQUAL(1, a.recorder.messages.size());

    disconnect(b, c);
    disconnect(a, b);
    run(a, b, c, now, 3500);
    connect(a, c);
    connectRight(c, b);
    run(a, b, c, now, 1200);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(c.node.link(Port::Left).up());
    TEST_ASSERT_TRUE(c.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    TEST_ASSERT_TRUE(a.node.sendNet(bi.id, "NODE_GET", body, now));
    run(a, b, c, now, 500);
    TEST_ASSERT_EQUAL(1, b.recorder.messages.size());
    TEST_ASSERT_EQUAL_STRING(ai.id, b.recorder.messages[0].src);
}

void test_buffered_pong_prevents_timeout_reset() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 900);
    char sid[33];
    snprintf(sid, sizeof(sid), "%s", a.node.link(Port::Right).sid);
    b.left.captureNextPong = true;
    run(a, b, now, 200);
    TEST_ASSERT_FALSE(b.left.captured.empty());
    now += 3000;
    a.right.incoming.insert(a.right.incoming.end(), b.left.captured.begin(), b.left.captured.end());
    a.node.tick(now);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_EQUAL_STRING(sid, a.node.link(Port::Right).sid);
}

void test_broadcast_shares_seq_across_paths() {
    Device a(randomA);
    Device b(randomB);
    Device c(randomC);
    connect(a, b);
    connectRight(b, c);
    connectRight(c, a);
    uint32_t now = 0;
    Identity ai = identityFor(1), bi = identityFor(2), ci = identityFor(3);
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    TEST_ASSERT_TRUE(c.node.begin(ci, now));
    run(a, b, c, now, 1400);
    const char* body = "{\"rev\":1,\"text\":\"alpha\"}";
    TEST_ASSERT_TRUE(a.node.sendNet("*", "STATE_EVT", body, now));
    run(a, b, c, now, 600);
    TEST_ASSERT_EQUAL(0, a.recorder.messages.size());
    TEST_ASSERT_EQUAL(1, b.recorder.messages.size());
    TEST_ASSERT_EQUAL(1, c.recorder.messages.size());
    TEST_ASSERT_EQUAL_STRING(body, b.recorder.bodies[0].c_str());
    TEST_ASSERT_EQUAL_STRING(body, c.recorder.bodies[0].c_str());
}

std::vector<uint8_t> craftedNet(const Identity& source, const Identity& target,
                                const char* sid, uint32_t tx, uint32_t seq) {
    char json[1024];
    char req[33];
    snprintf(req, sizeof(req), "123456789abcdef0123456789abcdef0");
    JsonWriter out(json, sizeof(json));
    out.beginObject();
    out.fieldUInt("v", 2);
    out.fieldString("t", "NET");
    out.fieldString("sid", sid);
    out.fieldUInt("tx", tx);
    out.beginObject("net");
    out.fieldString("src", source.id);
    out.fieldString("boot", source.boot);
    out.fieldUInt("seq", seq);
    out.fieldString("dst", target.id);
    out.fieldUInt("ttl", 3);
    out.fieldString("kind", "NODE_GET");
    out.beginObject("body");
    out.fieldString("req", req);
    out.endObject();
    out.endObject();
    out.endObject();
    if (!out.finish()) return {};
    char frame[1100];
    size_t len;
    if (!encodeFrame(out.data(), out.size(), frame, sizeof(frame), len)) return {};
    return std::vector<uint8_t>(frame, frame + len);
}

void test_busy_and_interbyte_timeout() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 900);
    Identity ai = identityFor(1), bi = identityFor(2);
    LinkInfo bLeft = b.node.link(Port::Left);
    const char* sid = bLeft.sid;
    for (uint32_t tx = 1; tx <= 5; ++tx) {
        std::vector<uint8_t> frame = craftedNet(ai, bi, sid, tx, 100 + tx);
        b.left.incoming.insert(b.left.incoming.end(), frame.begin(), frame.end());
    }
    b.node.tick(now);
    TEST_ASSERT_EQUAL(4, b.recorder.messages.size());
    std::string replies(a.right.incoming.begin(), a.right.incoming.end());
    TEST_ASSERT_TRUE(replies.find("LBUSY") != std::string::npos);

    Device c(randomA);
    Device d(randomB);
    connect(c, d);
    now = 0;
    Identity ci = identityFor(1), di = identityFor(2);
    TEST_ASSERT_TRUE(c.node.begin(ci, now));
    TEST_ASSERT_TRUE(d.node.begin(di, now));
    run(c, d, now, 900);
    std::vector<uint8_t> frame = craftedNet(ci, di, d.node.link(Port::Left).sid, 1, 77);
    d.left.incoming.insert(d.left.incoming.end(), frame.begin(), frame.begin() + 12);
    d.node.tick(now);
    now += 600;
    d.left.incoming.insert(d.left.incoming.end(), frame.begin() + 12, frame.end());
    d.node.tick(now);
    TEST_ASSERT_EQUAL(0, d.recorder.messages.size());
    now += 5;
    d.left.incoming.insert(d.left.incoming.end(), frame.begin(), frame.end());
    d.node.tick(now);
    TEST_ASSERT_EQUAL(1, d.recorder.messages.size());
}

void test_crc_prefix_resync() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 900);
    Identity ai = identityFor(1), bi = identityFor(2);
    LinkInfo link = b.node.link(Port::Left);
    std::vector<uint8_t> frame = craftedNet(ai, bi, link.sid, 1, 99);
    const uint8_t prefix[] = {'@','{','}','\t','0'};
    b.left.incoming.insert(b.left.incoming.end(), prefix, prefix + sizeof(prefix));
    b.left.incoming.insert(b.left.incoming.end(), frame.begin(), frame.end());
    b.node.tick(now);
    TEST_ASSERT_EQUAL(1, b.recorder.messages.size());
}

} // namespace

void runProtocolFaultTests() {
    RUN_TEST(test_repeated_hello_and_frame_recovery);
    RUN_TEST(test_failed_send_preserves_next_tx);
    RUN_TEST(test_one_way_failure_and_stale_session);
    RUN_TEST(test_three_node_forward_and_reorder);
    RUN_TEST(test_buffered_pong_prevents_timeout_reset);
    RUN_TEST(test_broadcast_shares_seq_across_paths);
    RUN_TEST(test_busy_and_interbyte_timeout);
    RUN_TEST(test_crc_prefix_resync);
}
