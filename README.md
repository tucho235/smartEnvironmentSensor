# Smart Environment Sensor

Firmware para un **ESP32-C3** conectado a un sensor ambiental **Bosch BME680**, con soporte para **Matter** y telemetría mediante **MQTT** hacia una Raspberry Pi para almacenamiento histórico en **InfluxDB** y visualización mediante **Grafana**.

El objetivo es construir un sensor ambiental compacto, autónomo y extensible que pueda integrarse tanto en sistemas de domótica compatibles con Matter como en una infraestructura propia de monitorización.

---

## Features

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

El ESP32-C3 funcionará como dispositivo Matter sobre Wi-Fi.

El dispositivo expondrá inicialmente:

* Temperature Sensor
* Humidity Sensor
* Pressure Sensor

La intención es utilizar los clusters y device types estándar de Matter, evitando implementar características propietarias cuando exista una representación estándar.

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

Matter estará orientado principalmente a la integración con domótica, mientras que MQTT permitirá almacenar mediciones históricas y realizar análisis detallados.

---

# Hardware

## Microcontroller

* ESP32-C3 SuperMini
* Wi-Fi
* Bluetooth LE
* Matter over Wi-Fi

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
│              Application                 │
│                                          │
│        Sensor management / logic         │
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

---

# Matter

El dispositivo se modelará inicialmente como un único Matter Node con varios endpoints.

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

Los valores del BME680 serán convertidos a las unidades y escalas requeridas por los clusters Matter correspondientes.

El firmware no debe asumir que los valores internos del BME680 pueden copiarse directamente a los atributos Matter.

---

# Sensor sampling

El BME680 será muestreado periódicamente.

Como configuración inicial se considera un intervalo de aproximadamente:

```text
2-5 segundos
```

El intervalo definitivo será configurable y podrá modificarse durante la optimización del firmware.

La frecuencia de lectura del sensor no necesariamente será igual a la frecuencia de publicación MQTT o de reporting Matter.

Ejemplo:

```text
BME680
   │
   │ cada 2 s
   ▼
Sensor data
   │
   ├──► Matter reporting
   │
   └──► MQTT telemetry
```

La estrategia definitiva de reporting será definida durante el desarrollo.

---

# MQTT

MQTT se utilizará como canal de telemetría para la infraestructura local.

Se pretende publicar las mediciones de forma estructurada, por ejemplo:

```json
{
  "temperature": 23.47,
  "humidity": 54.21,
  "pressure": 1008.32
}
```

El formato definitivo de los topics y payloads se definirá durante la implementación.

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

# InfluxDB / Grafana

La Raspberry Pi será responsable del almacenamiento histórico y visualización.

Las métricas previstas incluyen:

* Temperatura
* Humedad
* Presión
* Resistencia del gas (futuro)
* Estado del dispositivo
* RSSI Wi-Fi (futuro)
* Tiempo de actividad (futuro)

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

# Reliability

El firmware deberá contemplar:

* Reconexión automática de Wi-Fi.
* Reconexión automática de MQTT.
* Funcionamiento independiente de Matter y MQTT.
* Recuperación ante errores de comunicación I²C.
* Watchdog.
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

# Air Quality / VOC

El BME680 incorpora un sensor de gas.

En una futura etapa se investigará la utilización de la resistencia del gas para obtener información relacionada con:

* VOC
* Calidad del aire
* Cambios relativos en la calidad del aire

No se debe interpretar directamente la resistencia del gas como una concentración absoluta de VOC sin una estrategia de calibración adecuada.

Una posible futura representación Matter será mediante un dispositivo/cluster relacionado con **Air Quality**.

---

# Project Structure

La estructura actual incluye una base mínima ESP-IDF y la documentación inicial de hardware:

```text
smartEnvironmentSensor/
│
├── firmware/
│   ├── main/
│   │   ├── app_main.cpp
│   │   └── CMakeLists.txt
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
│
├── hardware/
│   └── README.md
│
├── docs/
│   └── hardware.md
├── .gitignore
├── README.md
├── AGENTS.md
└── LICENSE
```

Los módulos de BME680, Matter y MQTT se agregarán de forma incremental cuando se implemente cada etapa. La base actual solo inicializa la aplicación y registra las decisiones de hardware iniciales.

---

# Development Environment

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

# Build

Una vez instalado ESP-IDF:

```bash
cd firmware
idf.py set-target esp32c3
```

Compilar:

```bash
idf.py build
```

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

# Development Roadmap

## Phase 0 — ESP-IDF Base

* [x] Crear estructura mínima ESP-IDF.
* [x] Configurar target inicial `esp32c3` en `sdkconfig.defaults`.
* [x] Agregar `app_main.cpp` inicial.
* [x] Documentar hardware inicial en `docs/hardware.md`.
* [x] Compilar con `idf.py build` en un entorno con ESP-IDF instalado.

## Phase 1 — Hardware

* [x] Confirmar placa objetivo inicial: ESP32-C3 SuperMini.
* [x] Confirmar módulo BME680 con pines VCC/GND/SCL/SDA/SDO/CS.
* [x] Definir GPIO I²C iniciales: SDA GPIO4, SCL GPIO5.
* [x] Documentar cableado inicial del BME680.
* [ ] Verificar alimentación de 3.3 V.
* [ ] Probar comunicación I²C.

## Phase 2 — BME680

* [ ] Integrar driver BME680.
* [ ] Leer temperatura.
* [ ] Leer humedad.
* [ ] Leer presión.
* [ ] Leer resistencia del gas.
* [ ] Implementar manejo de errores.
* [ ] Implementar configuración de oversampling/filter/heater.

## Phase 3 — Matter

* [ ] Crear proyecto ESP-Matter.
* [ ] Implementar Temperature Sensor.
* [ ] Implementar Humidity Sensor.
* [ ] Implementar Pressure Sensor.
* [ ] Realizar commissioning.
* [ ] Verificar funcionamiento con Matter Controller.
* [ ] Implementar reporting adecuado.

## Phase 4 — MQTT

* [ ] Implementar cliente MQTT.
* [ ] Definir topics.
* [ ] Definir payload.
* [ ] Implementar reconexión.
* [ ] Integrar con broker existente.
* [ ] Verificar recepción desde Raspberry Pi.

## Phase 5 — InfluxDB / Grafana

* [ ] Crear esquema de almacenamiento.
* [ ] Registrar temperatura.
* [ ] Registrar humedad.
* [ ] Registrar presión.
* [ ] Crear dashboard.
* [ ] Agregar métricas de estado.

## Phase 6 — Reliability

* [ ] Wi-Fi auto reconnect.
* [ ] MQTT auto reconnect.
* [ ] I²C recovery.
* [ ] Watchdog.
* [ ] NVS.
* [ ] Manejo de errores.
* [ ] Pruebas prolongadas.

## Phase 7 — Advanced

* [ ] OTA.
* [ ] Air Quality.
* [ ] Gas resistance telemetry.
* [ ] Optimización de consumo.
* [ ] Configuración del intervalo de medición.
* [ ] Diagnóstico.
* [ ] Métricas internas del ESP32.

---

# Design Principles

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

# Status

🚧 **Early Development**

El proyecto se encuentra en etapa de diseño e implementación inicial.

Actualmente están definidos:

* ESP32-C3 como plataforma.
* BME680 como sensor.
* ESP-IDF como framework.
* ESP-Matter como implementación Matter.
* MQTT como canal de telemetría.
* InfluxDB + Grafana como plataforma de monitorización.

La base ESP-IDF mínima ya existe en `firmware/`. La integración del BME680, Matter y MQTT sigue pendiente y se implementará por etapas.
