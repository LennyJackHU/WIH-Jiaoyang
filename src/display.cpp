#include "display.h"
#include <SPI.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifItalic18pt7b.h>
#include "wlp2_json.h"

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> epd(
    GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

static bool spiBegun = false;

static const int16_t SCREEN_W = 200;
static const int16_t SCREEN_H = 200;
static const int16_t PAPER_X = 24;
static const int16_t PAPER_Y = 74;
static const int16_t PAPER_W = 152;
static const int16_t PAPER_H = 62;

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

static uint16_t textWidth(const GFXfont* font, const char* text) {
    if (!text || !text[0]) return 0;
    epd.setFont(font);
    int16_t x, y;
    uint16_t w, h;
    epd.getTextBounds(text, 0, 0, &x, &y, &w, &h);
    return w;
}

static void fitText(const GFXfont* font, const char* text, int16_t maxWidth,
                    char* out, size_t capacity) {
    if (!text) {
        out[0] = '\0';
        return;
    }
    snprintf(out, capacity, "%s", text);
    if (textWidth(font, out) <= maxWidth) return;
    size_t len = strlen(out);
    while (len > 0) {
        --len;
        while (len > 0 && (static_cast<unsigned char>(out[len]) & 0xC0) == 0x80) --len;
        out[len] = '\0';
        char candidate[wlp2::TEXT_MAX + 4];
        snprintf(candidate, sizeof(candidate), "%s...", out);
        if (textWidth(font, candidate) <= maxWidth) {
            snprintf(out, capacity, "%s", candidate);
            return;
        }
    }
    snprintf(out, capacity, ".");
}

static void drawCenteredIn(const char* value, const GFXfont* font,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t color) {
    if (!value || !value[0]) return;
    char fitted[wlp2::TEXT_MAX + 4];
    fitText(font, value, w - 8, fitted, sizeof(fitted));
    epd.setFont(font);
    int16_t bx, by;
    uint16_t bw, bh;
    epd.getTextBounds(fitted, 0, 0, &bx, &by, &bw, &bh);
    epd.setCursor(x + (w - bw) / 2 - bx, y + (h - bh) / 2 - by);
    epd.setTextColor(color);
    epd.print(fitted);
    epd.setTextColor(GxEPD_BLACK);
}

static const GFXfont* wordFont(const char* word, bool large, int16_t maxWidth,
                               char* fitted, size_t capacity) {
    if (!word || !word[0]) {
        fitted[0] = '\0';
        return &FreeSans9pt7b;
    }
    const bool phrase = strchr(word, ' ') != nullptr;
    const size_t length = strlen(word);
    const GFXfont* candidates[5];
    uint8_t count = 0;
    if (phrase) {
        candidates[count++] = large ? &FreeSansBold12pt7b : &FreeSans12pt7b;
        candidates[count++] = &FreeSans9pt7b;
    } else if (length <= 5) {
        candidates[count++] = large ? &FreeSansBold18pt7b : &FreeSansBold12pt7b;
        candidates[count++] = &FreeSans12pt7b;
        candidates[count++] = &FreeSans9pt7b;
    } else if (length <= 8) {
        candidates[count++] = large ? &FreeSerifItalic18pt7b : &FreeSerifItalic12pt7b;
        candidates[count++] = &FreeSerif12pt7b;
        candidates[count++] = &FreeSans9pt7b;
    } else {
        candidates[count++] = large ? &FreeSerif18pt7b : &FreeSerif12pt7b;
        candidates[count++] = &FreeSerifItalic12pt7b;
        candidates[count++] = &FreeSans9pt7b;
    }
    candidates[count++] = nullptr;
    for (uint8_t i = 0; i < count; ++i) {
        fitText(candidates[i], word, maxWidth, fitted, capacity);
        if (textWidth(candidates[i], fitted) <= maxWidth && strcmp(fitted, word) == 0) {
            return candidates[i];
        }
    }
    fitText(nullptr, word, maxWidth, fitted, capacity);
    return nullptr;
}

static void drawSmallLabel(const char* label, int16_t x, int16_t y,
                           uint16_t color = GxEPD_BLACK) {
    epd.setFont(nullptr);
    epd.setTextSize(1);
    epd.setTextColor(color);
    epd.setCursor(x, y);
    epd.print(label);
    epd.setTextColor(GxEPD_BLACK);
}

static void drawPaper(const char* word, int16_t x, int16_t y, int16_t w, int16_t h,
                      bool inverted, bool large = false, const char* number = nullptr) {
    if (inverted) epd.fillRect(x, y, w, h, GxEPD_BLACK);
    else epd.drawRect(x, y, w, h, GxEPD_BLACK);
    if (number) drawSmallLabel(number, x - 18, y + 2);
    char fitted[wlp2::TEXT_MAX + 4];
    const GFXfont* font = wordFont(word, large, w - 10, fitted, sizeof(fitted));
    drawCenteredIn(fitted, font, x + 3, y + 2, w - 6, h - 4,
                   inverted ? GxEPD_WHITE : GxEPD_BLACK);
}

static void renderWords(const char* prev, const char* cur, const char* next,
                        bool confirmed, bool centerOnly, const char* status) {
    epd.fillScreen(GxEPD_WHITE);
    if (!centerOnly) {
        drawSmallLabel(status && status[0] ? status : "PICK A PIECE", 12, 11);
        drawSmallLabel("WIH", 171, 11);
        epd.fillTriangle(15, 43, 10, 50, 20, 50, GxEPD_BLACK);
        drawPaper(prev, 31, 35, 135, 29, false);
        epd.fillTriangle(15, 166, 10, 159, 20, 159, GxEPD_BLACK);
        drawPaper(next, 38, 147, 145, 29, false);
        drawSmallLabel("TURN TO BROWSE   A:KEEP", 37, 187);
    }

    epd.fillRect(PAPER_X - 1, PAPER_Y - 6, 18, 5, GxEPD_BLACK);
    epd.fillRect(PAPER_X + 67, PAPER_Y - 6, 18, 5, GxEPD_BLACK);
    drawPaper(cur, PAPER_X, PAPER_Y, PAPER_W, PAPER_H, true, true);
    if (confirmed) {
        epd.drawRect(PAPER_X + 3, PAPER_Y + 3, PAPER_W - 6, PAPER_H - 6, GxEPD_WHITE);
    }
}

static void drawSentenceHeader(const char* status, uint8_t participants) {
    epd.fillRect(0, 0, SCREEN_W, 29, GxEPD_BLACK);
    drawSmallLabel(status && status[0] ? status : "MY COLLAGE", 10, 18, GxEPD_WHITE);
    char number[8];
    snprintf(number, sizeof(number), "%02u", participants);
    drawSmallLabel(number, 176, 18, GxEPD_WHITE);
}

static void drawSentenceFooter(const char* line) {
    epd.drawFastHLine(10, 174, 180, GxEPD_BLACK);
    int16_t x = (SCREEN_W - textWidth(nullptr, line)) / 2;
    drawSmallLabel(line, x, 188);
}

static void drawSentenceCollage(const char** words, uint8_t ownIndex, uint8_t count) {
    static const int16_t layout1[][4] = {
        {30, 62, 140, 62},
    };
    static const int16_t layout2[][4] = {
        {18, 46, 119, 44}, {62, 112, 126, 46},
    };
    static const int16_t layout3[][4] = {
        {29, 38, 112, 34}, {48, 82, 121, 42}, {27, 134, 163, 34},
    };
    const int16_t (*layout)[4] = count == 1 ? layout1 : (count == 2 ? layout2 : layout3);
    for (uint8_t i = 0; i < count; ++i) {
        char number[4];
        snprintf(number, sizeof(number), "%02u", i + 1);
        drawPaper(words[i], layout[i][0], layout[i][1], layout[i][2], layout[i][3],
                  i == ownIndex, count <= 2, number);
    }
}

static void renderSentence(const char* left, const char* own, const char* right,
                           const char* status, uint8_t participants) {
    epd.fillScreen(GxEPD_WHITE);
    drawSentenceHeader(status, participants);

    const char* words[3];
    uint8_t ownIndex = 0;
    uint8_t count = 0;
    if (left && left[0]) words[count++] = left;
    ownIndex = count;
    if (own && own[0]) words[count++] = own;
    if (right && right[0]) words[count++] = right;

    if (count == 0) {
        drawCenteredIn("Empty collage", &FreeSerif18pt7b, 15, 62, 170, 70, GxEPD_BLACK);
    } else {
        drawSentenceCollage(words, ownIndex, count);
    }
    drawSentenceFooter("A:SELECT  B:CLEAR");
}

void displayDrawWords(const char* prev, const char* cur, const char* next,
                      bool confirmed, bool partial, DisplayArea area, const char* status) {
    bool centerOnly = partial && area == DisplayArea::Center;
    if (centerOnly) epd.setPartialWindow(0, PAPER_Y - 7, SCREEN_W, PAPER_H + 7);
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
