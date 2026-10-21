#include "display.h"
#include <SPI.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> epd(
    GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

static bool spiBegun = false;

static const int16_t SCREEN_W = 200;
static const int16_t SCREEN_H = 200;
static const int16_t Y_TOP = 52;      // baseline of the previous word
static const int16_t Y_CENTER = 122;  // baseline of the current word
static const int16_t Y_BOTTOM = 184;  // baseline of the next word
static const int16_t BAND_Y = 80;     // selection band around the center
static const int16_t BAND_H = 62;

static void ensureControllerReady() {
    if (!spiBegun) {
        SPI.begin(PIN_EPD_CLK, -1, PIN_EPD_DIN, PIN_EPD_CS);
        spiBegun = true;
    }
    // The controller sleeps after every refresh (hibernate), so it must
    // be re-initialized before each update.
    epd.init(115200);
    epd.setRotation(0);
    epd.setTextColor(GxEPD_BLACK);
}

static void drawCentered(const char* word, const GFXfont* font, int16_t baseline,
                         uint16_t color) {
    if (!word || !word[0]) return;
    epd.setFont(font);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    epd.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
    epd.setCursor((SCREEN_W - tbw) / 2 - tbx, baseline);
    epd.setTextColor(color);
    epd.print(word);
    epd.setTextColor(GxEPD_BLACK);
}

// Width of a word at the center font; used to fall back when too wide.
static uint16_t centerWidth(const char* word, const GFXfont* font) {
    epd.setFont(font);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    epd.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
    return tbw;
}

static void render(const char* prev, const char* cur, const char* next, bool confirmed) {
    epd.fillScreen(GxEPD_WHITE);

    drawCentered(prev, &FreeSans12pt7b, Y_TOP, GxEPD_BLACK);
    drawCentered(next, &FreeSans12pt7b, Y_BOTTOM, GxEPD_BLACK);

    // Long words fall back to the smaller font so they never clip.
    const GFXfont* centerFont = &FreeSansBold18pt7b;
    if (centerWidth(cur, centerFont) > SCREEN_W - 24) {
        centerFont = &FreeSans12pt7b;
    }

    if (confirmed) {
        epd.fillRect(0, BAND_Y, SCREEN_W, BAND_H, GxEPD_BLACK);
        drawCentered(cur, centerFont, Y_CENTER, GxEPD_WHITE);
    } else {
        epd.drawRect(4, BAND_Y, SCREEN_W - 8, BAND_H, GxEPD_BLACK);
        // ">" and "<" markers flanking the selection band
        epd.fillTriangle(10, BAND_Y + BAND_H / 2 - 8, 10, BAND_Y + BAND_H / 2 + 8,
                         20, BAND_Y + BAND_H / 2, GxEPD_BLACK);
        epd.fillTriangle(SCREEN_W - 10, BAND_Y + BAND_H / 2 - 8,
                         SCREEN_W - 10, BAND_Y + BAND_H / 2 + 8,
                         SCREEN_W - 20, BAND_Y + BAND_H / 2, GxEPD_BLACK);
        drawCentered(cur, centerFont, Y_CENTER, GxEPD_BLACK);
    }
}

void displayDrawWords(const char* prev, const char* cur, const char* next,
                      bool confirmed, bool partial) {
    ensureControllerReady();
    if (partial) {
        epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    } else {
        epd.setFullWindow();
    }
    epd.firstPage();
    do {
        render(prev, cur, next, confirmed);
    } while (epd.nextPage());
    epd.hibernate();
}
