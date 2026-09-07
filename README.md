# Smart Environment Sensor

Firmware para un **ESP32-C3** conectado a un sensor ambiental **Bosch BME680**, con soporte para **Matter** y telemetría mediante **MQTT** hacia un broker local.

El firmware ya integra la lectura periódica del sensor, provisioning Wi-Fi por BLE,
un portal HTTP local, telemetría MQTT y una integración Matter opcional. Matter y
MQTT son independientes: un broker caído no detiene el muestreo ni el servicio
Matter.

---

## Funcionalidades

### Sensor

El BME680 proporciona:

* 🌡️ Temperatura
* 💧 Humedad relativa
* 🌬️ Presión atmosférica
* 🌫️ Resistencia del gas / información relacionada con calidad del aire

Inicialmente se implementarán:

* Temperatura
* Humedad
* Presión

La medición de gas y calidad del aire quedará preparada para una futura etapa.

### Matter

Matter se compila opcionalmente y se habilita mediante `APP_ENABLE_MATTER`.
El modelo depende de la configuración: por defecto hay un endpoint ambiental con
temperatura, humedad y presión; `APP_MATTER_SEPARATE_SENSOR_ENDPOINTS` crea un
endpoint por medición; `APP_MATTER_TEMPERATURE_ONLY` crea un `Temperature Sensor`
y `APP_MATTER_THERMOSTAT_ONLY` crea un `Thermostat` para validación. El endpoint
`PowerSource` es opcional y está desactivado en la build estándar.

### Telemetría

Además de Matter, el dispositivo podrá publicar las mediciones mediante MQTT.

Arquitectura prevista:

```text
BME680
   │
   │ I²C
   ▼
ESP32-C3
   │
   ├───────────────► Matter ─────► Matter Controller / Hub
   │
   └───────────────► MQTT ───────► Raspberry Pi
                                      │
                                      ▼
                                  InfluxDB
                                      │
                                      ▼
                                   Grafana
```

Matter estará orientado principalmente a la integración con domótica. El
firmware mide y publica por MQTT; el almacenamiento histórico y el análisis
posterior son responsabilidad de `smartInfrastructure`.

---

# Hardware

## Microcontrolador

* ESP32-C3 SuperMini
* Wi-Fi
* Bluetooth LE
* Matter sobre Wi-Fi

La placa objetivo inicial es un **ESP32-C3 SuperMini**. Si más adelante se usa una variante distinta de ESP32-C3, se deberá revisar especialmente el pinout, los pines de arranque y la disponibilidad de GPIO.

## Sensor

* Bosch BME680
* Comunicación I²C

Conexión prevista:

```text
BME680          ESP32-C3 SuperMini
──────────────────────────────────
VCC       ────► 3V3
GND       ────► GND
SDA       ────► GPIO4
SCL       ────► GPIO5
SDO       ────► GND   (dirección I²C 0x76)
CS        ────► 3V3   (modo I²C)
```

Se evitan GPIO8/GPIO9 para I²C en esta placa porque pueden interferir con funciones de arranque o BOOT según la variante del módulo. La dirección inicial prevista del BME680 es `0x76`.

---

# Software

## Firmware

El firmware utilizará:

* ESP-IDF
* ESP-Matter
* FreeRTOS
* Driver BME680
* Wi-Fi
* Matter
* MQTT

No se utilizará Arduino como framework principal.

La intención es mantener el proyecto basado en las herramientas oficiales de Espressif y utilizar APIs estándar siempre que sea posible.

---

# Arquitectura del firmware

La aplicación se dividirá conceptualmente en varias capas:

```text
┌──────────────────────────────────────────┐
│              Aplicación                 │
│                                          │
│       Gestión/lógica del sensor          │
└──────────────────┬───────────────────────┘
                   │
        ┌──────────┴───────────┐
        │                      │
        ▼                      ▼
┌───────────────┐       ┌───────────────┐
│  BME680       │       │ Matter        │
│  Driver       │       │ Endpoints     │
└───────┬───────┘       └───────────────┘
        │
        ▼
     I²C Bus

                   ┌───────────────┐
                   │ MQTT Client   │
                   └───────┬───────┘
                           │
                           ▼
                       Wi-Fi
```

La lectura del sensor y la comunicación Matter/MQTT deben mantenerse desacopladas.

Actualmente, el muestreo del BME680 corre en una tarea FreeRTOS dedicada
(`sensor_task`) cada 3 segundos. La última muestra se mantiene en un snapshot
protegido por mutex, con timestamp, secuencia, validez y último error. Matter y
MQTT consumen ese snapshot sin acceder al bus I²C ni al driver BME680.

---

# Matter

El dispositivo se modela como un único Matter Node. La build estándar de
validación está definida en `firmware/sdkconfig.matter-standard.defaults` y usa
un endpoint ambiental con temperatura, humedad y presión. Las variantes y los
clusters se detallan en [`docs/matter.md`](docs/matter.md).

El endpoint `PowerSource` es opcional y se mantiene apagado en la build Matter
estándar, porque el prototipo actual está alimentado por USB.

```text
Matter Node
│
└── Endpoint ambiental
    ├── TemperatureMeasurement
    ├── RelativeHumidityMeasurement
    └── PressureMeasurement
```

Los valores del BME680 serán convertidos a las unidades y escalas requeridas por los clusters Matter correspondientes.

El firmware no debe asumir que los valores internos del BME680 pueden copiarse directamente a los atributos Matter.

---

# Muestreo del sensor

El BME680 será muestreado periódicamente.

Como configuración inicial se considera un intervalo de aproximadamente:

```text
3 segundos
```

El intervalo está centralizado en `firmware/main/include/app_config.h`.

La frecuencia de lectura del sensor no necesariamente será igual a la frecuencia de publicación MQTT o de reporting Matter.

Ejemplo:

```text
BME680
   │
   │ cada 3 s
   ▼
 Datos del sensor
   │
   ├──► Reporting Matter
   │
   └──► Telemetría MQTT
```

La estrategia definitiva de reporting será definida durante el desarrollo. Matter
y MQTT deberán consumir la última muestra disponible desde la capa de servicio
del sensor.

---

# MQTT

MQTT se utilizará como canal de telemetría para la infraestructura local.

El contrato actual está documentado en [`docs/mqtt.md`](docs/mqtt.md): el topic
por defecto es `smart-environment-sensor/bme680/state` y el payload usa los
campos `temperature_c`, `humidity_percent` y `pressure_hpa`.

Arquitectura:

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

El ESP32 no escribirá directamente en InfluxDB.

---

# Observabilidad

Este repositorio se ocupa del firmware: leer el BME680, publicar por MQTT y
mantener Matter desacoplado de MQTT. La responsabilidad termina en la
publicación del mensaje MQTT; el ESP32 no escribe directamente en InfluxDB.

La ingesta MQTT, el almacenamiento histórico, el provisioning de datasources y
los dashboards están centralizados en
[`smartInfrastructure`](https://github.com/tucho235/smartInfrastructure).

El tópico de telemetría es `smart-environment-sensor/bme680/state` y el payload
contiene `temperature_c`, `humidity_percent` y `pressure_hpa`.

Ejemplo de dashboard:

```text
┌─────────────────────────────────────────┐
│           BME680 Environment             │
├─────────────────────────────────────────┤
│                                         │
│ Temperature       23.4 °C               │
│ Humidity          54.2 %                │
│ Pressure        1008.3 hPa              │
│                                         │
├─────────────────────────────────────────┤
│ Temperature history                      │
│                                         │
│      ╭──────╮                           │
│  ────╯      ╰────────                   │
│                                         │
├─────────────────────────────────────────┤
│ Humidity history                         │
│                                         │
│      ╭────────╮                         │
│ ─────╯        ╰────                     │
│                                         │
├─────────────────────────────────────────┤
│ Pressure history                         │
└─────────────────────────────────────────┘
```

---

# Fiabilidad

La implementación actual contempla:

* Reconexión automática de Wi-Fi.
* Reconexión automática de MQTT.
* Funcionamiento independiente de Matter y MQTT.
* Detección y registro de errores de comunicación I²C.
* Diagnósticos periódicos de heap y stack.
* Manejo de errores del BME680.
* Inicialización segura del sensor.
* Persistencia de configuración mediante NVS cuando sea necesario.

Un fallo del MQTT broker o de la Raspberry Pi **no debe impedir que el dispositivo continúe funcionando como dispositivo Matter**.

---

# OTA

Una futura versión del firmware deberá soportar actualizaciones OTA.

Objetivo:

```text
Firmware nuevo
      │
      ▼
Wi-Fi
      │
      ▼
ESP32-C3
      │
      ▼
OTA update
```

La implementación OTA deberá priorizar mecanismos seguros y recuperación ante actualizaciones fallidas.

---

# Calidad del aire / VOC

El BME680 incorpora un sensor de gas.

En una futura etapa se investigará la utilización de la resistencia del gas para obtener información relacionada con:

* VOC
* Calidad del aire
* Cambios relativos en la calidad del aire

No se debe interpretar directamente la resistencia del gas como una concentración absoluta de VOC sin una estrategia de calibración adecuada.

Una posible futura representación Matter será mediante un dispositivo/cluster relacionado con **Air Quality**.

---

# Estructura del proyecto

La estructura actual incluye la base ESP-IDF, el driver BME680 y una capa de servicio de sensor:

```text
smartEnvironmentSensor/
│
├── firmware/
│   ├── components/
│   │   └── bme68x/
│   │       ├── bme68x.c
│   │       ├── bme68x.h
│   │       ├── bme68x_defs.h
│   │       ├── CMakeLists.txt
│   │       └── README.md
│   ├── main/
│   │   ├── app_main.cpp
│   │   ├── bme680_sensor.cpp
│   │   ├── config_portal.cpp
│   │   ├── matter_config.cpp
│   │   ├── matter_device.cpp
│   │   ├── memory_diagnostics.cpp
│   │   ├── mqtt_config.cpp
│   │   ├── mqtt_telemetry.cpp
│   │   ├── sensor_service.cpp
│   │   ├── wifi_station.cpp
│   │   ├── include/
│   │   │   ├── app_config.h
│   │   │   ├── bme680_sensor.h
│   │   │   ├── config_portal.h
│   │   │   ├── matter_config.h
│   │   │   ├── matter_device.h
│   │   │   ├── mqtt_config.h
│   │   │   ├── mqtt_telemetry.h
│   │   │   ├── sensor_sample.h
│   │   │   ├── sensor_service.h
│   │   │   └── wifi_station.h
│   │   ├── CMakeLists.txt
│   │   ├── Kconfig.projbuild
│   │   └── idf_component.yml
│   ├── CMakeLists.txt
│   ├── dependencies.lock
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   ├── sdkconfig.matter.defaults
│   └── sdkconfig.matter-standard.defaults
│
├── hardware/
│   └── README.md
│
├── docs/
│   ├── hardware.md
│   ├── matter.md
│   ├── mqtt.md
│   └── wifi.md
│
├── .gitignore
├── README.md
├── AGENTS.md
└── LICENSE
```

El sensor BME680, la capa de servicio, Wi-Fi provisioning, MQTT, el portal web
de configuración y Matter están implementados de forma incremental. El driver
Bosch está aislado en `components/bme68x`; la aplicación lo adapta mediante
`Bme680Sensor` y `sensor_service`.

---

# Entorno de desarrollo

El firmware puede desarrollarse desde:

* macOS
* Linux
* Raspberry Pi

Windows no es un requisito.

El desarrollo principal utilizará:

```text
ESP-IDF
ESP-Matter
Git
CMake
Ninja
Python
```

VS Code puede utilizarse como IDE.

---

# Compilación

Una vez instalado ESP-IDF:

```bash
cd firmware
idf.py set-target esp32c3
```

Compilar:

```bash
idf.py build
```

El firmware no necesita SSID/password al compilar. Si no hay credenciales Wi-Fi
guardadas en NVS, el ESP32-C3 inicia provisioning BLE y permite configurarlas
desde una app compatible.

La opción local `BLE provisioning proof of possession` puede configurarse con
`idf.py menuconfig` si se quiere agregar una prueba de posesión durante el
provisioning. Ese valor queda en `firmware/sdkconfig`, que está ignorado por
Git.

No se deben agregar SSID/password ni proofs of possession a `sdkconfig.defaults`.

MQTT guarda su configuración en NVS. Como mecanismo de bootstrap o migración,
también puede tomar valores locales desde `idf.py menuconfig`:

```text
Smart Environment Sensor Configuration
```

Setear `MQTT broker URI bootstrap`, `MQTT username bootstrap`, `MQTT password
bootstrap` y, si hace falta, `Default MQTT telemetry topic`. Si no existe
configuración MQTT en NVS y el broker URI bootstrap queda vacío, la telemetría
MQTT espera configuración y el sensor sigue funcionando normalmente.

Cuando MQTT está configurado, el cliente espera a que Wi-Fi obtenga IP antes de
conectar al broker.

La forma recomendada de configurar MQTT es el portal web local. Después de
provisionar Wi-Fi, abrir en un navegador la IP que aparece en el monitor serie:

```text
http://<device-ip>/
```

El portal separa la configuración en tabs para MQTT y Matter.

La tab MQTT permite activar/desactivar el servicio MQTT, y guarda `broker URI`,
usuario, password, topic e intervalo en NVS. Si MQTT queda desactivado, el
firmware no intenta conectar al broker ni publicar telemetría.

La tab Matter muestra el QR/setup payload, el manual pairing code y permite
activar/desactivar el arranque del servicio Matter en NVS. Si Matter queda
desactivado, el firmware no inicia el nodo Matter en el siguiente boot.

Al guardar cambios desde el portal, el ESP32-C3 reinicia para aplicar la nueva
configuración. No se deben agregar credenciales MQTT a `sdkconfig.defaults`.

Durante el provisioning BLE inicial también existe soporte para enviar
configuración MQTT como JSON al endpoint `mqtt-config` o `custom-data`, pero la
app oficial Espressif BLE Provisioning para Android no muestra esos campos en
su UI.

Flashear:

```bash
idf.py flash
```

Monitor serie:

```bash
idf.py monitor
```

También se puede combinar:

```bash
idf.py flash monitor
```

---

# Hoja de ruta de desarrollo

## Fase 0 — Base ESP-IDF

* [x] Crear estructura mínima ESP-IDF.
* [x] Configurar target inicial `esp32c3` en `sdkconfig.defaults`.
* [x] Agregar `app_main.cpp` inicial.
* [x] Documentar hardware inicial en `docs/hardware.md`.
* [x] Compilar con `idf.py build` en un entorno con ESP-IDF instalado.

## Fase 1 — Hardware

* [x] Confirmar placa objetivo inicial: ESP32-C3 SuperMini.
* [x] Confirmar módulo BME680 con pines VCC/GND/SCL/SDA/SDO/CS.
* [x] Definir GPIO I²C iniciales: SDA GPIO4, SCL GPIO5.
* [x] Documentar cableado inicial del BME680.
* [x] Verificar alimentación de 3.3 V.
* [x] Agregar prueba inicial de comunicación I²C con lectura de chip ID.
* [x] Probar comunicación I²C en hardware.

## Fase 2 — BME680

* [x] Integrar driver BME680 usando Bosch BME68x Sensor API.
* [x] Leer temperatura.
* [x] Leer humedad.
* [x] Leer presión.
* [x] Mover muestreo a `sensor_task`.
* [x] Exponer última muestra mediante snapshot protegido por mutex.
* [ ] Leer resistencia del gas.
* [x] Implementar manejo básico de errores de inicialización y lectura.
* [x] Implementar configuración inicial de oversampling/filter para T/P/H.
* [ ] Implementar configuración de heater para gas.

## Fase 3 — Matter

* [x] Crear scaffold ESP-Matter opcional.
* [x] Documentar estrategia de red custom.
* [x] Implementar Temperature Sensor.
* [x] Implementar Humidity Sensor.
* [x] Implementar Pressure Sensor.
* [x] Resolver y validar build con componente `espressif/esp_matter`.
* [x] Agregar defaults locales para build Matter.
* [x] Agregar control runtime de Matter en el portal web local.
* [ ] Realizar commissioning.
* [ ] Verificar funcionamiento con Matter Controller.
* [ ] Implementar reporting adecuado.

## Fase 4 — MQTT

* [x] Implementar cliente MQTT.
* [x] Definir topics preliminares.
* [x] Definir payload preliminar.
* [x] Implementar reconexión.
* [x] Integrar con broker existente.
* [x] Verificar recepción desde Raspberry Pi.

## Fase 5 — Integración con la infraestructura

* [x] Publicar telemetría ambiental por MQTT.
* [x] Definir y documentar el contrato MQTT.
* [x] Desacoplar el firmware de InfluxDB y Grafana.

La ingesta, el almacenamiento, el provisioning y los dashboards se mantienen
en el repositorio
[`smartInfrastructure`](https://github.com/tucho235/smartInfrastructure).

## Fase 6 — Fiabilidad

* [x] Wi-Fi auto reconnect básico.
* [x] BLE Wi-Fi provisioning básico.
* [x] MQTT reconexión mediante ESP-MQTT.
* [ ] I²C recovery.
* [x] Diagnósticos periódicos de memoria y stack.
* [x] NVS para credenciales Wi-Fi.
* [x] Manejo básico de errores de sensor, Wi-Fi, MQTT y Matter.
* [ ] Pruebas prolongadas.

## Fase 7 — Avanzado

* [ ] OTA.
* [ ] Air Quality.
* [ ] Gas resistance telemetry.
* [ ] Optimización de consumo.
* [ ] Configuración del intervalo de medición.
* [ ] Diagnóstico.
* [ ] Métricas internas del ESP32.

---

# Principios de diseño

El proyecto seguirá estos principios:

1. **Matter y MQTT son interfaces independientes.**
2. **La Raspberry Pi no es necesaria para el funcionamiento Matter.**
3. **El BME680 debe estar desacoplado de la lógica Matter.**
4. **Se deben utilizar estándares Matter existentes cuando sea posible.**
5. **No introducir dependencias innecesarias.**
6. **Priorizar estabilidad sobre funcionalidades adicionales.**
7. **Toda funcionalidad nueva debe documentarse.**
8. **El firmware debe recuperarse automáticamente de fallos temporales de red.**
9. **No asumir que la frecuencia de medición debe coincidir con la frecuencia de reporting.**
10. **Las mediciones deben conservar sus unidades y escalas correctamente al pasar entre BME680, MQTT, InfluxDB y Matter.**

---

# Estado

🚧 **Early Development — integración Matter en validación**

El proyecto se encuentra en etapa de diseño e implementación inicial.

Actualmente están definidos:

* ESP32-C3 como plataforma.
* BME680 como sensor.
* ESP-IDF como framework.
* ESP-Matter como implementación Matter.
* MQTT como canal de telemetría.

La base ESP-IDF, lectura BME680, Wi-Fi provisioning BLE, portal local de
configuración, MQTT y la integración Matter ya existen. El commissioning y la
validación con un Matter Controller aún están pendientes. InfluxDB, Telegraf y
Grafana son responsabilidad de
[`smartInfrastructure`](https://github.com/tucho235/smartInfrastructure).
La validación completa de ESP-Matter y reporting con SmartThings sigue en ajuste
por etapas.
