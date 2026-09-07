#pragma once

#include "sdkconfig.h"

struct SensorSample {
    float temperature_c;
    float humidity_percent;
#if CONFIG_APP_SENSOR_BME680
    float pressure_hpa;
#endif
};
