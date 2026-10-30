#include <unity.h>

#include <deque>
#include <string>
#include <vector>
#include <string.h>

#include "wlp2_json.h"
#include "wlp2_protocol.h"

using namespace wlp2;

#define TEST_ASSERT_STR_NE(a, b) TEST_ASSERT_TRUE(strcmp((a), (b)) != 0)

namespace {

uint32_t randomStateA = 0x12345678;
uint32_t randomStateB = 0x87654321;

uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

uint32_t randomA() { return nextRandom(randomStateA); }
uint32_t randomB() { return nextRandom(randomStateB); }

class TestPort : public BytePort {
public:
    TestPort* peer = nullptr;
    bool online = true;
    std::deque<uint8_t> incoming;

    int read() override {
        if (incoming.empty()) return -1;
        uint8_t value = incoming.front();
        incoming.pop_front();
        return value;
    }

    void write(const uint8_t* data, size_t len) override {
        if (!online || !peer || !peer->online) return;
        peer->incoming.insert(peer->incoming.end(), data, data + len);
    }
};

class Recorder : public Handler {
public:
    std::vector<std::string> kinds;
    std::vector<std::string> bodies;
    unsigned linkChanges = 0;

    void onLinkChanged(Port) override { ++linkChanges; }
    void onNet(const NetMessage& message, const JsonReader& body) override {
        kinds.emplace_back(message.kind);
        bodies.emplace_back(body.data(), body.size());
    }
};

struct Device {
    TestPort left;
    TestPort right;
    Recorder recorder;
    Node node;

    explicit Device(RandomFn random, IdChangedFn changed = nullptr)
        : node(&left, &right, recorder, random, changed) {}
};

unsigned idChangesA = 0;
unsigned idChangesB = 0;
char persistedIdA[17] = {};
char persistedIdB[17] = {};

void persistIdA(const char* id) {
    ++idChangesA;
    snprintf(persistedIdA, sizeof(persistedIdA), "%s", id);
}

void persistIdB(const char* id) {
    ++idChangesB;
    snprintf(persistedIdB, sizeof(persistedIdB), "%s", id);
}

Identity identity(const char* id, const char* boot, const char** members, uint8_t count,
                  const char* kit = "0123456789abcdef0123456789abcdef") {
    (void)members;
    (void)count;
    Identity value = {};
    snprintf(value.id, sizeof(value.id), "%s", id);
    snprintf(value.boot, sizeof(value.boot), "%s", boot);
    snprintf(value.kit, sizeof(value.kit), "%s", kit);
    snprintf(value.fingerprint, sizeof(value.fingerprint), "%s", id);
    return value;
}

void connect(Device& a, Device& b) {
    a.right.peer = &b.left;
    b.left.peer = &a.right;
    a.right.online = b.left.online = true;
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

void test_crc_and_json() {
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926u, crc32("123456789", 9));
    const char* duplicate = "{\"v\":2,\"v\":2}";
    TEST_ASSERT_FALSE(JsonReader(duplicate, strlen(duplicate)).valid());
    const char* overflow = "{\"n\":4294967296}";
    uint32_t number;
    TEST_ASSERT_FALSE(JsonReader(overflow, strlen(overflow)).getUInt("n", number));
    const char* escaped = "{\"text\":\"\\u4f60\\u597d\"}";
    JsonReader json(escaped, strlen(escaped));
    char text[16];
    TEST_ASSERT_TRUE(json.valid());
    TEST_ASSERT_TRUE(json.getText("text", text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("你好", text);
    const char* rawUtf8 = "{\"text\":\"你好\"}";
    TEST_ASSERT_TRUE(JsonReader(rawUtf8, strlen(rawUtf8)).getText("text", text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("你好", text);
    const char* duplicateEscaped = "{\"\\u0076\":2,\"v\":2}";
    TEST_ASSERT_FALSE(JsonReader(duplicateEscaped, strlen(duplicateEscaped)).valid());
    const char* notNumber = "{\"n\":NaN}";
    TEST_ASSERT_FALSE(JsonReader(notNumber, strlen(notNumber)).valid());
    const char* controlText = "{\"text\":\"a\\nb\"}";
    TEST_ASSERT_FALSE(JsonReader(controlText, strlen(controlText)).getText("text", text, sizeof(text)));
    char largeText[128];
    std::string maxEscaped = "{\"text\":\"";
    for (unsigned i = 0; i < 32; ++i) maxEscaped += "\\u0061";
    maxEscaped += "\"}";
    TEST_ASSERT_TRUE(JsonReader(maxEscaped.data(), maxEscaped.size()).getText(
        "text", largeText, sizeof(largeText)));
    TEST_ASSERT_EQUAL_UINT32(32, strlen(largeText));
    std::string tooEscaped = "{\"text\":\"";
    for (unsigned i = 0; i < 33; ++i) tooEscaped += "\\u0061";
    tooEscaped += "\"}";
    TEST_ASSERT_FALSE(JsonReader(tooEscaped.data(), tooEscaped.size()).getText(
        "text", largeText, sizeof(largeText)));
    std::string longJson = "{\"text\":\"" + std::string(2048, 'a') + "\"}";
    TEST_ASSERT_FALSE(JsonReader(longJson.data(), longJson.size()).valid());

    char buffer[256];
    JsonWriter writer(buffer, sizeof(buffer));
    writer.beginObject();
    writer.fieldUInt("v", PROTOCOL_VERSION);
    writer.fieldString("t", "PING");
    writer.beginObject("body");
    writer.fieldString("text", "a\"b");
    writer.endObject();
    writer.endObject();
    TEST_ASSERT_TRUE(writer.finish());
    TEST_ASSERT_EQUAL_STRING("{\"v\":3,\"t\":\"PING\",\"body\":{\"text\":\"a\\\"b\"}}",
                             writer.data());
    char frame[512];
    size_t frameLen;
    TEST_ASSERT_TRUE(encodeFrame(writer.data(), writer.size(), frame, sizeof(frame), frameLen));
    TEST_ASSERT_EQUAL_UINT32(writer.size() + 11, frameLen);
    TEST_ASSERT_EQUAL_CHAR('@', frame[0]);
    TEST_ASSERT_EQUAL_CHAR('\n', frame[frameLen - 1]);
    JsonWriter nullWriter(nullptr, 16);
    nullWriter.beginObject();
    TEST_ASSERT_FALSE(nullWriter.finish());
    TEST_ASSERT_FALSE(encodeFrame(writer.data(), writer.size(), nullptr, 512, frameLen));
}

void test_json_depth_roundtrip() {
    char json[128];
    JsonWriter writer(json, sizeof(json));
    for (unsigned i = 0; i < JSON_MAX_DEPTH; ++i) writer.beginObject(i == 0 ? nullptr : "a");
    writer.fieldUInt("v", 1);
    for (unsigned i = 0; i < JSON_MAX_DEPTH; ++i) writer.endObject();
    TEST_ASSERT_TRUE(writer.finish());
    TEST_ASSERT_TRUE(JsonReader(writer.data(), writer.size()).valid());
    JsonWriter tooDeep(json, sizeof(json));
    for (unsigned i = 0; i <= JSON_MAX_DEPTH; ++i) tooDeep.beginObject(i == 0 ? nullptr : "a");
    TEST_ASSERT_FALSE(tooDeep.finish());
}

void test_handshake_information_and_disconnect() {
    randomStateA = 0x12345678;
    randomStateB = 0x87654321;
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    const char* members[] = {"0000000000000001", "0000000000000002", "0000000000000003"};
    Identity ai = identity("0000000000000001", "00000000000000a1", members, 3);
    Identity bi = identity("0000000000000002", "00000000000000b1", members, 3);
    uint32_t now = 0;
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    run(a, b, now, 1000);
    LinkInfo ar = a.node.link(Port::Right);
    LinkInfo bl = b.node.link(Port::Left);
    TEST_ASSERT_TRUE(ar.up());
    TEST_ASSERT_TRUE(bl.up());
    TEST_ASSERT_EQUAL_STRING(bi.id, ar.peerId);
    TEST_ASSERT_EQUAL_STRING(ai.id, bl.peerId);
    TEST_ASSERT_EQUAL(Port::Left, ar.peerPort);
    TEST_ASSERT_EQUAL(Port::Right, bl.peerPort);
    TEST_ASSERT_EQUAL_STRING(ar.sid, bl.sid);
    TEST_ASSERT_EQUAL_UINT32(1, a.node.topoRev());
    TEST_ASSERT_EQUAL_UINT32(1, b.node.topoRev());

    const char* request = "{\"req\":\"123456789abcdef0123456789abcdef0\"}";
    TEST_ASSERT_TRUE(a.node.sendNet(bi.id, "NODE_GET", request, now));
    run(a, b, now, 100);
    TEST_ASSERT_EQUAL(1, b.recorder.kinds.size());
    TEST_ASSERT_EQUAL_STRING("NODE_GET", b.recorder.kinds[0].c_str());
    TEST_ASSERT_EQUAL_STRING(request, b.recorder.bodies[0].c_str());

    char oldSid[33];
    snprintf(oldSid, sizeof(oldSid), "%s", ar.sid);
    disconnect(a, b);
    run(a, b, now, 3500);
    TEST_ASSERT_FALSE(a.node.link(Port::Right).up());
    TEST_ASSERT_FALSE(b.node.link(Port::Left).up());
    connect(a, b);
    run(a, b, now, 1000);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    TEST_ASSERT_TRUE(strcmp(oldSid, a.node.link(Port::Right).sid) != 0);
}

void test_foreign_kit_is_rejected() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    const char* members[] = {"0000000000000001", "0000000000000002", "0000000000000003"};
    Identity ai = identity("0000000000000001", "00000000000000a1", members, 3);
    Identity bi = identity("0000000000000002", "00000000000000b1", members, 3,
                           "fedcba9876543210fedcba9876543210");
    uint32_t now = 0;
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    run(a, b, now, 1000);
    TEST_ASSERT_FALSE(a.node.link(Port::Right).up());
    TEST_ASSERT_FALSE(b.node.link(Port::Left).up());
}

void test_unlisted_modules_join() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    const char* members[] = {"0000000000000001"};
    Identity ai = identity("1000000000000001", "00000000000000a1", members, 1);
    Identity bi = identity("2000000000000002", "00000000000000b1", members, 1);
    uint32_t now = 0;
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    run(a, b, now, 1000);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    TEST_ASSERT_EQUAL_STRING(bi.id, a.node.link(Port::Right).peerId);
}

void test_same_device_restart_keeps_id() {
    Device a(randomA);
    Device b(randomB, persistIdB);
    connect(a, b);
    const char* members[] = {"0000000000000001"};
    Identity ai = identity("1000000000000001", "00000000000000a1", members, 1);
    Identity bi = identity("2000000000000002", "00000000000000b1", members, 1);
    uint32_t now = 0;
    idChangesB = 0;
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    run(a, b, now, 1000);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());

    snprintf(bi.boot, sizeof(bi.boot), "%s", "00000000000000b2");
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    run(a, b, now, 1000);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    TEST_ASSERT_EQUAL_STRING("2000000000000002", b.node.identity().id);
    TEST_ASSERT_EQUAL_UINT32(0, idChangesB);
}

void test_id_conflict_reassigns_persists_and_recovers() {
    randomStateA = 0x12345678;
    randomStateB = 0x87654321;
    Device a(randomA, persistIdA);
    Device b(randomB, persistIdB);
    connect(a, b);
    const char* members[] = {"0000000000000001"};
    Identity ai = identity("4000000000000004", "00000000000000a1", members, 1);
    Identity bi = identity("4000000000000004", "00000000000000b1", members, 1);
    snprintf(bi.fingerprint, sizeof(bi.fingerprint), "%s", "5000000000000005");
    uint32_t now = 0;
    idChangesA = idChangesB = 0;
    persistedIdA[0] = persistedIdB[0] = '\0';
    TEST_ASSERT_TRUE(a.node.begin(ai, now));
    TEST_ASSERT_TRUE(b.node.begin(bi, now));
    run(a, b, now, 3000);

    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    TEST_ASSERT_STR_NE("4000000000000004", a.node.identity().id);
    TEST_ASSERT_STR_NE("4000000000000004", b.node.identity().id);
    TEST_ASSERT_STR_NE(a.node.identity().id, b.node.identity().id);
    TEST_ASSERT_TRUE(idChangesA > 0);
    TEST_ASSERT_TRUE(idChangesB > 0);
    TEST_ASSERT_EQUAL_STRING(a.node.identity().id, persistedIdA);
    TEST_ASSERT_EQUAL_STRING(b.node.identity().id, persistedIdB);
}

} // namespace

void setUp() {}
void tearDown() {}
void runGameTests();
void runProtocolFaultTests();

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc_and_json);
    RUN_TEST(test_json_depth_roundtrip);
    RUN_TEST(test_handshake_information_and_disconnect);
    RUN_TEST(test_foreign_kit_is_rejected);
    RUN_TEST(test_unlisted_modules_join);
    RUN_TEST(test_same_device_restart_keeps_id);
    RUN_TEST(test_id_conflict_reassigns_persists_and_recovers);
    runProtocolFaultTests();
    runGameTests();
    return UNITY_END();
}
