#include "bme680_sensor.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "smart_env";
constexpr TickType_t kSampleInterval = pdMS_TO_TICKS(3000);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Environment Sensor firmware starting");
    ESP_LOGI(TAG, "Target board: ESP32-C3 SuperMini");
    ESP_LOGI(TAG, "Planned BME680 I2C wiring: SDA GPIO4, SCL GPIO5, address 0x76");

    Bme680Sensor sensor;
    esp_err_t err = sensor.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BME680 startup failed: %s", esp_err_to_name(err));
        return;
    }

    while (true) {
        Bme680Sample sample = {};
        err = sensor.read_sample(sample);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "BME680 sample: temperature=%.2f C, humidity=%.2f %%, pressure=%.2f hPa",
                     sample.temperature_c, sample.humidity_percent, sample.pressure_hpa);
        } else {
            ESP_LOGW(TAG, "Failed to read BME680 sample: %s", esp_err_to_name(err));
        }

        vTaskDelay(kSampleInterval);
    }
}
