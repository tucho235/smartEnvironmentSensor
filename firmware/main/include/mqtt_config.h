#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

struct MqttConfig {
    bool enabled;
    char broker_uri[128];
    char username[64];
    char password[64];
    char topic[128];
    uint32_t publish_interval_ms;
};

bool mqtt_config_is_enabled(const MqttConfig &config);
esp_err_t mqtt_config_load(MqttConfig &config);
esp_err_t mqtt_config_save(const MqttConfig &config);
esp_err_t mqtt_config_save_json(const char *json, size_t length);
esp_err_t mqtt_config_seed_from_kconfig_if_empty();
