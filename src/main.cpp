#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_random.h>
#include "config.h"
#include "display.h"
#include "input.h"
#include "storage.h"
#include "wlp2_arduino.h"
#include "word_game.h"

static wlp2::Handler initialHandler;
static UartBytePort leftUart(Serial1);
static UartBytePort rightUart(Serial2);
static wlp2::Node node(&leftUart, &rightUart, initialHandler, esp_random);
static WordGame game(node, esp_random, esp_random);
static bool protocolReady = false;
static bool dirty = false;
static DisplayArea dirtyArea = DisplayArea::Full;
static uint32_t lastActivityMs = 0;
static uint32_t lastDrawMs = 0;
static uint32_t partialCount = 0;
static bool epdReady = false;
static bool forceFullRefresh = false;
static bool ignoreButtonsUntilRelease = false;
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
    snprintf(s.leftWord, sizeof(s.leftWord), "%s", game.leftWord());
    snprintf(s.rightWord, sizeof(s.rightWord), "%s", game.rightWord());
    snprintf(s.leftSource, sizeof(s.leftSource), "%s", game.leftSource());
    snprintf(s.rightSource, sizeof(s.rightSource), "%s", game.rightSource());
    s.receiptCount = game.exportReceipts(s.receipts, 8);
}

static bool applyState(const PersistState& s) {
    if (s.magic != STATE_MAGIC || s.first >= HISTORY_LEN || s.cur >= HISTORY_LEN ||
        s.last >= HISTORY_LEN || s.confirmed > 1 || s.receiptCount > 8 ||
        s.leftWord[wlp2::TEXT_MAX] != '\0' || s.rightWord[wlp2::TEXT_MAX] != '\0' ||
        s.leftSource[16] != '\0' || s.rightSource[16] != '\0') return false;
    for (int i = 0; i < HISTORY_LEN; i++) {
        if (s.hist[i] < 0 || s.hist[i] >= WORD_COUNT) return false;
    }
    if ((s.leftWord[0] && !wlp2::validText(s.leftWord, strlen(s.leftWord))) ||
        (s.rightWord[0] && !wlp2::validText(s.rightWord, strlen(s.rightWord))) ||
        (s.leftSource[0] && !wlp2::validHex(s.leftSource, 16)) ||
        (s.rightSource[0] && !wlp2::validHex(s.rightSource, 16))) return false;
    for (uint8_t i = 0; i < s.receiptCount; ++i) {
        const WordGame::ReceiptState& receipt = s.receipts[i];
        if (receipt.src[16] != '\0' || receipt.boot[16] != '\0' || receipt.req[32] != '\0' ||
            receipt.text[wlp2::TEXT_MAX] != '\0') return false;
        if (!wlp2::validHex(receipt.src, 16) || !wlp2::validHex(receipt.boot, 16) ||
            !wlp2::validHex(receipt.req, 32) ||
            !wlp2::validText(receipt.text, strlen(receipt.text)) || receipt.side > 1 ||
            receipt.completed > 1 || receipt.order == 0) return false;
    }
    Picker& picker = game.picker();
    memcpy(picker.hist, s.hist, sizeof(s.hist));
    picker.first = s.first;
    picker.cur = s.cur;
    picker.last = s.last;
    game.restore(s.confirmed != 0, s.leftWord, s.rightWord, s.leftSource, s.rightSource,
                 s.receipts, s.receiptCount);
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
    if (game.consumeDisplayRequest()) markDirty(DisplayArea::Full);
    if (ignoreButtonsUntilRelease) {
        if (inputButtonsReleased()) ignoreButtonsUntilRelease = false;
        return;
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
        uint8_t words = 1 + (game.leftWord()[0] ? 1 : 0) + (game.rightWord()[0] ? 1 : 0);
        displayDrawSentence(game.leftWord(), game.currentWord(), game.rightWord(),
                            status, words, !full);
    } else {
        displayDrawWords(game.picker().prevWord(), game.currentWord(), game.picker().nextWord(),
                         game.confirmed(), !full, requestedArea, status);
    }
    Serial.printf("draw: done full=%d area=%s elapsed=%lums\n", full,
                  requestedArea == DisplayArea::Center ? "center" : "screen",
                  (unsigned long)(millis() - drawStartMs));
    if (!full) partialCount++;
    lastDrawMs = millis();
    if (game.consumeDisplayRequest()) markDirty(DisplayArea::Full);
}

static bool saveAll() {
    PersistState s = {};
    fillState(s);
    rtcStateSave(s);
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

static bool wakePinsIdle() {
    return inputButtonsReleased();
}

static void goToSleep() {
    if (!wakePinsIdle()) {
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 1000) {
            lastLog = millis();
            Serial.printf("sleep blocked: btnA=%d btnB=%d\n",
                          digitalRead(PIN_BTN_A), digitalRead(PIN_BTN_B));
        }
        return;
    }
    if (dirty) draw(false);
    if (dirty) return;
    if (nvsDirty && static_cast<int32_t>(millis() - nextNvsRetryMs) < 0) return;
    if (!saveAll()) return;
    if (node.connectedCount() > 0 || game.transferPending()) {
        if (epdReady) {
            displayHibernate();
            epdReady = false;
            forceFullRefresh = true;
        }
        return;
    }
    if (epdReady) displayHibernate();

    int wakeLevel = inputPressedLevel(PIN_BTN_A);
    esp_err_t err = esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_A, wakeLevel);
    if (err == ESP_OK) err = rtc_gpio_init((gpio_num_t)PIN_BTN_A);
    if (err == ESP_OK) err = rtc_gpio_set_direction((gpio_num_t)PIN_BTN_A, RTC_GPIO_MODE_INPUT_ONLY);
    if (err == ESP_OK) {
        if (wakeLevel == 0) {
            err = rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_A);
            if (err == ESP_OK) err = rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_A);
        } else {
            err = rtc_gpio_pullup_dis((gpio_num_t)PIN_BTN_A);
            if (err == ESP_OK) err = rtc_gpio_pulldown_en((gpio_num_t)PIN_BTN_A);
        }
    }
    if (err != ESP_OK) {
        Serial.printf("sleep wakeup config failed: %d\n", err);
        return;
    }
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    neopixelWrite(PIN_RGB_LED, 0, 0, 0);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool wokeFromSleep = cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1;
    inputInit(wokeFromSleep);
    Serial.printf("buttons: A pressedLevel=%d B pressedLevel=%d\n",
                  inputPressedLevel(PIN_BTN_A), inputPressedLevel(PIN_BTN_B));
    bool uartReady = wlp2UartInit();
    wlp2::Identity identity;
    wlp2MakeIdentity(identity);
    bool identityReady = game.begin(identity, millis());
    protocolReady = uartReady && identityReady;
    Serial.printf("wlp2: configured=%d uart=%d identity=%d id=%s links=%u\n",
                  protocolReady, uartReady, identityReady, identity.id, node.connectedCount());
    Serial.printf("boot: reset=%d wakeup=%d\n", esp_reset_reason(), cause);
    PersistState s;
    ignoreButtonsUntilRelease = wokeFromSleep;
#if FRESH_RANDOM_ON_BOOT
    Serial.println("fresh start (test mode)");
    freshStart();
    dirty = true;
    draw(false);
#else
    if (wokeFromSleep && rtcStateLoad(s) && applyState(s)) {
        Serial.printf("wake from deep sleep, word=%s\n", game.currentWord());
        forceFullRefresh = true;
        game.consumeDisplayRequest();
    } else if (storageLoadNvs(s) && applyState(s)) {
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
    if (game.consumeDisplayRequest()) markDirty(DisplayArea::Full);
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

    if (now - lastActivityMs >= SLEEP_IDLE_MS) goToSleep();
    else if (dirty && now - lastDrawMs >= DISPLAY_THROTTLE_MS) draw(true);
}
