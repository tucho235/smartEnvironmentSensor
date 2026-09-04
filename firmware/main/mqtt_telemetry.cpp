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
#include "memory_diagnostics.h"
#include "mqtt_config.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "sensor_service.h"
#include "wifi_station.h"

namespace {
constexpr const char *TAG = "mqtt_telemetry";
constexpr uint32_t kMqttTelemetryTaskStackWords = 3072;
constexpr int kMqttTelemetryTaskPriority = 4;
constexpr int kMqttNetworkTimeoutMs = 5000;
constexpr int kMqttKeepaliveSeconds = 60;
constexpr size_t kPayloadBufferSize = 192;

esp_mqtt_client_handle_t s_client = nullptr;
TaskHandle_t s_publish_task_handle = nullptr;
MqttConfig s_config = {};
std::atomic<bool> s_started{false};
std::atomic<bool> s_network_events_registered{false};
std::atomic<bool> s_connected{false};
std::atomic<bool> s_client_started{false};
std::atomic<bool> s_configuration_logged_missing{false};
std::atomic<bool> s_configuration_logged_disabled{false};

const char *nullable_config_string(const char *value)
{
    return std::strlen(value) == 0 ? nullptr : value;
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
        if (s_client == nullptr) {
            return;
        }

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
    bool expected = false;
    if (!s_network_events_registered.compare_exchange_strong(expected, true)) {
        return ESP_OK;
    }

    esp_err_t err = esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        network_event_handler,
                                                        nullptr,
                                                        nullptr);
    if (err != ESP_OK) {
        s_network_events_registered = false;
        ESP_LOGE(TAG, "Failed to register MQTT IP event handler: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              WIFI_EVENT_STA_DISCONNECTED,
                                              network_event_handler,
                                              nullptr,
                                              nullptr);
    if (err != ESP_OK) {
        s_network_events_registered = false;
        ESP_LOGE(TAG, "Failed to register MQTT Wi-Fi event handler: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

uint32_t publish_interval_ms()
{
    return s_config.publish_interval_ms == 0 ? CONFIG_APP_MQTT_PUBLISH_INTERVAL_MS : s_config.publish_interval_ms;
}

esp_err_t initialize_mqtt_client_from_config()
{
    if (s_client != nullptr) {
        return ESP_OK;
    }

    esp_err_t err = mqtt_config_load(s_config);
    if (err == ESP_ERR_NOT_FOUND && !wifi_station_is_provisioning_active()) {
        err = mqtt_config_seed_from_kconfig_if_empty();
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to seed MQTT configuration from sdkconfig: %s", esp_err_to_name(err));
        }
        if (err == ESP_OK) {
            err = mqtt_config_load(s_config);
        }
    }

    if (err == ESP_ERR_NOT_FOUND) {
        bool expected = false;
        if (s_configuration_logged_missing.compare_exchange_strong(expected, true)) {
            ESP_LOGW(TAG, "MQTT telemetry waiting for configuration over BLE provisioning or idf.py menuconfig");
        }
        return err;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load MQTT configuration: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_config.enabled) {
        bool expected = false;
        if (s_configuration_logged_disabled.compare_exchange_strong(expected, true)) {
            ESP_LOGI(TAG, "MQTT telemetry disabled by configuration");
        }
        return ESP_ERR_INVALID_STATE;
    }

    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = s_config.broker_uri;
    mqtt_config.credentials.username = nullable_config_string(s_config.username);
    mqtt_config.credentials.authentication.password = nullable_config_string(s_config.password);
    mqtt_config.network.reconnect_timeout_ms = CONFIG_APP_MQTT_RECONNECT_INTERVAL_MS;
    mqtt_config.network.timeout_ms = kMqttNetworkTimeoutMs;
    mqtt_config.session.keepalive = kMqttKeepaliveSeconds;

    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    err = esp_mqtt_client_register_event(s_client,
                                         MQTT_EVENT_ANY,
                                         mqtt_event_handler,
                                         nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return err;
    }

    ESP_LOGI(TAG, "MQTT telemetry configured: topic=%s, interval=%lu ms",
             s_config.topic,
             static_cast<unsigned long>(s_config.publish_interval_ms));

    if (wifi_station_is_connected()) {
        ESP_RETURN_ON_ERROR(start_mqtt_client_if_needed(), TAG, "Failed to start MQTT client");
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

bool publish_latest_snapshot()
{
    SensorSnapshot snapshot = {};
    esp_err_t err = sensor_service_get_latest(snapshot);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No valid sensor snapshot available for MQTT publish: %s", esp_err_to_name(err));
        return false;
    }

    char payload[kPayloadBufferSize] = {};
    int payload_length = 0;
    err = format_payload(snapshot, payload, sizeof(payload), payload_length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to format MQTT payload: %s", esp_err_to_name(err));
        return false;
    }

    int msg_id = esp_mqtt_client_publish(s_client,
                                         s_config.topic,
                                         payload,
                                         payload_length,
                                         0,
                                         0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to submit MQTT telemetry, result=%d", msg_id);
        return false;
    }

    ESP_LOGI(TAG, "MQTT telemetry submitted, sequence=%lu, msg_id=%d",
             static_cast<unsigned long>(snapshot.sequence),
             msg_id);
    return true;
}

void mqtt_publish_task(void *)
{
    uint32_t published_since_diagnostics = 0;
    memory_diagnostics_log(TAG, "MQTT task started");

    while (true) {
        if (s_client == nullptr) {
            esp_err_t err = initialize_mqtt_client_from_config();
            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGI(TAG, "MQTT telemetry task stopping because MQTT is disabled");
                break;
            }
            if (err == ESP_ERR_NOT_FOUND && !wifi_station_is_provisioning_active()) {
                ESP_LOGD(TAG, "MQTT telemetry waiting for configuration");
            }
            if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "MQTT telemetry remains disabled: %s", esp_err_to_name(err));
            }
        }

        if (s_connected && s_client != nullptr) {
            if (publish_latest_snapshot()) {
                published_since_diagnostics++;
                if (published_since_diagnostics >= 12) {
                    published_since_diagnostics = 0;
                    memory_diagnostics_log(TAG, "MQTT task periodic");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(publish_interval_ms()));
    }

    s_started = false;
    s_publish_task_handle = nullptr;
    memory_diagnostics_log(TAG, "MQTT task stopped");
    vTaskDelete(nullptr);
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

    esp_err_t err = register_network_events();
    if (err != ESP_OK) {
        return err;
    }

    err = initialize_mqtt_client_from_config();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize MQTT telemetry: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_ERR_INVALID_STATE;
    }

    err = create_publish_task();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MQTT telemetry task: %s", esp_err_to_name(err));
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "MQTT telemetry service started");
    return ESP_OK;
}
