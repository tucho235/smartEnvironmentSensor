#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_types.h"
#include "esp_err.h"
#include "sensor_sample.h"

class Sht30Sensor {
public:
    esp_err_t init();
    esp_err_t read_sample(SensorSample &sample);

private:
    static uint8_t crc8(const uint8_t *data, size_t length);
    void cleanup();

    i2c_master_bus_handle_t bus_handle_ = nullptr;
    i2c_master_dev_handle_t device_handle_ = nullptr;
    bool initialized_ = false;
};
