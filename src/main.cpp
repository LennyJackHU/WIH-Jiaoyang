#include <Arduino.h>
#include <driver/rtc_io.h>
#include "config.h"
#include "picker.h"
#include "display.h"
#include "input.h"
#include "storage.h"

static Picker picker;
static bool confirmed = false;
static bool dirty = false; // screen content out of date
static uint32_t lastActivityMs = 0;
static uint32_t lastDrawMs = 0;
static uint32_t partialCount = 0;

static void fillState(PersistState& s) {
    s.magic = STATE_MAGIC;
    memcpy(s.hist, picker.hist, sizeof(s.hist));
    s.first = picker.first;
    s.cur = picker.cur;
    s.last = picker.last;
    s.confirmed = confirmed ? 1 : 0;
}

static bool applyState(const PersistState& s) {
    if (s.magic != STATE_MAGIC) return false;
    memcpy(picker.hist, s.hist, sizeof(s.hist));
    picker.first = s.first;
    picker.cur = s.cur;
    picker.last = s.last;
    confirmed = s.confirmed != 0;
    // sanity-clamp ring pointers and word indices before they are used
    if (picker.first >= HISTORY_LEN || picker.cur >= HISTORY_LEN ||
        picker.last >= HISTORY_LEN) return false;
    for (int i = 0; i < HISTORY_LEN; i++) {
        if (s.hist[i] < 0 || s.hist[i] >= WORD_COUNT) return false;
    }
    return true;
}

static void draw(bool partial) {
    bool full = !partial || partialCount >= FULL_REFRESH_EVERY;
    if (full) partialCount = 0;
    displayDrawWords(picker.prevWord(), picker.currentWord(), picker.nextWord(),
                     confirmed, !full);
    if (!full) partialCount++;
    lastDrawMs = millis();
    dirty = false;
}

static void saveAll() {
    PersistState s;
    fillState(s);
    rtcStateSave(s);
    storageSaveNvs(s); // NVS writes only happen here: confirm / pre-sleep
}

static void goToSleep() {
    if (dirty) draw(false);
    saveAll();

    const uint64_t wakeMask =
        (1ULL << PIN_ENC_A) | (1ULL << PIN_BTN_A) | (1ULL << PIN_BTN_B);
    // keep pull-ups alive so idle-HIGH lines can pull LOW to wake
    rtc_gpio_pullup_en((gpio_num_t)PIN_ENC_A);
    rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_A);
    rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_B);
    esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    inputInit();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    PersistState s;
    if (cause == ESP_SLEEP_WAKEUP_EXT1 && rtcStateLoad(s) && applyState(s)) {
        // Deep-sleep wake: the e-paper still shows the last frame.
        // Skip display init and redraw entirely.
        Serial.printf("wake from deep sleep, word=%s\n", picker.currentWord());
    } else if (cause != ESP_SLEEP_WAKEUP_EXT1 && storageLoadNvs(s) && applyState(s)) {
        // Power-cycle / reset: restore and repaint once (full refresh).
        Serial.printf("restored from NVS, word=%s\n", picker.currentWord());
        dirty = true;
        draw(false);
    } else {
        picker.init();
        dirty = true;
        draw(false);
    }
    lastActivityMs = millis();
}

void loop() {
    uint32_t now = millis();

    int steps = inputReadEncoderSteps();
    if (steps != 0) {
        for (int i = 0; i < abs(steps); i++) {
            if (steps > 0) picker.stepForward();
            else picker.stepBackward();
        }
        dirty = true;
        lastActivityMs = now;
    }

    switch (inputPollButtons()) {
    case BtnEvent::CONFIRM:
        confirmed = true;
        saveAll();
        dirty = true;
        lastActivityMs = now;
        break;
    case BtnEvent::REFRESH:
        if (confirmed) confirmed = false;
        else picker.refreshRandom();
        dirty = true;
        lastActivityMs = now;
        break;
    default:
        break;
    }

    if (dirty && now - lastDrawMs >= DISPLAY_THROTTLE_MS) {
        draw(true);
    }

    if (now - lastActivityMs >= SLEEP_TIMEOUT_MS) {
        goToSleep();
    }
}
