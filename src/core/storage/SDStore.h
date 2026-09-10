#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
namespace handheld::storage { enum class Error : uint8_t; class AtomicSource; }

class SDStore {
public:
    bool begin(SPIClass* spi, int csPin);
    void end();

    bool writeAtomic(const char* path, const uint8_t* data, size_t len);
    handheld::storage::Error writeAtomic(const char* path, const handheld::storage::AtomicSource& source);
    bool writeSimple(const char* path, const uint8_t* data, size_t len);
    // WriteQueue-facing alias (cardputer donor name)
    bool writeDirect(const char* path, const uint8_t* data, size_t len) {
        return writeSimple(path, data, len);
    }
    bool writeString(const char* path, const String& data);
    String readString(const char* path, bool (*reserve)(String&, size_t) = nullptr,
                      bool* admissionRefused = nullptr);

    bool ensureDir(const char* path);
    bool exists(const char* path);
    bool remove(const char* path);
    bool rename(const char* from, const char* to);
    File openDir(const char* path);
    File openFile(const char* path, const char* mode = "r");
    bool removeDir(const char* path);
    bool readFile(const char* path, uint8_t* buffer, size_t maxLen, size_t& bytesRead);

    bool isReady() const { return _ready; }
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;

    bool formatForRsDeck();
    bool wipeRsDeck();
    bool hasExistingData();

private:
    bool wipeDir(const char* path);
    bool _ready = false;
};
