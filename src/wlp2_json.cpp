#include "wlp2_json.h"

#include <stdio.h>
#include <string.h>

namespace wlp2 {
namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decodeUtf8(const char* data, size_t len, uint32_t& cp, size_t& used) {
    if (len == 0) return false;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(data);
    if (s[0] < 0x80) {
        cp = s[0];
        used = 1;
        return true;
    }
    unsigned need;
    uint32_t value;
    if (s[0] >= 0xC2 && s[0] <= 0xDF) {
        need = 2;
        value = s[0] & 0x1F;
    } else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        need = 3;
        value = s[0] & 0x0F;
    } else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        need = 4;
        value = s[0] & 0x07;
    } else {
        return false;
    }
    if (len < need) return false;
    for (unsigned i = 1; i < need; ++i) {
        if ((s[i] & 0xC0) != 0x80) return false;
        value = (value << 6) | (s[i] & 0x3F);
    }
    if ((need == 3 && value < 0x800) || (need == 4 && value < 0x10000) ||
        value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    cp = value;
    used = need;
    return true;
}

void appendUtf8(uint32_t cp, char* out, size_t capacity, size_t& len, bool& ok) {
    unsigned char bytes[4];
    size_t count;
    if (cp <= 0x7F) {
        bytes[0] = static_cast<unsigned char>(cp);
        count = 1;
    } else if (cp <= 0x7FF) {
        bytes[0] = 0xC0 | (cp >> 6);
        bytes[1] = 0x80 | (cp & 0x3F);
        count = 2;
    } else if (cp <= 0xFFFF) {
        bytes[0] = 0xE0 | (cp >> 12);
        bytes[1] = 0x80 | ((cp >> 6) & 0x3F);
        bytes[2] = 0x80 | (cp & 0x3F);
        count = 3;
    } else {
        bytes[0] = 0xF0 | (cp >> 18);
        bytes[1] = 0x80 | ((cp >> 12) & 0x3F);
        bytes[2] = 0x80 | ((cp >> 6) & 0x3F);
        bytes[3] = 0x80 | (cp & 0x3F);
        count = 4;
    }
    if (out) {
        if (len + count >= capacity) {
            ok = false;
            return;
        }
        for (size_t i = 0; i < count; ++i) out[len + i] = static_cast<char>(bytes[i]);
    }
    len += count;
}

bool scanString(const char* data, size_t len, size_t& used,
                char* out = nullptr, size_t capacity = 0, size_t* outLen = nullptr) {
    if (len < 2 || data[0] != '"') return false;
    bool ok = true;
    size_t written = 0;
    size_t i = 1;
    while (i < len) {
        unsigned char c = static_cast<unsigned char>(data[i++]);
        if (c == '"') {
            if (!ok) return false;
            if (out) {
                if (written >= capacity) return false;
                out[written] = '\0';
            }
            if (outLen) *outLen = written;
            used = i;
            return true;
        }
        if (c < 0x20) return false;
        uint32_t cp;
        size_t count;
        const char* copy = data + i - 1;
        if (c == '\\') {
            if (i >= len) return false;
            char escape = data[i++];
            const char* simple = nullptr;
            switch (escape) {
                case '"': simple = "\""; break;
                case '\\': simple = "\\"; break;
                case '/': simple = "/"; break;
                case 'b': simple = "\b"; break;
                case 'f': simple = "\f"; break;
                case 'n': simple = "\n"; break;
                case 'r': simple = "\r"; break;
                case 't': simple = "\t"; break;
                case 'u': {
                    if (i + 4 > len) return false;
                    cp = 0;
                    for (unsigned j = 0; j < 4; ++j) {
                        int digit = hexDigit(data[i + j]);
                        if (digit < 0) return false;
                        cp = (cp << 4) | digit;
                    }
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i + 6 > len || data[i] != '\\' || data[i + 1] != 'u') return false;
                        uint32_t low = 0;
                        for (unsigned j = 0; j < 4; ++j) {
                            int digit = hexDigit(data[i + 2 + j]);
                            if (digit < 0) return false;
                            low = (low << 4) | digit;
                        }
                        if (low < 0xDC00 || low > 0xDFFF) return false;
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    appendUtf8(cp, out, capacity, written, ok);
                    if (!ok) return false;
                    continue;
                }
                default: return false;
            }
            cp = static_cast<unsigned char>(simple[0]);
            count = 1;
        } else if (c < 0x80) {
            cp = c;
            count = 1;
        } else {
            if (!decodeUtf8(copy, len - i + 1, cp, count)) return false;
        }
        if (out) {
            if (written + count >= capacity) return false;
            memcpy(out + written, copy, count);
        }
        written += count;
        if (c == '\\') {
            if (out) out[written - 1] = static_cast<char>(cp);
        } else {
            i += count - 1;
        }
    }
    return false;
}

class Parser {
public:
    Parser(const char* data, size_t len) : p_(data), end_(data + len) {}

    bool document() {
        value(0);
        space();
        return ok_ && p_ == end_;
    }

    bool one(size_t& used) {
        const char* start = p_;
        space();
        value(0);
        used = static_cast<size_t>(p_ - start);
        return ok_;
    }

private:
    void space() {
        while (p_ < end_ && *p_ == ' ') ++p_;
    }

    void value(unsigned depth) {
        if (!ok_ || depth > 5) {
            ok_ = false;
            return;
        }
        space();
        if (p_ >= end_) {
            ok_ = false;
            return;
        }
        switch (*p_) {
            case '{': object(depth + 1); break;
            case '[': array(depth + 1); break;
            case '"': {
                size_t used;
                if (!scanString(p_, static_cast<size_t>(end_ - p_), used)) ok_ = false;
                else p_ += used;
                break;
            }
            case 't': literal("true", 4); break;
            case 'f': literal("false", 5); break;
            case 'n': literal("null", 4); break;
            default: number(); break;
        }
    }

    void literal(const char* word, size_t len) {
        if (static_cast<size_t>(end_ - p_) < len || memcmp(p_, word, len) != 0) ok_ = false;
        else p_ += len;
    }

    void number() {
        const char* start = p_;
        if (p_ < end_ && *p_ == '-') ++p_;
        if (p_ >= end_) {
            ok_ = false;
            return;
        }
        if (*p_ == '0') {
            ++p_;
            if (p_ < end_ && *p_ >= '0' && *p_ <= '9') ok_ = false;
        } else {
            if (*p_ < '1' || *p_ > '9') {
                ok_ = false;
                return;
            }
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        }
        if (p_ < end_ && *p_ == '.') {
            ++p_;
            if (p_ >= end_ || *p_ < '0' || *p_ > '9') ok_ = false;
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        }
        if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
            ++p_;
            if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
            if (p_ >= end_ || *p_ < '0' || *p_ > '9') ok_ = false;
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        }
        if (p_ == start) ok_ = false;
    }

    void object(unsigned depth) {
        ++p_;
        space();
        char keys[12][32];
        size_t keyLens[12];
        unsigned count = 0;
        if (p_ < end_ && *p_ == '}') {
            ++p_;
            return;
        }
        while (ok_) {
            space();
            size_t used, keyLen;
            if (count >= 12 || !scanString(p_, static_cast<size_t>(end_ - p_), used,
                                           keys[count], sizeof(keys[count]), &keyLen)) {
                ok_ = false;
                return;
            }
            for (unsigned i = 0; i < count; ++i) {
                if (keyLens[i] == keyLen && memcmp(keys[i], keys[count], keyLen) == 0) {
                    ok_ = false;
                    return;
                }
            }
            keyLens[count++] = keyLen;
            p_ += used;
            space();
            if (p_ >= end_ || *p_++ != ':') {
                ok_ = false;
                return;
            }
            value(depth);
            space();
            if (p_ >= end_) {
                ok_ = false;
                return;
            }
            if (*p_ == '}') {
                ++p_;
                return;
            }
            if (*p_++ != ',') {
                ok_ = false;
                return;
            }
        }
    }

    void array(unsigned depth) {
        ++p_;
        space();
        if (p_ < end_ && *p_ == ']') {
            ++p_;
            return;
        }
        while (ok_) {
            value(depth);
            space();
            if (p_ >= end_) {
                ok_ = false;
                return;
            }
            if (*p_ == ']') {
                ++p_;
                return;
            }
            if (*p_++ != ',') {
                ok_ = false;
                return;
            }
        }
    }

    const char* p_;
    const char* end_;
    bool ok_ = true;
};

JsonType spanType(const char* data, size_t len) {
    while (len && *data == ' ') {
        ++data;
        --len;
    }
    if (!len) return JsonType::Missing;
    switch (*data) {
        case 'n': return JsonType::Null;
        case 't':
        case 'f': return JsonType::Bool;
        case '"': return JsonType::String;
        case '{': return JsonType::Object;
        case '[': return JsonType::Array;
        default: return JsonType::Number;
    }
}

bool isControlText(const char* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t used;
        if (!decodeUtf8(data + i, len - i, cp, used)) return true;
        if (cp < 0x20 || cp == 0x7F) return true;
        i += used;
    }
    return false;
}

} // namespace

uint32_t crc32(const void* input, size_t len) {
    const unsigned char* data = static_cast<const unsigned char*>(input);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

bool validHex(const char* value, size_t digits) {
    if (!value || strlen(value) != digits) return false;
    for (size_t i = 0; i < digits; ++i) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}

bool validText(const char* value, size_t len) {
    return value && len > 0 && len <= TEXT_MAX && !isControlText(value, len);
}

JsonReader::JsonReader() = default;

JsonReader::JsonReader(const char* data, size_t len) : data_(data), len_(len) {
    if (!data || len == 0 || len > JSON_MAX || data[0] != '{') return;
    Parser parser(data, len);
    valid_ = parser.document();
}

bool JsonReader::find(const char* key, Span& span) const {
    span = {nullptr, 0, JsonType::Missing};
    if (!valid_ || !key) return false;
    const char* p = data_ + 1;
    const char* end = data_ + len_ - 1;
    while (p < end && *p == ' ') ++p;
    if (p < end && *p == '}') return false;
    while (p < end) {
        size_t keyUsed;
        if (!scanString(p, static_cast<size_t>(end - p), keyUsed)) return false;
        char decoded[48];
        size_t decodedLen;
        if (!scanString(p, static_cast<size_t>(end - p), keyUsed,
                        decoded, sizeof(decoded), &decodedLen)) return false;
        p += keyUsed;
        while (p < end && *p == ' ') ++p;
        if (p >= end || *p++ != ':') return false;
        Parser valueParser(p, static_cast<size_t>(end - p));
        size_t valueLen;
        if (!valueParser.one(valueLen)) return false;
        if (strlen(key) == decodedLen && memcmp(key, decoded, decodedLen) == 0) {
            span = {p, valueLen, spanType(p, valueLen)};
            return true;
        }
        p += valueLen;
        while (p < end && *p == ' ') ++p;
        if (p >= end || *p != ',') return false;
        ++p;
        while (p < end && *p == ' ') ++p;
    }
    return false;
}

bool JsonReader::contains(const char* key) const {
    Span span;
    return find(key, span);
}

JsonType JsonReader::type(const char* key) const {
    Span span;
    return find(key, span) ? span.type : JsonType::Missing;
}

bool JsonReader::getString(const char* key, char* out, size_t capacity) const {
    Span span;
    if (!find(key, span) || span.type != JsonType::String || !out || capacity == 0) return false;
    size_t used, decoded;
    return scanString(span.data, span.len, used, out, capacity, &decoded) && used == span.len;
}

bool JsonReader::getText(const char* key, char* out, size_t capacity) const {
    Span span;
    if (!find(key, span) || span.type != JsonType::String || span.len < 2 ||
        span.len - 2 > TEXT_MAX + 96) return false;
    size_t used, decoded;
    if (!scanString(span.data, span.len, used, out, capacity, &decoded) || used != span.len) return false;
    return validText(out, decoded);
}

bool JsonReader::getUInt(const char* key, uint32_t& out) const {
    Span span;
    if (!find(key, span) || span.type != JsonType::Number) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < span.len; ++i) {
        if (span.data[i] < '0' || span.data[i] > '9') return false;
        value = value * 10 + static_cast<unsigned>(span.data[i] - '0');
        if (value > 0xFFFFFFFFull) return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool JsonReader::getObject(const char* key, JsonReader& out) const {
    Span span;
    if (!find(key, span) || span.type != JsonType::Object) return false;
    out = JsonReader(span.data, span.len);
    return out.valid();
}

bool JsonReader::isNull(const char* key) const {
    return type(key) == JsonType::Null;
}

JsonWriter::JsonWriter(char* buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {
    if (capacity_) buffer_[0] = '\0';
}

void JsonWriter::append(char c) {
    if (!ok_ || len_ + 1 >= capacity_) {
        ok_ = false;
        return;
    }
    buffer_[len_++] = c;
    buffer_[len_] = '\0';
}

void JsonWriter::append(const char* data, size_t len) {
    if (!ok_ || !data || len_ + len >= capacity_) {
        ok_ = false;
        return;
    }
    memcpy(buffer_ + len_, data, len);
    len_ += len;
    buffer_[len_] = '\0';
}

void JsonWriter::beforeValue() {
    if (!ok_ || (depth_ > 0 && expectKey_[depth_ - 1])) {
        ok_ = false;
        return;
    }
    if (depth_ > 0) {
        expectKey_[depth_ - 1] = true;
        hasFields_[depth_ - 1] = true;
    }
}

void JsonWriter::key(const char* name) {
    if (!ok_ || depth_ == 0 || !expectKey_[depth_ - 1] || !name) {
        ok_ = false;
        return;
    }
    if (hasFields_[depth_ - 1]) append(',');
    string(name);
    append(':');
    expectKey_[depth_ - 1] = false;
}

void JsonWriter::beginObject(const char* name) {
    if (name) key(name);
    beforeValue();
    if (depth_ >= 6) {
        ok_ = false;
        return;
    }
    append('{');
    expectKey_[depth_] = true;
    hasFields_[depth_] = false;
    ++depth_;
}

void JsonWriter::endObject() {
    if (!ok_ || depth_ == 0 || !expectKey_[depth_ - 1]) {
        ok_ = false;
        return;
    }
    append('}');
    --depth_;
}

void JsonWriter::string(const char* value) {
    if (!value) {
        ok_ = false;
        return;
    }
    append('"');
    const unsigned char* p = reinterpret_cast<const unsigned char*>(value);
    while (*p && ok_) {
        unsigned char c = *p++;
        switch (c) {
            case '"': append("\\\"", 2); break;
            case '\\': append("\\\\", 2); break;
            case '\b': append("\\b", 2); break;
            case '\f': append("\\f", 2); break;
            case '\n': append("\\n", 2); break;
            case '\r': append("\\r", 2); break;
            case '\t': append("\\t", 2); break;
            default:
                if (c < 0x20) {
                    char escaped[7];
                    snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    append(escaped, 6);
                } else {
                    append(static_cast<char>(c));
                }
        }
    }
    append('"');
}

void JsonWriter::uint32(uint32_t value) {
    char digits[11];
    int len = snprintf(digits, sizeof(digits), "%lu", static_cast<unsigned long>(value));
    if (len <= 0) ok_ = false;
    else append(digits, static_cast<size_t>(len));
}

void JsonWriter::boolean(bool value) {
    if (value) append("true", 4);
    else append("false", 5);
}

void JsonWriter::null() {
    append("null", 4);
}

void JsonWriter::raw(const char* json) {
    if (!json) ok_ = false;
    else append(json, strlen(json));
}

void JsonWriter::raw(const char* json, size_t len) {
    if (!json) ok_ = false;
    else append(json, len);
}

void JsonWriter::fieldString(const char* name, const char* value) {
    key(name);
    beforeValue();
    string(value);
}

void JsonWriter::fieldUInt(const char* name, uint32_t value) {
    key(name);
    beforeValue();
    uint32(value);
}

void JsonWriter::fieldNull(const char* name) {
    key(name);
    beforeValue();
    null();
}

void JsonWriter::fieldRaw(const char* name, const char* json) {
    key(name);
    beforeValue();
    raw(json);
}

void JsonWriter::fieldRaw(const char* name, const char* json, size_t len) {
    key(name);
    beforeValue();
    raw(json, len);
}

bool JsonWriter::finish() {
    if (depth_ != 0) ok_ = false;
    return ok_;
}

bool encodeFrame(const char* json, size_t jsonLen, char* out, size_t capacity,
                 size_t& frameLen) {
    if (!json || jsonLen == 0 || jsonLen > JSON_MAX) return false;
    for (size_t i = 0; i < jsonLen; ++i) {
        if (json[i] == '\t' || json[i] == '\r' || json[i] == '\n') return false;
    }
    frameLen = jsonLen + 11;
    if (frameLen > FRAME_MAX || capacity < frameLen + 1) return false;
    out[0] = '@';
    memcpy(out + 1, json, jsonLen);
    out[jsonLen + 1] = '\t';
    snprintf(out + jsonLen + 2, 10, "%08lX\n",
             static_cast<unsigned long>(crc32(json, jsonLen)));
    out[frameLen] = '\0';
    return true;
}

} // namespace wlp2
