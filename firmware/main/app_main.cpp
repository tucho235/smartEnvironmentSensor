#include "matter_device.h"
#include "memory_diagnostics.h"
#include "sensor_service.h"
#include "wifi_station.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_CONFIG_PORTAL
#include "config_portal.h"
#endif

#if CONFIG_APP_ENABLE_MQTT_TELEMETRY
#include "mqtt_telemetry.h"
#endif

namespace {
constexpr const char *TAG = "smart_env";
#if CONFIG_APP_ENABLE_MQTT_TELEMETRY
constexpr uint32_t kMqttStartupDelayMs = 15000;
constexpr uint32_t kMqttCommissioningRetryDelayMs = 5000;
constexpr uint32_t kMqttCommissioningMaxWaitMs = 300000;
constexpr uint32_t kStartupTaskStackBytes = 2048;
#endif

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

#if CONFIG_APP_ENABLE_MQTT_TELEMETRY
void start_mqtt_telemetry()
{
    esp_err_t err = mqtt_telemetry_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT telemetry start requested");
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "MQTT telemetry not started; enable it and configure broker URI to use it");
    } else {
        ESP_LOGW(TAG, "MQTT telemetry not started: %s", esp_err_to_name(err));
    }
    memory_diagnostics_log(TAG, "After MQTT start attempt");
}

void delayed_mqtt_start_task(void *)
{
    ESP_LOGI(TAG, "MQTT telemetry startup delayed by %lu ms",
             static_cast<unsigned long>(kMqttStartupDelayMs));
    vTaskDelay(pdMS_TO_TICKS(kMqttStartupDelayMs));

    uint32_t waited_ms = 0;
    while (matter_device_is_commissioning_active() && waited_ms < kMqttCommissioningMaxWaitMs) {
        ESP_LOGI(TAG, "MQTT telemetry waiting for Matter commissioning to finish");
        vTaskDelay(pdMS_TO_TICKS(kMqttCommissioningRetryDelayMs));
        waited_ms += kMqttCommissioningRetryDelayMs;
    }

    start_mqtt_telemetry();
    vTaskDelete(nullptr);
}

esp_err_t schedule_delayed_mqtt_start()
{
    BaseType_t created = xTaskCreate(delayed_mqtt_start_task,
                                     "mqtt_startup",
                                     kStartupTaskStackBytes,
                                     nullptr,
                                     3,
                                     nullptr);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
#endif
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Environment Sensor firmware starting");
    ESP_LOGI(TAG, "Target board: ESP32-C3 SuperMini");
    ESP_LOGI(TAG, "Planned BME680 I2C wiring: SDA GPIO4, SCL GPIO5, address 0x76");
    memory_diagnostics_log(TAG, "Boot");

    esp_err_t err = initialize_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return;
    }
    memory_diagnostics_log(TAG, "After NVS init");

    err = sensor_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor service: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Sensor service started");
    memory_diagnostics_log(TAG, "After sensor service start");

    err = wifi_station_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi station start requested");
    } else {
        ESP_LOGW(TAG, "Wi-Fi station not started: %s", esp_err_to_name(err));
    }
    memory_diagnostics_log(TAG, "After Wi-Fi station start");

#if CONFIG_APP_ENABLE_CONFIG_PORTAL
    err = config_portal_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuration portal start requested");
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "Configuration portal already started");
    } else {
        ESP_LOGW(TAG, "Configuration portal not started: %s", esp_err_to_name(err));
    }
    memory_diagnostics_log(TAG, "After config portal start");
#else
    ESP_LOGI(TAG, "Configuration portal disabled by build configuration");
#endif

    err = matter_device_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Matter device start requested");
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Matter device disabled");
    } else {
        ESP_LOGW(TAG, "Matter device not started: %s", esp_err_to_name(err));
    }
    memory_diagnostics_log(TAG, "After Matter start");

#if CONFIG_APP_ENABLE_MQTT_TELEMETRY
    err = schedule_delayed_mqtt_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to schedule delayed MQTT telemetry start: %s", esp_err_to_name(err));
        start_mqtt_telemetry();
    }
#else
    ESP_LOGI(TAG, "MQTT telemetry disabled by build configuration");
#endif
}
