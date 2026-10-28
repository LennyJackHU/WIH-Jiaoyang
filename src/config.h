#pragma once

// ---- E-Paper (Waveshare 1.54" V2/V2.1, GDEH0154D67, 200x200) ----
#define PIN_EPD_BUSY 13
#define PIN_EPD_RST  14
#define PIN_EPD_DC   9
#define PIN_EPD_CS   10
#define PIN_EPD_CLK  12
#define PIN_EPD_DIN  11

// ---- Rotary encoder ----
#define PIN_ENC_A 5   // CLK, PCNT input; not a wake source
#define PIN_ENC_B 6   // DT
#define PIN_ENC_SW 4  // push switch, reserved (currently unused)

// ---- Buttons (polarity auto-detected per pin at boot) ----
#define PIN_BTN_A 7   // confirm / transfer
#define PIN_BTN_B 8   // refresh / cancel / clear sentence

// ---- Onboard RGB LED (v1.1=38, v1.0=48; override for your board) ----
#ifndef PIN_RGB_LED
#define PIN_RGB_LED 38
#endif

// ---- WLP2 neighbor UARTs ----
#define PIN_UART_L_TX 17
#define PIN_UART_L_RX 18
#define PIN_UART_R_TX 15
#define PIN_UART_R_RX 16

// Replace these provisioning placeholders with the actual kit member IDs.
#ifndef WIH_KIT_ID
#define WIH_KIT_ID "9d31f2a84c07e6b15a890d42c3f671e0"
#endif
#ifndef WIH_RECORDER_ID
#define WIH_RECORDER_ID "0000000000000003"
#endif
#ifndef WIH_MEMBER_1_ID
#define WIH_MEMBER_1_ID "0000000000000001"
#endif
#ifndef WIH_MEMBER_2_ID
#define WIH_MEMBER_2_ID "0000000000000002"
#endif
#ifndef WIH_MEMBER_3_ID
#define WIH_MEMBER_3_ID "0000000000000003"
#endif

// ---- Behaviour ----
#define DISPLAY_THROTTLE_MS   50    // extra delay after the BUSY-limited refresh
#define FULL_REFRESH_EVERY    20    // full refresh after N partial updates
#define SLEEP_IDLE_MS         5000  // keep partial-refresh state during interaction bursts
#define BTN_DEBOUNCE_MS       30
#define HISTORY_LEN           32    // ring size of recent word history

// Test switch: 1 = ignore saved state on boot, always start with fresh
// random words (verifies RNG + word list). Set to 0 for normal behavior.
#define FRESH_RANDOM_ON_BOOT  0
