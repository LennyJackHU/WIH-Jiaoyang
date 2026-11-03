#include <unity.h>
#include <deque>
#include <string>
#include <vector>
#include <string.h>
#include "web_serial.h"

namespace {

std::vector<std::string> frames;
uint32_t state = 1234567;
bool txReady = true;

uint32_t randomValue() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void capture(const char* data, size_t length) { frames.emplace_back(data, length); }
bool canWrite(size_t length) { return txReady && length <= 1024; }

class Port : public wlp2::BytePort {
public:
    std::deque<uint8_t> input;
    int read() override {
        if (input.empty()) return -1;
        const uint8_t value = input.front();
        input.pop_front();
        return value;
    }
    void write(const uint8_t*, size_t) override {}
};

struct Fixture {
    Port left, right;
    wlp2::Handler handler;
    wlp2::Node node;
    WordGame game;
    WebSerialEvents web;
    wlp2::Identity identity = {};

    Fixture()
        : node(&left, &right, handler, randomValue),
          game(node, randomValue, randomValue), web(capture, canWrite) {
        frames.clear();
        txReady = true;
        strcpy(identity.id, "0000000000000001");
        strcpy(identity.fingerprint, "00000000000000f1");
        strcpy(identity.boot, "00000000000000a1");
        strcpy(identity.kit, "0123456789abcdef0123456789abcdef");
    }

    void begin(uint32_t now = 0) {
        TEST_ASSERT_TRUE(game.begin(identity, now));
        web.begin(identity.fingerprint, identity.boot, now);
    }

    void command(const std::string& line) {
        for (char c : line) web.receive(c, game);
    }

    void request(const char* request = "0123456789abcdef") {
        command(std::string("WIH:{\"type\":\"get_sentence\",\"protocol\":1,\"request\":\"") +
                request + "\"}\n");
    }

    void confirm(uint32_t now) {
        const bool wasPicker = game.view() == WordGame::View::Picker;
        const bool wasConfirmed = game.confirmed();
        game.pressConfirm(now);
        web.observeConfirm(game, wasPicker, wasConfirmed);
    }
};

wlp2::JsonReader frame(size_t index, const char* type) {
    TEST_ASSERT_TRUE(index < frames.size());
    TEST_ASSERT_TRUE(frames[index].size() <= 1024);
    TEST_ASSERT_TRUE(frames[index].compare(0, 4, "WIH:") == 0);
    TEST_ASSERT_EQUAL_CHAR('\n', frames[index].back());
    wlp2::JsonReader json(frames[index].data() + 4, frames[index].size() - 5);
    TEST_ASSERT_TRUE(json.valid());
    char text[128];
    uint32_t number;
    TEST_ASSERT_TRUE(json.getString("type", text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING(type, text);
    TEST_ASSERT_TRUE(json.getUInt("protocol", number));
    TEST_ASSERT_EQUAL_UINT32(1, number);
    TEST_ASSERT_TRUE(json.getString("device", text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("00000000000000f1", text);
    return json;
}

void field(const wlp2::JsonReader& json, const char* key, const char* expected) {
    char text[128];
    TEST_ASSERT_TRUE(json.getString(key, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING(expected, text);
}

Sentence twoWords() {
    Sentence sentence;
    TEST_ASSERT_TRUE(sentence.insert(0, "hello", ""));
    TEST_ASSERT_TRUE(sentence.insert(1, "world", ""));
    return sentence;
}

void test_ready_capabilities_and_periodic_frames() {
    Fixture f;
    f.begin();
    auto json = frame(0, "ready");
    uint32_t capability;
    TEST_ASSERT_TRUE(json.getUInt("sentenceExport", capability));
    TEST_ASSERT_EQUAL_UINT32(1, capability);
    f.web.tick(1999);
    TEST_ASSERT_EQUAL_UINT32(1, frames.size());
    f.web.tick(2000);
    TEST_ASSERT_EQUAL_UINT32(2, frames.size());
    frame(1, "ready");
}

void test_first_confirm_emits_word_without_duplicate() {
    Fixture f;
    f.begin();
    const std::string word = f.game.currentWord();
    f.confirm(1);
    field(frame(1, "word_confirmed"), "word", word.c_str());
    f.confirm(2);
    TEST_ASSERT_EQUAL(WordGame::Status::NoLink, f.game.status());
    TEST_ASSERT_EQUAL_UINT32(2, frames.size());
}

void test_upload_emits_ordered_frames_without_changing_connection_flow() {
    Fixture f;
    f.begin();
    f.game.restoreSentence(twoWords(), false);
    TEST_ASSERT_TRUE(f.web.uploadSentence(f.game));
    TEST_ASSERT_FALSE(f.web.uploadSentence(f.game));
    for (uint32_t now = 1; now <= 4; ++now) f.web.tick(now);
    frame(1, "upload_begin");
    field(frame(2, "upload_word"), "word", "hello");
    field(frame(3, "upload_word"), "word", "world");
    frame(4, "upload_end");
    TEST_ASSERT_EQUAL_UINT32(5, frames.size());
}

void test_upload_snapshot_is_frozen() {
    Fixture f;
    f.begin();
    f.game.restoreSentence(twoWords(), false);
    TEST_ASSERT_TRUE(f.web.uploadSentence(f.game));
    Sentence changed;
    TEST_ASSERT_TRUE(changed.insert(0, "changed", ""));
    f.game.restoreSentence(changed, false);
    for (uint32_t now = 1; now <= 4; ++now) f.web.tick(now);
    field(frame(2, "upload_word"), "word", "hello");
    field(frame(3, "upload_word"), "word", "world");
}

void test_upload_tx_backpressure_preserves_frames() {
    Fixture f;
    f.begin();
    f.game.restoreSentence(twoWords(), false);
    TEST_ASSERT_TRUE(f.web.uploadSentence(f.game));
    txReady = false;
    f.web.tick(1);
    TEST_ASSERT_EQUAL_UINT32(1, frames.size());
    txReady = true;
    for (uint32_t now = 2; now <= 5; ++now) f.web.tick(now);
    frame(1, "upload_begin");
    frame(4, "upload_end");
}

void test_requested_sentence_and_empty_upload() {
    Fixture f;
    f.begin();
    f.request();
    TEST_ASSERT_FALSE(f.web.uploadSentence(f.game));
    f.web.tick(1);
    f.web.tick(2);
    frame(1, "sentence_begin");
    frame(2, "sentence_end");
}

void test_invalid_commands_recover_for_later_upload() {
    Fixture f;
    f.begin();
    f.game.restoreSentence(twoWords(), false);
    f.command("junk\nWIH:{bad}\n");
    f.command(std::string(200, 'x') + "\n");
    TEST_ASSERT_TRUE(f.web.uploadSentence(f.game));
    for (uint32_t now = 1; now <= 4; ++now) f.web.tick(now);
    frame(1, "upload_begin");
    frame(4, "upload_end");
}

} // namespace

void runWebSerialTests() {
    RUN_TEST(test_ready_capabilities_and_periodic_frames);
    RUN_TEST(test_first_confirm_emits_word_without_duplicate);
    RUN_TEST(test_upload_emits_ordered_frames_without_changing_connection_flow);
    RUN_TEST(test_upload_snapshot_is_frozen);
    RUN_TEST(test_upload_tx_backpressure_preserves_frames);
    RUN_TEST(test_requested_sentence_and_empty_upload);
    RUN_TEST(test_invalid_commands_recover_for_later_upload);
}
