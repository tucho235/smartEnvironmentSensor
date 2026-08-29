#include "bme680_sensor.h"

#include <cstdint>

#include "bme68x.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "BME680";

constexpr gpio_num_t kI2cSdaGpio = GPIO_NUM_4;
constexpr gpio_num_t kI2cSclGpio = GPIO_NUM_5;
constexpr uint8_t kBme680Address = 0x76;
constexpr uint32_t kI2cClockHz = 100000;
constexpr int kI2cTimeoutMs = 100;

esp_err_t bme68x_status_to_esp_err(int8_t status)
{
    switch (status) {
    case BME68X_OK:
        return ESP_OK;
    case BME68X_E_NULL_PTR:
        return ESP_ERR_INVALID_ARG;
    case BME68X_E_DEV_NOT_FOUND:
        return ESP_ERR_NOT_FOUND;
    case BME68X_E_COM_FAIL:
        return ESP_FAIL;
    case BME68X_E_INVALID_LENGTH:
        return ESP_ERR_INVALID_SIZE;
    default:
        return ESP_FAIL;
    }
}
}

esp_err_t Bme680Sensor::init()
{
    cleanup();

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = kI2cSdaGpio;
    bus_config.scl_io_num = kI2cSclGpio;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus initialized: SDA GPIO%d, SCL GPIO%d, %lu Hz",
             kI2cSdaGpio, kI2cSclGpio, kI2cClockHz);

    err = i2c_master_probe(bus_handle_, kBme680Address, kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BME680 not detected at address 0x%02X: %s",
                 kBme680Address, esp_err_to_name(err));
        cleanup();
        return err;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = kBme680Address;
    device_config.scl_speed_hz = kI2cClockHz;

    err = i2c_master_bus_add_device(bus_handle_, &device_config, &device_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add BME680 I2C device: %s", esp_err_to_name(err));
        cleanup();
        return err;
    }

    dev_ = {};
    dev_.intf = BME68X_I2C_INTF;
    dev_.read = Bme680Sensor::i2c_read;
    dev_.write = Bme680Sensor::i2c_write;
    dev_.delay_us = Bme680Sensor::delay_us;
    dev_.intf_ptr = this;
    dev_.amb_temp = 25;

    int8_t status = bme68x_init(&dev_);
    if (status != BME68X_OK) {
        err = bme68x_status_to_esp_err(status);
        ESP_LOGE(TAG, "BME680 init failed: status %d, %s", status, esp_err_to_name(err));
        cleanup();
        return err;
    }

    conf_ = {};
    conf_.filter = BME68X_FILTER_OFF;
    conf_.odr = BME68X_ODR_NONE;
    conf_.os_hum = BME68X_OS_2X;
    conf_.os_pres = BME68X_OS_4X;
    conf_.os_temp = BME68X_OS_8X;

    status = bme68x_set_conf(&conf_, &dev_);
    if (status != BME68X_OK) {
        err = bme68x_status_to_esp_err(status);
        ESP_LOGE(TAG, "BME680 configuration failed: status %d, %s", status, esp_err_to_name(err));
        cleanup();
        return err;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "BME680 initialized and configured");
    return ESP_OK;
}

esp_err_t Bme680Sensor::read_sample(Bme680Sample &sample)
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    int8_t status = bme68x_set_op_mode(BME68X_FORCED_MODE, &dev_);
    if (status != BME68X_OK) {
        return bme68x_status_to_esp_err(status);
    }

    const uint32_t measurement_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf_, &dev_);
    dev_.delay_us(measurement_us, dev_.intf_ptr);

    bme68x_data data = {};
    uint8_t data_count = 0;
    status = bme68x_get_data(BME68X_FORCED_MODE, &data, &data_count, &dev_);
    if (status != BME68X_OK || data_count == 0) {
        ESP_LOGW(TAG, "No BME680 sample available: status %d, count %u", status, data_count);
        return bme68x_status_to_esp_err(status);
    }

    sample.temperature_c = data.temperature;
    sample.humidity_percent = data.humidity;
    sample.pressure_hpa = data.pressure / 100.0f;

    return ESP_OK;
}

int8_t Bme680Sensor::i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    auto *sensor = static_cast<Bme680Sensor *>(intf_ptr);
    return sensor->read_register(reg_addr, reg_data, len) == ESP_OK ? BME68X_OK : BME68X_E_COM_FAIL;
}

int8_t Bme680Sensor::i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    auto *sensor = static_cast<Bme680Sensor *>(intf_ptr);
    return sensor->write_register(reg_addr, reg_data, len) == ESP_OK ? BME68X_OK : BME68X_E_COM_FAIL;
}

void Bme680Sensor::delay_us(uint32_t period_us, void *)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS((period_us + 999) / 1000);
    vTaskDelay(delay_ticks == 0 ? 1 : delay_ticks);
}

esp_err_t Bme680Sensor::read_register(uint8_t reg_addr, uint8_t *data, uint32_t len)
{
    if (device_handle_ == nullptr || data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(device_handle_, &reg_addr, sizeof(reg_addr), data, len, kI2cTimeoutMs);
}

esp_err_t Bme680Sensor::write_register(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len)
{
    if (device_handle_ == nullptr || reg_data == nullptr || len == 0 || len > 31) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[32] = {};
    buffer[0] = reg_addr;
    for (uint32_t i = 0; i < len; ++i) {
        buffer[i + 1] = reg_data[i];
    }

    return i2c_master_transmit(device_handle_, buffer, len + 1, kI2cTimeoutMs);
}

void Bme680Sensor::cleanup()
{
    if (device_handle_ != nullptr) {
        i2c_master_bus_rm_device(device_handle_);
        device_handle_ = nullptr;
    }

    if (bus_handle_ != nullptr) {
        i2c_del_master_bus(bus_handle_);
        bus_handle_ = nullptr;
    }

    dev_ = {};
    conf_ = {};
    initialized_ = false;
}
