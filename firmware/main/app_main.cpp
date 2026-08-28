#include "esp_log.h"

namespace {
constexpr const char *TAG = "smart_env";
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Environment Sensor firmware starting");
    ESP_LOGI(TAG, "Target board: ESP32-C3 SuperMini");
    ESP_LOGI(TAG, "Planned BME680 I2C wiring: SDA GPIO4, SCL GPIO5, address 0x76");
}
