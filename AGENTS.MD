# AGENTS.md

## Project

**BME680 Matter Sensor**

Firmware for an ESP32-C3 connected to a Bosch BME680 environmental sensor.

The device is intended to expose environmental measurements through Matter while simultaneously publishing telemetry through MQTT to a local Raspberry Pi infrastructure.

---

# Primary Goals

The firmware must provide:

* Temperature measurement.
* Relative humidity measurement.
* Atmospheric pressure measurement.
* Matter support over Wi-Fi.
* MQTT telemetry.
* Automatic Wi-Fi recovery.
* Automatic MQTT recovery.
* Robust BME680/I²C communication.

Future functionality may include:

* Gas resistance measurement.
* Air quality information.
* OTA firmware updates.
* Device diagnostics.
* Advanced power management.

Do not implement future functionality unless explicitly requested.

---

# Hardware

Target MCU:

```text
ESP32-C3
```

Sensor:

```text
Bosch BME680
```

Communication between the MCU and sensor:

```text
I²C
```

The exact ESP32-C3 Mini board and GPIO assignments must be confirmed before hard-coding pins.

Do not assume GPIO assignments unless they are documented in the project configuration or explicitly provided by the developer.

---

# Software Stack

Use:

* ESP-IDF
* ESP-Matter
* FreeRTOS
* C/C++

Do **not** migrate the project to Arduino unless explicitly requested.

Prefer official Espressif components and APIs whenever practical.

Avoid unnecessary third-party dependencies.

---

# Matter Architecture

The device should be implemented as a Matter Node with multiple endpoints.

Initial architecture:

```text
Matter Node
│
├── Endpoint 1
│   └── Temperature Sensor
│
├── Endpoint 2
│   └── Humidity Sensor
│
└── Endpoint 3
    └── Pressure Sensor
```

Use standard Matter device types and clusters.

Do not invent proprietary Matter attributes when an appropriate standard cluster exists.

Matter must remain independent from MQTT.

A failure of MQTT or the Raspberry Pi must not prevent Matter from operating.

---

# Sensor Architecture

Keep BME680 access isolated from Matter and MQTT logic.

Preferred architecture:

```text
BME680 Driver
      │
      ▼
Sensor abstraction
      │
      ├──────────► Matter
      │
      └──────────► MQTT
```

Do not allow Matter-specific code to directly manipulate the low-level BME680 driver.

Do not allow MQTT-specific code to directly access the I²C hardware.

The sensor abstraction should expose physical measurements using appropriate native units, for example:

```text
temperature → °C
humidity    → %RH
pressure    → hPa or Pa
gas         → Ω
```

Conversion to Matter-specific representations should occur at the Matter interface layer.

Conversion to InfluxDB/MQTT representations should occur at the telemetry layer.

---

# Measurement

The BME680 should be sampled periodically.

Initial target:

```text
2–5 seconds
```

Do not assume that the sensor sampling interval must equal the Matter reporting interval or MQTT publishing interval.

These should be independently configurable.

Avoid unnecessary sensor reads and network traffic.

---

# Matter Reporting

Matter reporting should be based on appropriate reporting configuration and meaningful value changes where possible.

Do not blindly report every sensor sample unless required.

Respect the data types, ranges, units and scaling defined by the Matter clusters.

Do not directly copy floating-point sensor values into Matter attributes without verifying the required Matter representation.

---

# MQTT

MQTT is used for local telemetry.

Preferred architecture:

```text
ESP32-C3
    │
    │ MQTT
    ▼
MQTT Broker
    │
    ▼
Raspberry Pi
    │
    ▼
InfluxDB
    │
    ▼
Grafana
```

The ESP32 must not connect directly to InfluxDB unless explicitly requested.

MQTT topics and payload format should be documented before implementation.

A structured payload may be used, for example:

```json
{
  "temperature": 23.47,
  "humidity": 54.21,
  "pressure": 1008.32
}
```

Do not introduce a final topic/payload convention without documenting it.

---

# Connectivity

Wi-Fi must reconnect automatically after temporary connectivity loss.

MQTT must reconnect automatically after:

* Wi-Fi interruption.
* Broker restart.
* Temporary network failure.

Connection failures must not block the main sensor task indefinitely.

Avoid long blocking operations in FreeRTOS tasks.

---

# Error Handling

The firmware must handle:

* BME680 initialization failure.
* I²C communication errors.
* Invalid sensor readings.
* Wi-Fi disconnection.
* MQTT disconnection.
* Matter communication problems.
* Unexpected sensor responses.

Use appropriate ESP-IDF error handling mechanisms.

Prefer explicit error checking over silently ignoring return values.

Do not continuously retry a failing operation in a tight loop.

Use appropriate retry delays/backoff.

---

# I²C

The BME680 driver must use the ESP-IDF I²C APIs appropriate for the selected ESP-IDF version.

I²C initialization must be centralized.

Avoid initializing the I²C peripheral multiple times from unrelated modules.

If I²C communication becomes corrupted, consider implementing bus recovery rather than requiring a complete device reboot.

Do not implement I²C recovery until normal communication is working and the failure mode is understood.

---

# FreeRTOS

Use FreeRTOS tasks for periodic operations where appropriate.

Potential tasks:

```text
sensor_task
mqtt_task / MQTT event handling
Matter event handling
```

Avoid creating unnecessary tasks.

Prefer event-driven APIs where provided by ESP-IDF or ESP-Matter.

Do not use arbitrary busy loops or `delay()`-style blocking logic.

Use:

```cpp
vTaskDelay()
```

or appropriate FreeRTOS synchronization mechanisms.

---

# Thread Safety

Sensor data may be accessed by multiple subsystems.

If sensor data is shared between tasks, use appropriate synchronization.

Possible approaches include:

* Mutex.
* Critical section.
* Message queue.
* Immutable sensor sample structure.

Do not introduce shared global mutable state without considering concurrency.

---

# Configuration

Hardware-specific configuration should not be scattered throughout the source code.

Examples:

```text
I²C SDA pin
I²C SCL pin
BME680 address
measurement interval
MQTT broker
MQTT port
MQTT topics
```

Configuration should eventually be centralized.

Use Kconfig or appropriate ESP-IDF configuration mechanisms where practical.

Credentials must never be committed to Git.

---

# Secrets

Never commit:

* Wi-Fi passwords.
* MQTT passwords.
* API keys.
* Certificates containing private keys.
* Matter credentials.
* Tokens.
* Personal infrastructure credentials.

Use local configuration files or ESP-IDF configuration mechanisms that are excluded from version control.

Review `.gitignore` before committing.

---

# Logging

Use ESP-IDF logging facilities.

Prefer:

```cpp
ESP_LOGE()
ESP_LOGW()
ESP_LOGI()
ESP_LOGD()
ESP_LOGV()
```

Use a consistent module TAG.

Example:

```cpp
static const char *TAG = "BME680";
```

Useful logs include:

* Sensor initialization.
* Sensor errors.
* Wi-Fi connection state.
* MQTT connection state.
* Matter commissioning state.
* Sensor measurements during debugging.

Do not log credentials or sensitive network information.

Avoid excessive logging in production paths.

---

# Units

Internally use physically meaningful units.

Recommended:

```text
Temperature: °C
Humidity:    %RH
Pressure:    Pa or hPa
Gas:         Ω
```

Matter-specific conversion/scaling must happen at the Matter interface.

MQTT/InfluxDB units must be documented.

Do not mix units silently.

For example, do not store pressure sometimes as Pa and sometimes as hPa.

---

# Code Style

Use modern C/C++ appropriate for ESP-IDF.

Prefer:

* Clear naming.
* Small functions.
* Explicit error handling.
* RAII where appropriate in C++.
* `const` where applicable.
* Minimal global state.
* Modular components.

Avoid:

* Giant functions.
* Magic numbers.
* Hidden side effects.
* Unnecessary macros.
* Copy/paste implementations.
* Blocking network operations inside sensor code.

---

# Project Structure

Expected structure:

```text
bme680-matter/
│
├── firmware/
│   ├── main/
│   │   ├── app_main.cpp
│   │   ├── bme680_sensor.cpp
│   │   ├── bme680_sensor.h
│   │   ├── matter_device.cpp
│   │   ├── matter_device.h
│   │   └── CMakeLists.txt
│   │
│   ├── components/
│   │   └── bme680/
│   │
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
│
├── hardware/
├── docs/
├── examples/
├── README.md
├── AGENTS.md
└── .gitignore
```

The structure may evolve as implementation progresses.

Do not reorganize the entire repository without a concrete reason.

---

# Development Workflow

Before making significant changes:

1. Inspect the existing project.
2. Read `README.md`.
3. Read `AGENTS.md`.
4. Inspect the current ESP-IDF/ESP-Matter versions.
5. Understand the existing architecture.
6. Make the smallest reasonable change.
7. Build the project.
8. Fix compilation errors.
9. Review the resulting diff.
10. Update documentation when behavior or architecture changes.

Do not overwrite working functionality unnecessarily.

---

# Version Compatibility

Do not arbitrarily upgrade:

* ESP-IDF.
* ESP-Matter.
* BME680 dependencies.

Before changing framework versions:

1. Determine the current versions.
2. Check compatibility.
3. Document the reason.
4. Make the minimum required changes.
5. Build and test the complete project.

Avoid mixing APIs from different ESP-Matter/ESP-IDF releases.

If an API is unclear, inspect the installed framework/component source and documentation rather than guessing.

---

# Build Requirements

Target:

```text
esp32c3
```

Expected commands:

```bash
idf.py set-target esp32c3
idf.py build
idf.py flash
idf.py monitor
```

Combined:

```bash
idf.py flash monitor
```

A code change should not be considered complete if the project does not build successfully.

---

# Testing Strategy

Development should proceed incrementally.

## Stage 1

Verify:

```text
ESP32-C3
   │
   ▼
I²C
   │
   ▼
BME680
```

Confirm:

* Device detected.
* Temperature valid.
* Humidity valid.
* Pressure valid.

## Stage 2

Verify Matter independently using simulated sensor values if necessary.

## Stage 3

Replace simulated values with BME680 measurements.

## Stage 4

Add MQTT.

## Stage 5

Integrate with InfluxDB/Grafana.

## Stage 6

Perform long-running stability tests.

Do not debug all subsystems simultaneously when a simpler isolated test is possible.

---

# Future Features

Potential future features:

```text
- Gas resistance
- Air quality
- OTA
- Device diagnostics
- Wi-Fi RSSI telemetry
- Uptime telemetry
- Configurable measurement interval
- Configurable MQTT parameters
- NVS persistence
- Power optimization
- Deep sleep
```

These are intentionally out of scope until requested.

---

# Important Constraints

1. Do not replace ESP-IDF with Arduino.
2. Do not couple Matter to MQTT.
3. Do not couple the BME680 driver to Matter.
4. Do not make the Raspberry Pi a dependency for Matter operation.
5. Do not hard-code credentials.
6. Do not guess hardware GPIO assignments.
7. Do not guess Matter cluster data types or scaling.
8. Do not upgrade dependencies without checking compatibility.
9. Do not introduce unnecessary third-party libraries.
10. Do not implement future features without explicit approval.
11. Always build after meaningful code changes.
12. Keep documentation synchronized with architectural changes.

---

# Current Project Status

The project is currently in the design/initial development phase.

Confirmed architecture:

```text
ESP32-C3
    │
    │ I²C
    ▼
BME680
    │
    ├──────────────► Matter
    │
    └──────────────► MQTT
                         │
                         ▼
                    Raspberry Pi
                         │
                         ▼
                      InfluxDB
                         │
                         ▼
                       Grafana
```

Initial Matter measurements:

```text
Temperature
Humidity
Pressure
```

Future:

```text
Gas resistance
Air quality
OTA
Diagnostics
```

---

# Definition of Done

A feature is considered complete when:

* The implementation is documented.
* The project builds successfully.
* Error handling is implemented appropriately.
* No credentials are committed.
* Existing functionality remains intact.
* Relevant configuration is documented.
* The code follows the project architecture.
* The resulting Git diff has been reviewed.
