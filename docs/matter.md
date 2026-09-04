# Matter

Matter support is being integrated as an optional firmware feature.

The Matter runtime is validated from the smallest controller-compatible shape
first. The Matter-only validation build exposes one Temperature Sensor endpoint,
matching Espressif's `esp-lowcode-matter` `temperature_sensor` product shape:
device type `0x0302` version `1` with the `TemperatureMeasurement` cluster.
Humidity and pressure are added only after that baseline updates correctly in a
controller.

```text
Matter Node
├── Endpoint 1: Temperature Sensor
│   └── TemperatureMeasurement cluster
└── Endpoint 2: Power Source, optional
    └── PowerSource cluster
```

The implementation is isolated in `matter_device.cpp/.h` and consumes sensor
data only through `sensor_service_get_latest()`. It must not access the BME680
driver or the I2C bus directly, and it must remain independent from MQTT.

## Build Flag

Matter is disabled by default:

```text
Smart Environment Sensor Configuration
└── Enable Matter endpoint integration
```

When enabled, the project pulls the managed `espressif/esp_matter` component and
starts a Matter node with Temperature, Humidity and Pressure Sensor endpoints.

`APP_MATTER_TEMPERATURE_ONLY` creates only endpoint 1 as a standard Temperature
Sensor with `TemperatureMeasurement.MeasuredValue`. The USB-powered Matter
validation build enables this mode and leaves the optional static `PowerSource`
endpoint disabled.

This keeps the current MQTT-only firmware build stable while the Matter
dependency and commissioning flow are validated.

An ESP32-C3 build with `APP_ENABLE_MATTER=y` and managed component
`espressif/esp_matter` `1.6.0` has been validated with ESP-IDF `6.1.0`.

The versioned defaults file for this variant is:

```text
firmware/sdkconfig.matter-standard.defaults
```

Build the Matter variant without modifying the normal local `sdkconfig`:

```bash
idf.py -B build-matter-standard \
  -DSDKCONFIG=sdkconfig.matter-standard \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter-standard.defaults" \
  build
```

Flash the same build directory once it compiles:

```bash
idf.py -B build-matter-standard -DSDKCONFIG=sdkconfig.matter-standard -p PORT flash monitor
```

## Cluster Selection

The Matter standard variant keeps the broad default ESP-Matter cluster catalog
while the endpoint model is validated. The firmware only creates runtime
endpoints for:

```text
Endpoint 1:
  Temperature Sensor device type + TemperatureMeasurement
Endpoint 2, optional:
  Power Source device type + PowerSource
```

It also keeps the required root-node commissioning and operational clusters.
Once this shape is confirmed across controllers, humidity and pressure can be
added back incrementally.

OpenThread support is disabled for the Wi-Fi-only ESP32-C3 build:

```text
CONFIG_ESP_MATTER_ENABLE_OPENTHREAD is not set
```

## Network Commissioning

When Matter is enabled, Wi-Fi commissioning is handled by the standard Matter
Network Commissioning cluster. This keeps the device compatible with Matter
controllers such as SmartThings, which expect to send or validate the Wi-Fi
network during commissioning.

The Matter variant intentionally leaves ESP-Matter custom network configuration
disabled:

```text
CONFIG_CUSTOM_NETWORK_CONFIG is not set
```

The project BLE provisioning flow remains available when Matter is disabled at
runtime. When Matter is enabled and no Wi-Fi credentials are stored, the firmware
does not start the Espressif BLE provisioning service, avoiding a conflict with
CHIPoBLE during Matter commissioning.

When Matter is enabled, the local Wi-Fi station layer initializes ESP-IDF
networking, starts station mode and observes Wi-Fi/IP events, but it does not
call `esp_wifi_connect()` or schedule reconnects. ESP-Matter's connectivity
manager owns station connect and reconnect attempts so only one subsystem drives
association with the access point.

When Matter is enabled, the Wi-Fi provisioning BLE scheme must not use the
`network_prov_scheme_ble_event_cb_free_btdm` handler. That handler releases BTDM
memory after provisioning, while ESP-Matter still needs BLE for CHIPoBLE
commissioning.

The development build currently uses Matter test setup parameters:

```text
QR payload:     MT:Y.K9042C00KA0648G00
Setup passcode: 20202021
Discriminator:  3840
Manual code:    34970112332
```

These values are suitable only for local development and must be replaced before
any production-style firmware.

## Runtime Portal

After Wi-Fi connects, the local configuration portal exposes a Matter tab:

```text
http://<device-ip>/matter-tab
```

The tab shows the current development QR/setup payload, manual pairing code,
setup PIN and discriminator. It also exposes an `Enable Matter service`
checkbox.

The checkbox is stored in NVS under the application Matter namespace. When it is
disabled, the firmware skips Matter startup on the next boot. The build-time
`APP_ENABLE_MATTER` flag still controls whether ESP-Matter is compiled into the
firmware at all.

## Units

Internal firmware units:

```text
temperature_c     degrees Celsius
humidity_percent  relative humidity percent
pressure_hpa      hectopascals
```

Matter representations:

```text
TemperatureMeasurement.MeasuredValue          0.01 degrees Celsius
RelativeHumidityMeasurement.MeasuredValue    0.01 percent
PressureMeasurement.MeasuredValue            0.1 kPa, equivalent to hPa
```

The Matter layer performs these conversions locally before updating attributes.

## Next Validation Steps

1. Enable `APP_ENABLE_MATTER` in local `sdkconfig`.
2. Ensure ESP-Matter is resolved by the IDF Component Manager.
3. Keep standard Matter Network Commissioning enabled.
4. Build for `esp32c3`.
5. Flash and confirm BME680 and Wi-Fi behavior.
6. Commission the device with a Matter controller.
7. Verify endpoint discovery and temperature reporting.

The Matter controller test should confirm endpoint 1 exposes a standard
Temperature Sensor device type and reports `TemperatureMeasurement.MeasuredValue`.
Controller-specific compatibility changes should only be added after the
baseline Matter-only build is validated.
