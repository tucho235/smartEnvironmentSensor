# Matter

Matter es una funcionalidad opcional del firmware y está desactivada por
defecto en la configuración normal.

El código admite varios modelos de endpoints. Los valores predeterminados de
validación estándar usan el modelo ambiental completo en un endpoint:
temperatura, humedad relativa y presión. También hay un modo de solo temperatura
para probar la compatibilidad con controladores.

```text
Matter Node
└── Environmental endpoint (default)
    ├── TemperatureMeasurement
    ├── RelativeHumidityMeasurement
    └── PressureMeasurement
```

La implementación está aislada en `matter_device.cpp/.h` y consume los datos del
sensor únicamente mediante `sensor_service_get_latest()`. No debe acceder
directamente al driver BME680 ni al bus I²C, y debe mantenerse independiente de
MQTT.

## Opción de compilación

Matter está desactivado por defecto:

```text
Smart Environment Sensor Configuration
└── Enable Matter endpoint integration
```

Cuando se habilita, el proyecto obtiene el componente administrado
`espressif/esp_matter` e inicia un nodo Matter. El modelo de endpoints se
selecciona mediante opciones mutuamente excluyentes:

```text
APP_MATTER_THERMOSTAT_ONLY              un endpoint Thermostat
APP_MATTER_TEMPERATURE_ONLY             un endpoint Temperature Sensor
APP_MATTER_SEPARATE_SENSOR_ENDPOINTS    un endpoint por medición
todo desactivado                         un endpoint con los tres clusters
```

`APP_MATTER_TEMPERATURE_ONLY` crea el endpoint 1 como un `Temperature Sensor`
estándar con `TemperatureMeasurement.MeasuredValue`.
`APP_MATTER_ENABLE_POWER_SOURCE` agrega un endpoint `PowerSource` USB estático y
opcional; los valores predeterminados de validación estándar lo desactivan.

Esto mantiene estable la build actual solo con MQTT mientras se validan la
dependencia Matter y el flujo de commissioning.

Se validó una build para ESP32-C3 con `APP_ENABLE_MATTER=y`, el componente
administrado `espressif/esp_matter` `1.6.0` y ESP-IDF `6.1.0`.

El archivo versionado de valores predeterminados para esta variante es:

```text
firmware/sdkconfig.matter-standard.defaults
```

Esta variante habilita Matter, MQTT y el portal local de configuración,
selecciona el modelo ambiental completo y desactiva `PowerSource`.

Compilar la variante Matter sin modificar el `sdkconfig` local normal:

```bash
idf.py -B build-matter-standard \
  -DSDKCONFIG=sdkconfig.matter-standard \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter-standard.defaults" \
  build
```

Una vez compilada, flashear usando el mismo directorio de build:

```bash
idf.py -B build-matter-standard -DSDKCONFIG=sdkconfig.matter-standard -p PORT flash monitor
```

## Selección de clusters

La variante Matter estándar conserva el catálogo amplio de clusters
predeterminado de ESP-Matter mientras se valida el modelo de endpoints. El
firmware solo crea en tiempo de ejecución los endpoints siguientes:

```text
Endpoint predeterminado:
  device type de Temperature Sensor + TemperatureMeasurement
  device type de Humidity Sensor + RelativeHumidityMeasurement
  device type de Pressure Sensor + PressureMeasurement
Endpoint opcional:
  device type de Power Source + PowerSource
```

También conserva los clusters de commissioning y operacionales requeridos por el
nodo raíz. Los modelos de solo temperatura y de endpoints separados están
disponibles cuando un controlador concreto requiere una forma más reducida.

El soporte de OpenThread está desactivado en la build de ESP32-C3 solo con Wi-Fi:

```text
CONFIG_ESP_MATTER_ENABLE_OPENTHREAD is not set
```

## Commissioning de red

Cuando Matter está habilitado, el commissioning Wi-Fi lo gestiona el cluster
estándar Matter Network Commissioning. Esto mantiene la compatibilidad con
controladores Matter como SmartThings, que esperan enviar o validar la red Wi-Fi
durante el commissioning.

La variante Matter deja intencionadamente desactivada la configuración de red
personalizada de ESP-Matter:

```text
CONFIG_CUSTOM_NETWORK_CONFIG is not set
```

El flujo de provisioning BLE del proyecto sigue disponible cuando Matter está
desactivado en tiempo de ejecución. Cuando Matter está habilitado y no hay
credenciales Wi-Fi almacenadas, el firmware no inicia el servicio BLE de
provisioning de Espressif para evitar conflictos con CHIPoBLE durante el
commissioning Matter.

Cuando Matter está habilitado, la capa local de estación Wi-Fi inicializa la red
de ESP-IDF, inicia el modo estación y observa los eventos Wi-Fi/IP, pero no llama
a `esp_wifi_connect()` ni programa reconexiones. El gestor de conectividad de
ESP-Matter controla la conexión y las reconexiones para que solo un subsistema
gestione la asociación con el punto de acceso.

Cuando Matter está habilitado, el esquema BLE de provisioning Wi-Fi no debe usar
el handler `network_prov_scheme_ble_event_cb_free_btdm`. Ese handler libera la
memoria BTDM después del provisioning, mientras ESP-Matter todavía necesita BLE
para el commissioning CHIPoBLE.

La build de desarrollo utiliza actualmente estos parámetros de prueba Matter:

```text
QR payload:     MT:Y.K9042C00KA0648G00
Setup passcode: 20202021
Discriminator:  3840
Manual code:    34970112332
```

Estos valores solo sirven para desarrollo local. Están compilados en la build
Matter actual y deben reemplazarse antes de utilizar un firmware de producción.

## Portal en tiempo de ejecución

Después de conectar Wi-Fi, el portal local de configuración expone una pestaña
Matter:

```text
http://<device-ip>/matter-tab
```

La pestaña muestra el QR/setup payload de desarrollo actual, el código manual de
emparejamiento, el PIN de configuración y el discriminator. También incluye una
casilla `Enable Matter service`.

La casilla se almacena en NVS dentro del namespace Matter de la aplicación.
Cuando se desactiva, el firmware omite el arranque de Matter en el siguiente
boot. La opción de compilación `APP_ENABLE_MATTER` sigue controlando si
ESP-Matter se incluye o no en el firmware.

## Unidades

Internal firmware units:

```text
temperature_c     degrees Celsius
humidity_percent  relative humidity percent
pressure_hpa      hectopascals
```

Representaciones Matter:

```text
TemperatureMeasurement.MeasuredValue          0.01 degrees Celsius
RelativeHumidityMeasurement.MeasuredValue    0.01 percent
PressureMeasurement.MeasuredValue            0.1 kPa, equivalent to hPa
```

La capa Matter realiza estas conversiones localmente antes de actualizar los
atributos.

## Próximos pasos de validación

1. Habilitar `APP_ENABLE_MATTER` en el `sdkconfig` local.
2. Confirmar que ESP-Matter se resuelve mediante IDF Component Manager.
3. Mantener habilitado Matter Network Commissioning estándar.
4. Compilar para `esp32c3`.
5. Flashear y confirmar el funcionamiento del BME680 y Wi-Fi.
6. Hacer el commissioning del dispositivo con un controlador Matter.
7. Verificar el descubrimiento de endpoints y el reporting de temperatura,
   humedad y presión.

La prueba con un controlador Matter debe confirmar el modelo de endpoints
seleccionado y sus atributos de medición correspondientes. Los valores Matter
se actualizan desde el último snapshot del sensor cada
`APP_MATTER_UPDATE_INTERVAL_MS` milisegundos, independientemente del intervalo
de muestreo de 3 segundos del BME680. El reporting formal y la compatibilidad
con controladores todavía no se consideran completos.
