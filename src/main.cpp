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
static bool epdReady = false;        // displayInit() done since boot
static bool forceFullRefresh = false; // first draw after deep-sleep wake
static bool ignoreButtonsUntilRelease = false; // swallow the wake-up press

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
    if (!epdReady) {
        displayInit();
        epdReady = true;
    }
    // After deep sleep the controller lost its partial-refresh base frame,
    // so the first draw must be a full refresh (GxEPD2 recommended practice).
    bool full = forceFullRefresh || !partial || partialCount >= FULL_REFRESH_EVERY;
    if (full) {
        partialCount = 0;
        forceFullRefresh = false;
    }
    Serial.printf("draw: full=%d cur=%s\n", full, picker.currentWord());
    displayDrawWords(picker.prevWord(), picker.currentWord(), picker.nextWord(),
                     confirmed, !full);
    Serial.println("draw: done");
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

static bool wakePinsIdle() {
    return inputButtonsReleased(); // polarity auto-detected at boot
}

static void goToSleep() {
    if (!wakePinsIdle()) {
        // Level-triggered wake: sleeping while a wake pin is at its active
        // level would re-wake instantly. Wait for release instead.
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 1000) {
            lastLog = millis();
            Serial.printf("sleep blocked: btnA=%d btnB=%d\n",
                          digitalRead(PIN_BTN_A), digitalRead(PIN_BTN_B));
        }
        return;
    }
    if (dirty) draw(false);
    saveAll();
    if (epdReady) displayHibernate(); // panel sleeps only when the MCU sleeps

    // Wake on button A only (present on every board). The encoder is not a
    // wake source: its line can sit at an arbitrary level mid-detent and
    // cause instant re-wakes. Wake level follows the detected polarity.
    int wakeLevel = inputPressedLevel(PIN_BTN_A);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_A, wakeLevel);
    rtc_gpio_init((gpio_num_t)PIN_BTN_A);
    rtc_gpio_set_direction((gpio_num_t)PIN_BTN_A, RTC_GPIO_MODE_INPUT_ONLY);
    if (wakeLevel == 0) {
        rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_A);
        rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_A);
    } else {
        rtc_gpio_pullup_dis((gpio_num_t)PIN_BTN_A);
        rtc_gpio_pulldown_en((gpio_num_t)PIN_BTN_A);
    }
    Serial.flush();
    delay(20); // let RTC pulls settle before the power-domain switch
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    neopixelWrite(PIN_RGB_LED, 0, 0, 0); // onboard RGB LED off
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10); // wait for USB CDC host
    inputInit();
    Serial.printf("buttons: A pressedLevel=%d B pressedLevel=%d\n",
                  inputPressedLevel(PIN_BTN_A), inputPressedLevel(PIN_BTN_B));

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("boot: reset=%d wakeup=%d\n", esp_reset_reason(), cause);
    PersistState s;
#if FRESH_RANDOM_ON_BOOT
    // test mode: skip restore, always randomize
    Serial.println("fresh start (test mode)");
    picker.init();
    dirty = true;
    draw(false);
#else
    bool wokeFromSleep = cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1;
    if (wokeFromSleep && rtcStateLoad(s) && applyState(s)) {
        // Deep-sleep wake: the e-paper still shows the last frame.
        // Skip display init and redraw entirely.
        Serial.printf("wake from deep sleep, word=%s\n", picker.currentWord());
        forceFullRefresh = true;
        ignoreButtonsUntilRelease = true; // the press that woke us is not an action
    } else if (!wokeFromSleep && storageLoadNvs(s) && applyState(s)) {
        // Power-cycle / reset: restore and repaint once (full refresh).
        Serial.printf("restored from NVS, word=%s\n", picker.currentWord());
        dirty = true;
        draw(false);
    } else {
        Serial.println("fresh start");
        picker.init();
        dirty = true;
        draw(false);
    }
#endif
    Serial.println("setup done");
    lastActivityMs = millis();
}

void loop() {
    uint32_t now = millis();

    static uint32_t lastBeat = 0;
    if (now - lastBeat >= 2000) {
        lastBeat = now;
        Serial.printf("alive ms=%lu dirty=%d confirmed=%d A=%d B=%d cur=%s\n",
                      (unsigned long)now, dirty, confirmed,
                      digitalRead(PIN_BTN_A), digitalRead(PIN_BTN_B),
                      picker.currentWord());
    }

    int steps = inputReadEncoderSteps();
    if (steps != 0) {
        Serial.printf("encoder steps=%d\n", steps);
        confirmed = false;
        for (int i = 0; i < abs(steps); i++) {
            // direction flipped to match the physical knob feel
            if (steps > 0) picker.stepBackward();
            else picker.stepForward();
        }
        dirty = true;
        lastActivityMs = now;
    }

    if (ignoreButtonsUntilRelease) {
        if (inputButtonsReleased()) {
            ignoreButtonsUntilRelease = false;
        }
    } else {
    switch (inputPollButtons()) {
    case BtnEvent::CONFIRM:
        if (confirmed) {
            // Second press on an already-selected word: send it to the
            // nearby module. Transmission is intentionally not implemented
            // yet; this is only the hook for it.
            Serial.println("button A again: transmit to nearby module (TODO)");
        } else {
            Serial.println("button A confirmed");
            confirmed = true;
            saveAll();
            dirty = true;
        }
        lastActivityMs = now;
        break;
    case BtnEvent::REFRESH:
        Serial.println("button B refresh/cancel");
        if (confirmed) confirmed = false;
        else picker.refreshRandom();
        dirty = true;
        lastActivityMs = now;
        break;
    default:
        break;
    }
    }

    if (dirty && now - lastDrawMs >= DISPLAY_THROTTLE_MS) {
        draw(true);
    }

    if (!dirty && now - lastActivityMs >= SLEEP_IDLE_MS) {
        goToSleep(); // nothing to show and no input: power down immediately
    }
}
