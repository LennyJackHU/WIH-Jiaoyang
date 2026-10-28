#include <unity.h>

#include <deque>
#include <string>
#include <string.h>

#include "word_game.h"

using namespace wlp2;

#define TEST_ASSERT_STR_NE(a, b) TEST_ASSERT_TRUE(strcmp((a), (b)) != 0)

namespace {

uint32_t stateA = 0x13572468;
uint32_t stateB = 0x24681357;
uint32_t stateC = 0x55aa33cc;

uint32_t stepRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

uint32_t randomA() { return stepRandom(stateA); }
uint32_t randomB() { return stepRandom(stateB); }
uint32_t randomC() { return stepRandom(stateC); }

class TestPort : public BytePort {
public:
    TestPort* peer = nullptr;
    bool online = true;
    bool dropAccepts = false;
    bool dropNextAccept = false;
    std::deque<uint8_t> incoming;

    int read() override {
        if (incoming.empty()) return -1;
        uint8_t value = incoming.front();
        incoming.pop_front();
        return value;
    }

    void write(const uint8_t* data, size_t len) override {
        if (!online || !peer || !peer->online) return;
        std::string frame(reinterpret_cast<const char*>(data), len);
        bool accept = frame.find("\"kind\":\"WORD_ACCEPT\"") != std::string::npos;
        if (accept && (dropAccepts || dropNextAccept)) {
            dropNextAccept = false;
            return;
        }
        peer->incoming.insert(peer->incoming.end(), data, data + len);
    }
};

struct Device {
    TestPort left;
    TestPort right;
    Handler initialHandler;
    Node node;
    WordGame game;

    explicit Device(RandomFn random)
        : node(&left, &right, initialHandler, random), game(node, random, random) {}
};

Identity identity(const char* id, const char* boot) {
    static const char* members[] = {
        "0000000000000001", "0000000000000002", "0000000000000003"
    };
    Identity value = {};
    snprintf(value.id, sizeof(value.id), "%s", id);
    snprintf(value.boot, sizeof(value.boot), "%s", boot);
    snprintf(value.kit, sizeof(value.kit), "%s", "0123456789abcdef0123456789abcdef");
    snprintf(value.recorder, sizeof(value.recorder), "%s", "0000000000000003");
    value.role = Role::Word;
    for (unsigned i = 0; i < 3; ++i) value.members[i] = members[i];
    value.memberCount = 3;
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
        a.game.tick(now);
        b.game.tick(now);
    }
}

void run(Device& a, Device& b, Device& c, uint32_t& now, uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 5) {
        now += 5;
        a.game.tick(now);
        b.game.tick(now);
        c.game.tick(now);
    }
}

void beginPair(Device& a, Device& b, uint32_t& now) {
    Identity ai = identity("0000000000000001", "00000000000000a1");
    Identity bi = identity("0000000000000002", "00000000000000b1");
    TEST_ASSERT_TRUE(a.game.begin(ai, now));
    TEST_ASSERT_TRUE(b.game.begin(bi, now));
}

void test_picker_and_buttons() {
    stateA = 0x13572468;
    Picker picker(randomA);
    picker.init();
    TEST_ASSERT_TRUE(picker.prevWord());
    TEST_ASSERT_TRUE(picker.nextWord());
    TEST_ASSERT_STR_NE(picker.prevWord(), picker.currentWord());
    TEST_ASSERT_STR_NE(picker.currentWord(), picker.nextWord());
    std::string initial = picker.currentWord();
    std::string next = picker.nextWord();
    picker.stepForward();
    TEST_ASSERT_EQUAL_STRING(next.c_str(), picker.currentWord());
    picker.stepBackward();
    TEST_ASSERT_EQUAL_STRING(initial.c_str(), picker.currentWord());
    for (unsigned i = 0; i < 100; ++i) {
        std::string before = picker.currentWord();
        picker.stepForward();
        TEST_ASSERT_STR_NE(before.c_str(), picker.currentWord());
    }
    for (unsigned i = 0; i < 100; ++i) {
        std::string before = picker.currentWord();
        picker.stepBackward();
        TEST_ASSERT_STR_NE(before.c_str(), picker.currentWord());
    }
    std::string beforeRefresh = picker.currentWord();
    picker.refreshRandom();
    TEST_ASSERT_STR_NE(beforeRefresh.c_str(), picker.currentWord());
}

void test_left_transfer_and_node_info() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1300);
    TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
    TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    TEST_ASSERT_TRUE(a.game.remote(Port::Right).orderValid);
    TEST_ASSERT_TRUE(b.game.remote(Port::Left).orderValid);
    TEST_ASSERT_EQUAL_STRING(b.game.currentWord(), a.game.remote(Port::Right).text);
    TEST_ASSERT_EQUAL_STRING(a.game.currentWord(), b.game.remote(Port::Left).text);
    TEST_ASSERT_EQUAL_UINT8(2, a.game.participantCount());

    std::string sent = a.game.currentWord();
    std::string receiverOwn = b.game.currentWord();
    a.game.pressConfirm(now);
    TEST_ASSERT_TRUE(a.game.confirmed());
    TEST_ASSERT_FALSE(a.game.transferPending());
    a.game.pressConfirm(now);
    TEST_ASSERT_TRUE(a.game.transferPending());
    run(a, b, now, 1300);
    TEST_ASSERT_FALSE(a.game.transferPending());
    TEST_ASSERT_FALSE(a.game.confirmed());
    TEST_ASSERT_STR_NE(sent.c_str(), a.game.currentWord());
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.leftWord());
    TEST_ASSERT_EQUAL_STRING("", b.game.rightWord());
    TEST_ASSERT_EQUAL_STRING(receiverOwn.c_str(), b.game.currentWord());
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, b.game.view());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_TRUE(b.game.consumeDisplayRequest());
    TEST_ASSERT_TRUE(b.game.consumeSaveRequest());
    run(a, b, now, 600);
    TEST_ASSERT_EQUAL_STRING(a.game.currentWord(), b.game.remote(Port::Left).text);
    b.game.pressRefresh(now);
    TEST_ASSERT_EQUAL(WordGame::View::Picker, b.game.view());
    TEST_ASSERT_FALSE(b.game.hasSentence());
}

void test_reverse_transfer_goes_to_right() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    std::string sent = b.game.currentWord();
    std::string receiverOwn = a.game.currentWord();
    b.game.pressConfirm(now);
    b.game.pressConfirm(now);
    run(a, b, now, 1200);
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), a.game.rightWord());
    TEST_ASSERT_EQUAL_STRING("", a.game.leftWord());
    TEST_ASSERT_EQUAL_STRING(receiverOwn.c_str(), a.game.currentWord());
    TEST_ASSERT_STR_NE(sent.c_str(), b.game.currentWord());
    TEST_ASSERT_FALSE(b.game.confirmed());
}

void test_lost_accept_is_idempotent() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    std::string sent = a.game.currentWord();
    b.left.dropNextAccept = true;
    a.game.pressConfirm(now);
    a.game.pressConfirm(now);
    run(a, b, now, 1800);
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.leftWord());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.transferPending());
    TEST_ASSERT_STR_NE(sent.c_str(), a.game.currentWord());
}

void test_hotplug_during_transfer() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    std::string sent = a.game.currentWord();
    a.game.pressConfirm(now);
    a.game.pressConfirm(now);
    std::string request = a.game.pendingRequest();
    disconnect(a, b);
    run(a, b, now, 3500);
    TEST_ASSERT_TRUE(a.game.transferPending());
    TEST_ASSERT_EQUAL_STRING(request.c_str(), a.game.pendingRequest());
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), a.game.currentWord());
    TEST_ASSERT_EQUAL(0, b.game.receivedTransfers());
    connect(a, b);
    run(a, b, now, 1400);
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.leftWord());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.transferPending());
    TEST_ASSERT_STR_NE(sent.c_str(), a.game.currentWord());
}

void test_reconnect_after_lost_accept() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    std::string sent = a.game.currentWord();
    b.left.dropAccepts = true;
    a.game.pressConfirm(now);
    a.game.pressConfirm(now);
    std::string request = a.game.pendingRequest();
    run(a, b, now, 850);
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.leftWord());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_TRUE(a.game.transferPending());
    disconnect(a, b);
    run(a, b, now, 3500);
    TEST_ASSERT_TRUE(b.game.hasSentence());
    TEST_ASSERT_EQUAL_STRING(request.c_str(), a.game.pendingRequest());
    b.left.dropAccepts = false;
    connect(a, b);
    run(a, b, now, 1800);
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.leftWord());
    TEST_ASSERT_FALSE(a.game.transferPending());
}

void test_repeated_full_game_cycles() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    for (unsigned round = 0; round < 24; ++round) {
        Device& sender = round % 2 ? b : a;
        Device& receiver = round % 2 ? a : b;
        std::string sent = sender.game.currentWord();
        uint32_t received = receiver.game.receivedTransfers();
        if (round % 5 == 2) {
            TestPort& ackPort = &sender == &a ? receiver.left : receiver.right;
            ackPort.dropNextAccept = true;
        }
        sender.game.pressConfirm(now);
        sender.game.pressConfirm(now);
        run(a, b, now, 1800);
        TEST_ASSERT_FALSE(sender.game.transferPending());
        TEST_ASSERT_FALSE(sender.game.confirmed());
        TEST_ASSERT_STR_NE(sent.c_str(), sender.game.currentWord());
        TEST_ASSERT_EQUAL_UINT32(received + 1, receiver.game.receivedTransfers());
        const char* slot = &sender == &a ? receiver.game.leftWord() : receiver.game.rightWord();
        TEST_ASSERT_EQUAL_STRING(sent.c_str(), slot);
        disconnect(a, b);
        run(a, b, now, 3300);
        if (receiver.game.hasSentence()) {
            TEST_ASSERT_EQUAL_STRING(sent.c_str(), slot);
        }
        connect(a, b);
        run(a, b, now, 900);
        TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
        TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    }
}

void test_cancel_restore_and_no_link() {
    Device a(randomA);
    uint32_t now = 0;
    Identity ai = identity("0000000000000001", "00000000000000a1");
    TEST_ASSERT_TRUE(a.game.begin(ai, now));
    std::string initial = a.game.currentWord();
    a.game.pressConfirm(now);
    TEST_ASSERT_TRUE(a.game.confirmed());
    a.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(WordGame::Status::NoLink, a.game.status());
    TEST_ASSERT_FALSE(a.game.transferPending());
    a.game.pressRefresh(now);
    TEST_ASSERT_FALSE(a.game.confirmed());
    a.game.pressConfirm(now);
    a.game.turnEncoder(1, now);
    TEST_ASSERT_FALSE(a.game.confirmed());
    TEST_ASSERT_STR_NE(initial.c_str(), a.game.currentWord());
    std::string beforeRefresh = a.game.currentWord();
    a.game.pressRefresh(now);
    TEST_ASSERT_STR_NE(beforeRefresh.c_str(), a.game.currentWord());
    a.game.restore(false, "alpha", "omega");
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, a.game.view());
    TEST_ASSERT_EQUAL_STRING("alpha", a.game.leftWord());
    TEST_ASSERT_EQUAL_STRING("omega", a.game.rightWord());
    TEST_ASSERT_TRUE(a.game.consumeDisplayRequest());
    a.game.pressRefresh(now);
    TEST_ASSERT_FALSE(a.game.hasSentence());
    TEST_ASSERT_EQUAL(WordGame::View::Picker, a.game.view());
}

void test_three_contributions_and_two_link_guard() {
    Device left(randomA);
    Device center(randomB);
    Device right(randomC);
    connect(left, center);
    connect(center, right);
    uint32_t now = 0;
    beginPair(left, center, now);
    Identity ri = identity("0000000000000003", "00000000000000c1");
    ri.role = Role::Recorder;
    TEST_ASSERT_TRUE(right.game.begin(ri, now));
    run(left, center, right, now, 1500);
    TEST_ASSERT_TRUE(center.game.remote(Port::Left).orderValid);
    TEST_ASSERT_TRUE(center.game.remote(Port::Right).orderValid);
    TEST_ASSERT_EQUAL_UINT8(3, center.game.participantCount());
    center.game.pressConfirm(now);
    center.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(WordGame::Status::TwoLinks, center.game.status());
    TEST_ASSERT_FALSE(center.game.transferPending());
    center.game.pressRefresh(now);

    std::string leftSent = left.game.currentWord();
    std::string centerOwn = center.game.currentWord();
    std::string rightSent = right.game.currentWord();
    left.game.pressConfirm(now);
    left.game.pressConfirm(now);
    right.game.pressConfirm(now);
    right.game.pressConfirm(now);
    run(left, center, right, now, 1600);
    TEST_ASSERT_EQUAL_STRING(leftSent.c_str(), center.game.leftWord());
    TEST_ASSERT_EQUAL_STRING(centerOwn.c_str(), center.game.currentWord());
    TEST_ASSERT_EQUAL_STRING(rightSent.c_str(), center.game.rightWord());
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, center.game.view());
    TEST_ASSERT_EQUAL_UINT32(2, center.game.receivedTransfers());
    disconnect(center, right);
    run(left, center, right, now, 3500);
    TEST_ASSERT_EQUAL_UINT8(2, center.game.participantCount());
    TEST_ASSERT_EQUAL_STRING(leftSent.c_str(), center.game.leftWord());
    TEST_ASSERT_EQUAL_STRING(rightSent.c_str(), center.game.rightWord());
    center.game.pressRefresh(now);
    TEST_ASSERT_FALSE(center.game.hasSentence());
}

} // namespace

void runGameTests() {
    RUN_TEST(test_picker_and_buttons);
    RUN_TEST(test_left_transfer_and_node_info);
    RUN_TEST(test_reverse_transfer_goes_to_right);
    RUN_TEST(test_lost_accept_is_idempotent);
    RUN_TEST(test_hotplug_during_transfer);
    RUN_TEST(test_reconnect_after_lost_accept);
    RUN_TEST(test_repeated_full_game_cycles);
    RUN_TEST(test_cancel_restore_and_no_link);
    RUN_TEST(test_three_contributions_and_two_link_guard);
}
