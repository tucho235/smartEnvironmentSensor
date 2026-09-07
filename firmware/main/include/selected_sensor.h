#pragma once

#include "sdkconfig.h"

#if CONFIG_APP_SENSOR_BME680
#include "bme680_sensor.h"
using SelectedSensor = Bme680Sensor;
constexpr const char *kSelectedSensorName = "BME680";
#elif CONFIG_APP_SENSOR_SHT30
#include "sht30_sensor.h"
using SelectedSensor = Sht30Sensor;
constexpr const char *kSelectedSensorName = "SHT30";
#else
#error "A supported environmental sensor must be selected"
#endif
