# MQTT

MQTT telemetry is implemented using the managed Espressif ESP-MQTT component.

The ESP32-C3 will publish telemetry to a local MQTT broker, expected to run on
the Raspberry Pi. The ESP32 must not connect directly to InfluxDB.

MQTT is optional at runtime. If no broker URI is configured, the firmware keeps
sampling the BME680 and skips MQTT startup.

When MQTT is configured, the telemetry module waits until Wi-Fi has an IP
address before starting the MQTT client. This avoids an expected initial broker
connection failure during boot while Wi-Fi is still associating.

## Configuration

Configure the local broker connection:

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
MQTT broker URI
MQTT username
MQTT password
MQTT telemetry topic
MQTT telemetry publish interval in milliseconds
```

Example broker URI:

```text
mqtt://192.168.3.10:1883
```

The MQTT username and password are written to local `firmware/sdkconfig`, which
is ignored by Git. Do not add broker credentials to `sdkconfig.defaults`.

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
