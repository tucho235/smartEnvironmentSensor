#pragma once

#include <cstdint>

#include "bme68x.h"
#include "driver/i2c_types.h"
#include "esp_err.h"

struct Bme680Sample {
    float temperature_c;
    float humidity_percent;
    float pressure_hpa;
};

class Bme680Sensor {
public:
    esp_err_t init();
    esp_err_t read_sample(Bme680Sample &sample);

private:
    static int8_t i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static int8_t i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static void delay_us(uint32_t period_us, void *intf_ptr);

    esp_err_t read_register(uint8_t reg_addr, uint8_t *data, uint32_t len);
    esp_err_t write_register(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len);
    void cleanup();

    i2c_master_bus_handle_t bus_handle_ = nullptr;
    i2c_master_dev_handle_t device_handle_ = nullptr;
    bme68x_dev dev_ = {};
    bme68x_conf conf_ = {};
    bool initialized_ = false;
};
