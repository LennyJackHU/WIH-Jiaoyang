#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include "config.h"
#include "sentence.h"

// Waveshare 1.54" V2/V2.1 module (GDEH0154D67, SSD1681), 200x200 B/W.
// Full 200x200 frame buffer = 5000 bytes RAM, no paging needed.
extern GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> epd;

// Call once before the first draw of a boot cycle. The controller then
// stays awake so repeated partial refreshes do not flash.
void displayInit();

enum class DisplayArea : uint8_t { Full, Center };

// Draws the three-line layout. partial=true uses partial refresh (fast,
// no flicker); partial=false does a full refresh (clears ghosting).
// Center limits a partial refresh to the selection band; Full redraws all lines.
// confirmed inverts the center band to signal the confirmed selection.
void displayDrawWords(const char* prev, const char* cur, const char* next,
                      bool confirmed, bool partial, DisplayArea area,
                      const char* status = nullptr);

void displayDrawSentence(const Sentence& sentence, uint8_t cursor, SentenceEdit edit,
                         bool insertAfter, const char* candidate, const char* status,
                         bool partial);

// Puts the panel into deep sleep. The next refresh requires displayInit().
void displayHibernate();
