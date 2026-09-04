#pragma once

#include "esp_err.h"

esp_err_t matter_device_start();
bool matter_device_is_commissioning_active();
