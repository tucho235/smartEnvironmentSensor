idf_component_register(
    SRCS "app_main.cpp" "bme680_sensor.cpp"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES bme68x
)
