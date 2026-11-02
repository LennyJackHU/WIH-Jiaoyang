#include "input.h"
#include <ESP32Encoder.h>

// Counts per detent of a typical EC11 encoder in full-quad mode.
static const int ENC_COUNTS_PER_STEP = 4;
static const uint32_t BTN_LONG_MS = 800;

static ESP32Encoder encoder;
static int64_t lastCount = 0;
static int stepRemainder = 0;

struct DebouncedButton {
    uint8_t pin;
    bool activeLow;
    bool stable;
    bool lastReading;
    uint32_t lastChangeMs;
};

static DebouncedButton btnA;
static DebouncedButton btnB;
static DebouncedButton btnSW;

static void buttonConfigure(DebouncedButton& b, uint8_t pin, bool activeLow) {
    b.pin = pin;
    b.activeLow = activeLow;
    b.lastChangeMs = 0;
    pinMode(pin, activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    b.stable = b.lastReading = digitalRead(pin);
}

static void buttonInit(DebouncedButton& b, uint8_t pin) {
    b.pin = pin;
    b.lastChangeMs = 0;
    pinMode(pin, INPUT_PULLUP);
    int highs = 0, samples = 0;
    uint32_t start = millis();
    while (millis() - start < 60) {
        if (digitalRead(pin) == HIGH) highs++;
        samples++;
        delay(2);
    }
    buttonConfigure(b, pin, highs * 2 >= samples);
}

static bool pressedLevel(const DebouncedButton& b) {
    return b.activeLow ? LOW : HIGH;
}

static bool isPressed(const DebouncedButton& b) {
    return digitalRead(b.pin) == pressedLevel(b);
}

static bool allButtonsReleased() {
    return !isPressed(btnA) && !isPressed(btnB) && !isPressed(btnSW);
}

void inputInit() {
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    encoder.attachFullQuad(PIN_ENC_A, PIN_ENC_B);
    encoder.setFilter(1023);
    encoder.clearCount();
    lastCount = 0;
    stepRemainder = 0;
    buttonConfigure(btnSW, PIN_ENC_SW, true);
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
        Serial.printf("key pin=%u %s\n", b.pin,
                      b.stable == pressedLevel(b) ? "down" : "up");
        if (b.stable == pressedLevel(b)) return true;
    }
    return false;
}

static BtnEvent pendingSingle = BtnEvent::NONE;
static uint32_t pendingSingleMs = 0;
static bool chordLatched = false;

BtnEvent inputPollButtons() {
    uint32_t now = millis();
    bool aPressed = pressed(btnA);
    bool bPressed = pressed(btnB);
    bool swPressed = pressed(btnSW);

    if (chordLatched) {
        if (allButtonsReleased()) chordLatched = false;
        return BtnEvent::NONE;
    }
    if (swPressed) {
        pendingSingle = BtnEvent::NONE;
        return BtnEvent::EDIT;
    }
    if (aPressed && bPressed) {
        pendingSingle = BtnEvent::NONE;
        chordLatched = true;
        return BtnEvent::EDIT;
    }
    if (aPressed || bPressed) {
        if (pendingSingle != BtnEvent::NONE) {
            pendingSingle = BtnEvent::NONE;
            chordLatched = true;
            return BtnEvent::EDIT;
        }
        pendingSingle = aPressed ? BtnEvent::CONFIRM : BtnEvent::REFRESH;
        pendingSingleMs = now;
        return BtnEvent::NONE;
    }

    DebouncedButton& button = pendingSingle == BtnEvent::CONFIRM ? btnA : btnB;
    if (pendingSingle == BtnEvent::CONFIRM || pendingSingle == BtnEvent::REFRESH) {
        if (button.stable != pressedLevel(button)) {
            BtnEvent event = pendingSingle;
            pendingSingle = BtnEvent::NONE;
            return event;
        }
        if (now - pendingSingleMs >= BTN_LONG_MS) {
            BtnEvent event = pendingSingle == BtnEvent::CONFIRM ?
                BtnEvent::LONG_CONFIRM : BtnEvent::LONG_REFRESH;
            pendingSingle = BtnEvent::NONE;
            chordLatched = true;
            return event;
        }
    }
    return BtnEvent::NONE;
}

int inputPressedLevel(uint8_t pin) {
    if (pin == PIN_ENC_SW) return pressedLevel(btnSW);
    return pin == PIN_BTN_A ? pressedLevel(btnA) : pressedLevel(btnB);
}
