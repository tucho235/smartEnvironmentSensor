#include "sensor_service.h"
#include "esp_err.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "smart_env";
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Environment Sensor firmware starting");
    ESP_LOGI(TAG, "Target board: ESP32-C3 SuperMini");
    ESP_LOGI(TAG, "Planned BME680 I2C wiring: SDA GPIO4, SCL GPIO5, address 0x76");

    esp_err_t err = sensor_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor service: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Sensor service started");
}
