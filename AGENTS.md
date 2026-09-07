# AGENTS.md

## Proyecto

**Smart Environment Sensor**

Firmware para una ESP32-C3 SuperMini conectada a un sensor ambiental Bosch BME680.
El dispositivo expone mediciones mediante Matter y publica telemetría por MQTT
hacia la infraestructura local de una Raspberry Pi.

## Objetivos principales

El firmware debe proporcionar temperatura, humedad relativa, presión atmosférica,
Matter sobre Wi-Fi, telemetría MQTT, recuperación automática de Wi-Fi y MQTT, y
comunicación robusta con el BME680 mediante I²C.

Las funcionalidades futuras pueden incluir resistencia del gas, calidad del aire,
OTA, diagnósticos y administración avanzada de energía. No implementarlas salvo
solicitud explícita.

## Hardware

MCU objetivo: `ESP32-C3 SuperMini`.

Sensor: `Bosch BME680`, conectado mediante I²C.

Cableado inicial:

```text
BME680 VCC  -> ESP32-C3 SuperMini 3V3
BME680 GND  -> ESP32-C3 SuperMini GND
BME680 SDA  -> ESP32-C3 SuperMini GPIO4
BME680 SCL  -> ESP32-C3 SuperMini GPIO5
BME680 SDO  -> GND, dirección I²C 0x76
BME680 CS   -> 3V3, modo I²C
```

Usar estos pines salvo que se actualice la configuración del proyecto. No usar
GPIO8/GPIO9 para I²C sin revisar antes el arranque y strapping de la placa.

## Stack de software

Usar ESP-IDF, ESP-Matter, FreeRTOS y C/C++. No migrar a Arduino. Preferir
componentes y APIs oficiales de Espressif y evitar dependencias innecesarias.

## Arquitectura Matter

Implementar el dispositivo como un Matter Node con device types y clusters
estándar. El modelo actual depende de la configuración: por defecto usa un
endpoint ambiental con temperatura, humedad y presión; también admite endpoints
separados, solo temperatura o Thermostat para validación.

Matter debe permanecer independiente de MQTT. Un fallo del broker o de la
Raspberry Pi no debe impedir el funcionamiento de Matter.

## Arquitectura del sensor

Mantener el acceso al BME680 aislado de Matter y MQTT:

```text
BME680 Driver
      │
      ▼
Sensor abstraction
      │
      ├──────────► Matter
      │
      └──────────► MQTT
```

Matter y MQTT deben consumir snapshots de `sensor_service`; no deben acceder
directamente al driver ni al bus I²C. Las unidades internas son °C, %RH, hPa o
Pa y Ω. Las conversiones Matter y MQTT deben hacerse en sus respectivas capas.

## Medición y reporting

El BME680 se muestrea cada 3 segundos, intervalo definido en
`firmware/main/include/app_config.h`. El muestreo, el reporting Matter y la
publicación MQTT deben configurarse de forma independiente.

Matter debe usar reporting apropiado y cambios significativos cuando sea
posible. No copiar valores flotantes directamente a atributos Matter sin
verificar tipo, rango, unidad y escala.

## MQTT

MQTT se usa para telemetría local:

```text
ESP32-C3 -> MQTT Broker -> Raspberry Pi -> InfluxDB -> Grafana
```

El ESP32 no debe conectarse directamente a InfluxDB. El topic y payload deben
estar documentados. MQTT debe reconectarse tras interrupciones de Wi-Fi,
reinicios del broker o fallos temporales de red.

## Conectividad

Wi-Fi debe reconectarse automáticamente después de pérdidas temporales. Cuando
Matter está activo, ESP-Matter controla la conexión y reconexión; en otro caso,
lo hace la capa Wi-Fi local. Los fallos de red no deben bloquear la tarea del
sensor indefinidamente.

## Manejo de errores

Gestionar fallos del BME680, errores I²C, mediciones inválidas, desconexiones
Wi-Fi/MQTT, problemas Matter y respuestas inesperadas. Usar códigos de error y
logs de ESP-IDF, comprobar retornos explícitamente y aplicar espera/backoff,
nunca bucles de reintento ajustados.

## I²C

Usar las APIs I²C de ESP-IDF correspondientes a la versión seleccionada y
centralizar la inicialización. No inicializar el bus desde varios módulos. No
implementar recuperación del bus hasta que la comunicación normal funcione y se
conozca el fallo concreto.

## FreeRTOS y seguridad de hilos

Usar tareas FreeRTOS solo cuando sean necesarias, `vTaskDelay()` y APIs orientadas
a eventos. Evitar busy loops y bloqueos prolongados. Proteger los datos
compartidos con mutex, colas, secciones críticas o snapshots inmutables. No
introducir estado global mutable sin considerar la concurrencia.

## Configuración y secretos

Centralizar pines, dirección BME680, intervalos, broker, puerto y topics. Usar
Kconfig o mecanismos apropiados de ESP-IDF. Nunca incluir en Git contraseñas,
tokens, claves, certificados privados, credenciales Matter ni datos personales
de infraestructura. Revisar `.gitignore`; las credenciales locales deben quedar
en `sdkconfig` ignorado u otros archivos excluidos.

## Logging

Usar `ESP_LOGE()`, `ESP_LOGW()`, `ESP_LOGI()`, `ESP_LOGD()` y `ESP_LOGV()` con
un TAG consistente. Registrar inicialización y errores del sensor, estados de
Wi-Fi/MQTT, commissioning Matter y mediciones de depuración. Nunca registrar
credenciales ni información de red sensible y evitar logs excesivos en producción.

## Estilo de código

Usar C/C++ moderno apropiado para ESP-IDF, nombres claros, funciones pequeñas,
manejo explícito de errores, `const`, RAII cuando corresponda, estado global
mínimo y componentes modulares. Evitar funciones gigantes, números mágicos,
efectos ocultos, macros innecesarias, copias y operaciones de red bloqueantes en
el código del sensor.

## Estructura y flujo de desarrollo

La estructura actual está definida en `README.md`. Antes de cambios importantes:

1. Inspeccionar el proyecto, `README.md` y `AGENTS.md`.
2. Revisar las versiones actuales de ESP-IDF y ESP-Matter.
3. Entender la arquitectura existente.
4. Hacer el cambio mínimo razonable.
5. Compilar cuando existan archivos de build y corregir errores si corresponde.
6. Revisar el diff y actualizar la documentación cuando cambie el comportamiento.

No sobrescribir funcionalidad existente innecesariamente.

## Compatibilidad de versiones

No actualizar ESP-IDF, ESP-Matter ni dependencias BME680 arbitrariamente.
Determinar primero las versiones, comprobar compatibilidad, documentar el motivo,
hacer el cambio mínimo y compilar el proyecto completo. Si una API no está
clara, inspeccionar el código fuente del framework instalado en lugar de adivinar.

## Build y pruebas

Objetivo: `esp32c3`.

```bash
idf.py set-target esp32c3
idf.py build
idf.py flash
idf.py monitor
idf.py flash monitor
```

Validar incrementalmente: primero BME680/I²C y lecturas válidas; después Matter
aislado; luego mediciones reales en Matter; MQTT; integración con la
infraestructura; y finalmente pruebas prolongadas. No depurar todos los
subsistemas simultáneamente si pueden aislarse.

## Funcionalidades futuras

Gas, calidad del aire, OTA, diagnósticos, RSSI, uptime, intervalos configurables,
parámetros MQTT, persistencia NVS avanzada, optimización energética y deep sleep
están fuera de alcance hasta recibir solicitud explícita.

## Restricciones importantes

1. No reemplazar ESP-IDF por Arduino.
2. No acoplar Matter con MQTT.
3. No acoplar el driver BME680 con Matter.
4. No hacer que la Raspberry Pi sea necesaria para Matter.
5. No incluir credenciales hard-codeadas.
6. No adivinar GPIO ni tipos o escalas Matter.
7. No actualizar dependencias sin comprobar compatibilidad.
8. No introducir dependencias de terceros innecesarias.
9. No implementar funcionalidades futuras sin aprobación explícita.
10. Compilar después de cambios significativos de código.
11. Mantener sincronizada la documentación con la arquitectura.

## Estado actual

El proyecto está en desarrollo inicial. Ya existen la base ESP-IDF, el driver y
servicio BME680, provisioning Wi-Fi BLE, portal web local, MQTT, persistencia
NVS, diagnósticos de memoria y la integración Matter opcional. El commissioning,
la validación completa con controladores Matter, el reporting formal, OTA, gas y
calidad del aire siguen pendientes.

## Definición de terminado

Una funcionalidad está terminada cuando está documentada, el proyecto compila,
el manejo de errores es apropiado, no se comprometen credenciales, no se rompe
la funcionalidad existente, la configuración relevante está documentada, el
código respeta la arquitectura y el diff fue revisado.
