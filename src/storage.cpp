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

static_assert(sizeof(LegacyPersistStateV1) == 72, "WIH1 layout changed");
static_assert(sizeof(LegacyPersistStateV2) == 268, "WIH2 layout changed");
static const uint32_t LEGACY_STATE_MAGIC_V1 = 0x57494831u;
static const uint32_t LEGACY_STATE_MAGIC_V2 = 0x57494832u;

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

bool storageLoadNvs(PersistState& out) {
    Preferences prefs;
    if (!prefs.begin("wih", true)) return false;
    size_t len = prefs.getBytesLength("state");
    bool ok = false;
    if (len == sizeof(PersistState)) {
        ok = prefs.getBytes("state", &out, sizeof(PersistState)) == len &&
             out.magic == STATE_MAGIC;
    } else if (len == sizeof(LegacyPersistStateV2)) {
        LegacyPersistStateV2 legacy;
        if (prefs.getBytes("state", &legacy, sizeof(legacy)) == len &&
            legacy.magic == LEGACY_STATE_MAGIC_V2) {
            PersistState migrated = {};
            migratePicker(migrated, STATE_MAGIC, legacy.hist, legacy.first, legacy.cur,
                          legacy.last, legacy.confirmed);
            snprintf(migrated.leftWord, sizeof(migrated.leftWord), "%s", legacy.leftWord);
            snprintf(migrated.rightWord, sizeof(migrated.rightWord), "%s", legacy.rightWord);
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
