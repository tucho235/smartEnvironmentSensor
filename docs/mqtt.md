# MQTT

MQTT is planned but not implemented yet.

The ESP32-C3 will publish telemetry to a local MQTT broker, expected to run on
the Raspberry Pi. The ESP32 must not connect directly to InfluxDB.

## Preliminary Topic

```text
smart-environment-sensor/bme680/state
```

## Preliminary Payload

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

## Publishing Plan

MQTT should publish the latest `sensor_service` snapshot. It must not access the
BME680 driver or I2C bus directly.

The initial MQTT publish interval should be independent from the sensor sampling
interval. A good starting point is 10 seconds.

MQTT reconnect handling should be independent from Matter. A broker or
Raspberry Pi failure must not stop sensor sampling or Matter operation.
