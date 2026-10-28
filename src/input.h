#pragma once
#include <Arduino.h>
#include "config.h"

enum class BtnEvent { NONE, CONFIRM, REFRESH };

void inputInit();
// Net encoder steps since the last call (positive = clockwise).
// Hardware PCNT keeps counting while the display blocks.
int inputReadEncoderSteps();
BtnEvent inputPollButtons();

// Both buttons at their released level (polarity auto-detected at boot).
bool inputButtonsReleased();
// Level (0/1) that means "pressed" for the given button pin; used to
// configure the deep-sleep wake level.
int inputPressedLevel(uint8_t pin);
