#pragma once
#include <Arduino.h>
#include "config.h"

enum class BtnEvent { NONE, CONFIRM, REFRESH };

void inputInit();
// Net encoder steps since the last call (positive = clockwise).
// Hardware PCNT keeps counting while the display blocks.
int inputReadEncoderSteps();
BtnEvent inputPollButtons();
