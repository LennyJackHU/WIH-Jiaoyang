#include <unity.h>
#include <string>
#include <vector>
#include <string.h>
#include "web_serial.h"

namespace {
std::vector<std::string> frames;
uint32_t randomState = 1234567;
uint32_t randomValue() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
void capture(const char* data, size_t length) { frames.emplace_back(data, length); }

class QuietPort : public wlp2::BytePort {
public:
    int read() override { return -1; }
    void write(const uint8_t*, size_t) override {}
};

struct Fixture {
    QuietPort left, right;
    wlp2::Handler handler;
    wlp2::Node node;
    WordGame game;
    WebSerialEvents web;
    wlp2::Identity identity = {};

    Fixture() : node(&left, &right, handler, randomValue),
                game(node, randomValue, randomValue), web(capture) {
        frames.clear();
        randomState = 1234567;
        strcpy(identity.id, "0000000000000001");
        strcpy(identity.fingerprint, "00000000000000f1");
        strcpy(identity.boot, "00000000000000a1");
        strcpy(identity.kit, "0123456789abcdef0123456789abcdef");
    }
    void begin(uint32_t now = 0) {
        TEST_ASSERT_TRUE(game.begin(identity, now));
        web.begin(identity.fingerprint, identity.boot, now);
    }
};

void checkFrame(size_t index, const char* type, const char* word = nullptr,
                uint32_t sequence = 0, const char* session = "00000000000000a1") {
    TEST_ASSERT_TRUE(index < frames.size());
    const std::string& frame = frames[index];
    TEST_ASSERT_TRUE(frame.compare(0, 4, "WIH:") == 0);
    TEST_ASSERT_EQUAL_CHAR('\n', frame.back());
    TEST_ASSERT_TRUE(frame.size() <= 1024);
    wlp2::JsonReader json(frame.data() + 4, frame.size() - 5);
    TEST_ASSERT_TRUE(json.valid());
    char text[128];
    uint32_t value;
    TEST_ASSERT_TRUE(json.getString("type", text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING(type, text);
    TEST_ASSERT_TRUE(json.getUInt("protocol", value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_TRUE(json.getString("device", text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("00000000000000f1", text);
    TEST_ASSERT_TRUE(json.getString("session", text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING(session, text);
    if (word) {
        TEST_ASSERT_TRUE(json.getText("word", text, sizeof(text)));
        TEST_ASSERT_EQUAL_STRING(word, text);
        TEST_ASSERT_TRUE(json.getUInt("seq", value));
        TEST_ASSERT_EQUAL_UINT32(sequence, value);
    } else {
        TEST_ASSERT_FALSE(json.contains("word"));
        TEST_ASSERT_FALSE(json.contains("seq"));
    }
}

void test_web_ready_periodic_and_no_word_replay() {
    Fixture f;
    f.begin(100);
    checkFrame(0, "ready");
    f.web.tick(2099);
    TEST_ASSERT_EQUAL_UINT32(1, frames.size());
    f.web.tick(2100);
    f.web.tick(4100);
    TEST_ASSERT_EQUAL_UINT32(3, frames.size());
    checkFrame(1, "ready");
    checkFrame(2, "ready");
}

void test_web_ready_handles_millis_rollover() {
    Fixture f;
    f.begin(0xffffff00u);
    f.web.tick(1743);
    TEST_ASSERT_EQUAL_UINT32(1, frames.size());
    f.web.tick(1744);
    TEST_ASSERT_EQUAL_UINT32(2, frames.size());
    checkFrame(1, "ready");
}

void test_web_only_first_confirm_emits() {
    Fixture f;
    f.begin();
    const std::string word = f.game.currentWord();
    f.web.pressConfirm(f.game, 10);
    TEST_ASSERT_TRUE(f.game.confirmed());
    checkFrame(1, "word_confirmed", word.c_str(), 1);
    f.web.pressConfirm(f.game, 20);
    TEST_ASSERT_EQUAL(WordGame::Status::NoLink, f.game.status());
    TEST_ASSERT_EQUAL_UINT32(2, frames.size());
}

void test_web_reconfirm_same_word_is_new_event() {
    Fixture f;
    f.begin();
    const std::string word = f.game.currentWord();
    f.web.pressConfirm(f.game, 10);
    f.game.pressRefresh(20); // B cancels a confirmed selection.
    TEST_ASSERT_FALSE(f.game.confirmed());
    TEST_ASSERT_EQUAL_STRING(word.c_str(), f.game.currentWord());
    f.web.pressConfirm(f.game, 30);
    TEST_ASSERT_EQUAL_UINT32(3, frames.size());
    checkFrame(2, "word_confirmed", word.c_str(), 2);
}

void test_web_encoder_and_refresh_do_not_emit() {
    Fixture f;
    f.begin();
    f.game.turnEncoder(1, 10);
    f.game.pressRefresh(20);
    f.web.tick(30);
    TEST_ASSERT_EQUAL_UINT32(1, frames.size());
    f.web.pressConfirm(f.game, 40);
    checkFrame(1, "word_confirmed", f.game.currentWord(), 1);
    f.game.turnEncoder(1, 50);
    TEST_ASSERT_FALSE(f.game.confirmed());
    f.web.pressConfirm(f.game, 60);
    checkFrame(2, "word_confirmed", f.game.currentWord(), 2);
}

void test_web_restored_confirmation_is_not_new_word() {
    Fixture f;
    f.begin();
    f.game.restore(true, "", "");
    f.web.pressConfirm(f.game, 10);
    f.web.tick(20);
    TEST_ASSERT_TRUE(f.game.confirmed());
    TEST_ASSERT_EQUAL_UINT32(1, frames.size());
}

void test_web_sentence_edit_does_not_emit() {
    Fixture f;
    f.begin();
    Sentence sentence;
    TEST_ASSERT_TRUE(sentence.insert(0, "hello", f.identity.id));
    TEST_ASSERT_TRUE(sentence.insert(1, "world", f.identity.id));
    f.game.restoreSentence(sentence, true);
    TEST_ASSERT_EQUAL(WordGame::View::Sentence, f.game.view());
    f.web.pressConfirm(f.game, 10);
    f.web.pressConfirm(f.game, 20);
    TEST_ASSERT_EQUAL_UINT32(1, frames.size());
}

void test_web_new_boot_restarts_sequence() {
    Fixture f;
    f.begin();
    f.web.pressConfirm(f.game, 10);
    checkFrame(1, "word_confirmed", f.game.currentWord(), 1);
    f.web.begin(f.identity.fingerprint, "00000000000000a2", 20);
    checkFrame(2, "ready", nullptr, 0, "00000000000000a2");
    f.game.pressRefresh(30);
    f.web.pressConfirm(f.game, 40);
    checkFrame(3, "word_confirmed", f.game.currentWord(), 1, "00000000000000a2");
}

void test_web_invalid_identity_and_missing_writer_are_safe() {
    Fixture f;
    TEST_ASSERT_TRUE(f.game.begin(f.identity, 0));
    f.web.begin(nullptr, f.identity.boot, 0);
    f.web.tick(2000);
    f.web.pressConfirm(f.game, 2010);
    TEST_ASSERT_TRUE(f.game.confirmed()); // Web failure must not disable gameplay.
    TEST_ASSERT_EQUAL_UINT32(0, frames.size());
    f.web.begin(f.identity.fingerprint, "not-a-session", 3000);
    f.web.tick(5000);
    WebSerialEvents noWriter(nullptr);
    noWriter.begin(f.identity.fingerprint, f.identity.boot, 0);
    noWriter.tick(2000);
    TEST_ASSERT_EQUAL_UINT32(0, frames.size());
}
} // namespace

void runWebSerialTests() {
    RUN_TEST(test_web_ready_periodic_and_no_word_replay);
    RUN_TEST(test_web_ready_handles_millis_rollover);
    RUN_TEST(test_web_only_first_confirm_emits);
    RUN_TEST(test_web_reconfirm_same_word_is_new_event);
    RUN_TEST(test_web_encoder_and_refresh_do_not_emit);
    RUN_TEST(test_web_restored_confirmation_is_not_new_word);
    RUN_TEST(test_web_sentence_edit_does_not_emit);
    RUN_TEST(test_web_new_boot_restarts_sequence);
    RUN_TEST(test_web_invalid_identity_and_missing_writer_are_safe);
}
