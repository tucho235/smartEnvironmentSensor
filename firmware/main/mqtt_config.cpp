#include "mqtt_config.h"

#include <algorithm>
#include <cstring>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace {
constexpr const char *TAG = "mqtt_config";
constexpr const char *kNamespace = "mqtt";
constexpr const char *kBrokerUriKey = "uri";
constexpr const char *kUsernameKey = "user";
constexpr const char *kPasswordKey = "pass";
constexpr const char *kTopicKey = "topic";
constexpr const char *kPublishIntervalKey = "pub_ms";
constexpr const char *kEnabledKey = "enabled";

bool copy_string(char *destination, size_t destination_size, const char *source)
{
    if (source == nullptr) {
        source = "";
    }

    size_t length = std::strlen(source);
    if (length >= destination_size) {
        return false;
    }

    std::memcpy(destination, source, length + 1);
    return true;
}

esp_err_t get_string(nvs_handle_t handle, const char *key, char *destination, size_t destination_size)
{
    size_t required_size = destination_size;
    esp_err_t err = nvs_get_str(handle, key, destination, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        destination[0] = '\0';
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGW(TAG, "Stored MQTT value is too large: key=%s", key);
    }

    return err;
}

esp_err_t set_string(nvs_handle_t handle, const char *key, const char *value)
{
    if (std::strlen(value) == 0) {
        esp_err_t err = nvs_erase_key(handle, key);
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }

    return nvs_set_str(handle, key, value);
}

const cJSON *optional_json_string(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == nullptr) {
        return nullptr;
    }
    return cJSON_IsString(item) ? item : nullptr;
}

esp_err_t load_kconfig(MqttConfig &config)
{
    config.enabled = true;
    if (!copy_string(config.broker_uri, sizeof(config.broker_uri), CONFIG_APP_MQTT_BROKER_URI) ||
        !copy_string(config.username, sizeof(config.username), CONFIG_APP_MQTT_USERNAME) ||
        !copy_string(config.password, sizeof(config.password), CONFIG_APP_MQTT_PASSWORD) ||
        !copy_string(config.topic, sizeof(config.topic), CONFIG_APP_MQTT_TELEMETRY_TOPIC)) {
        return ESP_ERR_INVALID_SIZE;
    }

    config.publish_interval_ms = CONFIG_APP_MQTT_PUBLISH_INTERVAL_MS;
    return ESP_OK;
}
} // namespace

bool mqtt_config_is_enabled(const MqttConfig &config)
{
    return config.enabled && std::strlen(config.broker_uri) > 0;
}

esp_err_t mqtt_config_load(MqttConfig &config)
{
    config = {};
    config.enabled = true;
    config.publish_interval_ms = CONFIG_APP_MQTT_PUBLISH_INTERVAL_MS;
    if (!copy_string(config.topic, sizeof(config.topic), CONFIG_APP_MQTT_TELEMETRY_TOPIC)) {
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = get_string(handle, kBrokerUriKey, config.broker_uri, sizeof(config.broker_uri));
    if (err == ESP_OK) {
        err = get_string(handle, kUsernameKey, config.username, sizeof(config.username));
    }
    if (err == ESP_OK) {
        err = get_string(handle, kPasswordKey, config.password, sizeof(config.password));
    }
    if (err == ESP_OK) {
        err = get_string(handle, kTopicKey, config.topic, sizeof(config.topic));
    }
    if (err == ESP_OK) {
        uint8_t enabled = 1;
        esp_err_t enabled_err = nvs_get_u8(handle, kEnabledKey, &enabled);
        if (enabled_err == ESP_OK) {
            config.enabled = enabled != 0;
        } else if (enabled_err != ESP_ERR_NVS_NOT_FOUND) {
            err = enabled_err;
        }
    }
    if (err == ESP_OK) {
        uint32_t publish_interval_ms = 0;
        esp_err_t interval_err = nvs_get_u32(handle, kPublishIntervalKey, &publish_interval_ms);
        if (interval_err == ESP_OK && publish_interval_ms > 0) {
            config.publish_interval_ms = publish_interval_ms;
        } else if (interval_err != ESP_OK && interval_err != ESP_ERR_NVS_NOT_FOUND) {
            err = interval_err;
        }
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }

    if (!config.enabled) {
        return ESP_OK;
    }

    return mqtt_config_is_enabled(config) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t mqtt_config_save(const MqttConfig &config)
{
    if (config.enabled && !mqtt_config_is_enabled(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strlen(config.topic) == 0 || config.publish_interval_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = set_string(handle, kBrokerUriKey, config.broker_uri);
    if (err == ESP_OK) {
        err = set_string(handle, kUsernameKey, config.username);
    }
    if (err == ESP_OK) {
        err = set_string(handle, kPasswordKey, config.password);
    }
    if (err == ESP_OK) {
        err = set_string(handle, kTopicKey, config.topic);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, kPublishIntervalKey, config.publish_interval_ms);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kEnabledKey, config.enabled ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t mqtt_config_save_json(const char *json, size_t length)
{
    if (json == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    MqttConfig config = {};
    esp_err_t err = mqtt_config_load(config);
    if (err != ESP_OK) {
        err = load_kconfig(config);
    }

    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (err == ESP_OK && enabled != nullptr) {
        if (!cJSON_IsBool(enabled)) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            config.enabled = cJSON_IsTrue(enabled);
        }
    }

    const cJSON *broker_uri = optional_json_string(root, "broker_uri");
    if (broker_uri != nullptr && !copy_string(config.broker_uri, sizeof(config.broker_uri), broker_uri->valuestring)) {
        err = ESP_ERR_INVALID_SIZE;
    }

    const cJSON *username = optional_json_string(root, "username");
    if (err == ESP_OK && username != nullptr && !copy_string(config.username, sizeof(config.username), username->valuestring)) {
        err = ESP_ERR_INVALID_SIZE;
    }

    const cJSON *password = optional_json_string(root, "password");
    if (err == ESP_OK && password != nullptr && !copy_string(config.password, sizeof(config.password), password->valuestring)) {
        err = ESP_ERR_INVALID_SIZE;
    }

    const cJSON *topic = optional_json_string(root, "topic");
    if (err == ESP_OK && topic != nullptr && !copy_string(config.topic, sizeof(config.topic), topic->valuestring)) {
        err = ESP_ERR_INVALID_SIZE;
    }

    const cJSON *publish_interval_ms = cJSON_GetObjectItemCaseSensitive(root, "publish_interval_ms");
    if (err == ESP_OK && publish_interval_ms != nullptr) {
        if (!cJSON_IsNumber(publish_interval_ms) || publish_interval_ms->valuedouble < 1000.0) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            config.publish_interval_ms = static_cast<uint32_t>(std::min(publish_interval_ms->valuedouble, 3600000.0));
        }
    }

    if (err == ESP_OK) {
        err = mqtt_config_save(config);
    }

    cJSON_Delete(root);
    return err;
}

esp_err_t mqtt_config_seed_from_kconfig_if_empty()
{
    MqttConfig existing = {};
    esp_err_t err = mqtt_config_load(existing);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    MqttConfig config = {};
    err = load_kconfig(config);
    if (err != ESP_OK) {
        return err;
    }
    if (!mqtt_config_is_enabled(config)) {
        return ESP_ERR_NOT_FOUND;
    }

    err = mqtt_config_save(config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Seeded MQTT configuration from local sdkconfig into NVS");
    }

    return err;
}
