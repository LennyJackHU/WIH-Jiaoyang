#include <Arduino.h>
#include <esp_random.h>
#include <esp32-hal-psram.h>
#include "config.h"
#include "display.h"
#include "input.h"
#include "storage.h"
#include "wlp2_arduino.h"
#include "word_game.h"

static wlp2::Handler initialHandler;
static UartBytePort leftUart(Serial1);
static UartBytePort rightUart(Serial2);
static void nodeIdChanged(const char* id) {
    bool saved = storageSaveNodeId(id);
    Serial.printf("wlp2: reassigned id=%s saved=%d\n", id, saved);
}
static wlp2::Node node(&leftUart, &rightUart, initialHandler, esp_random, nodeIdChanged);
static WordGame game(node, esp_random, esp_random);
static bool protocolReady = false;
static bool dirty = false;
static DisplayArea dirtyArea = DisplayArea::Full;
static uint32_t lastActivityMs = 0;
static uint32_t lastDrawMs = 0;
static uint32_t partialCount = 0;
static bool epdReady = false;
static bool forceFullRefresh = false;
static bool nvsDirty = false;
static uint32_t nextNvsRetryMs = 0;

static void fillState(PersistState& s) {
    Picker& picker = game.picker();
    s.magic = STATE_MAGIC;
    memcpy(s.hist, picker.hist, sizeof(s.hist));
    s.first = picker.first;
    s.cur = picker.cur;
    s.last = picker.last;
    s.confirmed = game.confirmed() ? 1 : 0;
    const Sentence& sentence = game.sentence();
    s.sentenceCount = sentence.count();
    s.sentenceCursor = sentence.cursor();
    for (uint8_t i = 0; i < sentence.count(); ++i) {
        snprintf(s.sentenceWords[i], sizeof(s.sentenceWords[i]), "%s", sentence.text(i));
        snprintf(s.sentenceSources[i], sizeof(s.sentenceSources[i]), "%s", sentence.source(i));
    }
    s.receiptCount = game.exportReceipts(s.receipts, 8);
}

static bool applyState(const PersistState& s) {
    if (s.magic != STATE_MAGIC || s.first >= HISTORY_LEN || s.cur >= HISTORY_LEN ||
        s.last >= HISTORY_LEN || s.confirmed > 1 || s.receiptCount > 8 ||
        s.sentenceCount > Sentence::MAX_WORDS ||
        (s.sentenceCount == 0 && s.sentenceCursor != 0) ||
        (s.sentenceCount > 0 && s.sentenceCursor >= s.sentenceCount)) return false;
    for (int i = 0; i < HISTORY_LEN; i++) {
        if (s.hist[i] < 0 || s.hist[i] >= WORD_COUNT) return false;
    }
    for (uint8_t i = 0; i < Sentence::MAX_WORDS; ++i) {
        if (s.sentenceWords[i][wlp2::TEXT_MAX] != '\0' || s.sentenceSources[i][16] != '\0')
            return false;
        if (i >= s.sentenceCount && (s.sentenceWords[i][0] || s.sentenceSources[i][0])) return false;
        if (i < s.sentenceCount &&
            (!wlp2::validText(s.sentenceWords[i], strlen(s.sentenceWords[i])) ||
             (s.sentenceSources[i][0] && !wlp2::validHex(s.sentenceSources[i], 16)))) return false;
    }
    for (uint8_t i = 0; i < s.receiptCount; ++i) {
        const WordGame::ReceiptState& receipt = s.receipts[i];
        if (receipt.src[16] != '\0' || receipt.boot[16] != '\0' || receipt.req[32] != '\0' ||
            receipt.text[wlp2::TEXT_MAX] != '\0') return false;
        if (!wlp2::validHex(receipt.src, 16) || !wlp2::validHex(receipt.boot, 16) ||
            !wlp2::validHex(receipt.req, 32) ||
            !wlp2::validText(receipt.text, strlen(receipt.text)) || receipt.side > 1 ||
            receipt.completed > 1 || receipt.order == 0 || receipt.positioned > 1 ||
            receipt.position > Sentence::MAX_WORDS) return false;
    }
    Sentence sentence;
    for (uint8_t i = 0; i < s.sentenceCount; ++i) {
        if (!sentence.insert(i, s.sentenceWords[i], s.sentenceSources[i])) return false;
    }
    sentence.setCursor(s.sentenceCursor);
    Picker& picker = game.picker();
    memcpy(picker.hist, s.hist, sizeof(s.hist));
    picker.first = s.first;
    picker.cur = s.cur;
    picker.last = s.last;
    game.restoreSentence(sentence, s.confirmed != 0, s.receipts, s.receiptCount);
    return true;
}

static void freshStart() {
    game.picker().init();
    game.restore(false, "", "");
}

static void markDirty(DisplayArea area) {
    if (!dirty || area == DisplayArea::Full) dirtyArea = area;
    dirty = true;
}

static void handleButtons(uint32_t now) {
    if (game.consumeDisplayRequest()) {
        markDirty(DisplayArea::Full);
        lastActivityMs = now;
    }
    switch (inputPollButtons()) {
    case BtnEvent::CONFIRM: {
        bool wasConfirmed = game.confirmed();
        bool wasSentence = game.view() == WordGame::View::Sentence;
        WordGame::Status oldStatus = game.status();
        game.pressConfirm(now);
        game.consumeDisplayRequest();
        bool centerOnly = !wasConfirmed && game.confirmed() && !wasSentence &&
                          oldStatus == WordGame::Status::None &&
                          game.status() == WordGame::Status::None;
        markDirty(centerOnly ? DisplayArea::Center : DisplayArea::Full);
        Serial.printf("button A: confirmed=%d pending=%d status=%s\n",
                      game.confirmed(), game.transferPending(), game.statusText());
        lastActivityMs = now;
        break;
    }
    case BtnEvent::REFRESH: {
        bool centerOnly = game.view() == WordGame::View::Picker && game.confirmed() &&
                          !game.transferPending() && game.status() == WordGame::Status::None;
        game.pressRefresh(now);
        game.consumeDisplayRequest();
        centerOnly = centerOnly && game.status() == WordGame::Status::None;
        markDirty(centerOnly ? DisplayArea::Center : DisplayArea::Full);
        Serial.println("button B: refresh/cancel/clear");
        lastActivityMs = now;
        break;
    }
    default:
        break;
    }
}

static void serviceProtocolDuringDisplay(const void*) {
    delay(1);
    uint32_t now = millis();
    static uint32_t lastPumpMs = 0;
    if (now - lastPumpMs >= 5) {
        lastPumpMs = now;
        game.tick(now);
        handleButtons(now);
    }
}

static void draw(bool partial) {
    epd.epd2.setBusyCallback(serviceProtocolDuringDisplay);
    if (!epdReady) {
        displayInit();
        epdReady = true;
    }
    DisplayArea requestedArea = dirtyArea;
    dirty = false;
    dirtyArea = DisplayArea::Full;
    bool full = forceFullRefresh || !partial || partialCount >= FULL_REFRESH_EVERY;
    if (full) {
        partialCount = 0;
        forceFullRefresh = false;
    }
    uint32_t drawStartMs = millis();
    const char* status = protocolReady ? game.statusText() : "SET KIT / MEMBER IDS";
    Serial.printf("draw: full=%d view=%s cur=%s\n", full,
                  game.view() == WordGame::View::Sentence ? "sentence" : "picker",
                  game.currentWord());
    if (game.view() == WordGame::View::Sentence) {
        const char* candidate = "";
        if (node.connectedCount() == 1) {
            wlp2::Port port = node.link(wlp2::Port::Left).up() ? wlp2::Port::Left : wlp2::Port::Right;
            const WordGame::RemoteSnapshot& remote = game.remote(port);
            if (remote.valid && remote.orderValid) candidate = remote.text;
        }
        displayDrawSentence(game.sentence(), game.sentence().cursor(), game.editMode(),
                            game.insertAfter(), candidate, status, !full);
    } else {
        displayDrawWords(game.picker().prevWord(), game.currentWord(), game.picker().nextWord(),
                         game.confirmed(), !full, requestedArea, status);
    }
    Serial.printf("draw: done full=%d area=%s elapsed=%lums\n", full,
                  requestedArea == DisplayArea::Center ? "center" : "screen",
                  (unsigned long)(millis() - drawStartMs));
    if (!full) partialCount++;
    lastDrawMs = millis();
    if (game.consumeDisplayRequest()) {
        markDirty(DisplayArea::Full);
        lastActivityMs = lastDrawMs;
    }
}

static bool saveAll() {
    static PersistState s;
    memset(&s, 0, sizeof(s));
    fillState(s);
    bool ok = storageSaveNvs(s);
    if (ok) {
        nvsDirty = false;
        game.consumeSaveRequest();
    } else {
        nvsDirty = true;
        nextNvsRetryMs = millis() + 1000;
        Serial.println("NVS save failed; retry in 1000ms");
    }
    return ok;
}

static void serviceNvs(uint32_t now) {
    if (nvsDirty) {
        if (static_cast<int32_t>(now - nextNvsRetryMs) >= 0) saveAll();
    } else if (game.consumeSaveRequest()) {
        saveAll();
    }
}

static void hibernateDisplayWhenIdle() {
    if (!epdReady && !dirty) return;
    if (dirty) draw(false);
    if (dirty) return;
    if (nvsDirty && static_cast<int32_t>(millis() - nextNvsRetryMs) < 0) return;
    if (!saveAll()) return;
    if (epdReady) {
        displayHibernate();
        epdReady = false;
        forceFullRefresh = true;
        Serial.println("display: hibernate; MCU/UART awake");
    }
}

void setup() {
    Serial.begin(115200);
    neopixelWrite(PIN_RGB_LED, 0, 0, 0);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);
    inputInit();
    Serial.printf("buttons: A pressedLevel=%d B pressedLevel=%d\n",
                  inputPressedLevel(PIN_BTN_A), inputPressedLevel(PIN_BTN_B));
    bool uartReady = wlp2UartInit();
    wlp2::Identity identity;
    wlp2MakeIdentity(identity);
    bool identityReady = game.begin(identity, millis());
    protocolReady = uartReady && identityReady;
    Serial.printf("wlp2: configured=%d uart=%d identity=%d id=%s links=%u\n",
                  protocolReady, uartReady, identityReady, identity.id, node.connectedCount());
    Serial.printf("boot: reset=%d\n", esp_reset_reason());
#ifdef BOARD_HAS_PSRAM
    Serial.printf("psram: found=%d size=%u free=%u\n", psramFound(),
                  (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
#else
    Serial.println("psram: disabled");
#endif
    static PersistState s;
#if FRESH_RANDOM_ON_BOOT
    Serial.println("fresh start (test mode)");
    freshStart();
    dirty = true;
    draw(false);
#else
    if (storageLoadNvs(s) && applyState(s)) {
        Serial.printf("restored from NVS, word=%s\n", game.currentWord());
        dirty = true;
        draw(false);
    } else {
        Serial.println("fresh start");
        freshStart();
        dirty = true;
        draw(false);
    }
#endif
    Serial.println("setup done");
    lastActivityMs = millis();
}

void loop() {
    uint32_t now = millis();
    game.tick(now);
    if (game.consumeDisplayRequest()) {
        markDirty(DisplayArea::Full);
        lastActivityMs = now;
    }
    serviceNvs(now);

    static uint32_t lastBeat = 0;
    if (now - lastBeat >= 2000) {
        lastBeat = now;
        Serial.printf("alive ms=%lu dirty=%d confirmed=%d links=%u pending=%d uartErr=%lu cur=%s\n",
                      (unsigned long)now, dirty, game.confirmed(), node.connectedCount(),
                      game.transferPending(),
                      (unsigned long)wlp2UartErrors(wlp2::Port::Any), game.currentWord());
    }

    int steps = inputReadEncoderSteps();
    if (steps != 0) {
        Serial.printf("encoder steps=%d\n", steps);
        game.turnEncoder(steps, now);
        game.consumeDisplayRequest();
        markDirty(DisplayArea::Full);
        lastActivityMs = now;
    }

    handleButtons(now);
    serviceNvs(now);

    if (now - lastActivityMs >= DISPLAY_IDLE_MS) hibernateDisplayWhenIdle();
    else if (dirty && now - lastDrawMs >= DISPLAY_THROTTLE_MS) draw(true);
}
