#include "input.h"
#include <ESP32Encoder.h>

// Counts per detent of a typical EC11 encoder in full-quad mode.
static const int ENC_COUNTS_PER_STEP = 4;

static ESP32Encoder encoder;
static int64_t lastCount = 0;
static int stepRemainder = 0;

struct DebouncedButton {
    uint8_t pin;
    bool activeLow;   // detected at boot: idle HIGH -> active low
    bool stable;
    bool lastReading;
    uint32_t lastChangeMs;
};

static DebouncedButton btnA;
static DebouncedButton btnB;

static void buttonInit(DebouncedButton& b, uint8_t pin) {
    b.pin = pin;
    b.lastChangeMs = 0;
    // Polarity detection (assumes the button is not held at boot).
    // Sample for 60 ms: button modules can produce a brief transient
    // right after power-on, so a single early read is not reliable.
    //  - mostly HIGH with pull-up: bare button to GND / module pull-up
    //    -> active low, keep the pull-up
    //  - mostly LOW: module with pull-down / button to 3V3
    //    -> active high, use a pull-down so the press can be seen
    pinMode(pin, INPUT_PULLUP);
    int highs = 0, samples = 0;
    uint32_t start = millis();
    while (millis() - start < 60) {
        if (digitalRead(pin) == HIGH) highs++;
        samples++;
        delay(2);
    }
    b.activeLow = highs * 2 >= samples;
    pinMode(pin, b.activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    b.stable = b.lastReading = digitalRead(pin);
}

static bool pressedLevel(const DebouncedButton& b) {
    return b.activeLow ? LOW : HIGH;
}

static bool isPressed(const DebouncedButton& b) {
    return digitalRead(b.pin) == pressedLevel(b);
}

void inputInit() {
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    encoder.attachFullQuad(PIN_ENC_A, PIN_ENC_B);
    encoder.setFilter(1023);
    encoder.clearCount();
    buttonInit(btnA, PIN_BTN_A);
    buttonInit(btnB, PIN_BTN_B);
}

int inputReadEncoderSteps() {
    int64_t count = encoder.getCount();
    int delta = (int)(count - lastCount);
    if (delta == 0) return 0;
    lastCount = count;

    int total = delta + stepRemainder;
    int steps = total / ENC_COUNTS_PER_STEP;
    stepRemainder = total % ENC_COUNTS_PER_STEP;
    return steps;
}

static bool pressed(DebouncedButton& b) {
    bool reading = digitalRead(b.pin);
    if (reading != b.lastReading) {
        b.lastReading = reading;
        b.lastChangeMs = millis();
    }
    if (reading != b.stable && millis() - b.lastChangeMs >= BTN_DEBOUNCE_MS) {
        b.stable = reading;
        if (b.stable == pressedLevel(b)) return true;
    }
    return false;
}

BtnEvent inputPollButtons() {
    if (pressed(btnA)) return BtnEvent::CONFIRM;
    if (pressed(btnB)) return BtnEvent::REFRESH;
    return BtnEvent::NONE;
}

bool inputButtonsReleased() {
    return !isPressed(btnA) && !isPressed(btnB);
}

int inputPressedLevel(uint8_t pin) {
    return pin == PIN_BTN_A ? pressedLevel(btnA) : pressedLevel(btnB);
}
