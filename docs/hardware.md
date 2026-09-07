# Hardware

## Placa objetivo

La placa objetivo inicial es una ESP32-C3 SuperMini.

Si más adelante se utiliza otra variante de ESP32-C3, revisar su pinout, los
pines de arranque, la configuración de flash y los GPIO disponibles antes de
cambiar la configuración del firmware.

## Sensor ambiental

El sensor inicial es un Bosch BME680 conectado mediante I²C.

Cableado inicial:

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

## Notas sobre I²C

La dirección I²C inicial es `0x76`, seleccionada conectando `SDO` a GND.

GPIO4 y GPIO5 son los pines SDA/SCL iniciales documentados para este proyecto.
No mover I²C a GPIO8/GPIO9 en la ESP32-C3 SuperMini sin comprobar la variante
concreta de la placa y las implicaciones sobre arranque y strapping.

La inicialización de I²C está centralizada en la capa del sensor BME680. El
código de Matter y MQTT debe consumir snapshots del sensor y no acceder al bus
directamente.

## Comprobación inicial del firmware

El firmware inicializa I²C en GPIO4/GPIO5, inicializa la API de sensores BME68x
de Bosch y lee temperatura, humedad relativa y presión en modo forzado.

Registro esperado en caso de éxito:

```text
BME680 initialized and configured
BME680 sample: temperature=... C, humidity=... %, pressure=... hPa
```

Si no se detecta el dispositivo, comprobar nuevamente la alimentación, GND, la
ubicación de SDA/SCL, `SDO` conectado a GND y `CS` conectado a 3V3.
