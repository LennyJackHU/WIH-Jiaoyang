#include "storage.h"
#include <Preferences.h>

static RTC_DATA_ATTR PersistState rtcBlob;

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
        prefs.getBytes("state", &out, sizeof(PersistState));
        ok = out.magic == STATE_MAGIC;
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
