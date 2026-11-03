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

static void drawWrappedIn(const char* value, int16_t x, int16_t y,
                          int16_t w, int16_t h, uint16_t color) {
    if (!value || !value[0] || w < 6 || h < 8) return;
    const uint8_t maxLines = h >= 32 ? 4 : (h >= 24 ? 3 : (h >= 16 ? 2 : 1));
    char lines[4][wlp2::TEXT_MAX + 1];
    uint8_t count = 0;
    const char* cursor = value;
    epd.setFont(nullptr);
    epd.setTextSize(1);
    while (*cursor && count < maxLines) {
        while (*cursor == ' ') ++cursor;
        if (!*cursor) break;
        const size_t remaining = strlen(cursor);
        size_t take = remaining;
        if (textWidth(nullptr, cursor) > w) {
            const size_t maxChars = static_cast<size_t>(w / 6);
            take = remaining < maxChars ? remaining : maxChars;
            if (take < remaining) {
                size_t space = take;
                while (space > 0 && cursor[space] != ' ') --space;
                if (space > 0) take = space;
            }
        }
        size_t length = take;
        while (length > 0 && cursor[length - 1] == ' ') --length;
        memcpy(lines[count], cursor, length);
        lines[count][length] = '\0';
        ++count;
        cursor += take;
    }
    const int16_t startY = y + (h - static_cast<int16_t>(count) * 8) / 2;
    epd.setTextColor(color);
    for (uint8_t i = 0; i < count; ++i) {
        epd.setCursor(x + (w - textWidth(nullptr, lines[i])) / 2, startY + i * 8);
        epd.print(lines[i]);
    }
    epd.setTextColor(GxEPD_BLACK);
}

static void drawCenteredIn(const char* value, const GFXfont* font,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t color) {
    if (!value || !value[0]) return;
    if (textWidth(font, value) > w - 8) {
        drawWrappedIn(value, x + 4, y + 2, w - 8, h - 4, color);
        return;
    }
    epd.setFont(font);
    int16_t bx, by;
    uint16_t bw, bh;
    epd.getTextBounds(value, 0, 0, &bx, &by, &bw, &bh);
    epd.setCursor(x + (w - bw) / 2 - bx, y + (h - bh) / 2 - by);
    epd.setTextColor(color);
    epd.print(value);
    epd.setTextColor(GxEPD_BLACK);
}

static const GFXfont* wordFont(const char* word, bool large, int16_t maxWidth,
                               char* fitted, size_t capacity) {
    if (!word || !word[0]) {
        fitted[0] = '\0';
        return &FreeSans9pt7b;
    }
    snprintf(fitted, capacity, "%s", word);
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
    for (uint8_t i = 0; i < count; ++i) {
        if (textWidth(candidates[i], word) <= maxWidth) return candidates[i];
    }
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
                      bool inverted, bool large = false) {
    if (inverted) epd.fillRect(x, y, w, h, GxEPD_BLACK);
    else epd.drawRect(x, y, w, h, GxEPD_BLACK);
    char fitted[wlp2::TEXT_MAX + 4];
    const GFXfont* font = wordFont(word, large, w - 10, fitted, sizeof(fitted));
    drawCenteredIn(fitted, font, x + 3, y + 2, w - 6, h - 4,
                   inverted ? GxEPD_WHITE : GxEPD_BLACK);
}

static void renderWords(const char* prev, const char* cur, const char* next,
                        bool confirmed, bool centerOnly, const char* status,
                        bool standalone, bool standaloneConfirm) {
    epd.fillScreen(GxEPD_WHITE);
    if (!centerOnly) {
        const char* title = status && status[0] ? status :
            standaloneConfirm ? (standalone ? "EXIT SOLO?" : "ENTER SOLO?") :
            standalone ? "SOLO PICK" : "PICK A PIECE";
        drawSmallLabel(title, 12, 11);
        drawSmallLabel("WIH", 171, 11);
        epd.fillTriangle(15, 43, 10, 50, 20, 50, GxEPD_BLACK);
        drawPaper(prev, 31, 35, 135, 29, false);
        epd.fillTriangle(15, 166, 10, 159, 20, 159, GxEPD_BLACK);
        drawPaper(next, 38, 147, 145, 29, false);
        const char* hint = standaloneConfirm ? "A:CONFIRM  B:CANCEL" :
            standalone ? (confirmed ? "A:ADD  B:CANCEL  HOLD A:EXIT" :
                                      "A:KEEP  B:SHUFFLE  HOLD A:EXIT") :
            confirmed ? "A:SEND  B:CANCEL  HOLD A:SOLO" :
            "A:KEEP  B:SHUFFLE  HOLD A:SOLO";
        drawSmallLabel(hint, (SCREEN_W - textWidth(nullptr, hint)) / 2, 187);
    }

    epd.fillRect(PAPER_X - 1, PAPER_Y - 6, 18, 5, GxEPD_BLACK);
    epd.fillRect(PAPER_X + 67, PAPER_Y - 6, 18, 5, GxEPD_BLACK);
    drawPaper(cur, PAPER_X, PAPER_Y, PAPER_W, PAPER_H, true, true);
    if (confirmed) {
        epd.drawRect(PAPER_X + 3, PAPER_Y + 3, PAPER_W - 6, PAPER_H - 6, GxEPD_WHITE);
    }
}

static void drawSentenceHeader(const char* status, uint8_t pieces,
                               uint8_t page, uint8_t pages) {
    epd.fillRect(0, 0, SCREEN_W, 29, GxEPD_BLACK);
    drawSmallLabel(status && status[0] ? status : "MY COLLAGE", 10, 18, GxEPD_WHITE);
    char number[12];
    if (pages > 1) snprintf(number, sizeof(number), "P%u/%u", page + 1, pages);
    else snprintf(number, sizeof(number), "%02u", pieces);
    drawSmallLabel(number, SCREEN_W - 10 - textWidth(nullptr, number), 18, GxEPD_WHITE);
}

static void drawFooter(const char* first, const char* second = nullptr) {
    drawSmallLabel(first, (SCREEN_W - textWidth(nullptr, first)) / 2, second ? 182 : 188);
    if (second) drawSmallLabel(second, (SCREEN_W - textWidth(nullptr, second)) / 2, 194);
}

static void drawCaret(int16_t x, int16_t y, int16_t h) {
    if (x < 2) x = 2;
    if (x > SCREEN_W - 4) x = SCREEN_W - 4;
    epd.fillRect(x, y, 3, h, GxEPD_BLACK);
}

static void drawSentenceCell(const char* word, uint8_t position, bool own,
                             bool selected, int16_t x, int16_t y) {
    const int16_t w = 89;
    const int16_t h = 36;
    const uint16_t foreground = selected ? GxEPD_WHITE : GxEPD_BLACK;
    if (selected) epd.fillRect(x, y, w, h, GxEPD_BLACK);
    else epd.drawRect(x, y, w, h, GxEPD_BLACK);

    char number[4];
    snprintf(number, sizeof(number), "%02u", position + 1);
    drawSmallLabel(number, x + 4, y + 9, foreground);
    if (own) epd.fillRect(x + w - 7, y + 4, 3, 3, foreground);

    if (word && word[0]) {
        char fitted[wlp2::TEXT_MAX + 4];
        const GFXfont* font = wordFont(word, false, w - 22, fitted, sizeof(fitted));
        if (font) drawCenteredIn(fitted, font, x + 13, y + 2, w - 17, h - 4, foreground);
        else drawWrappedIn(fitted, x + 3, y + 10, w - 6, h - 12, foreground);
    }
}

static void drawSentenceGrid(const Sentence& sentence, uint8_t cursor,
                             SentenceEdit edit, uint8_t insertPosition) {
    static const uint8_t PAGE_SIZE = 6;
    const uint8_t count = sentence.count();
    uint8_t anchor = cursor;
    if (edit == SentenceEdit::InsertCursor) {
        anchor = insertPosition < count ? insertPosition : (count ? count - 1 : 0);
    }
    const uint8_t pageStart = sentence.empty() ? 0 : (anchor / PAGE_SIZE) * PAGE_SIZE;
    for (uint8_t slot = 0; slot < PAGE_SIZE; ++slot) {
        const uint8_t index = pageStart + slot;
        const bool present = index < count;
        const int16_t x = 8 + (slot % 2) * 95;
        const int16_t y = 37 + (slot / 2) * 42;
        drawSentenceCell(present ? sentence.text(index) : "", index,
                         present && sentence.source(index)[0] == '\0',
                         present && edit != SentenceEdit::InsertCursor && index == cursor, x, y);
    }
    if (edit == SentenceEdit::InsertCursor) {
        if (count == 0) {
            drawCaret(4, 41, 28);
        } else {
            const bool before = insertPosition < count;
            const uint8_t index = before ? insertPosition : count - 1;
            const uint8_t slot = index - pageStart;
            const int16_t cellX = 8 + (slot % 2) * 95;
            const int16_t cellY = 37 + (slot / 2) * 42;
            drawCaret(before ? cellX - 4 : cellX + 91, cellY + 4, 28);
        }
    }
}

static void renderSentence(const Sentence& sentence, uint8_t cursor, SentenceEdit edit,
                           uint8_t insertPosition, const char* status) {
    epd.fillScreen(GxEPD_WHITE);
    const char* title = status;
    if (edit == SentenceEdit::ClearConfirm) title = "CLEAR COLLAGE?";
    else if (edit == SentenceEdit::DeleteConfirm) title = "DELETE THIS PIECE?";
    else if (edit == SentenceEdit::UploadConfirm) title = "UPLOAD TO COMPUTER?";
    const uint8_t pages = sentence.empty() ? 1 : (sentence.count() + 5) / 6;
    uint8_t anchor = cursor;
    if (edit == SentenceEdit::InsertCursor) {
        anchor = insertPosition < sentence.count() ? insertPosition :
                 (sentence.empty() ? 0 : sentence.count() - 1);
    }
    const uint8_t page = sentence.empty() ? 0 : anchor / 6;
    drawSentenceHeader(title, sentence.count(), page, pages);
    drawSentenceGrid(sentence, cursor, edit, insertPosition);

    epd.drawFastHLine(10, 174, 180, GxEPD_BLACK);
    if (edit == SentenceEdit::ClearConfirm) {
        drawFooter("A:CLEAR ALL  B:CANCEL");
    } else if (edit == SentenceEdit::DeleteConfirm) {
        drawFooter("A:DELETE  B:CANCEL");
    } else if (edit == SentenceEdit::UploadConfirm) {
        drawFooter("A:UPLOAD  B:CANCEL");
    } else if (edit == SentenceEdit::InsertCursor) {
        drawFooter("A:SELECT  B:CANCEL", "ROTATE:POSITION  HOLD A:UPLOAD");
    } else {
        drawFooter("A:INSERT  B:DELETE", "HOLD A:UPLOAD  HOLD B:CLEAR");
    }
}

void displayDrawWords(const char* prev, const char* cur, const char* next,
                      bool confirmed, bool partial, DisplayArea area, const char* status,
                      bool standalone, bool standaloneConfirm) {
    bool centerOnly = partial && area == DisplayArea::Center;
    if (centerOnly) epd.setPartialWindow(0, PAPER_Y - 7, SCREEN_W, PAPER_H + 7);
    else if (partial) epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    else epd.setFullWindow();
    epd.firstPage();
    do {
        renderWords(prev, cur, next, confirmed, centerOnly, status,
                    standalone, standaloneConfirm);
    } while (epd.nextPage());
}

void displayDrawSentence(const Sentence& sentence, uint8_t cursor, SentenceEdit edit,
                         uint8_t insertPosition, const char* status, bool partial) {
    if (partial) epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    else epd.setFullWindow();
    epd.firstPage();
    do {
        renderSentence(sentence, cursor, edit, insertPosition, status);
    } while (epd.nextPage());
}

void displayHibernate() {
    epd.hibernate();
}
