#include "storage.h"
#include <Preferences.h>

static RTC_DATA_ATTR PersistState rtcBlob;

struct LegacyPersistStateV1 {
    uint32_t magic;
    int16_t hist[HISTORY_LEN];
    uint8_t first;
    uint8_t cur;
    uint8_t last;
    uint8_t confirmed;
};

struct LegacyReceiptStateV3 {
    char src[17];
    char boot[17];
    char req[33];
    char text[wlp2::TEXT_MAX + 1];
    uint8_t side;
    uint8_t completed;
    uint64_t order;
};

struct LegacyPersistStateV2 {
    uint32_t magic;
    int16_t hist[HISTORY_LEN];
    uint8_t first;
    uint8_t cur;
    uint8_t last;
    uint8_t confirmed;
    char leftWord[wlp2::TEXT_MAX + 1];
    char rightWord[wlp2::TEXT_MAX + 1];
};

struct LegacyPersistStateV3 {
    uint32_t magic;
    int16_t hist[HISTORY_LEN];
    uint8_t first;
    uint8_t cur;
    uint8_t last;
    uint8_t confirmed;
    char leftWord[wlp2::TEXT_MAX + 1];
    char rightWord[wlp2::TEXT_MAX + 1];
    char leftSource[17];
    char rightSource[17];
    uint8_t receiptCount;
    LegacyReceiptStateV3 receipts[8];
};

static_assert(sizeof(LegacyPersistStateV1) == 72, "WIH1 layout changed");
static_assert(sizeof(LegacyPersistStateV2) == 268, "WIH2 layout changed");
static const uint32_t LEGACY_STATE_MAGIC_V1 = 0x57494831u;
static const uint32_t LEGACY_STATE_MAGIC_V2 = 0x57494832u;
static const uint32_t LEGACY_STATE_MAGIC_V3 = 0x57494833u;

bool rtcStateValid() {
    return rtcBlob.magic == STATE_MAGIC;
}

void rtcStateSave(const PersistState& state) {
    rtcBlob = state;
    rtcBlob.magic = STATE_MAGIC;
}

bool rtcStateLoad(PersistState& out) {
    if (!rtcStateValid()) return false;
    out = rtcBlob;
    return true;
}

static void migratePicker(PersistState& out, uint32_t magic, const int16_t* hist,
                          uint8_t first, uint8_t cur, uint8_t last, uint8_t confirmed) {
    out.magic = magic;
    for (int i = 0; i < HISTORY_LEN; ++i) out.hist[i] = hist[i];
    out.first = first;
    out.cur = cur;
    out.last = last;
    out.confirmed = confirmed;
}

static bool terminated(const char* value, size_t capacity) {
    return memchr(value, '\0', capacity) != nullptr;
}

bool storageLoadNvs(PersistState& out) {
    Preferences prefs;
    if (!prefs.begin("wih", true)) return false;
    size_t len = prefs.getBytesLength("state");
    bool ok = false;
    if (len == sizeof(PersistState)) {
        ok = prefs.getBytes("state", &out, sizeof(PersistState)) == len &&
             out.magic == STATE_MAGIC;
    } else if (len == sizeof(LegacyPersistStateV3)) {
        LegacyPersistStateV3 legacy;
        if (prefs.getBytes("state", &legacy, sizeof(legacy)) == len &&
            legacy.magic == LEGACY_STATE_MAGIC_V3) {
            bool valid = legacy.receiptCount <= 8 &&
                terminated(legacy.leftWord, sizeof(legacy.leftWord)) &&
                terminated(legacy.rightWord, sizeof(legacy.rightWord)) &&
                terminated(legacy.leftSource, sizeof(legacy.leftSource)) &&
                terminated(legacy.rightSource, sizeof(legacy.rightSource));
            for (uint8_t i = 0; valid && i < legacy.receiptCount; ++i) {
                valid = terminated(legacy.receipts[i].src, sizeof(legacy.receipts[i].src)) &&
                    terminated(legacy.receipts[i].boot, sizeof(legacy.receipts[i].boot)) &&
                    terminated(legacy.receipts[i].req, sizeof(legacy.receipts[i].req)) &&
                    terminated(legacy.receipts[i].text, sizeof(legacy.receipts[i].text));
            }
            if (!valid) {
                prefs.end();
                return false;
            }
            PersistState migrated = {};
            migratePicker(migrated, STATE_MAGIC, legacy.hist, legacy.first, legacy.cur,
                          legacy.last, legacy.confirmed);
            migrated.sentenceCount = (legacy.leftWord[0] ? 1 : 0) +
                                     (legacy.rightWord[0] ? 1 : 0);
            snprintf(migrated.sentenceWords[0], sizeof(migrated.sentenceWords[0]), "%s",
                     legacy.leftWord);
            snprintf(migrated.sentenceSources[0], sizeof(migrated.sentenceSources[0]), "%s",
                     legacy.leftSource);
            uint8_t right = legacy.leftWord[0] ? 1 : 0;
            snprintf(migrated.sentenceWords[right], sizeof(migrated.sentenceWords[right]), "%s",
                     legacy.rightWord);
            snprintf(migrated.sentenceSources[right], sizeof(migrated.sentenceSources[right]), "%s",
                     legacy.rightSource);
            migrated.receiptCount = legacy.receiptCount;
            for (uint8_t i = 0; i < migrated.receiptCount; ++i) {
                snprintf(migrated.receipts[i].src, sizeof(migrated.receipts[i].src), "%s",
                         legacy.receipts[i].src);
                snprintf(migrated.receipts[i].boot, sizeof(migrated.receipts[i].boot), "%s",
                         legacy.receipts[i].boot);
                snprintf(migrated.receipts[i].req, sizeof(migrated.receipts[i].req), "%s",
                         legacy.receipts[i].req);
                snprintf(migrated.receipts[i].text, sizeof(migrated.receipts[i].text), "%s",
                         legacy.receipts[i].text);
                migrated.receipts[i].side = legacy.receipts[i].side;
                migrated.receipts[i].completed = legacy.receipts[i].completed;
                migrated.receipts[i].order = legacy.receipts[i].order;
            }
            out = migrated;
            ok = true;
        }
    } else if (len == sizeof(LegacyPersistStateV2)) {
        LegacyPersistStateV2 legacy;
        if (prefs.getBytes("state", &legacy, sizeof(legacy)) == len &&
            legacy.magic == LEGACY_STATE_MAGIC_V2) {
            if (!terminated(legacy.leftWord, sizeof(legacy.leftWord)) ||
                !terminated(legacy.rightWord, sizeof(legacy.rightWord))) {
                prefs.end();
                return false;
            }
            PersistState migrated = {};
            migratePicker(migrated, STATE_MAGIC, legacy.hist, legacy.first, legacy.cur,
                          legacy.last, legacy.confirmed);
            migrated.sentenceCount = (legacy.leftWord[0] ? 1 : 0) +
                                     (legacy.rightWord[0] ? 1 : 0);
            snprintf(migrated.sentenceWords[0], sizeof(migrated.sentenceWords[0]), "%s",
                     legacy.leftWord);
            uint8_t right = legacy.leftWord[0] ? 1 : 0;
            snprintf(migrated.sentenceWords[right], sizeof(migrated.sentenceWords[right]), "%s",
                     legacy.rightWord);
            out = migrated;
            ok = true;
        }
    } else if (len == sizeof(LegacyPersistStateV1)) {
        LegacyPersistStateV1 legacy;
        if (prefs.getBytes("state", &legacy, sizeof(legacy)) == len &&
            legacy.magic == LEGACY_STATE_MAGIC_V1) {
            PersistState migrated = {};
            migratePicker(migrated, STATE_MAGIC, legacy.hist, legacy.first, legacy.cur,
                          legacy.last, legacy.confirmed);
            out = migrated;
            ok = true;
        }
    }
    prefs.end();
    return ok;
}

bool storageSaveNvs(const PersistState& state) {
    Preferences prefs;
    if (!prefs.begin("wih", false)) return false;
    PersistState copy = state;
    copy.magic = STATE_MAGIC;
    size_t written = prefs.putBytes("state", &copy, sizeof(copy));
    prefs.end();
    return written == sizeof(copy);
}
