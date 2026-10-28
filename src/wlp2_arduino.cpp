#include "wlp2_arduino.h"

#include <esp_random.h>
#include "config.h"

int UartBytePort::read() {
    return serial_.available() ? serial_.read() : -1;
}

void UartBytePort::write(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        size_t written = serial_.write(data + sent, len - sent);
        if (written == 0) delay(1);
        else sent += written;
    }
}

void wlp2UartInit() {
    Serial1.begin(115200, SERIAL_8N1, PIN_UART_L_RX, PIN_UART_L_TX);
    Serial2.begin(115200, SERIAL_8N1, PIN_UART_R_RX, PIN_UART_R_TX);
}

void wlp2MakeIdentity(wlp2::Identity& identity) {
    memset(&identity, 0, sizeof(identity));
    unsigned long long mac = ESP.getEfuseMac();
    snprintf(identity.id, sizeof(identity.id), "0000%012llx", mac);
    uint64_t boot = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    snprintf(identity.boot, sizeof(identity.boot), "%016llx",
             static_cast<unsigned long long>(boot));
    snprintf(identity.kit, sizeof(identity.kit), "%s", WIH_KIT_ID);
    snprintf(identity.recorder, sizeof(identity.recorder), "%s", WIH_RECORDER_ID);
    identity.role = strcmp(identity.id, identity.recorder) == 0
        ? wlp2::Role::Recorder : wlp2::Role::Word;
    identity.members[0] = WIH_MEMBER_1_ID;
    identity.members[1] = WIH_MEMBER_2_ID;
    identity.members[2] = WIH_MEMBER_3_ID;
    identity.memberCount = 3;
}
