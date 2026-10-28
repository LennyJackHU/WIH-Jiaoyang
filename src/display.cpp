#include "display.h"
#include <SPI.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "wlp2_json.h"

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> epd(
    GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

static bool spiBegun = false;

static const int16_t SCREEN_W = 200;
static const int16_t SCREEN_H = 200;
static const int16_t BAND_Y = 69;
static const int16_t BAND_H = 62;
static const int16_t CENTER_MAX_W = SCREEN_W - 52;
static const int16_t SENTENCE_X = 10;
static const int16_t SENTENCE_W = 180;

void displayInit() {
    if (!spiBegun) {
        SPI.begin(PIN_EPD_CLK, -1, PIN_EPD_DIN, PIN_EPD_CS);
        spiBegun = true;
    }
    epd.init(115200, true, 2, false);
    epd.setRotation(0);
    epd.setTextColor(GxEPD_BLACK);
    epd.setTextSize(1);
}

static uint16_t textWidth(const GFXfont* font, const char* word) {
    epd.setFont(font);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    epd.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
    return tbw;
}

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

static void drawHeader(const char* status, uint8_t participants) {
    epd.setFont(nullptr);
    epd.setTextSize(1);
    epd.setTextColor(GxEPD_BLACK);
    epd.setCursor(6, 5);
    epd.print(status && status[0] ? status : "SENTENCE");
    if (participants) {
        char count[8];
        snprintf(count, sizeof(count), "%u/3", participants);
        epd.setCursor(174, 5);
        epd.print(count);
    }
}

static void renderWords(const char* prev, const char* cur, const char* next,
                        bool confirmed, bool centerOnly, const char* status) {
    epd.fillScreen(GxEPD_WHITE);
    if (!centerOnly) {
        if (status && status[0]) drawHeader(status, 0);
        drawCentered(prev, &FreeSans12pt7b, 15, BAND_Y, GxEPD_BLACK);
        drawCentered(next, &FreeSans12pt7b, BAND_Y + BAND_H, SCREEN_H, GxEPD_BLACK);
    }

    const GFXfont* centerFont = fitCenterFont(cur);
    if (confirmed) {
        epd.fillRect(0, BAND_Y, SCREEN_W, BAND_H, GxEPD_BLACK);
        drawCentered(cur, centerFont, BAND_Y, BAND_Y + BAND_H, GxEPD_WHITE);
    } else {
        epd.drawRect(4, BAND_Y, SCREEN_W - 8, BAND_H, GxEPD_BLACK);
        const int16_t cy = BAND_Y + BAND_H / 2;
        epd.fillTriangle(10, cy - 8, 10, cy + 8, 20, cy, GxEPD_BLACK);
        epd.fillTriangle(SCREEN_W - 10, cy - 8, SCREEN_W - 10, cy + 8,
                         SCREEN_W - 20, cy, GxEPD_BLACK);
        drawCentered(cur, centerFont, BAND_Y, BAND_Y + BAND_H, GxEPD_BLACK);
    }
}

static void fitWord(const GFXfont* font, const char* word, char* out, size_t capacity) {
    snprintf(out, capacity, "%s", word);
    if (textWidth(font, out) <= SENTENCE_W) return;
    size_t len = strlen(out);
    while (len > 0) {
        --len;
        while (len > 0 && (static_cast<unsigned char>(out[len]) & 0xC0) == 0x80) --len;
        out[len] = '\0';
        char candidate[wlp2::TEXT_MAX + 4];
        snprintf(candidate, sizeof(candidate), "%s...", out);
        if (textWidth(font, candidate) <= SENTENCE_W) {
            snprintf(out, capacity, "%s", candidate);
            return;
        }
    }
    snprintf(out, capacity, ".");
}

static uint8_t flowLines(const GFXfont* font, const char** words, uint8_t count) {
    uint8_t lines = 1;
    int16_t x = 0;
    uint16_t space = textWidth(font, " ");
    for (uint8_t i = 0; i < count; ++i) {
        char fitted[wlp2::TEXT_MAX + 4];
        fitWord(font, words[i], fitted, sizeof(fitted));
        uint16_t width = textWidth(font, fitted);
        if (x > 0 && x + space + width > SENTENCE_W) {
            ++lines;
            x = 0;
        }
        if (x > 0) x += space;
        x += width;
    }
    return lines;
}

static void renderSentence(const char* left, const char* own, const char* right,
                           const char* status, uint8_t participants) {
    epd.fillScreen(GxEPD_WHITE);
    drawHeader(status, participants);
    epd.drawRect(4, 28, 192, 136, GxEPD_BLACK);
    const char* words[3];
    bool local[3];
    uint8_t count = 0;
    if (left && left[0]) {
        words[count] = left;
        local[count++] = false;
    }
    words[count] = own;
    local[count++] = true;
    if (right && right[0]) {
        words[count] = right;
        local[count++] = false;
    }

    const GFXfont* font = &FreeSans12pt7b;
    int16_t lineHeight = 29;
    uint8_t lines = flowLines(font, words, count);
    bool fits = true;
    for (uint8_t i = 0; i < count; ++i) {
        if (textWidth(font, words[i]) > SENTENCE_W) fits = false;
    }
    if (!fits || lines > 4) {
        font = &FreeSans9pt7b;
        lineHeight = 23;
        lines = flowLines(font, words, count);
    }
    if (lines > 5) {
        font = nullptr;
        lineHeight = 15;
        lines = flowLines(font, words, count);
    }
    int16_t y = 30 + (132 - lines * lineHeight) / 2;
    int16_t x = 0;
    uint16_t space = textWidth(font, " ");
    epd.setFont(font);
    for (uint8_t i = 0; i < count; ++i) {
        char fitted[wlp2::TEXT_MAX + 4];
        fitWord(font, words[i], fitted, sizeof(fitted));
        int16_t tbx, tby;
        uint16_t width, height;
        epd.getTextBounds(fitted, 0, 0, &tbx, &tby, &width, &height);
        if (x > 0 && x + space + width > SENTENCE_W) {
            x = 0;
            y += lineHeight;
        }
        if (x > 0) x += space;
        int16_t baseline = y + (lineHeight - height) / 2 - tby;
        if (local[i]) {
            epd.fillRect(SENTENCE_X + x - 3, baseline + tby - 3,
                         width + 6, height + 6, GxEPD_BLACK);
        }
        epd.setCursor(SENTENCE_X + x - tbx, baseline);
        epd.setTextColor(local[i] ? GxEPD_WHITE : GxEPD_BLACK);
        epd.print(fitted);
        epd.setTextColor(GxEPD_BLACK);
        x += width;
    }
    epd.setFont(nullptr);
    epd.setCursor(45, 178);
    epd.print("A:SELECT  B:CLEAR");
    epd.fillTriangle(8, 183, 8, 191, 16, 187, GxEPD_BLACK);
    epd.fillTriangle(192, 183, 192, 191, 184, 187, GxEPD_BLACK);
}

void displayDrawWords(const char* prev, const char* cur, const char* next,
                      bool confirmed, bool partial, DisplayArea area, const char* status) {
    bool centerOnly = partial && area == DisplayArea::Center;
    if (centerOnly) epd.setPartialWindow(0, BAND_Y, SCREEN_W, BAND_H);
    else if (partial) epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    else epd.setFullWindow();
    epd.firstPage();
    do {
        renderWords(prev, cur, next, confirmed, centerOnly, status);
    } while (epd.nextPage());
}

void displayDrawSentence(const char* left, const char* own, const char* right,
                         const char* status, uint8_t participants, bool partial) {
    if (partial) epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    else epd.setFullWindow();
    epd.firstPage();
    do {
        renderSentence(left, own, right, status, participants);
    } while (epd.nextPage());
}

void displayHibernate() {
    epd.hibernate();
}
