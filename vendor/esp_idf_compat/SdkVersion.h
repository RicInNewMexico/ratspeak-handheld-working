#pragma once
#include "esp_idf_version.h"
#include "sdkconfig.h"
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(4, 4, 7) || !CONFIG_IDF_TARGET_ESP32S3
#error "Review/remove the Ratspeak compatibility backports when changing the ESP32-S3 SDK"
#endif
