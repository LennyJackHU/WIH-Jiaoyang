#pragma once

// ---- E-Paper (Waveshare 1.54" V2/V2.1, GDEH0154D67, 200x200) ----
#define PIN_EPD_BUSY 13
#define PIN_EPD_RST  14
#define PIN_EPD_DC   9
#define PIN_EPD_CS   10
#define PIN_EPD_CLK  12
#define PIN_EPD_DIN  11

// ---- Rotary encoder ----
#define PIN_ENC_A 5   // CLK, RTC GPIO, deep-sleep wake source
#define PIN_ENC_B 6   // DT
#define PIN_ENC_SW 4  // push switch, reserved (currently unused)

// ---- Buttons (polarity auto-detected per pin at boot) ----
#define PIN_BTN_A 7   // confirm
#define PIN_BTN_B 8   // refresh / cancel

// ---- Onboard RGB LED ----
#define PIN_RGB_LED 38

// ---- Behaviour ----
#define DISPLAY_THROTTLE_MS   400   // min interval between screen updates
#define FULL_REFRESH_EVERY    20    // full refresh after N partial updates
#define SLEEP_IDLE_MS         400   // no input and screen clean -> deep sleep
#define BTN_DEBOUNCE_MS       30
#define HISTORY_LEN           32    // ring size of recent word history

// Test switch: 1 = ignore saved state on boot, always start with fresh
// random words (verifies RNG + word list). Set to 0 for normal behavior.
#define FRESH_RANDOM_ON_BOOT  0
