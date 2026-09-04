#include "config_portal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "matter_config.h"
#include "mqtt_config.h"
#include "sdkconfig.h"
#include "wifi_station.h"

namespace {
constexpr const char *TAG = "config_portal";
constexpr size_t kRequestBodyMaxLength = 1024;
constexpr size_t kFieldMaxLength = 512;
constexpr size_t kPortalPageMaxLength = 2048;
constexpr uint32_t kRestartDelayMs = 1200;

httpd_handle_t s_server = nullptr;
char s_page_buffer[kPortalPageMaxLength] = {};
SemaphoreHandle_t s_page_mutex = nullptr;
bool s_started = false;

enum class ActiveTab {
    kMqtt,
    kMatter,
};

void restart_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
    ESP_LOGI(TAG, "Restarting to apply portal configuration");
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

bool receive_form_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (req->content_len <= 0 || static_cast<size_t>(req->content_len) >= body_size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form body");
        return false;
    }

    size_t received_total = 0;
    while (received_total < static_cast<size_t>(req->content_len)) {
        const int received = httpd_req_recv(req,
                                           body + received_total,
                                           static_cast<size_t>(req->content_len) - received_total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive form body");
            return false;
        }
        received_total += static_cast<size_t>(received);
    }
    body[received_total] = '\0';
    return true;
}

esp_err_t send_portal(httpd_req_t *req, const char *status, ActiveTab active_tab)
{
    if (s_page_mutex == nullptr || xSemaphoreTake(s_page_mutex, 0) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Connection", "close");
        return httpd_resp_sendstr(req, "Portal busy, retry.");
    }

    MqttConfig mqtt_config = {};
    esp_err_t err = mqtt_config_load(mqtt_config);
    const bool mqtt_configured = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load MQTT configuration for portal: %s", esp_err_to_name(err));
    }

    MatterConfig matter_config = {};
    err = matter_config_load(matter_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load Matter configuration for portal: %s", esp_err_to_name(err));
        matter_config.enabled = true;
    }

    char *page = s_page_buffer;
    std::memset(page, 0, kPortalPageMaxLength);
    char *cursor = page;
    size_t remaining = kPortalPageMaxLength;
    bool ok = true;

    auto append = [&](const char *text) {
        if (!ok) {
            return;
        }
        const size_t length = std::strlen(text);
        if (length >= remaining) {
            ok = false;
            return;
        }
        std::memcpy(cursor, text, length);
        cursor += length;
        remaining -= length;
        *cursor = '\0';
    };

    auto append_escaped = [&](const char *text) {
        for (const char *value = text; ok && *value != '\0'; ++value) {
            switch (*value) {
            case '&':
                append("&amp;");
                break;
            case '<':
                append("&lt;");
                break;
            case '>':
                append("&gt;");
                break;
            case '"':
                append("&quot;");
                break;
            default: {
                char character[2] = {*value, '\0'};
                append(character);
                break;
            }
            }
        }
    };

    auto append_tab = [&](ActiveTab tab, const char *href, const char *label) {
        append("<a");
        if (active_tab == tab) {
            append(" aria-current=\"page\"");
        }
        append(" href=\"");
        append(href);
        append("\">");
        append(label);
        append("</a>");
    };

    append("<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>Smart Environment Sensor</title><style>"
           "body{font-family:system-ui,sans-serif;margin:24px;max-width:640px}"
           "nav{display:flex;gap:8px;margin:16px 0}"
           "nav a{padding:10px 14px;border:1px solid #999;border-radius:6px;text-decoration:none;color:#111}"
           "nav a[aria-current=page]{background:#111;color:#fff}"
           "input:not([type=checkbox]){box-sizing:border-box;max-width:100%;width:100%}"
           "button{padding:10px 14px}"
           "</style></head><body><h1>Smart Environment Sensor</h1>");

    if (status != nullptr) {
        append("<p class=\"status\">");
        append_escaped(status);
        append("</p>");
    }

    append("<nav aria-label=\"Configuration tabs\">");
    append_tab(ActiveTab::kMqtt, "/mqtt-tab", "MQTT");
    append_tab(ActiveTab::kMatter, "/matter-tab", "Matter");
    append("</nav>");

    if (active_tab == ActiveTab::kMqtt) {
        char interval[16] = {};
        snprintf(interval, sizeof(interval), "%lu", static_cast<unsigned long>(mqtt_config.publish_interval_ms));

        append("<h2>MQTT</h2><form method=\"post\" action=\"/mqtt\">"
               "<label><input name=\"en\" type=\"checkbox\" value=\"1\"");
        if (mqtt_config.enabled) {
            append(" checked");
        }
        append("> Enable MQTT service</label>"
               "<p><label>Broker URI<br><input name=\"b\" placeholder=\"mqtt://192.168.3.10:1883\" value=\"");
        append_escaped(mqtt_config.broker_uri);
        append("\"></label></p><p><label>User<br><input name=\"u\" autocomplete=\"username\" value=\"");
        append_escaped(mqtt_config.username);
        append("\"></label></p><p><label>Password<br><input name=\"p\" type=\"password\" autocomplete=\"current-password\" placeholder=\"");
        append(mqtt_configured ? "Leave blank to keep current password" : "Optional");
        append("\"></label></p><label><input name=\"cp\" type=\"checkbox\" value=\"1\"> Clear stored password</label>"
               "<p><label>Topic<br><input name=\"t\" required value=\"");
        append_escaped(mqtt_config.topic);
        append("\"></label></p><p><label>Interval ms<br><input name=\"i\" type=\"number\" min=\"1000\" max=\"3600000\" step=\"1000\" required value=\"");
        append(interval);
        append("\"></label></p><button type=\"submit\">Save and restart</button></form>"
               "<p><a href=\"/matter-tab\">Matter settings</a></p>");
    } else {
        append("<h2>Matter</h2><p><a href=\"/mqtt-tab\">MQTT settings</a></p>"
               "<form method=\"post\" action=\"/matter\">"
               "<label><input name=\"en\" type=\"checkbox\" value=\"1\"");
        if (matter_config.enabled) {
            append(" checked");
        }
        append("> Enable Matter service</label>"
               "<div class=\"codes\"><p>QR payload<br><code>");
        append_escaped(kMatterSetupQrPayload);
        append("</code></p><p>Manual code<br><code>");
        append_escaped(kMatterManualPairingCode);
        append("</code></p><p>Setup PIN<br><code>");
        append_escaped(kMatterSetupPasscode);
        append("</code></p><p>Discriminator<br><code>");
        append_escaped(kMatterDiscriminator);
        append("</code></p></div><p><a target=\"_blank\" rel=\"noopener\" href=\"");
        append_escaped(kMatterSetupQrUrl);
        append("\">Open QR code</a></p><button type=\"submit\">Save and restart</button></form>");
#if CONFIG_APP_ENABLE_MATTER
        append("<p>Matter is included in this firmware build.</p>");
#else
        append("<p>Matter is not included in this firmware build.</p>");
#endif
    }

    append("</body></html>");

    if (!ok) {
        xSemaphoreGive(s_page_mutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Portal page too large");
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Connection", "close");
    ESP_LOGI(TAG,
             "Serving %s portal page: %u bytes, free heap=%lu, min free heap=%lu",
             active_tab == ActiveTab::kMqtt ? "MQTT" : "Matter",
             static_cast<unsigned>(std::strlen(page)),
             static_cast<unsigned long>(esp_get_free_heap_size()),
             static_cast<unsigned long>(esp_get_minimum_free_heap_size()));
    const esp_err_t send_err = httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    if (send_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send portal page: %s", esp_err_to_name(send_err));
    } else {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    xSemaphoreGive(s_page_mutex);
    return send_err;
}

esp_err_t send_restart_page(httpd_req_t *req, const char *status, const char *return_path)
{
    if (s_page_mutex == nullptr || xSemaphoreTake(s_page_mutex, 0) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Connection", "close");
        return httpd_resp_sendstr(req, "Portal busy, retry.");
    }

    char *page = s_page_buffer;
    std::snprintf(page,
                  kPortalPageMaxLength,
                  "<!doctype html><html><head><meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<title>Smart Environment Sensor</title></head>"
                  "<body><h1>Smart Environment Sensor</h1><p>%s</p>"
                  "<p>The device will restart now.</p><p><a href=\"%s\">Back</a></p></body></html>",
                  status,
                  return_path);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t err = httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send restart page: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(s_page_mutex);
    return err;
}

esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    return send_portal(req, nullptr, ActiveTab::kMqtt);
}

esp_err_t mqtt_tab_get_handler(httpd_req_t *req)
{
    return send_portal(req, nullptr, ActiveTab::kMqtt);
}

esp_err_t matter_tab_get_handler(httpd_req_t *req)
{
    return send_portal(req, nullptr, ActiveTab::kMatter);
}

esp_err_t mqtt_post_handler(httpd_req_t *req)
{
    char body[kRequestBodyMaxLength + 1] = {};
    if (!receive_form_body(req, body, sizeof(body))) {
        return ESP_FAIL;
    }

    MqttConfig config = {};
    esp_err_t err = mqtt_config_load(config);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load existing MQTT configuration: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load MQTT configuration");
    }

    char field[kFieldMaxLength] = {};
    config.enabled = form_field(body, "en", field, sizeof(field)) && std::strcmp(field, "1") == 0;

    form_field(body, "b", field, sizeof(field));
    if (!copy_field(config.broker_uri, sizeof(config.broker_uri), field) ||
        (config.enabled && std::strlen(config.broker_uri) == 0)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid broker URI");
    }

    if (form_field(body, "u", field, sizeof(field)) &&
        !copy_field(config.username, sizeof(config.username), field)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid username");
    }

    const bool clear_password = form_field(body, "cp", field, sizeof(field)) && std::strcmp(field, "1") == 0;
    if (clear_password) {
        config.password[0] = '\0';
    } else if (form_field(body, "p", field, sizeof(field)) && std::strlen(field) > 0 &&
               !copy_field(config.password, sizeof(config.password), field)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid password");
    }

    if (form_field(body, "t", field, sizeof(field)) &&
        !copy_field(config.topic, sizeof(config.topic), field)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid telemetry topic");
    }

    if (form_field(body, "i", field, sizeof(field))) {
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
    esp_err_t response_err = send_restart_page(req, "MQTT configuration saved.", "/mqtt-tab");
    xTaskCreate(restart_task, "config_restart", 2048, nullptr, 5, nullptr);
    return response_err;
}

esp_err_t matter_post_handler(httpd_req_t *req)
{
    char body[kRequestBodyMaxLength + 1] = {};
    if (!receive_form_body(req, body, sizeof(body))) {
        return ESP_FAIL;
    }

    char field[kFieldMaxLength] = {};
    MatterConfig config = {};
    esp_err_t err = matter_config_load(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load existing Matter configuration: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load Matter configuration");
    }

    config.enabled = form_field(body, "en", field, sizeof(field)) && std::strcmp(field, "1") == 0;

    err = matter_config_save(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save Matter configuration from portal: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to save Matter configuration");
    }

    ESP_LOGI(TAG, "Matter configuration saved from web portal");
    esp_err_t response_err = send_restart_page(req, "Matter configuration saved.", "/matter-tab");
    xTaskCreate(restart_task, "config_restart", 2048, nullptr, 5, nullptr);
    return response_err;
}

esp_err_t start_http_server()
{
    if (s_server != nullptr) {
        return ESP_OK;
    }

    if (s_page_mutex == nullptr) {
        s_page_mutex = xSemaphoreCreateMutex();
        if (s_page_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 15;

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

    httpd_uri_t mqtt_tab_uri = {};
    mqtt_tab_uri.uri = "/mqtt-tab";
    mqtt_tab_uri.method = HTTP_GET;
    mqtt_tab_uri.handler = mqtt_tab_get_handler;
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &mqtt_tab_uri), TAG, "Failed to register MQTT tab handler");

    httpd_uri_t matter_tab_uri = {};
    matter_tab_uri.uri = "/matter-tab";
    matter_tab_uri.method = HTTP_GET;
    matter_tab_uri.handler = matter_tab_get_handler;
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &matter_tab_uri), TAG, "Failed to register Matter tab handler");

    httpd_uri_t favicon_uri = {};
    favicon_uri.uri = "/favicon.ico";
    favicon_uri.method = HTTP_GET;
    favicon_uri.handler = favicon_get_handler;
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &favicon_uri), TAG, "Failed to register favicon handler");

    httpd_uri_t mqtt_uri = {};
    mqtt_uri.uri = "/mqtt";
    mqtt_uri.method = HTTP_POST;
    mqtt_uri.handler = mqtt_post_handler;
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &mqtt_uri), TAG, "Failed to register MQTT handler");

    httpd_uri_t matter_uri = {};
    matter_uri.uri = "/matter";
    matter_uri.method = HTTP_POST;
    matter_uri.handler = matter_post_handler;
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &matter_uri), TAG, "Failed to register Matter handler");

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
