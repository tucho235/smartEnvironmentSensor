#include "wifi_station.h"

#include <cstring>
#include <cstdio>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "protocomm_ble.h"
#include "protocomm_security.h"
#include "sdkconfig.h"

namespace {
constexpr const char *TAG = "wifi";
constexpr int64_t kReconnectDelayUs = 5 * 1000 * 1000;
constexpr size_t kServiceNameMaxLength = 16;

esp_timer_handle_t s_reconnect_timer = nullptr;
bool s_started = false;
bool s_provisioning_active = false;

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
    if (event_base == NETWORK_PROV_EVENT) {
        switch (event_id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "BLE Wi-Fi provisioning started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV:
            ESP_LOGI(TAG, "Received Wi-Fi credentials over BLE");
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL: {
            auto *reason = static_cast<network_prov_wifi_sta_fail_reason_t *>(event_data);
            const char *reason_text = *reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR
                                          ? "authentication failed"
                                          : "access point not found";
            ESP_LOGW(TAG, "Wi-Fi provisioning failed: %s", reason_text);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Wi-Fi provisioning successful");
            break;
        case NETWORK_PROV_END: {
            ESP_LOGI(TAG, "BLE Wi-Fi provisioning stopped");
            s_provisioning_active = false;
            esp_err_t err = network_prov_mgr_deinit();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to deinitialize provisioning manager: %s", esp_err_to_name(err));
            }
            break;
        }
        default:
            break;
        }
        return;
    }

    if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
        case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
            ESP_LOGI(TAG, "BLE provisioning client connected");
            break;
        case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
            ESP_LOGI(TAG, "BLE provisioning client disconnected");
            break;
        default:
            break;
        }
        return;
    }

    if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "BLE provisioning security session established");
            break;
        case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
            ESP_LOGW(TAG, "BLE provisioning security parameters were invalid");
            break;
        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGW(TAG, "BLE provisioning proof of possession mismatch");
            break;
        default:
            break;
        }
        return;
    }

    if (s_provisioning_active && event_base == WIFI_EVENT) {
        return;
    }

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

esp_err_t register_event_handler(esp_event_base_t event_base, int32_t event_id)
{
    esp_err_t err = esp_event_handler_instance_register(event_base,
                                                        event_id,
                                                        wifi_event_handler,
                                                        nullptr,
                                                        nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
    }
    return err;
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

esp_err_t initialize_network_stack()
{
    esp_err_t err = esp_netif_init();
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

    return ESP_OK;
}

esp_err_t register_wifi_events()
{
    ESP_RETURN_ON_ERROR(register_event_handler(WIFI_EVENT, ESP_EVENT_ANY_ID), TAG, "Wi-Fi event handler failed");
    ESP_RETURN_ON_ERROR(register_event_handler(IP_EVENT, IP_EVENT_STA_GOT_IP), TAG, "IP event handler failed");
    ESP_RETURN_ON_ERROR(register_event_handler(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID), TAG, "Provisioning event handler failed");
    ESP_RETURN_ON_ERROR(register_event_handler(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID), TAG, "BLE event handler failed");
    ESP_RETURN_ON_ERROR(register_event_handler(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID), TAG, "Security event handler failed");

    return ESP_OK;
}

void get_provisioning_service_name(char *service_name, size_t length)
{
    uint8_t mac[6] = {};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read Wi-Fi MAC for provisioning name: %s", esp_err_to_name(err));
        std::snprintf(service_name, length, "%sDEVICE", CONFIG_APP_WIFI_PROV_SERVICE_PREFIX);
        return;
    }

    std::snprintf(service_name,
                  length,
                  "%s%02X%02X%02X",
                  CONFIG_APP_WIFI_PROV_SERVICE_PREFIX,
                  mac[3],
                  mac[4],
                  mac[5]);
}

const char *proof_of_possession()
{
    return std::strlen(CONFIG_APP_WIFI_PROV_POP) == 0 ? nullptr : CONFIG_APP_WIFI_PROV_POP;
}

void log_provisioning_instructions(const char *service_name)
{
    ESP_LOGI(TAG, "Provision from the Espressif BLE provisioning app");
    ESP_LOGI(TAG, "BLE provisioning device name: %s", service_name);
    if (proof_of_possession() == nullptr) {
        ESP_LOGI(TAG,
                 "QR payload: {\"ver\":\"v1\",\"name\":\"%s\",\"transport\":\"ble\",\"network\":\"wifi\"}",
                 service_name);
    } else {
        ESP_LOGI(TAG, "Use the proof of possession configured locally in menuconfig");
    }
}

esp_err_t start_saved_wifi()
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi station mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t start_ble_provisioning()
{
    network_prov_mgr_config_t prov_config = {};
    prov_config.scheme = network_prov_scheme_ble;
    prov_config.scheme_event_handler.event_cb = network_prov_scheme_ble_event_cb_free_btdm;
    prov_config.scheme_event_handler.user_data = nullptr;
    prov_config.app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
    prov_config.network_prov_wifi_conn_cfg.wifi_conn_attempts = CONFIG_APP_WIFI_PROV_MAX_ATTEMPTS;

    esp_err_t err = network_prov_mgr_init(prov_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize provisioning manager: %s", esp_err_to_name(err));
        return err;
    }

    bool provisioned = false;
    err = network_prov_mgr_is_wifi_provisioned(&provisioned);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read Wi-Fi provisioning state: %s", esp_err_to_name(err));
        network_prov_mgr_deinit();
        return err;
    }

    if (provisioned) {
        ESP_LOGI(TAG, "Wi-Fi credentials already provisioned");
        err = network_prov_mgr_deinit();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deinitialize provisioning manager: %s", esp_err_to_name(err));
        }
        return start_saved_wifi();
    }

    char service_name[kServiceNameMaxLength] = {};
    get_provisioning_service_name(service_name, sizeof(service_name));

    const char *pop = proof_of_possession();
    network_prov_security_t security = NETWORK_PROV_SECURITY_1;

    s_provisioning_active = true;
    err = network_prov_mgr_start_provisioning(security, pop, service_name, nullptr);
    if (err != ESP_OK) {
        s_provisioning_active = false;
        ESP_LOGE(TAG, "Failed to start BLE provisioning: %s", esp_err_to_name(err));
        network_prov_mgr_deinit();
        return err;
    }

    log_provisioning_instructions(service_name);
    return ESP_OK;
}
} // namespace

esp_err_t wifi_station_start()
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = create_reconnect_timer();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi reconnect timer: %s", esp_err_to_name(err));
        return err;
    }

    err = initialize_network_stack();
    if (err != ESP_OK) {
        return err;
    }

    ESP_RETURN_ON_ERROR(register_wifi_events(), TAG, "Failed to register Wi-Fi events");
    ESP_RETURN_ON_ERROR(start_ble_provisioning(), TAG, "Failed to start Wi-Fi or BLE provisioning");

    s_started = true;
    return ESP_OK;
}
