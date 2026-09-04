#pragma once

#include "esp_err.h"

struct MatterConfig {
    bool enabled;
};

constexpr const char *kMatterSetupQrPayload = "MT:Y.K9042C00KA0648G00";
constexpr const char *kMatterSetupQrUrl =
    "https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3AY.K9042C00KA0648G00";
constexpr const char *kMatterManualPairingCode = "34970112332";
constexpr const char *kMatterSetupPasscode = "20202021";
constexpr const char *kMatterDiscriminator = "3840";

esp_err_t matter_config_load(MatterConfig &config);
esp_err_t matter_config_save(const MatterConfig &config);
