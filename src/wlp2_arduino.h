#pragma once

#include <Arduino.h>
#include "wlp2_protocol.h"

class UartBytePort : public wlp2::BytePort {
public:
    explicit UartBytePort(HardwareSerial& serial) : serial_(serial) {}
    int read() override;
    void write(const uint8_t* data, size_t len) override;

private:
    HardwareSerial& serial_;
};

bool wlp2UartInit();
uint32_t wlp2UartErrors(wlp2::Port port);
void wlp2MakeIdentity(wlp2::Identity& identity);
