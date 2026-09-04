set(app_requires
    esp_driver_i2c
    esp_event
    esp_netif
    esp_timer
    esp_wifi
    nvs_flash
)

set(app_priv_requires
    bme68x
)

set(app_sources
    "app_main.cpp"
    "bme680_sensor.cpp"
    "matter_config.cpp"
    "matter_device.cpp"
    "memory_diagnostics.cpp"
    "sensor_service.cpp"
    "wifi_station.cpp"
)

if(CONFIG_APP_ENABLE_MQTT_TELEMETRY OR CONFIG_APP_ENABLE_CONFIG_PORTAL)
    list(APPEND app_requires espressif__cjson)
    list(APPEND app_sources "mqtt_config.cpp")
endif()

if(CONFIG_APP_ENABLE_MQTT_TELEMETRY)
    list(APPEND app_requires espressif__mqtt)
    list(APPEND app_sources "mqtt_telemetry.cpp")
endif()

if(CONFIG_APP_ENABLE_CONFIG_PORTAL)
    list(APPEND app_requires esp_http_server http_parser)
    list(APPEND app_sources "config_portal.cpp")
endif()

if(CONFIG_APP_ENABLE_MATTER)
    list(APPEND app_requires espressif__esp_matter)
endif()

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "include"
    REQUIRES ${app_requires}
    PRIV_REQUIRES ${app_priv_requires}
)

if(CONFIG_APP_ENABLE_MATTER)
    target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
endif()

if(CONFIG_APP_ENABLE_CONFIG_PORTAL)
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "$ENV{IDF_PATH}/components/esp_http_server/include"
        "$ENV{IDF_PATH}/components/http_parser"
    )
endif()
