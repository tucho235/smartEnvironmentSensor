idf_component_register(
    SRCS "app_main.cpp" "bme680_sensor.cpp" "sensor_service.cpp"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_driver_i2c esp_timer
    PRIV_REQUIRES bme68x
)
