#include "mqtt_telemetry.h"
#include "sensor_service.h"
#include "wifi_station.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

namespace {
constexpr const char *TAG = "smart_env";

esp_err_t initialize_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init needs erase: %s", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS");
        err = nvs_flash_init();
    }

    return err;
}
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Environment Sensor firmware starting");
    ESP_LOGI(TAG, "Target board: ESP32-C3 SuperMini");
    ESP_LOGI(TAG, "Planned BME680 I2C wiring: SDA GPIO4, SCL GPIO5, address 0x76");

    esp_err_t err = initialize_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return;
    }

    err = sensor_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor service: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Sensor service started");

    err = wifi_station_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi station start requested");
    } else {
        ESP_LOGW(TAG, "Wi-Fi station not started: %s", esp_err_to_name(err));
    }

    err = mqtt_telemetry_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT telemetry start requested");
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "MQTT telemetry not started; configure broker URI to enable it");
    } else {
        ESP_LOGW(TAG, "MQTT telemetry not started: %s", esp_err_to_name(err));
    }
}
