#include "display.h"
#include <SPI.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> epd(
    GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

static bool spiBegun = false;

static const int16_t SCREEN_W = 200;
static const int16_t SCREEN_H = 200;
static const int16_t BAND_Y = 69;     // selection band, vertically centered
static const int16_t BAND_H = 62;
static const int16_t CENTER_MAX_W = SCREEN_W - 52; // keep clear of the <> markers

void displayInit() {
    if (!spiBegun) {
        SPI.begin(PIN_EPD_CLK, -1, PIN_EPD_DIN, PIN_EPD_CS);
        spiBegun = true;
    }
    epd.init(115200);
    epd.setRotation(0);
    epd.setTextColor(GxEPD_BLACK);
}

static uint16_t textWidth(const GFXfont* font, const char* word) {
    epd.setFont(font);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    epd.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
    return tbw;
}

// Draws word centered horizontally on the screen and vertically inside
// [yTop, yBottom], using the glyph's real bounds (works for any font size).
static void drawCentered(const char* word, const GFXfont* font,
                         int16_t yTop, int16_t yBottom, uint16_t color) {
    if (!word || !word[0]) return;
    epd.setFont(font);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    epd.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
    int16_t x = (SCREEN_W - tbw) / 2 - tbx;
    int16_t baseline = yTop + (yBottom - yTop) / 2 - tby - (int16_t)tbh / 2;
    epd.setCursor(x, baseline);
    epd.setTextColor(color);
    epd.print(word);
    epd.setTextColor(GxEPD_BLACK);
}

static const GFXfont* fitCenterFont(const char* word) {
    static const GFXfont* const tiers[] = {
        &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSans12pt7b,
    };
    for (const GFXfont* f : tiers) {
        if (textWidth(f, word) <= CENTER_MAX_W) return f;
    }
    return &FreeSans12pt7b;
}

static void render(const char* prev, const char* cur, const char* next, bool confirmed) {
    epd.fillScreen(GxEPD_WHITE);

    drawCentered(prev, &FreeSans12pt7b, 0, BAND_Y, GxEPD_BLACK);
    drawCentered(next, &FreeSans12pt7b, BAND_Y + BAND_H, SCREEN_H, GxEPD_BLACK);

    const GFXfont* centerFont = fitCenterFont(cur);

    if (confirmed) {
        epd.fillRect(0, BAND_Y, SCREEN_W, BAND_H, GxEPD_BLACK);
        drawCentered(cur, centerFont, BAND_Y, BAND_Y + BAND_H, GxEPD_WHITE);
    } else {
        epd.drawRect(4, BAND_Y, SCREEN_W - 8, BAND_H, GxEPD_BLACK);
        // ">" and "<" markers flanking the selection band
        const int16_t cy = BAND_Y + BAND_H / 2;
        epd.fillTriangle(10, cy - 8, 10, cy + 8, 20, cy, GxEPD_BLACK);
        epd.fillTriangle(SCREEN_W - 10, cy - 8, SCREEN_W - 10, cy + 8,
                         SCREEN_W - 20, cy, GxEPD_BLACK);
        drawCentered(cur, centerFont, BAND_Y, BAND_Y + BAND_H, GxEPD_BLACK);
    }
}

void displayDrawWords(const char* prev, const char* cur, const char* next,
                      bool confirmed, bool partial) {
    if (partial) {
        epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    } else {
        epd.setFullWindow();
    }
    epd.firstPage();
    do {
        render(prev, cur, next, confirmed);
    } while (epd.nextPage());
}

void displayHibernate() {
    epd.hibernate();
}
