# Hardware

## Placa objetivo

La placa objetivo inicial es una ESP32-C3 SuperMini.

Si más adelante se utiliza otra variante de ESP32-C3, revisar su pinout, los
pines de arranque, la configuración de flash y los GPIO disponibles antes de
cambiar la configuración del firmware.

## Sensores ambientales

Cada build usa un Bosch BME680 o un Sensirion SHT30 conectado mediante I²C.

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

Cableado SHT30:

```text
SHT30           ESP32-C3 SuperMini
----------------------------------
VCC       ---> 3V3
GND       ---> GND
SDA       ---> GPIO4
SCL       ---> GPIO5
ADDR      ---> GND o valor predeterminado del módulo (I2C address 0x44)
```

Si `ADDR` está conectado a 3V3, seleccionar `APP_SHT30_I2C_ADDRESS=0x45`.

## Notas sobre I²C

La dirección BME680 es `0x76`. La dirección SHT30 es configurable entre `0x44`
y `0x45`.

GPIO4 y GPIO5 son los pines SDA/SCL iniciales documentados para este proyecto.
No mover I²C a GPIO8/GPIO9 en la ESP32-C3 SuperMini sin comprobar la variante
concreta de la placa y las implicaciones sobre arranque y strapping.

La inicialización de I²C está centralizada en el driver seleccionado. El
código de Matter y MQTT debe consumir snapshots del sensor y no acceder al bus
directamente.

## Comprobación inicial del firmware

El firmware inicializa I²C en GPIO4/GPIO5. La build BME680 lee temperatura,
humedad relativa y presión en modo forzado. La build SHT30 realiza mediciones
single-shot de temperatura y humedad y descarta la muestra si falla alguno de
los CRC recibidos.

Registro esperado en caso de éxito:

```text
BME680 initialized and configured
BME680 sample: temperature=... C, humidity=... %, pressure=... hPa
```

Para SHT30:

```text
SHT30 initialized at address 0x44
SHT30 sample: temperature=... C, humidity=... %
```

Si no se detecta el dispositivo, comprobar nuevamente la alimentación, GND y la
ubicación de SDA/SCL. Para BME680, comprobar también `SDO` conectado a GND y
`CS` conectado a 3V3; para SHT30, comprobar el estado de `ADDR` frente a la
dirección seleccionada en Kconfig.
