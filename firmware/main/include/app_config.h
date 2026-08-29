#pragma once

#include <cstdint>

#include "hal/gpio_types.h"

namespace app_config {
constexpr gpio_num_t kI2cSdaGpio = GPIO_NUM_4;
constexpr gpio_num_t kI2cSclGpio = GPIO_NUM_5;
constexpr uint8_t kBme680I2cAddress = 0x76;
constexpr uint32_t kI2cClockHz = 100000;
constexpr int kI2cTimeoutMs = 100;

constexpr uint32_t kSensorSampleIntervalMs = 3000;
constexpr uint32_t kSensorRetryIntervalMs = 5000;
constexpr uint32_t kSensorTaskStackWords = 4096;
constexpr int kSensorTaskPriority = 5;
} // namespace app_config
