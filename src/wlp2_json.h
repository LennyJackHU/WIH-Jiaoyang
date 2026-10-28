#pragma once

#include <stddef.h>
#include <stdint.h>

namespace wlp2 {

static const size_t JSON_MAX = 2048;
static const size_t FRAME_MAX = 2059;
static const size_t NET_MAX = 1920;
static const size_t TEXT_MAX = 96;

enum class JsonType : uint8_t { Missing, Null, Bool, Number, String, Object, Array };

uint32_t crc32(const void* data, size_t len);
bool validHex(const char* value, size_t digits);
bool validText(const char* value, size_t len);

class JsonReader {
public:
    JsonReader();
    JsonReader(const char* data, size_t len);

    bool valid() const { return valid_; }
    size_t size() const { return len_; }
    const char* data() const { return data_; }
    bool contains(const char* key) const;
    JsonType type(const char* key) const;
    bool getString(const char* key, char* out, size_t capacity) const;
    bool getText(const char* key, char* out, size_t capacity) const;
    bool getUInt(const char* key, uint32_t& out) const;
    bool getObject(const char* key, JsonReader& out) const;
    bool isNull(const char* key) const;

private:
    struct Span {
        const char* data;
        size_t len;
        JsonType type;
    };

    bool find(const char* key, Span& span) const;

    const char* data_ = nullptr;
    size_t len_ = 0;
    bool valid_ = false;
};

class JsonWriter {
public:
    JsonWriter(char* buffer, size_t capacity);

    void beginObject(const char* key = nullptr);
    void endObject();
    void key(const char* name);
    void string(const char* value);
    void text(const char* value) { string(value); }
    void uint32(uint32_t value);
    void boolean(bool value);
    void null();
    void raw(const char* json);
    void raw(const char* json, size_t len);
    void fieldString(const char* name, const char* value);
    void fieldUInt(const char* name, uint32_t value);
    void fieldNull(const char* name);
    void fieldRaw(const char* name, const char* json);
    void fieldRaw(const char* name, const char* json, size_t len);

    bool finish();
    bool ok() const { return ok_; }
    size_t size() const { return len_; }
    const char* data() const { return buffer_; }

private:
    void beforeValue();
    void append(char c);
    void append(const char* data, size_t len);

    char* buffer_;
    size_t capacity_;
    size_t len_ = 0;
    bool ok_ = true;
    uint8_t depth_ = 0;
    bool expectKey_[6] = {};
    bool hasFields_[6] = {};
};

bool encodeFrame(const char* json, size_t jsonLen, char* out, size_t capacity,
                 size_t& frameLen);

} // namespace wlp2
