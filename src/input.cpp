#include "input.h"
#include <ESP32Encoder.h>

// Counts per detent of a typical EC11 encoder in full-quad mode.
static const int ENC_COUNTS_PER_STEP = 4;

static ESP32Encoder encoder;
static int64_t lastCount = 0;
static int stepRemainder = 0;

struct DebouncedButton {
    uint8_t pin;
    bool stable;      // pull-up: released reads HIGH
    bool lastReading;
    uint32_t lastChangeMs;

    explicit DebouncedButton(uint8_t p)
        : pin(p), stable(true), lastReading(true), lastChangeMs(0) {}
};

static DebouncedButton btnA{PIN_BTN_A};
static DebouncedButton btnB{PIN_BTN_B};

void inputInit() {
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    encoder.attachFullQuad(PIN_ENC_A, PIN_ENC_B);
    encoder.setFilter(1023);
    encoder.clearCount();
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
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
        if (!b.stable) return true; // stable LOW = pressed
    }
    return false;
}

BtnEvent inputPollButtons() {
    if (pressed(btnA)) return BtnEvent::CONFIRM;
    if (pressed(btnB)) return BtnEvent::REFRESH;
    return BtnEvent::NONE;
}
