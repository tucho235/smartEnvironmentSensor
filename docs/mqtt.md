# MQTT

MQTT telemetry is implemented using the managed Espressif ESP-MQTT component.

The ESP32-C3 will publish telemetry to a local MQTT broker, expected to run on
the Raspberry Pi. The ESP32 must not connect directly to InfluxDB.

MQTT is optional at runtime. If no broker URI is configured, the firmware keeps
sampling the BME680 and skips MQTT startup.

The firmware stores MQTT configuration in NVS under the application namespace.
Normal firmware flashes do not erase this configuration.

When MQTT is configured, the telemetry module waits until Wi-Fi has an IP
address before starting the MQTT client. This avoids an expected initial broker
connection failure during boot while Wi-Fi is still associating.

## Configuration Sources

The preferred runtime configuration path is BLE provisioning custom data during
initial device provisioning.

For migration and local development, the firmware can also seed MQTT NVS
configuration from `idf.py menuconfig` if NVS does not already contain a broker
URI.

Configure bootstrap values:

```bash
cd firmware
idf.py menuconfig
```

Then open:

```text
Smart Environment Sensor Configuration
```

Set:

```text
MQTT broker URI bootstrap
MQTT username bootstrap
MQTT password bootstrap
MQTT telemetry topic
MQTT telemetry publish interval in milliseconds
```

Example broker URI:

```text
mqtt://192.168.3.10:1883
```

The MQTT username and password are written to local `firmware/sdkconfig`, which
is ignored by Git. Do not add broker credentials to `sdkconfig.defaults`.

After the first successful boot, the firmware stores these values in NVS. Future
normal flashes can keep using the NVS copy without recompiling credentials into
the firmware.

## BLE Provisioning Payload

During first Wi-Fi provisioning, the firmware exposes two BLE provisioning
endpoints for MQTT configuration:

```text
mqtt-config
custom-data
```

Both endpoints accept the same JSON payload:

```json
{
  "broker_uri": "mqtt://192.168.3.10:1883",
  "username": "esp32",
  "password": "YOUR_PASSWORD",
  "topic": "smart-environment-sensor/bme680/state",
  "publish_interval_ms": 10000
}
```

Only `broker_uri` is required when no previous MQTT configuration exists.
Omitted optional fields keep their existing value or fall back to the project
defaults.

The `custom-data` endpoint is compatible with Espressif's `esp_prov.py`
`--custom_data` option.

## Topic

```text
smart-environment-sensor/bme680/state
```

## Payload

```json
{
  "temperature_c": 24.32,
  "humidity_percent": 50.44,
  "pressure_hpa": 1011.62
}
```

## Units

```text
temperature_c    degrees Celsius
humidity_percent relative humidity percent
pressure_hpa     hectopascals
```

## Publishing Behavior

MQTT should publish the latest `sensor_service` snapshot. It must not access the
BME680 driver or I2C bus directly.

The initial MQTT publish interval is 10 seconds. It is independent from the
sensor sampling interval.

MQTT reconnect handling should be independent from Matter. A broker or
Raspberry Pi failure must not stop sensor sampling or Matter operation.

The initial hardware test confirmed telemetry publishing to a Mosquitto broker
running on the Raspberry Pi with username/password authentication.

## Raspberry Pi Test

From the Raspberry Pi broker host:

```bash
sudo docker exec -it mosquitto \
  mosquitto_sub -h localhost -p 1883 -u esp32 -P 'YOUR_PASSWORD' \
  -t 'smart-environment-sensor/#' -v
```

Expected telemetry:

```text
smart-environment-sensor/bme680/state {"temperature_c":24.32,"humidity_percent":50.44,"pressure_hpa":1011.62}
```
