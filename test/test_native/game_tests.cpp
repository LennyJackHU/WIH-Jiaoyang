#include <unity.h>

#include <deque>
#include <string>
#include <vector>
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

uint16_t randomSequence[16];
unsigned randomSequencePos;
uint32_t sequenceRandom() { return randomSequence[randomSequencePos++]; }

class TestPort : public BytePort {
public:
    TestPort* peer = nullptr;
    bool online = true;
    bool dropAccepts = false;
    bool dropNextAccept = false;
    bool dropPulls = false;
    bool dropNextPull = false;
    bool dropOffers = false;
    bool dropNextOffer = false;
    unsigned accepts = 0;
    unsigned droppedAccepts = 0;
    unsigned pulls = 0;
    unsigned offers = 0;
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
        bool pull = frame.find("\"kind\":\"WORD_PULL\"") != std::string::npos;
        bool offer = frame.find("\"kind\":\"WORD_OFFER\"") != std::string::npos;
        if (accept) ++accepts;
        if (pull) ++pulls;
        if (offer) ++offers;
        if ((accept && (dropAccepts || dropNextAccept)) ||
            (pull && (dropPulls || dropNextPull)) ||
            (offer && (dropOffers || dropNextOffer))) {
            if (accept) {
                dropNextAccept = false;
                ++droppedAccepts;
            }
            if (pull) dropNextPull = false;
            if (offer) dropNextOffer = false;
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

void connectReversed(Device& a, Device& b) {
    a.left.peer = &b.right;
    b.right.peer = &a.left;
    a.left.online = b.right.online = true;
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

Sentence makeSentence(const std::vector<std::string>& words, uint8_t cursor = 0) {
    Sentence sentence;
    for (const auto& word : words) {
        TEST_ASSERT_TRUE(sentence.insert(sentence.count(), word.c_str(), ""));
    }
    sentence.setCursor(cursor);
    return sentence;
}

void assertSentence(const WordGame& game, const std::vector<std::string>& words) {
    TEST_ASSERT_EQUAL_UINT32(words.size(), game.sentence().count());
    for (size_t i = 0; i < words.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(words[i].c_str(), game.sentence().text(i));
    }
}

void requestPull(WordGame& game, uint32_t now, bool after = false) {
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, game.view());
    game.pressConfirm(now);
    TEST_ASSERT_EQUAL(SentenceEdit::InsertSide, game.editMode());
    if (after) {
        game.pressRefresh(now);
        TEST_ASSERT_TRUE(game.insertAfter());
    }
    game.pressConfirm(now);
    TEST_ASSERT_TRUE(game.pullPending());
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
    assertSentence(b.game, {sent});
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.leftWord());
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.rightWord());
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
    assertSentence(a.game, {sent});
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), a.game.rightWord());
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), a.game.leftWord());
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
    std::vector<std::string> expected;
    for (unsigned round = 0; round < Sentence::MAX_WORDS; ++round) {
        std::string offered = a.game.currentWord();
        if (round % 5 == 2) b.left.dropNextAccept = true;
        if (round == 0) {
            a.game.pressConfirm(now);
            a.game.pressConfirm(now);
            TEST_ASSERT_TRUE(a.game.transferPending());
        } else {
            requestPull(b.game, now);
        }
        run(a, b, now, 1800);
        TEST_ASSERT_FALSE(a.game.transferPending());
        TEST_ASSERT_FALSE(b.game.pullPending());
        TEST_ASSERT_FALSE(a.game.confirmed());
        TEST_ASSERT_STR_NE(offered.c_str(), a.game.currentWord());
        TEST_ASSERT_EQUAL_UINT32(round + 1, b.game.receivedTransfers());
        expected.insert(expected.begin(), offered);
        assertSentence(b.game, expected);
        TEST_ASSERT_EQUAL_UINT8(0, b.game.sentence().cursor());
        disconnect(a, b);
        run(a, b, now, 3300);
        assertSentence(b.game, expected);
        connect(a, b);
        run(a, b, now, 900);
        TEST_ASSERT_TRUE(a.node.link(Port::Right).up());
        TEST_ASSERT_TRUE(b.node.link(Port::Left).up());
    }
    TEST_ASSERT_EQUAL_UINT8(Sentence::MAX_WORDS, b.game.sentence().count());
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

void test_refresh_never_returns_current_word() {
    const uint16_t values[] = {10, 11, 12, 11, 12, 13};
    for (unsigned i = 0; i < 6; ++i) randomSequence[i] = values[i];
    randomSequencePos = 0;
    Picker picker(sequenceRandom);
    picker.init();
    TEST_ASSERT_EQUAL_STRING(WORDS[11], picker.currentWord());
    picker.refreshRandom();
    TEST_ASSERT_EQUAL_STRING(WORDS[12], picker.currentWord());
    TEST_ASSERT_EQUAL_STRING(WORDS[13], picker.nextWord());
}

void test_simultaneous_bidirectional_transfer_keeps_sentence() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    std::string aSent = a.game.currentWord();
    std::string bSent = b.game.currentWord();
    a.game.pressConfirm(now);
    b.game.pressConfirm(now);
    a.game.pressConfirm(now);
    b.game.pressConfirm(now);
    run(a, b, now, 1800);
    TEST_ASSERT_FALSE(a.game.transferPending());
    TEST_ASSERT_FALSE(b.game.transferPending());
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, a.game.view());
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, b.game.view());
    TEST_ASSERT_EQUAL_STRING(bSent.c_str(), a.game.rightWord());
    TEST_ASSERT_EQUAL_STRING(aSent.c_str(), b.game.leftWord());
    TEST_ASSERT_STR_NE(aSent.c_str(), a.game.currentWord());
    TEST_ASSERT_STR_NE(bSent.c_str(), b.game.currentWord());
}

void test_error_requires_pending_peer() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    a.game.pressConfirm(now);
    a.game.pressConfirm(now);
    TEST_ASSERT_TRUE(a.game.transferPending());
    char bodyText[96];
    snprintf(bodyText, sizeof(bodyText), "{\"req\":\"%s\",\"code\":\"INVALID\"}",
             a.game.pendingRequest());
    JsonReader body(bodyText, strlen(bodyText));
    NetMessage forged = {};
    snprintf(forged.src, sizeof(forged.src), "0000000000000003");
    snprintf(forged.boot, sizeof(forged.boot), "00000000000000c1");
    snprintf(forged.dst, sizeof(forged.dst), "%s", a.node.identity().id);
    snprintf(forged.kind, sizeof(forged.kind), "ERROR");
    forged.ingress = Port::Right;
    forged.ttl = 3;
    forged.seq = 99;
    a.game.onNet(forged, body);
    TEST_ASSERT_TRUE(a.game.transferPending());
    snprintf(forged.src, sizeof(forged.src), "%s", b.node.identity().id);
    snprintf(forged.boot, sizeof(forged.boot), "%s", b.node.identity().boot);
    a.game.onNet(forged, body);
    TEST_ASSERT_FALSE(a.game.transferPending());
}

void test_cancel_requests_save() {
    Device a(randomA);
    uint32_t now = 0;
    Identity ai = identity("0000000000000001", "00000000000000a1");
    TEST_ASSERT_TRUE(a.game.begin(ai, now));
    a.game.pressConfirm(now);
    TEST_ASSERT_TRUE(a.game.consumeSaveRequest());
    a.game.pressRefresh(now);
    TEST_ASSERT_TRUE(a.game.consumeSaveRequest());
    a.game.pressConfirm(now);
    TEST_ASSERT_TRUE(a.game.consumeSaveRequest());
    a.game.turnEncoder(1, now);
    TEST_ASSERT_TRUE(a.game.consumeSaveRequest());
}

NetMessage testMessage(Device& src, Device& dst, Port ingress, const char* kind,
                       const char* req, const char* text) {
    NetMessage message = {};
    snprintf(message.src, sizeof(message.src), "%s", src.node.identity().id);
    snprintf(message.boot, sizeof(message.boot), "%s", src.node.identity().boot);
    snprintf(message.dst, sizeof(message.dst), "%s", dst.node.identity().id);
    snprintf(message.kind, sizeof(message.kind), "%s", kind);
    message.ingress = ingress;
    message.ttl = 3;
    message.seq = 1;
    (void)req;
    (void)text;
    return message;
}

void test_reversed_reconnect_keeps_word_position() {
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
    std::string req = a.game.pendingRequest();
    TEST_ASSERT_FALSE(req.empty());
    run(a, b, now, 850);
    assertSentence(b.game, {sent});
    disconnect(a, b);
    run(a, b, now, 3500);
    TEST_ASSERT_TRUE(a.game.transferPending());
    b.left.dropAccepts = false;
    connectReversed(a, b);
    run(a, b, now, 1800);
    assertSentence(b.game, {sent});
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.leftWord());
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), b.game.rightWord());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.transferPending());
    TEST_ASSERT_TRUE(b.game.exportReceipts(nullptr, 0) == 0);
}

void test_restored_receipt_blocks_replay_after_clear() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    std::string sent = a.game.currentWord();
    a.game.pressConfirm(now);
    a.game.pressConfirm(now);
    std::string req = a.game.pendingRequest();
    run(a, b, now, 1200);
    WordGame::ReceiptState receipts[8];
    uint8_t count = b.game.exportReceipts(receipts, 8);
    TEST_ASSERT_EQUAL_UINT8(1, count);
    b.game.pressRefresh(now);
    TEST_ASSERT_FALSE(b.game.hasSentence());
    disconnect(a, b);
    run(a, b, now, 3500);

    Device restored(randomC);
    Identity bi = identity("0000000000000002", "00000000000000b1");
    TEST_ASSERT_TRUE(restored.game.begin(bi, now));
    restored.game.restore(false, "", "", a.node.identity().id, "", receipts, count);
    connect(a, restored);
    run(a, restored, now, 1000);
    NetMessage offer = testMessage(a, restored, Port::Left, "WORD_OFFER", req.c_str(), sent.c_str());
    char bodyText[256];
    snprintf(bodyText, sizeof(bodyText), "{\"req\":\"%s\",\"text\":\"%s\"}", req.c_str(), sent.c_str());
    JsonReader body(bodyText, strlen(bodyText));
    restored.game.onNet(offer, body);
    TEST_ASSERT_EQUAL_UINT32(0, restored.game.receivedTransfers());
    TEST_ASSERT_FALSE(restored.game.hasSentence());
}

void test_unknown_restored_source_appends_without_overwrite() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    b.game.restore(false, "alpha", "");
    run(a, b, now, 1000);
    NetMessage offer = testMessage(a, b, Port::Left, "WORD_OFFER",
                                   "123456789abcdef0123456789abcdef0", "beta");
    const char* bodyText = "{\"req\":\"123456789abcdef0123456789abcdef0\",\"text\":\"beta\"}";
    JsonReader body(bodyText, strlen(bodyText));
    b.game.onNet(offer, body);
    assertSentence(b.game, {"beta", "alpha"});
    TEST_ASSERT_EQUAL_UINT8(0, b.game.sentence().cursor());
    TEST_ASSERT_EQUAL(WordGame::Status::ReceivedLeft, b.game.status());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
}

void test_positioned_pull_order_and_cursor_from_each_side() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    Sentence initial = makeSentence({"alpha", "omega"});
    a.game.restoreSentence(initial, false);
    b.game.restoreSentence(initial, false);

    std::string fromRight = b.game.currentWord();
    requestPull(a.game, now);
    TEST_ASSERT_FALSE(a.game.insertAfter());
    run(a, b, now, 1600);
    assertSentence(a.game, {fromRight, "alpha", "omega"});
    TEST_ASSERT_EQUAL_UINT8(0, a.game.sentence().cursor());
    TEST_ASSERT_EQUAL_UINT32(1, a.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.pullPending());
    TEST_ASSERT_FALSE(b.game.transferPending());
    TEST_ASSERT_STR_NE(fromRight.c_str(), b.game.currentWord());

    std::string fromLeft = a.game.currentWord();
    requestPull(b.game, now, true);
    run(a, b, now, 1600);
    assertSentence(b.game, {"alpha", fromLeft, "omega"});
    TEST_ASSERT_EQUAL_UINT8(1, b.game.sentence().cursor());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_FALSE(b.game.pullPending());
    TEST_ASSERT_FALSE(a.game.transferPending());
    TEST_ASSERT_STR_NE(fromLeft.c_str(), a.game.currentWord());
}

void test_sentence_cursor_delete_confirm_cancel_and_clear() {
    Device a(randomA);
    uint32_t now = 0;
    Identity ai = identity("0000000000000001", "00000000000000a1");
    TEST_ASSERT_TRUE(a.game.begin(ai, now));
    a.game.restoreSentence(makeSentence({"alpha", "beta", "omega"}, 1), false);
    TEST_ASSERT_EQUAL_UINT8(1, a.game.sentence().cursor());
    a.game.turnEncoder(1, now);
    TEST_ASSERT_EQUAL_UINT8(2, a.game.sentence().cursor());
    a.game.turnEncoder(1, now);
    TEST_ASSERT_EQUAL_UINT8(0, a.game.sentence().cursor());
    a.game.turnEncoder(-1, now);
    TEST_ASSERT_EQUAL_UINT8(2, a.game.sentence().cursor());

    a.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(SentenceEdit::DeleteConfirm, a.game.editMode());
    a.game.pressRefresh(now);
    TEST_ASSERT_EQUAL(SentenceEdit::Navigate, a.game.editMode());
    assertSentence(a.game, {"alpha", "beta", "omega"});
    TEST_ASSERT_EQUAL_UINT8(2, a.game.sentence().cursor());
    a.game.pressConfirm(now);
    a.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(SentenceEdit::Navigate, a.game.editMode());
    assertSentence(a.game, {"alpha", "beta"});
    TEST_ASSERT_EQUAL_UINT8(1, a.game.sentence().cursor());
    a.game.pressRefresh(now);
    TEST_ASSERT_EQUAL(WordGame::View::Picker, a.game.view());
    TEST_ASSERT_TRUE(a.game.sentence().empty());
}

void test_empty_sentence_can_pull() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    a.game.restoreSentence(makeSentence({"seed"}), false);
    disconnect(a, b);
    run(a, b, now, 3500);
    a.game.pressConfirm(now);
    a.game.pressConfirm(now);
    TEST_ASSERT_TRUE(a.game.sentence().empty());
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, a.game.view());

    connect(a, b);
    run(a, b, now, 1000);
    std::string offered = b.game.currentWord();
    requestPull(a.game, now);
    run(a, b, now, 1600);
    assertSentence(a.game, {offered});
    TEST_ASSERT_EQUAL_UINT8(0, a.game.sentence().cursor());
    TEST_ASSERT_EQUAL_UINT32(1, a.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.pullPending());
    TEST_ASSERT_STR_NE(offered.c_str(), b.game.currentWord());
}

void test_hotplug_preserves_pull_request_without_duplicate() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    a.game.restoreSentence(makeSentence({"alpha"}), false);
    std::string offered = b.game.currentWord();
    a.right.dropNextPull = true;
    requestPull(a.game, now);
    std::string request = a.game.pendingPullRequest();
    TEST_ASSERT_FALSE(request.empty());
    disconnect(a, b);
    run(a, b, now, 3500);
    TEST_ASSERT_TRUE(a.game.pullPending());
    TEST_ASSERT_EQUAL_STRING(request.c_str(), a.game.pendingPullRequest());
    TEST_ASSERT_EQUAL_STRING(offered.c_str(), b.game.currentWord());
    TEST_ASSERT_EQUAL_UINT32(0, b.game.receivedTransfers());
    assertSentence(a.game, {"alpha"});

    connect(a, b);
    run(a, b, now, 1800);
    assertSentence(a.game, {offered, "alpha"});
    TEST_ASSERT_EQUAL_UINT32(1, a.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.pullPending());
    TEST_ASSERT_TRUE(a.right.pulls >= 2);
    run(a, b, now, 1500);
    assertSentence(a.game, {offered, "alpha"});
    TEST_ASSERT_EQUAL_UINT32(1, a.game.receivedTransfers());
}

void test_positioned_pull_lost_accept_is_idempotent() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    a.game.restoreSentence(makeSentence({"alpha", "omega"}, 1), false);
    std::string offered = b.game.currentWord();
    a.right.dropNextAccept = true;
    requestPull(a.game, now, true);
    run(a, b, now, 1800);
    assertSentence(a.game, {"alpha", "omega", offered});
    TEST_ASSERT_EQUAL_UINT8(2, a.game.sentence().cursor());
    TEST_ASSERT_EQUAL_UINT32(1, a.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.pullPending());
    TEST_ASSERT_FALSE(b.game.transferPending());
    TEST_ASSERT_EQUAL_UINT32(1, a.right.droppedAccepts);
    TEST_ASSERT_EQUAL_UINT32(2, a.right.accepts);
    TEST_ASSERT_EQUAL_UINT32(1, b.left.offers);
    TEST_ASSERT_STR_NE(offered.c_str(), b.game.currentWord());
}

void test_sentence_capacity_and_position_bounds() {
    Sentence direct;
    TEST_ASSERT_FALSE(direct.insert(1, "bad", ""));
    TEST_ASSERT_TRUE(direct.insert(0, "alpha", ""));
    TEST_ASSERT_FALSE(direct.insert(2, "bad", ""));
    TEST_ASSERT_FALSE(direct.remove(1));
    direct.clear();
    std::vector<std::string> full;
    for (unsigned i = 0; i < Sentence::MAX_WORDS; ++i) {
        char word[16];
        snprintf(word, sizeof(word), "w%02u", i);
        full.emplace_back(word);
        TEST_ASSERT_TRUE(direct.insert(direct.count(), word, ""));
    }
    TEST_ASSERT_EQUAL_UINT8(Sentence::MAX_WORDS, direct.count());
    TEST_ASSERT_FALSE(direct.insert(0, "overflow", ""));
    TEST_ASSERT_FALSE(direct.insert(Sentence::MAX_WORDS + 1, "overflow", ""));
    TEST_ASSERT_FALSE(direct.remove(Sentence::MAX_WORDS));
    for (unsigned i = 0; i < full.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(full[i].c_str(), direct.text(i));
    }

    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    b.game.restoreSentence(makeSentence({"alpha"}), false);
    NetMessage invalidPosition = testMessage(a, b, Port::Left, "WORD_OFFER",
        "11111111111111111111111111111111", "beta");
    const char* invalidText =
        "{\"req\":\"11111111111111111111111111111111\",\"text\":\"beta\",\"pos\":2}";
    JsonReader invalidBody(invalidText, strlen(invalidText));
    b.game.onNet(invalidPosition, invalidBody);
    assertSentence(b.game, {"alpha"});
    TEST_ASSERT_EQUAL(WordGame::Status::SlotBusy, b.game.status());
    TEST_ASSERT_EQUAL_UINT32(0, b.game.receivedTransfers());

    b.game.restoreSentence(makeSentence(full), false);
    b.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(SentenceEdit::Navigate, b.game.editMode());
    TEST_ASSERT_FALSE(b.game.pullPending());
    TEST_ASSERT_EQUAL(WordGame::Status::SlotBusy, b.game.status());
    NetMessage fullOffer = testMessage(a, b, Port::Left, "WORD_OFFER",
        "22222222222222222222222222222222", "overflow");
    const char* fullText =
        "{\"req\":\"22222222222222222222222222222222\",\"text\":\"overflow\",\"pos\":0}";
    JsonReader fullBody(fullText, strlen(fullText));
    b.game.onNet(fullOffer, fullBody);
    assertSentence(b.game, full);
    TEST_ASSERT_EQUAL(WordGame::Status::SlotBusy, b.game.status());
    TEST_ASSERT_EQUAL_UINT32(0, b.game.receivedTransfers());
}

void test_restore_sentence_cursor_order_and_replay_tombstone() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    Sentence saved;
    TEST_ASSERT_TRUE(saved.insert(0, "alpha", a.node.identity().id));
    TEST_ASSERT_TRUE(saved.insert(1, "beta", b.node.identity().id));
    TEST_ASSERT_TRUE(saved.insert(2, "omega", ""));
    saved.setCursor(1);
    WordGame::ReceiptState tombstone = {};
    snprintf(tombstone.src, sizeof(tombstone.src), "%s", a.node.identity().id);
    snprintf(tombstone.boot, sizeof(tombstone.boot), "%s", a.node.identity().boot);
    snprintf(tombstone.req, sizeof(tombstone.req), "33333333333333333333333333333333");
    snprintf(tombstone.text, sizeof(tombstone.text), "ghost");
    tombstone.side = 0;
    tombstone.positioned = true;
    tombstone.position = 1;
    tombstone.completed = 1;
    tombstone.order = 7;
    b.game.restoreSentence(saved, true, &tombstone, 1);
    assertSentence(b.game, {"alpha", "beta", "omega"});
    TEST_ASSERT_EQUAL_UINT8(1, b.game.sentence().cursor());
    TEST_ASSERT_TRUE(b.game.confirmed());
    TEST_ASSERT_EQUAL_STRING(a.node.identity().id, b.game.sentence().source(0));
    TEST_ASSERT_EQUAL_STRING(b.node.identity().id, b.game.sentence().source(1));

    b.game.pressRefresh(now);
    TEST_ASSERT_FALSE(b.game.hasSentence());
    NetMessage replay = testMessage(a, b, Port::Left, "WORD_OFFER",
                                    tombstone.req, tombstone.text);
    const char* replayText =
        "{\"req\":\"33333333333333333333333333333333\",\"text\":\"ghost\",\"pos\":1}";
    JsonReader replayBody(replayText, strlen(replayText));
    b.game.onNet(replay, replayBody);
    TEST_ASSERT_EQUAL_UINT32(0, b.game.receivedTransfers());
    TEST_ASSERT_FALSE(b.game.hasSentence());
    TEST_ASSERT_EQUAL(WordGame::View::Picker, b.game.view());
}

void test_simultaneous_bidirectional_positioned_pulls() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    a.game.restoreSentence(makeSentence({"a0", "a1"}), false);
    b.game.restoreSentence(makeSentence({"b0", "b1"}, 1), false);
    std::string aOffer = a.game.currentWord();
    std::string bOffer = b.game.currentWord();
    requestPull(a.game, now);
    requestPull(b.game, now, true);
    run(a, b, now, 1800);
    assertSentence(a.game, {bOffer, "a0", "a1"});
    assertSentence(b.game, {"b0", "b1", aOffer});
    TEST_ASSERT_EQUAL_UINT8(0, a.game.sentence().cursor());
    TEST_ASSERT_EQUAL_UINT8(2, b.game.sentence().cursor());
    TEST_ASSERT_EQUAL_UINT32(1, a.game.receivedTransfers());
    TEST_ASSERT_EQUAL_UINT32(1, b.game.receivedTransfers());
    TEST_ASSERT_FALSE(a.game.pullPending());
    TEST_ASSERT_FALSE(b.game.pullPending());
    TEST_ASSERT_FALSE(a.game.transferPending());
    TEST_ASSERT_FALSE(b.game.transferPending());
    TEST_ASSERT_STR_NE(aOffer.c_str(), a.game.currentWord());
    TEST_ASSERT_STR_NE(bOffer.c_str(), b.game.currentWord());
}

void test_insert_mode_disconnect_and_cancel_return_to_navigation() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    b.game.restoreSentence(makeSentence({"alpha", "beta"}), false);
    b.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(SentenceEdit::InsertSide, b.game.editMode());
    disconnect(a, b);
    run(a, b, now, 3500);
    TEST_ASSERT_EQUAL(SentenceEdit::Navigate, b.game.editMode());
    b.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(SentenceEdit::DeleteConfirm, b.game.editMode());
    b.game.pressRefresh(now);
    TEST_ASSERT_EQUAL(SentenceEdit::Navigate, b.game.editMode());

    connect(a, b);
    run(a, b, now, 1000);
    b.game.pressConfirm(now);
    b.game.pressConfirm(now);
    TEST_ASSERT_TRUE(b.game.pullPending());
    b.game.pressRefresh(now);
    TEST_ASSERT_FALSE(b.game.pullPending());
    TEST_ASSERT_EQUAL(SentenceEdit::Navigate, b.game.editMode());
}

void test_remote_candidate_change_requests_display() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    b.game.restoreSentence(makeSentence({"alpha"}), false);
    b.game.pressConfirm(now);
    TEST_ASSERT_EQUAL(SentenceEdit::InsertSide, b.game.editMode());
    while (b.game.consumeDisplayRequest()) {}
    std::string before = b.game.remote(Port::Left).text;
    a.game.pressRefresh(now);
    run(a, b, now, 100);
    TEST_ASSERT_STR_NE(before.c_str(), b.game.remote(Port::Left).text);
    TEST_ASSERT_TRUE(b.game.consumeDisplayRequest());
}

void test_incomplete_receipts_are_not_evicted() {
    Device a(randomA);
    Device b(randomB);
    connect(a, b);
    uint32_t now = 0;
    beginPair(a, b, now);
    run(a, b, now, 1000);
    char firstReq[33];
    for (unsigned i = 0; i < 9; ++i) {
        char req[33];
        snprintf(req, sizeof(req), "%08x%08x%08x%08x", i, i, i, i);
        if (i == 0) snprintf(firstReq, sizeof(firstReq), "%s", req);
        NetMessage offer = testMessage(a, b, Port::Left, "WORD_OFFER", req, "word");
        char bodyText[128];
        snprintf(bodyText, sizeof(bodyText), "{\"req\":\"%s\",\"text\":\"word\"}", req);
        JsonReader body(bodyText, strlen(bodyText));
        b.game.onNet(offer, body);
    }
    TEST_ASSERT_EQUAL_UINT32(8, b.game.receivedTransfers());
    TEST_ASSERT_EQUAL(WordGame::Status::SlotBusy, b.game.status());
    NetMessage done = testMessage(a, b, Port::Left, "WORD_DONE", firstReq, "word");
    char doneText[96];
    snprintf(doneText, sizeof(doneText), "{\"req\":\"%s\"}", firstReq);
    JsonReader doneBody(doneText, strlen(doneText));
    b.game.onNet(done, doneBody);
    NetMessage ninth = testMessage(a, b, Port::Left, "WORD_OFFER",
                                   "88888888888888888888888888888888", "word");
    const char* ninthText = "{\"req\":\"88888888888888888888888888888888\",\"text\":\"word\"}";
    JsonReader ninthBody(ninthText, strlen(ninthText));
    b.game.onNet(ninth, ninthBody);
    TEST_ASSERT_EQUAL_UINT32(9, b.game.receivedTransfers());
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
    RUN_TEST(test_refresh_never_returns_current_word);
    RUN_TEST(test_simultaneous_bidirectional_transfer_keeps_sentence);
    RUN_TEST(test_error_requires_pending_peer);
    RUN_TEST(test_cancel_requests_save);
    RUN_TEST(test_reversed_reconnect_keeps_word_position);
    RUN_TEST(test_restored_receipt_blocks_replay_after_clear);
    RUN_TEST(test_unknown_restored_source_appends_without_overwrite);
    RUN_TEST(test_positioned_pull_order_and_cursor_from_each_side);
    RUN_TEST(test_sentence_cursor_delete_confirm_cancel_and_clear);
    RUN_TEST(test_empty_sentence_can_pull);
    RUN_TEST(test_hotplug_preserves_pull_request_without_duplicate);
    RUN_TEST(test_positioned_pull_lost_accept_is_idempotent);
    RUN_TEST(test_sentence_capacity_and_position_bounds);
    RUN_TEST(test_restore_sentence_cursor_order_and_replay_tombstone);
    RUN_TEST(test_simultaneous_bidirectional_positioned_pulls);
    RUN_TEST(test_insert_mode_disconnect_and_cancel_return_to_navigation);
    RUN_TEST(test_remote_candidate_change_requests_display);
    RUN_TEST(test_incomplete_receipts_are_not_evicted);
}
