#include "mqtt_telemetry.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "sensor_service.h"
#include "wifi_station.h"

namespace {
constexpr const char *TAG = "mqtt_telemetry";
constexpr uint32_t kMqttTelemetryTaskStackWords = 4096;
constexpr int kMqttTelemetryTaskPriority = 4;
constexpr int kMqttNetworkTimeoutMs = 5000;
constexpr int kMqttKeepaliveSeconds = 60;
constexpr size_t kPayloadBufferSize = 192;

esp_mqtt_client_handle_t s_client = nullptr;
TaskHandle_t s_publish_task_handle = nullptr;
std::atomic<bool> s_started{false};
std::atomic<bool> s_connected{false};
std::atomic<bool> s_client_started{false};

const char *nullable_config_string(const char *value)
{
    return std::strlen(value) == 0 ? nullptr : value;
}

bool mqtt_configured()
{
    return std::strlen(CONFIG_APP_MQTT_BROKER_URI) > 0;
}

void mqtt_event_handler(void *, esp_event_base_t, int32_t event_id, void *event_data)
{
    auto event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT publish acknowledged, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle == nullptr) {
            ESP_LOGW(TAG, "MQTT error without details");
            break;
        }

        if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGW(TAG, "MQTT connection refused, return_code=%d",
                     event->error_handle->connect_return_code);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGW(TAG, "MQTT transport error, esp_err=0x%x, sock_errno=%d",
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_transport_sock_errno);
        } else {
            ESP_LOGW(TAG, "MQTT error, type=%d", event->error_handle->error_type);
        }
        break;
    default:
        break;
    }
}

esp_err_t start_mqtt_client_if_needed()
{
    if (s_client == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    bool expected = false;
    if (!s_client_started.compare_exchange_strong(expected, true)) {
        return ESP_OK;
    }

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        s_client_started = false;
        ESP_LOGW(TAG, "Failed to start MQTT client after Wi-Fi connected: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MQTT client start requested after Wi-Fi connected");
    return ESP_OK;
}

void network_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        esp_err_t err = start_mqtt_client_if_needed();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MQTT client not started on Wi-Fi connect: %s", esp_err_to_name(err));
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
    }
}

esp_err_t register_network_events()
{
    esp_err_t err = esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        network_event_handler,
                                                        nullptr,
                                                        nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT IP event handler: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              WIFI_EVENT_STA_DISCONNECTED,
                                              network_event_handler,
                                              nullptr,
                                              nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT Wi-Fi event handler: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t format_payload(const SensorSnapshot &snapshot, char *payload, size_t payload_size, int &payload_length)
{
    int written = std::snprintf(payload,
                                payload_size,
                                "{\"temperature_c\":%.2f,\"humidity_percent\":%.2f,\"pressure_hpa\":%.2f}",
                                snapshot.sample.temperature_c,
                                snapshot.sample.humidity_percent,
                                snapshot.sample.pressure_hpa);
    if (written < 0 || static_cast<size_t>(written) >= payload_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    payload_length = written;
    return ESP_OK;
}

void publish_latest_snapshot()
{
    SensorSnapshot snapshot = {};
    esp_err_t err = sensor_service_get_latest(snapshot);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No valid sensor snapshot available for MQTT publish: %s", esp_err_to_name(err));
        return;
    }

    char payload[kPayloadBufferSize] = {};
    int payload_length = 0;
    err = format_payload(snapshot, payload, sizeof(payload), payload_length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to format MQTT payload: %s", esp_err_to_name(err));
        return;
    }

    int msg_id = esp_mqtt_client_enqueue(s_client,
                                         CONFIG_APP_MQTT_TELEMETRY_TOPIC,
                                         payload,
                                         payload_length,
                                         0,
                                         0,
                                         true);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to enqueue MQTT telemetry, result=%d", msg_id);
        return;
    }

    ESP_LOGI(TAG, "MQTT telemetry queued, sequence=%lu, msg_id=%d",
             static_cast<unsigned long>(snapshot.sequence),
             msg_id);
}

void mqtt_publish_task(void *)
{
    while (true) {
        if (s_connected && s_client != nullptr) {
            publish_latest_snapshot();
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_MQTT_PUBLISH_INTERVAL_MS));
    }
}

esp_err_t create_publish_task()
{
    BaseType_t created = xTaskCreate(mqtt_publish_task,
                                     "mqtt_telemetry",
                                     kMqttTelemetryTaskStackWords,
                                     nullptr,
                                     kMqttTelemetryTaskPriority,
                                     &s_publish_task_handle);
    if (created != pdPASS) {
        s_publish_task_handle = nullptr;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
} // namespace

esp_err_t mqtt_telemetry_start()
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mqtt_configured()) {
        ESP_LOGW(TAG, "MQTT telemetry disabled; configure APP_MQTT_BROKER_URI with idf.py menuconfig");
        return ESP_ERR_INVALID_STATE;
    }

    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = CONFIG_APP_MQTT_BROKER_URI;
    mqtt_config.credentials.username = nullable_config_string(CONFIG_APP_MQTT_USERNAME);
    mqtt_config.credentials.authentication.password = nullable_config_string(CONFIG_APP_MQTT_PASSWORD);
    mqtt_config.network.reconnect_timeout_ms = CONFIG_APP_MQTT_RECONNECT_INTERVAL_MS;
    mqtt_config.network.timeout_ms = kMqttNetworkTimeoutMs;
    mqtt_config.session.keepalive = kMqttKeepaliveSeconds;

    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client,
                                                   MQTT_EVENT_ANY,
                                                   mqtt_event_handler,
                                                   nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return err;
    }

    err = create_publish_task();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MQTT telemetry task: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return err;
    }

    err = register_network_events();
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "MQTT telemetry waiting for Wi-Fi: topic=%s, interval=%d ms",
             CONFIG_APP_MQTT_TELEMETRY_TOPIC,
             CONFIG_APP_MQTT_PUBLISH_INTERVAL_MS);

    if (wifi_station_is_connected()) {
        ESP_RETURN_ON_ERROR(start_mqtt_client_if_needed(), TAG, "Failed to start MQTT client");
    }

    return ESP_OK;
}
