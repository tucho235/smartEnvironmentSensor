# Hardware

## Target Board

The initial target board is an ESP32-C3 SuperMini.

If a different ESP32-C3 board variant is used later, review its pinout,
bootstrapping pins, flash configuration, and available GPIOs before changing
the firmware configuration.

## Environmental Sensor

The initial sensor is a Bosch BME680 connected over I2C.

Initial wiring:

```text
BME680          ESP32-C3 SuperMini
----------------------------------
VCC       ---> 3V3
GND       ---> GND
SDA       ---> GPIO4
SCL       ---> GPIO5
SDO       ---> GND   (I2C address 0x76)
CS        ---> 3V3   (I2C mode)
```

## I2C Notes

The initial I2C address is `0x76`, selected by connecting `SDO` to ground.

GPIO4 and GPIO5 are the documented initial SDA/SCL pins for this project.
Do not move I2C to GPIO8/GPIO9 on the ESP32-C3 SuperMini without checking the
specific board variant and boot/strapping implications.

I2C initialization should be centralized in firmware once the BME680 driver is
introduced, so Matter and MQTT code never access the bus directly.

## Initial Firmware Check

The firmware initializes I2C on GPIO4/GPIO5, initializes the Bosch BME68x
Sensor API, and reads temperature, relative humidity, and pressure in forced
mode.

Expected success log:

```text
BME680 initialized and configured
BME680 sample: temperature=... C, humidity=... %, pressure=... hPa
```

If the device is not detected, re-check power, ground, SDA/SCL placement, `SDO`
to ground, and `CS` to 3V3.
