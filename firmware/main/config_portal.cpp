#include "config_portal.h"

#include <cstdlib>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_config.h"
#include "wifi_station.h"

namespace {
constexpr const char *TAG = "config_portal";
constexpr size_t kRequestBodyMaxLength = 1024;
constexpr size_t kFieldMaxLength = 512;
constexpr uint32_t kRestartDelayMs = 1200;

httpd_handle_t s_server = nullptr;
bool s_started = false;

void restart_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
    ESP_LOGI(TAG, "Restarting to apply MQTT configuration");
    esp_restart();
}

bool is_hex_digit(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

uint8_t hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<uint8_t>(value - 'a' + 10);
    }
    return static_cast<uint8_t>(value - 'A' + 10);
}

void url_decode(char *value)
{
    char *read = value;
    char *write = value;

    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (*read == '%' && is_hex_digit(read[1]) && is_hex_digit(read[2])) {
            *write++ = static_cast<char>((hex_value(read[1]) << 4) | hex_value(read[2]));
            read += 3;
        } else {
            *write++ = *read++;
        }
    }

    *write = '\0';
}

bool form_field(const char *body, const char *name, char *value, size_t value_size)
{
    const size_t name_length = std::strlen(name);
    const char *cursor = body;

    while (cursor != nullptr && *cursor != '\0') {
        const char *next = std::strchr(cursor, '&');
        const size_t pair_length = next == nullptr ? std::strlen(cursor) : static_cast<size_t>(next - cursor);
        const char *equals = static_cast<const char *>(std::memchr(cursor, '=', pair_length));

        if (equals != nullptr &&
            static_cast<size_t>(equals - cursor) == name_length &&
            std::strncmp(cursor, name, name_length) == 0) {
            const size_t raw_length = pair_length - name_length - 1;
            if (raw_length >= value_size) {
                return false;
            }

            std::memcpy(value, equals + 1, raw_length);
            value[raw_length] = '\0';
            url_decode(value);
            return true;
        }

        cursor = next == nullptr ? nullptr : next + 1;
    }

    if (value_size > 0) {
        value[0] = '\0';
    }
    return false;
}

bool copy_field(char *destination, size_t destination_size, const char *source)
{
    const size_t length = std::strlen(source);
    if (length >= destination_size) {
        return false;
    }

    std::memcpy(destination, source, length + 1);
    return true;
}

void html_escape_send(httpd_req_t *req, const char *value)
{
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        switch (*cursor) {
        case '&':
            httpd_resp_sendstr_chunk(req, "&amp;");
            break;
        case '<':
            httpd_resp_sendstr_chunk(req, "&lt;");
            break;
        case '>':
            httpd_resp_sendstr_chunk(req, "&gt;");
            break;
        case '"':
            httpd_resp_sendstr_chunk(req, "&quot;");
            break;
        default:
            httpd_resp_send_chunk(req, cursor, 1);
            break;
        }
    }
}

esp_err_t send_form(httpd_req_t *req, const char *status)
{
    MqttConfig config = {};
    esp_err_t err = mqtt_config_load(config);
    const bool configured = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load MQTT configuration for portal: %s", esp_err_to_name(err));
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
                             "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                             "<title>Smart Environment Sensor</title>"
                             "<style>"
                             ":root{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;color:#1d252c;background:#eef3f0}"
                             "body{margin:0;min-height:100vh;display:grid;place-items:center;padding:24px}"
                             "main{width:min(560px,100%);background:#fff;border:1px solid #d9e1dc;border-radius:8px;padding:24px;box-shadow:0 12px 32px #23362d1f}"
                             "h1{font-size:24px;margin:0 0 4px}.sub{color:#5d6b64;margin:0 0 20px}"
                             "label{display:block;font-weight:650;margin:14px 0 6px}"
                             "input{box-sizing:border-box;width:100%;font:inherit;padding:11px;border:1px solid #b9c5bf;border-radius:6px}"
                             "input:focus{outline:2px solid #2e7d5b33;border-color:#2e7d5b}"
                             ".row{display:flex;gap:10px;align-items:center;margin-top:14px}.row input{width:auto}"
                             ".row label{margin:0;font-weight:600}"
                             "button{font:inherit;font-weight:700;margin-top:20px;padding:12px 14px;border:0;border-radius:6px;background:#225f45;color:white;width:100%}"
                             ".status{background:#e7f5ee;border:1px solid #b7ddca;border-radius:6px;padding:10px 12px;margin:0 0 18px}"
                             ".note{font-size:14px;color:#5d6b64;margin:14px 0 0}"
                             "</style></head><body><main><h1>Smart Environment Sensor</h1>"
                             "<p class=\"sub\">MQTT configuration</p>");

    if (status != nullptr) {
        httpd_resp_sendstr_chunk(req, "<p class=\"status\">");
        httpd_resp_sendstr_chunk(req, status);
        httpd_resp_sendstr_chunk(req, "</p>");
    }

    httpd_resp_sendstr_chunk(req,
                             "<form method=\"post\" action=\"/mqtt\">"
                             "<div class=\"row\"><input id=\"mqtt_enabled\" name=\"mqtt_enabled\" type=\"checkbox\" value=\"1\"");
    if (config.enabled) {
        httpd_resp_sendstr_chunk(req, " checked");
    }
    httpd_resp_sendstr_chunk(req,
                             "><label for=\"mqtt_enabled\">Enable MQTT service</label></div>"
                             "<label for=\"broker_uri\">Broker URI</label><input id=\"broker_uri\" name=\"broker_uri\" placeholder=\"mqtt://192.168.3.10:1883\" value=\"");
    html_escape_send(req, config.broker_uri);
    httpd_resp_sendstr_chunk(req,
                             "\"><label for=\"username\">Username</label><input id=\"username\" name=\"username\" autocomplete=\"username\" value=\"");
    html_escape_send(req, config.username);
    httpd_resp_sendstr_chunk(req,
                             "\"><label for=\"password\">Password</label><input id=\"password\" name=\"password\" type=\"password\" autocomplete=\"current-password\" placeholder=\"");
    httpd_resp_sendstr_chunk(req, configured ? "Leave blank to keep current password" : "Optional");
    httpd_resp_sendstr_chunk(req,
                             "\"><div class=\"row\"><input id=\"clear_password\" name=\"clear_password\" type=\"checkbox\" value=\"1\"><label for=\"clear_password\">Clear stored password</label></div>"
                             "<label for=\"topic\">Telemetry topic</label><input id=\"topic\" name=\"topic\" required value=\"");
    html_escape_send(req, config.topic);
    httpd_resp_sendstr_chunk(req,
                             "\"><label for=\"publish_interval_ms\">Publish interval ms</label><input id=\"publish_interval_ms\" name=\"publish_interval_ms\" type=\"number\" min=\"1000\" max=\"3600000\" step=\"1000\" required value=\"");

    char interval[16] = {};
    snprintf(interval, sizeof(interval), "%lu", static_cast<unsigned long>(config.publish_interval_ms));
    httpd_resp_sendstr_chunk(req, interval);
    httpd_resp_sendstr_chunk(req,
                             "\"><button type=\"submit\">Save and restart</button></form>"
                             "<p class=\"note\">Password values are never displayed. The device restarts after saving so MQTT reconnects with the new settings.</p>"
                             "</main></body></html>");

    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    return send_form(req, nullptr);
}

esp_err_t mqtt_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > kRequestBodyMaxLength) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form body");
    }

    char body[kRequestBodyMaxLength + 1] = {};
    size_t received_total = 0;
    while (received_total < static_cast<size_t>(req->content_len)) {
        const int received = httpd_req_recv(req,
                                           body + received_total,
                                           static_cast<size_t>(req->content_len) - received_total);
        if (received <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive form body");
        }
        received_total += static_cast<size_t>(received);
    }
    body[received_total] = '\0';

    MqttConfig config = {};
    esp_err_t err = mqtt_config_load(config);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load existing MQTT configuration: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load MQTT configuration");
    }

    char field[kFieldMaxLength] = {};
    config.enabled = form_field(body, "mqtt_enabled", field, sizeof(field)) && std::strcmp(field, "1") == 0;

    form_field(body, "broker_uri", field, sizeof(field));
    if (!copy_field(config.broker_uri, sizeof(config.broker_uri), field) ||
        (config.enabled && std::strlen(config.broker_uri) == 0)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid broker URI");
    }

    if (form_field(body, "username", field, sizeof(field)) &&
        !copy_field(config.username, sizeof(config.username), field)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid username");
    }

    const bool clear_password = form_field(body, "clear_password", field, sizeof(field)) && std::strcmp(field, "1") == 0;
    if (clear_password) {
        config.password[0] = '\0';
    } else if (form_field(body, "password", field, sizeof(field)) && std::strlen(field) > 0 &&
               !copy_field(config.password, sizeof(config.password), field)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid password");
    }

    if (form_field(body, "topic", field, sizeof(field)) &&
        !copy_field(config.topic, sizeof(config.topic), field)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid telemetry topic");
    }

    if (form_field(body, "publish_interval_ms", field, sizeof(field))) {
        char *end = nullptr;
        const unsigned long interval = std::strtoul(field, &end, 10);
        if (end == field || *end != '\0' || interval < 1000 || interval > 3600000) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid publish interval");
        }
        config.publish_interval_ms = static_cast<uint32_t>(interval);
    }

    err = mqtt_config_save(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save MQTT configuration from portal: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to save MQTT configuration");
    }

    ESP_LOGI(TAG, "MQTT configuration saved from web portal");
    xTaskCreate(restart_task, "config_restart", 2048, nullptr, 5, nullptr);
    return send_form(req, "MQTT configuration saved. The device will restart now.");
}

esp_err_t start_http_server()
{
    if (s_server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start configuration portal: %s", esp_err_to_name(err));
        s_server = nullptr;
        return err;
    }

    httpd_uri_t root_uri = {};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = root_get_handler;
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &root_uri), TAG, "Failed to register root handler");

    httpd_uri_t mqtt_uri = {};
    mqtt_uri.uri = "/mqtt";
    mqtt_uri.method = HTTP_POST;
    mqtt_uri.handler = mqtt_post_handler;
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &mqtt_uri), TAG, "Failed to register MQTT handler");

    ESP_LOGI(TAG, "Configuration portal started on http://<device-ip>/");
    return ESP_OK;
}

void stop_http_server()
{
    if (s_server == nullptr) {
        return;
    }

    esp_err_t err = httpd_stop(s_server);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop configuration portal: %s", esp_err_to_name(err));
    }
    s_server = nullptr;
}

void network_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        esp_err_t err = start_http_server();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Configuration portal not started: %s", esp_err_to_name(err));
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        stop_http_server();
    }
}

esp_err_t register_network_events()
{
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                           IP_EVENT_STA_GOT_IP,
                                                           network_event_handler,
                                                           nullptr,
                                                           nullptr),
                        TAG,
                        "Failed to register config portal IP event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                           WIFI_EVENT_STA_DISCONNECTED,
                                                           network_event_handler,
                                                           nullptr,
                                                           nullptr),
                        TAG,
                        "Failed to register config portal Wi-Fi event handler");
    return ESP_OK;
}
} // namespace

esp_err_t config_portal_start()
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(register_network_events(), TAG, "Failed to register network events");
    s_started = true;

    if (wifi_station_is_connected()) {
        return start_http_server();
    }

    ESP_LOGI(TAG, "Configuration portal waiting for Wi-Fi");
    return ESP_OK;
}
