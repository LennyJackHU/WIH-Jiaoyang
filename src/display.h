#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include "config.h"

// Waveshare 1.54" V2/V2.1 module (GDEH0154D67, SSD1681), 200x200 B/W.
// Full 200x200 frame buffer = 5000 bytes RAM, no paging needed.
extern GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> epd;

// Draws the three-line layout. Initializes SPI and the controller on
// demand (required after hibernate), so it is safe to call any time.
// partial=true uses partial refresh (fast, no flicker); partial=false
// does a full refresh (clears ghosting). The panel is sent back to
// sleep after every refresh to avoid holding it at high voltage.
// confirmed inverts the center band to signal the confirmed selection.
void displayDrawWords(const char* prev, const char* cur, const char* next,
                      bool confirmed, bool partial);
