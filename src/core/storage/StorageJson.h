#pragma once

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

namespace handheld::storage {

// ArduinoJson emits common escaped controls and NUL correctly, but emits some
// other C0 bytes literally. Compact JSON has no control bytes outside strings;
// normalizing those bytes makes the existing serializer's output standard JSON.
// This writer does not parse JSON or duplicate the record schema.
template<class Writer>
class JsonWriter {
public:
    explicit JsonWriter(Writer& writer) : _writer(writer) {}

    size_t write(uint8_t byte) {
        if (!_ok) return 0;
        if (byte < 0x20) {
            constexpr char hex[] = "0123456789abcdef";
            const uint8_t escaped[] = {'\\', 'u', '0', '0',
                uint8_t(hex[byte >> 4]), uint8_t(hex[byte & 15])};
            _ok = _writer.write(escaped, sizeof(escaped)) == sizeof(escaped);
            if (_ok) _bytes += sizeof(escaped);
        } else {
            _ok = _writer.write(&byte, 1) == 1;
            if (_ok) ++_bytes;
        }
        return _ok ? 1 : 0;
    }

    size_t write(const uint8_t* data, size_t length) {
        size_t done = 0;
        while (done < length && write(data[done])) ++done;
        return done;
    }
    bool ok() const { return _ok; }
    size_t bytesWritten() const { return _bytes; }

private:
    Writer& _writer;
    size_t _bytes = 0;
    bool _ok = true;
};

struct JsonByteCounter {
    size_t write(const uint8_t*, size_t length) { return length; }
};

inline size_t measureStoredJson(ArduinoJson::JsonVariantConst document) {
    JsonByteCounter counter;
    JsonWriter<JsonByteCounter> writer(counter);
    serializeJson(document, writer);
    return writer.bytesWritten();
}

template<class Writer>
bool writeStoredJson(ArduinoJson::JsonVariantConst document, Writer& destination, size_t expected) {
    JsonWriter<Writer> writer(destination);
    serializeJson(document, writer);
    return writer.ok() && writer.bytesWritten() == expected;
}

} // namespace handheld::storage
