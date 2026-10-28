#include <unity.h>

#include <deque>
#include <string>
#include <vector>
#include <string.h>

#include "wlp2_json.h"
#include "wlp2_protocol.h"

using namespace wlp2;

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

    explicit Device(RandomFn random) : node(&left, &right, recorder, random) {}
};

Identity identity(const char* id, const char* boot, const char** members, uint8_t count,
                  const char* kit = "0123456789abcdef0123456789abcdef") {
    Identity value = {};
    snprintf(value.id, sizeof(value.id), "%s", id);
    snprintf(value.boot, sizeof(value.boot), "%s", boot);
    snprintf(value.kit, sizeof(value.kit), "%s", kit);
    snprintf(value.recorder, sizeof(value.recorder), "%s", "0000000000000003");
    value.role = Role::Word;
    for (unsigned i = 0; i < count; ++i) value.members[i] = members[i];
    value.memberCount = count;
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
    writer.fieldUInt("v", 2);
    writer.fieldString("t", "PING");
    writer.beginObject("body");
    writer.fieldString("text", "a\"b");
    writer.endObject();
    writer.endObject();
    TEST_ASSERT_TRUE(writer.finish());
    TEST_ASSERT_EQUAL_STRING("{\"v\":2,\"t\":\"PING\",\"body\":{\"text\":\"a\\\"b\"}}",
                             writer.data());
    char frame[512];
    size_t frameLen;
    TEST_ASSERT_TRUE(encodeFrame(writer.data(), writer.size(), frame, sizeof(frame), frameLen));
    TEST_ASSERT_EQUAL_UINT32(writer.size() + 11, frameLen);
    TEST_ASSERT_EQUAL_CHAR('@', frame[0]);
    TEST_ASSERT_EQUAL_CHAR('\n', frame[frameLen - 1]);
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

void test_recorder_role_mismatch_is_rejected() {
    Device a(randomA);
    const char* members[] = {"0000000000000001", "0000000000000002", "0000000000000003"};
    Identity wordAsRecorder = identity("0000000000000003", "00000000000000c1", members, 3);
    uint32_t now = 0;
    TEST_ASSERT_FALSE(a.node.begin(wordAsRecorder, now));
    Identity recorderAsWord = identity("0000000000000002", "00000000000000b1", members, 3);
    recorderAsWord.role = Role::Recorder;
    TEST_ASSERT_FALSE(a.node.begin(recorderAsWord, now));
}

} // namespace

void setUp() {}
void tearDown() {}
void runGameTests();
void runProtocolFaultTests();

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc_and_json);
    RUN_TEST(test_handshake_information_and_disconnect);
    RUN_TEST(test_foreign_kit_is_rejected);
    RUN_TEST(test_recorder_role_mismatch_is_rejected);
    runProtocolFaultTests();
    runGameTests();
    return UNITY_END();
}
