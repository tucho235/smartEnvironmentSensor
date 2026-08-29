#pragma once

#include <cstdint>

#include "esp_err.h"
#include "sensor_sample.h"

struct SensorSnapshot {
    Bme680Sample sample;
    int64_t timestamp_ms;
    uint32_t sequence;
    esp_err_t last_error;
    bool valid;
};

esp_err_t sensor_service_start();
esp_err_t sensor_service_get_latest(SensorSnapshot &snapshot);
