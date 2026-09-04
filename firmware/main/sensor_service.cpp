#include "sensor_service.h"

#include "app_config.h"
#include "bme680_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "memory_diagnostics.h"

namespace {
constexpr const char *TAG = "sensor_task";

SemaphoreHandle_t s_snapshot_mutex = nullptr;
TaskHandle_t s_sensor_task_handle = nullptr;
SensorSnapshot s_latest_snapshot = {};

void update_snapshot(const Bme680Sample *sample, esp_err_t last_error)
{
    if (xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_latest_snapshot.last_error = last_error;
    if (sample != nullptr) {
        s_latest_snapshot.sample = *sample;
        s_latest_snapshot.timestamp_ms = esp_timer_get_time() / 1000;
        s_latest_snapshot.sequence += 1;
        s_latest_snapshot.valid = true;
    }

    xSemaphoreGive(s_snapshot_mutex);
}

void sensor_task(void *)
{
    Bme680Sensor sensor;
    uint32_t samples_since_diagnostics = 0;
    memory_diagnostics_log(TAG, "Sensor task started");

    while (true) {
        esp_err_t err = sensor.init();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "BME680 sensor task started");
            break;
        }

        ESP_LOGW(TAG, "BME680 init failed: %s; retrying in %lu ms",
                 esp_err_to_name(err), app_config::kSensorRetryIntervalMs);
        update_snapshot(nullptr, err);
        vTaskDelay(pdMS_TO_TICKS(app_config::kSensorRetryIntervalMs));
    }

    while (true) {
        Bme680Sample sample = {};
        esp_err_t err = sensor.read_sample(sample);
        if (err == ESP_OK) {
            update_snapshot(&sample, ESP_OK);
            ESP_LOGI(TAG, "BME680 sample: temperature=%.2f C, humidity=%.2f %%, pressure=%.2f hPa",
                     sample.temperature_c, sample.humidity_percent, sample.pressure_hpa);
            samples_since_diagnostics++;
            if (samples_since_diagnostics >= 30) {
                samples_since_diagnostics = 0;
                memory_diagnostics_log(TAG, "Sensor task periodic");
            }
        } else {
            update_snapshot(nullptr, err);
            ESP_LOGW(TAG, "Failed to read BME680 sample: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(app_config::kSensorSampleIntervalMs));
    }
}
} // namespace

esp_err_t sensor_service_start()
{
    if (s_sensor_task_handle != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_snapshot_mutex == nullptr) {
        s_snapshot_mutex = xSemaphoreCreateMutex();
        if (s_snapshot_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t created = xTaskCreate(sensor_task,
                                     "sensor_task",
                                     app_config::kSensorTaskStackWords,
                                     nullptr,
                                     app_config::kSensorTaskPriority,
                                     &s_sensor_task_handle);
    if (created != pdPASS) {
        s_sensor_task_handle = nullptr;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t sensor_service_get_latest(SensorSnapshot &snapshot)
{
    if (s_snapshot_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    snapshot = s_latest_snapshot;
    xSemaphoreGive(s_snapshot_mutex);

    return snapshot.valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}
