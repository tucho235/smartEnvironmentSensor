# SDD: soporte seleccionable BME680/SHT30

## Estado del trabajo

Este documento describe el diseño y el estado de implementación de la rama:

```text
feature/sht30-support
```

La implementación está realizada y revisada estáticamente, pero todavía no fue
compilada con ESP-IDF ni validada en hardware. La compilación y las pruebas deben
continuarse en el entorno de desarrollo de la Mac.

## Objetivo

Permitir generar firmware para uno de estos sensores, seleccionado en tiempo de
compilación:

| Sensor | Temperatura | Humedad | Presión |
|---|---:|---:|---:|
| Bosch BME680 | Sí | Sí | Sí |
| Sensirion SHT30 | Sí | Sí | No |

Matter y MQTT deben consumir snapshots de `sensor_service`; ninguno debe acceder
directamente al driver ni al bus I²C. La selección del sensor no debe cambiar el
comportamiento de Wi-Fi, provisioning, portal, NVS o reconexión MQTT.

## Requisitos funcionales

1. La build debe seleccionar exactamente un sensor mediante Kconfig.
2. BME680 debe conservar el comportamiento validado de temperatura, humedad y
   presión.
3. SHT30 debe medir temperatura y humedad mediante I²C y validar el CRC de ambos
   valores antes de aceptar la muestra.
4. La build SHT30 no debe publicar, simular ni crear un valor de presión.
5. El topic MQTT debe poder configurarse por dispositivo desde el portal HTML y
   persistirse en NVS junto con broker, usuario, contraseña e intervalo.
6. El payload MQTT BME680 debe conservar `temperature_c`, `humidity_percent` y
   `pressure_hpa`.
7. El payload MQTT SHT30 debe contener únicamente `temperature_c` y
   `humidity_percent`.
8. El modelo Matter estándar BME680 debe crear clusters de temperatura, humedad
   y presión.
9. El modelo Matter estándar SHT30 debe crear clusters de temperatura y humedad.
10. Las variantes Matter existentes de solo temperatura, thermostat y endpoints
    separados deben seguir disponibles como overrides explícitos.

## Decisiones de diseño

### Una base de código, no dos ramas permanentes

La rama `feature/sht30-support` sirve para desarrollar y validar el cambio. El
resultado está diseñado para integrarse luego a `main`, manteniendo ambos sensores
en una única base de código mediante:

```text
APP_SENSOR_BME680
APP_SENSOR_SHT30
```

Esto evita mantener dos versiones divergentes de Wi-Fi, Matter, MQTT y el portal.

### Abstracción del sensor

```text
Bme680Sensor ─┐
              ├── SelectedSensor ── sensor_service ── Matter
Sht30Sensor ──┘                       │
                                      └────────────── MQTT
```

`selected_sensor.h` elige el driver durante la compilación. `SensorSample` es el
tipo común. El miembro `pressure_hpa` solo existe en la build BME680, lo que evita
representar presión inexistente como cero en SHT30.

### Protocolo SHT30

La implementación usa:

- Los mismos pines I²C del proyecto: SDA GPIO4 y SCL GPIO5.
- Dirección `0x44` por defecto y `0x45` opcional mediante Kconfig.
- Soft reset `0x30A2` durante la inicialización.
- Medición single-shot de alta repetibilidad sin clock stretching: `0x2400`.
- Lectura de seis bytes: temperatura, CRC, humedad y CRC.
- CRC-8 con inicialización `0xFF` y polinomio `0x31`.
- Conversión indicada por Sensirion:

```text
temperature_c = -45 + 175 * raw_temperature / 65535
humidity_percent = 100 * raw_humidity / 65535
```

Una muestra con CRC inválido se rechaza y no reemplaza el último snapshot válido.

### MQTT y ubicación

Los valores iniciales de topic son:

```text
smart-environment-sensor/bme680/state
smart-environment-sensor/sht30/state
```

El portal ya permite modificar el topic y guardarlo en NVS. Para las dos unidades
reales se recomienda configurar topics por ubicación:

```text
smart-environment-sensor/indoor/state
smart-environment-sensor/outdoor/state
```

El topic guardado en NVS prevalece sobre el valor inicial de Kconfig. Un flasheo
normal no cambia el topic existente; borrar NVS sí restaura los valores iniciales.

### Matter y commissioning

Los clusters se seleccionan en compilación:

```text
BME680 -> TemperatureMeasurement
          RelativeHumidityMeasurement
          PressureMeasurement

SHT30  -> TemperatureMeasurement
          RelativeHumidityMeasurement
```

Los códigos actuales son credenciales estáticas de desarrollo incluidas en la
build. Dos placas pueden agregarse a SmartThings como nodos independientes, pero
deben emparejarse una por vez porque comparten passcode y discriminator.

La generación/provisión de credenciales únicas por unidad no forma parte de este
cambio. Debe implementarse posteriormente con el proveedor de datos de fábrica
compatible con las versiones instaladas de ESP-IDF y ESP-Matter, sin inventar una
API ni generar credenciales aleatorias al arrancar.

## Archivos principales

### Nuevos

- `firmware/main/sht30_sensor.cpp`
- `firmware/main/include/sht30_sensor.h`
- `firmware/main/include/selected_sensor.h`
- `firmware/sdkconfig.sht30.defaults`
- `docs/sht30-support-sdd.md`

### Modificados

- `firmware/main/Kconfig.projbuild`: selección de sensor, dirección SHT30 y topic
  inicial dependiente del sensor.
- `firmware/main/CMakeLists.txt`: compila solo el driver seleccionado; BME68x solo
  es dependencia en la build BME680.
- `firmware/main/include/sensor_sample.h`: snapshot genérico y presión exclusiva
  de BME680.
- `firmware/main/sensor_service.cpp`: instancia `SelectedSensor`.
- `firmware/main/mqtt_telemetry.cpp`: payload dependiente de las capacidades.
- `firmware/main/matter_device.cpp`: cluster de presión exclusivo de BME680.
- `firmware/main/app_main.cpp`: log de cableado correspondiente a la build.
- `README.md`, `docs/hardware.md`, `docs/mqtt.md`, `docs/matter.md`.

## Configuración y compilación

Las versiones documentadas por el proyecto son ESP-IDF `6.1.0` y ESP-Matter
`1.6.0`. No actualizarlas como parte de esta validación.

### BME680 estándar

Desde `firmware/`:

```bash
idf.py -B build-matter-standard \
  -DSDKCONFIG=sdkconfig.matter-standard \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter-standard.defaults" \
  build
```

La selección predeterminada de Kconfig es BME680.

### SHT30 estándar

Desde `firmware/`:

```bash
idf.py -B build-sht30 \
  -DSDKCONFIG=sdkconfig.sht30 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter-standard.defaults;sdkconfig.sht30.defaults" \
  build
```

Si `ADDR` está conectado a 3V3, cambiar `APP_SHT30_I2C_ADDRESS` a `0x45` en el
`sdkconfig` local antes de compilar.

## Plan de validación en la Mac

### 1. Preparar el entorno

1. Activar ESP-IDF `6.1.0`.
2. Confirmar `idf.py --version`.
3. Abrir el repositorio y cambiar a `feature/sht30-support`.
4. Ejecutar `git status` y revisar que estén presentes todos los cambios de este
   documento.

### 2. Compilar ambas variantes

1. Compilar BME680 con `build-matter-standard`.
2. Compilar SHT30 con `build-sht30`.
3. Corregir cualquier error de API contra el framework instalado inspeccionando
   primero sus headers o fuentes.
4. Ejecutar `git diff --check` y revisar el diff completo.

### 3. Validar primero SHT30 aislado

Cableado esperado:

```text
SHT30 VCC  -> 3V3
SHT30 GND  -> GND
SHT30 SDA  -> GPIO4
SHT30 SCL  -> GPIO5
SHT30 ADDR -> GND/default para 0x44, o 3V3 para 0x45
```

Flashear y monitorear:

```bash
idf.py -B build-sht30 -DSDKCONFIG=sdkconfig.sht30 -p PORT flash monitor
```

Confirmar:

- Detección en la dirección configurada.
- Lecturas razonables cada 3 segundos.
- Ausencia de errores CRC en operación normal.
- Recuperación después de una desconexión I²C temporal, sin busy loops.

### 4. Validar MQTT

1. Abrir `http://<device-ip>/mqtt-tab`.
2. Configurar broker, usuario, contraseña y topic por ubicación.
3. Confirmar que el topic persiste después de reiniciar.
4. Verificar que SHT30 publica solo temperatura/humedad.
5. Verificar que BME680 sigue publicando temperatura/humedad/presión.
6. Reiniciar el broker y confirmar reconexión automática.

Ejemplo de observación:

```bash
mosquitto_sub -h BROKER -u USER -P PASSWORD \
  -t 'smart-environment-sensor/#' -v
```

### 5. Validar Matter/SmartThings

1. Emparejar una placa por vez.
2. Confirmar que la placa BME680 expone temperatura, humedad y presión.
3. Confirmar que la placa SHT30 expone temperatura y humedad, sin presión.
4. Renombrar los nodos como Indoor y Outdoor en SmartThings.
5. Reiniciar broker/Raspberry Pi y confirmar que Matter continúa operativo.

### 6. Validar Grafana

1. Confirmar que Telegraf está suscrito a los topics indoor/outdoor.
2. Verificar que ambos dispositivos producen series separadas.
3. Aceptar que `pressure_hpa` existe únicamente para la serie BME680.
4. Ajustar el dashboard o la ingesta en `smartInfrastructure` si actualmente
   asume que todos los mensajes contienen presión.

## Criterios de aceptación

- Ambas variantes compilan para `esp32c3` con las versiones actuales.
- BME680 conserva su comportamiento validado.
- SHT30 entrega temperatura/humedad válidas y rechaza CRC incorrectos.
- MQTT publica exactamente los campos disponibles para cada sensor.
- El topic se configura y persiste desde el portal HTML.
- Matter crea únicamente los clusters correspondientes a cada sensor.
- Los dos nodos pueden incorporarse a SmartThings.
- Un fallo MQTT no afecta Matter ni el muestreo.
- README y documentos técnicos permanecen sincronizados.
- No se agregan credenciales ni secretos al repositorio.

## Pendientes conocidos

- Ejecutar las dos compilaciones completas: no se realizaron en el entorno donde
  se implementó el cambio porque ESP-IDF no estaba instalado.
- Validar el SHT30 real y confirmar si su dirección es `0x44` o `0x45`.
- Probar cómo representa SmartThings el endpoint ambiental combinado del SHT30.
- Revisar `smartInfrastructure` para asegurar que la ingesta tolere payloads sin
  `pressure_hpa`.
- Diseñar credenciales Matter únicas por unidad usando el mecanismo oficial de
  factory data de las versiones instaladas.

## Referencia del sensor

El protocolo, los comandos, el CRC y las conversiones implementadas provienen de
la hoja de datos oficial Sensirion SHT3x-DIS, aplicable al SHT30.
