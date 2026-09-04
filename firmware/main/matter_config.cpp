#include "matter_config.h"

#include "nvs.h"

namespace {
constexpr const char *kNamespace = "matter";
constexpr const char *kEnabledKey = "enabled";
} // namespace

esp_err_t matter_config_load(MatterConfig &config)
{
    config = {};
    config.enabled = true;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t enabled = 1;
    err = nvs_get_u8(handle, kEnabledKey, &enabled);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    config.enabled = enabled != 0;
    return ESP_OK;
}

esp_err_t matter_config_save(const MatterConfig &config)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, kEnabledKey, config.enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
