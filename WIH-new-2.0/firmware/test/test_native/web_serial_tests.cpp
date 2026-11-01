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
uint32_t randomValue() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
void capture(const char* data, size_t length) { frames.emplace_back(data, length); }
bool canWrite(size_t length) { return txReady && length <= 1024; }
class Port : public wlp2::BytePort {
public:
    Port* peer = nullptr;
    std::deque<uint8_t> input;
    int read() override { if (input.empty()) return -1; const uint8_t b=input.front(); input.pop_front(); return b; }
    void write(const uint8_t* b, size_t n) override { if (peer) peer->input.insert(peer->input.end(), b, b+n); }
};
struct Fixture {
    Port left, right;
    wlp2::Handler handler;
    wlp2::Node node;
    WordGame game;
    WebSerialEvents web;
    wlp2::Identity identity = {};
    Fixture() : node(&left,&right,handler,randomValue), game(node,randomValue,randomValue), web(capture,canWrite) {
        frames.clear(); txReady=true;
        strcpy(identity.id,"0000000000000001"); strcpy(identity.fingerprint,"00000000000000f1");
        strcpy(identity.boot,"00000000000000a1"); strcpy(identity.kit,"0123456789abcdef0123456789abcdef");
    }
    void begin(uint32_t now=0) { TEST_ASSERT_TRUE(game.begin(identity,now)); web.begin(identity.fingerprint,identity.boot,now); }
    void command(const std::string& line) { for(char c:line) web.receive(c,game); }
    void confirm(uint32_t now) {
        const bool wasPicker=game.view()==WordGame::View::Picker;
        const bool wasConfirmed=game.confirmed();
        game.pressConfirm(now);
        web.observeConfirm(game,wasPicker,wasConfirmed);
    }
    void request(const char* kind, const char* request="0123456789abcdef") {
        command(std::string("WIH:{\"type\":\"")+kind+"\",\"protocol\":1,\"request\":\""+request+"\"}\n");
    }
};
wlp2::JsonReader frame(size_t index, const char* type) {
    TEST_ASSERT_TRUE(index < frames.size());
    TEST_ASSERT_TRUE(frames[index].size()<=1024);
    TEST_ASSERT_TRUE(frames[index].compare(0,4,"WIH:")==0);
    TEST_ASSERT_EQUAL_CHAR('\n',frames[index].back());
    wlp2::JsonReader json(frames[index].data()+4,frames[index].size()-5);
    TEST_ASSERT_TRUE(json.valid()); char text[128]; uint32_t number;
    TEST_ASSERT_TRUE(json.getString("type",text,sizeof(text))); TEST_ASSERT_EQUAL_STRING(type,text);
    TEST_ASSERT_TRUE(json.getUInt("protocol",number)); TEST_ASSERT_EQUAL_UINT32(1,number);
    TEST_ASSERT_TRUE(json.getString("device",text,sizeof(text))); TEST_ASSERT_EQUAL_STRING("00000000000000f1",text);
    return json;
}
void field(const wlp2::JsonReader& json,const char* key,const char* expected) {
    char text[128]; TEST_ASSERT_TRUE(json.getString(key,text,sizeof(text))); TEST_ASSERT_EQUAL_STRING(expected,text);
}
void test_ready_capabilities_and_periodic_rollover() {
    Fixture f; f.begin(0xffffff00u);
    auto json=frame(0,"ready"); uint32_t n;
    TEST_ASSERT_FALSE(json.contains("wordExport"));
    TEST_ASSERT_TRUE(json.getUInt("sentenceExport",n)); TEST_ASSERT_EQUAL_UINT32(1,n);
    f.web.tick(1743); TEST_ASSERT_EQUAL_UINT32(1,frames.size());
    f.web.tick(1744); TEST_ASSERT_EQUAL_UINT32(2,frames.size()); frame(1,"ready");
}
void test_first_hardware_confirm_emits_word_with_sequence() {
    Fixture f; f.begin(); const std::string word=f.game.currentWord();
    f.confirm(1);
    auto json=frame(1,"word_confirmed"); field(json,"word",word.c_str());
    uint32_t seq; TEST_ASSERT_TRUE(json.getUInt("seq",seq)); TEST_ASSERT_EQUAL_UINT32(1,seq);
    TEST_ASSERT_TRUE(f.game.confirmed()); TEST_ASSERT_FALSE(f.game.transferPending());
    TEST_ASSERT_TRUE(f.game.sentence().empty());
}

void test_second_confirm_keeps_original_transfer_without_duplicate() {
    Fixture f; f.begin(); f.confirm(1); f.confirm(2);
    TEST_ASSERT_EQUAL(WordGame::Status::NoLink,f.game.status());
    TEST_ASSERT_EQUAL_UINT32(2,frames.size());
}

void test_cancel_and_reconfirm_upload_same_word_as_new_event() {
    Fixture f; f.begin(); const std::string word=f.game.currentWord();
    f.confirm(1); f.game.pressRefresh(2); f.confirm(3);
    auto json=frame(2,"word_confirmed"); field(json,"word",word.c_str());
    uint32_t seq; TEST_ASSERT_TRUE(json.getUInt("seq",seq)); TEST_ASSERT_EQUAL_UINT32(2,seq);
}
void test_encoder_restore_and_sentence_edit_do_not_upload_word() {
    Fixture f; f.begin(); f.game.turnEncoder(1,1); f.web.tick(2);
    f.game.restore(true,"",""); f.confirm(3);
    TEST_ASSERT_EQUAL_UINT32(1,frames.size());
    f.game.pressEdit(4); f.confirm(5); f.game.pressClear(6); f.confirm(7);
    TEST_ASSERT_EQUAL_UINT32(1,frames.size());
}

void test_snapshot_is_frozen_ordered_and_does_not_mutate_game() {
    Fixture f; f.begin(); Sentence s; s.insert(0,"hello",""); s.insert(1,"world","");
    f.game.restoreSentence(s,true); f.game.consumeSaveRequest(); f.game.consumeDisplayRequest();
    f.request("get_sentence");
    TEST_ASSERT_FALSE(f.game.consumeSaveRequest()); TEST_ASSERT_FALSE(f.game.consumeDisplayRequest());
    TEST_ASSERT_TRUE(f.game.confirmed()); TEST_ASSERT_EQUAL_UINT8(2,f.game.sentence().count());
    s.clear(); s.insert(0,"changed",""); f.game.restoreSentence(s,false);
    for(int t=1;t<=4;++t) f.web.tick(t);
    frame(1,"sentence_begin"); field(frame(2,"sentence_word"),"word","hello");
    field(frame(3,"sentence_word"),"word","world"); frame(4,"sentence_end");
    TEST_ASSERT_EQUAL_UINT32(5,frames.size());
}
void test_empty_sentence_never_injects_candidate() {
    Fixture f; f.begin(); f.request("get_sentence"); f.web.tick(1); f.web.tick(2);
    auto json=frame(1,"sentence_begin"); uint32_t n;
    TEST_ASSERT_TRUE(json.getUInt("count",n)); TEST_ASSERT_EQUAL_UINT32(0,n);
    frame(2,"sentence_end"); TEST_ASSERT_EQUAL_UINT32(3,frames.size());
}
void test_max_sentence_and_escaped_words_fit_frames() {
    Fixture f; f.begin(); Sentence s; std::string text(96,'"');
    for(uint8_t i=0;i<12;++i) TEST_ASSERT_TRUE(s.insert(i,text.c_str(),""));
    f.game.restoreSentence(s,false); f.request("get_sentence");
    for(int t=1;t<=14;++t) f.web.tick(t);
    TEST_ASSERT_EQUAL_UINT32(15,frames.size());
    for(unsigned i=0;i<12;++i) {
        auto json=frame(i+2,"sentence_word"); field(json,"word",text.c_str()); uint32_t n;
        TEST_ASSERT_TRUE(json.getUInt("index",n)); TEST_ASSERT_EQUAL_UINT32(i,n);
    }
    frame(14,"sentence_end");
}
void test_invalid_and_oversize_commands_recover() {
    Fixture f; f.begin(); f.command("junk\nWIH:{bad}\n");
    f.request("get_sentence","bad"); f.request("get_word"); f.request("delete_sentence");
    f.command(std::string(200,'x')); f.request("get_sentence"); f.web.tick(1);
    TEST_ASSERT_EQUAL_UINT32(1,frames.size());
    f.request("get_sentence"); f.web.tick(2); f.web.tick(3);
    frame(1,"sentence_begin"); frame(2,"sentence_end");
}

void test_tx_backpressure_preserves_response() {
    Fixture f; f.begin(); txReady=false; f.request("get_sentence"); f.web.tick(1);
    TEST_ASSERT_EQUAL_UINT32(1,frames.size());
    txReady=true; f.web.tick(2); f.web.tick(3); frame(1,"sentence_begin"); frame(2,"sentence_end");
}

void test_new_request_supersedes_old() {
    Fixture f; f.begin(); f.request("get_sentence"); f.web.tick(1);
    f.request("get_sentence","fedcba9876543210"); f.web.tick(2); f.web.tick(3);
    field(frame(2,"sentence_begin"),"request","fedcba9876543210");
    field(frame(3,"sentence_end"),"request","fedcba9876543210");
}

void test_patch3_edit_and_clear_interactions_unchanged_by_reads() {
    Fixture f; f.begin(); const std::string word=f.game.currentWord(); f.game.pressEdit(1);
    TEST_ASSERT_EQUAL_STRING(word.c_str(),f.game.sentence().text(0));
    f.game.pressClear(2); TEST_ASSERT_EQUAL(SentenceEdit::ClearConfirm,f.game.editMode());
    f.request("get_sentence"); f.web.tick(3); f.web.tick(4); f.web.tick(5);
    TEST_ASSERT_EQUAL(SentenceEdit::ClearConfirm,f.game.editMode());
    TEST_ASSERT_EQUAL_UINT8(1,f.game.sentence().count());
    f.game.pressConfirm(6); TEST_ASSERT_EQUAL(SentenceEdit::Navigate,f.game.editMode());
    f.game.pressClear(7); f.game.pressClear(8); TEST_ASSERT_TRUE(f.game.sentence().empty());
}
void test_pogo_receive_then_export_keeps_display_order() {
    Fixture receiver,sender; receiver.begin();
    strcpy(sender.identity.id,"0000000000000002"); strcpy(sender.identity.fingerprint,"00000000000000f2");
    strcpy(sender.identity.boot,"00000000000000a2"); TEST_ASSERT_TRUE(sender.game.begin(sender.identity,0));
    sender.right.peer=&receiver.left; receiver.left.peer=&sender.right;
    uint32_t now=0;
    auto run=[&](uint32_t duration){for(uint32_t t=0;t<duration;t+=5){now+=5;sender.game.tick(now);receiver.game.tick(now);}};
    run(2500); TEST_ASSERT_EQUAL_UINT8(1,sender.node.connectedCount());
    const std::string first=sender.game.currentWord(); sender.game.pressConfirm(now); sender.game.pressConfirm(now+1); run(1500);
    const std::string second=sender.game.currentWord(); sender.game.pressConfirm(now); sender.game.pressConfirm(now+1); run(1500);
    TEST_ASSERT_EQUAL_UINT8(2,receiver.game.sentence().count()); receiver.request("get_sentence");
    for(unsigned t=1;t<=4;++t)receiver.web.tick(now+t);
    field(frame(2,"sentence_word"),"word",second.c_str()); field(frame(3,"sentence_word"),"word",first.c_str());
}
} // namespace
void runWebSerialTests() {
    RUN_TEST(test_ready_capabilities_and_periodic_rollover);
    RUN_TEST(test_first_hardware_confirm_emits_word_with_sequence);
    RUN_TEST(test_second_confirm_keeps_original_transfer_without_duplicate);
    RUN_TEST(test_cancel_and_reconfirm_upload_same_word_as_new_event);
    RUN_TEST(test_encoder_restore_and_sentence_edit_do_not_upload_word);
    RUN_TEST(test_snapshot_is_frozen_ordered_and_does_not_mutate_game);
    RUN_TEST(test_empty_sentence_never_injects_candidate);
    RUN_TEST(test_max_sentence_and_escaped_words_fit_frames);
    RUN_TEST(test_invalid_and_oversize_commands_recover);
    RUN_TEST(test_tx_backpressure_preserves_response);
    RUN_TEST(test_new_request_supersedes_old);
    RUN_TEST(test_patch3_edit_and_clear_interactions_unchanged_by_reads);
    RUN_TEST(test_pogo_receive_then_export_keeps_display_order);
}
