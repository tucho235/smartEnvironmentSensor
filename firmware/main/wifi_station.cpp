#include "wifi_station.h"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

namespace {
constexpr const char *TAG = "wifi";
constexpr int64_t kReconnectDelayUs = 5 * 1000 * 1000;

esp_timer_handle_t s_reconnect_timer = nullptr;
bool s_started = false;

void reconnect_timer_callback(void *)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi reconnect request failed: %s", esp_err_to_name(err));
    }
}

void schedule_reconnect()
{
    if (s_reconnect_timer == nullptr) {
        return;
    }

    esp_timer_stop(s_reconnect_timer);
    esp_err_t err = esp_timer_start_once(s_reconnect_timer, kReconnectDelayUs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to schedule Wi-Fi reconnect: %s", esp_err_to_name(err));
    }
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi station started");
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Initial Wi-Fi connect request failed: %s", esp_err_to_name(err));
            schedule_reconnect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        auto *event = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u; reconnecting in 5 seconds", event->reason);
        schedule_reconnect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        esp_timer_stop(s_reconnect_timer);
        ESP_LOGI(TAG, "Wi-Fi connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t create_reconnect_timer()
{
    if (s_reconnect_timer != nullptr) {
        return ESP_OK;
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = reconnect_timer_callback;
    timer_args.name = "wifi_reconnect";

    return esp_timer_create(&timer_args, &s_reconnect_timer);
}

bool wifi_configured()
{
    return std::strlen(CONFIG_APP_WIFI_SSID) > 0;
}

esp_err_t copy_wifi_credentials(wifi_config_t &wifi_config)
{
    const size_t ssid_len = std::strlen(CONFIG_APP_WIFI_SSID);
    const size_t password_len = std::strlen(CONFIG_APP_WIFI_PASSWORD);

    if (ssid_len >= sizeof(wifi_config.sta.ssid)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (password_len >= sizeof(wifi_config.sta.password)) {
        return ESP_ERR_INVALID_SIZE;
    }

    std::memcpy(wifi_config.sta.ssid, CONFIG_APP_WIFI_SSID, ssid_len);
    std::memcpy(wifi_config.sta.password, CONFIG_APP_WIFI_PASSWORD, password_len);
    wifi_config.sta.threshold.authmode = password_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    return ESP_OK;
}
} // namespace

esp_err_t wifi_station_start()
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!wifi_configured()) {
        ESP_LOGW(TAG, "Wi-Fi is not configured; set APP_WIFI_SSID with idf.py menuconfig");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = create_reconnect_timer();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi reconnect timer: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize esp-netif: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return err;
    }

    if (esp_netif_create_default_wifi_sta() == nullptr) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi station netif");
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              wifi_event_handler,
                                              nullptr,
                                              nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Wi-Fi event handler: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              wifi_event_handler,
                                              nullptr,
                                              nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_config = {};
    err = copy_wifi_credentials(wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invalid Wi-Fi credential length: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi station mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi config: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    s_started = true;
    return ESP_OK;
}
