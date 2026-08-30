idf_component_register(
    SRCS "app_main.cpp" "bme680_sensor.cpp" "config_portal.cpp" "mqtt_config.cpp" "mqtt_telemetry.cpp" "sensor_service.cpp" "wifi_station.cpp"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_driver_i2c esp_event esp_http_server esp_netif esp_timer esp_wifi espressif__cjson espressif__mqtt nvs_flash
    PRIV_REQUIRES bme68x
)
