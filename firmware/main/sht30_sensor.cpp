#include "sht30_sensor.h"

#include <cstddef>

#include "app_config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "SHT30";
constexpr uint8_t kSoftResetCommand[] = {0x30, 0xA2};
constexpr uint8_t kHighRepeatabilityCommand[] = {0x24, 0x00};
constexpr size_t kMeasurementResponseSize = 6;
constexpr uint32_t kSoftResetDelayMs = 2;
constexpr uint32_t kMeasurementDelayMs = 20;
} // namespace

esp_err_t Sht30Sensor::init()
{
    cleanup();

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = app_config::kI2cSdaGpio;
    bus_config.scl_io_num = app_config::kI2cSclGpio;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus initialized: SDA GPIO%d, SCL GPIO%d, %lu Hz",
             app_config::kI2cSdaGpio,
             app_config::kI2cSclGpio,
             static_cast<unsigned long>(app_config::kI2cClockHz));

    err = i2c_master_probe(bus_handle_, app_config::kSht30I2cAddress, app_config::kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHT30 not detected at address 0x%02X: %s",
                 app_config::kSht30I2cAddress,
                 esp_err_to_name(err));
        cleanup();
        return err;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = app_config::kSht30I2cAddress;
    device_config.scl_speed_hz = app_config::kI2cClockHz;

    err = i2c_master_bus_add_device(bus_handle_, &device_config, &device_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SHT30 I2C device: %s", esp_err_to_name(err));
        cleanup();
        return err;
    }

    err = i2c_master_transmit(device_handle_,
                              kSoftResetCommand,
                              sizeof(kSoftResetCommand),
                              app_config::kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHT30 soft reset failed: %s", esp_err_to_name(err));
        cleanup();
        return err;
    }

    const TickType_t reset_delay = pdMS_TO_TICKS(kSoftResetDelayMs);
    vTaskDelay(reset_delay == 0 ? 1 : reset_delay);
    initialized_ = true;
    ESP_LOGI(TAG, "SHT30 initialized at address 0x%02X", app_config::kSht30I2cAddress);
    return ESP_OK;
}

esp_err_t Sht30Sensor::read_sample(SensorSample &sample)
{
    if (!initialized_ || device_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_master_transmit(device_handle_,
                                        kHighRepeatabilityCommand,
                                        sizeof(kHighRepeatabilityCommand),
                                        app_config::kI2cTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(kMeasurementDelayMs));

    uint8_t response[kMeasurementResponseSize] = {};
    err = i2c_master_receive(device_handle_, response, sizeof(response), app_config::kI2cTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }

    if (crc8(response, 2) != response[2] || crc8(response + 3, 2) != response[5]) {
        ESP_LOGW(TAG, "SHT30 measurement CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    const uint16_t raw_temperature = (static_cast<uint16_t>(response[0]) << 8) | response[1];
    const uint16_t raw_humidity = (static_cast<uint16_t>(response[3]) << 8) | response[4];

    sample.temperature_c = -45.0f + 175.0f * static_cast<float>(raw_temperature) / 65535.0f;
    sample.humidity_percent = 100.0f * static_cast<float>(raw_humidity) / 65535.0f;
    return ESP_OK;
}

uint8_t Sht30Sensor::crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) != 0 ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                                    : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

void Sht30Sensor::cleanup()
{
    if (device_handle_ != nullptr) {
        i2c_master_bus_rm_device(device_handle_);
        device_handle_ = nullptr;
    }
    if (bus_handle_ != nullptr) {
        i2c_del_master_bus(bus_handle_);
        bus_handle_ = nullptr;
    }
    initialized_ = false;
}
