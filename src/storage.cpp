#include "storage.h"
#include <Preferences.h>

static RTC_DATA_ATTR PersistState rtcBlob;

struct LegacyPersistState {
    uint32_t magic;
    int16_t hist[HISTORY_LEN];
    uint8_t first;
    uint8_t cur;
    uint8_t last;
    uint8_t confirmed;
};

static const uint32_t LEGACY_STATE_MAGIC = 0x57494831u;

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

bool storageLoadNvs(PersistState& out) {
    Preferences prefs;
    prefs.begin("wih", true);
    size_t len = prefs.getBytesLength("state");
    bool ok = false;
    if (len == sizeof(PersistState)) {
        ok = prefs.getBytes("state", &out, sizeof(PersistState)) == len &&
             out.magic == STATE_MAGIC;
    } else if (len == sizeof(LegacyPersistState)) {
        LegacyPersistState legacy;
        if (prefs.getBytes("state", &legacy, sizeof(legacy)) == len &&
            legacy.magic == LEGACY_STATE_MAGIC) {
            PersistState migrated = {};
            migrated.magic = STATE_MAGIC;
            for (int i = 0; i < HISTORY_LEN; ++i) migrated.hist[i] = legacy.hist[i];
            migrated.first = legacy.first;
            migrated.cur = legacy.cur;
            migrated.last = legacy.last;
            migrated.confirmed = legacy.confirmed;
            out = migrated;
            ok = true;
        }
    }
    prefs.end();
    return ok;
}

void storageSaveNvs(const PersistState& state) {
    Preferences prefs;
    prefs.begin("wih", false);
    PersistState copy = state;
    copy.magic = STATE_MAGIC;
    prefs.putBytes("state", &copy, sizeof(PersistState));
    prefs.end();
}
