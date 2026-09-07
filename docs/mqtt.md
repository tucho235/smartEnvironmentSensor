# MQTT

La telemetría MQTT está implementada mediante el componente administrado
ESP-MQTT de Espressif.

La ESP32-C3 publica telemetría en un broker MQTT local, previsto para ejecutarse
en la Raspberry Pi. El ESP32 no debe conectarse directamente a InfluxDB.

MQTT es opcional en tiempo de ejecución. Si no se configura una URI de broker,
el firmware continúa muestreando el sensor seleccionado y omite el arranque de MQTT.

El firmware almacena la configuración MQTT en NVS, dentro del namespace de la
aplicación. Un flasheo normal del firmware no borra esta configuración.

Cuando MQTT está configurado, el módulo de telemetría espera hasta que Wi-Fi
obtiene una dirección IP antes de iniciar el cliente MQTT. Esto evita el fallo
de conexión inicial esperado mientras Wi-Fi todavía se asocia.

## Fuentes de configuración

La forma preferida de configurar el dispositivo en tiempo de ejecución es el
portal web local. Después de completar el provisioning Wi-Fi, abrir la IP del
dispositivo que aparece en el monitor serie:

```text
http://<device-ip>/mqtt-tab
```

La URL raíz (`http://<device-ip>/`) también abre la pestaña MQTT por compatibilidad.

El portal permite configurar:

```text
Enable MQTT service
Broker URI
Username
Password
Telemetry topic
Publish interval
```

La contraseña nunca vuelve a mostrarse en el formulario. Dejar vacío el campo
de contraseña conserva la contraseña existente. Una casilla específica permite
borrar la contraseña almacenada. Después de guardar, el dispositivo se reinicia
para que MQTT se reconecte usando la nueva configuración.

Si `Enable MQTT service` está desmarcado, el firmware conserva la configuración
MQTT almacenada, pero no crea el cliente MQTT, no se conecta al broker ni publica
telemetría.

Para migración y desarrollo local, el firmware también puede inicializar la
configuración MQTT en NVS desde `idf.py menuconfig` si NVS todavía no contiene
una URI de broker.

Configurar valores iniciales:

```bash
cd firmware
idf.py menuconfig
```

Luego abrir:

```text
Smart Environment Sensor Configuration
```

Establecer:

```text
MQTT broker URI bootstrap
MQTT username bootstrap
MQTT password bootstrap
MQTT telemetry topic
MQTT telemetry publish interval in milliseconds
```

Ejemplo de URI de broker:

```text
mqtt://192.168.3.10:1883
```

El usuario y la contraseña MQTT se escriben en el archivo local
`firmware/sdkconfig`, ignorado por Git. No agregar credenciales del broker a
`sdkconfig.defaults`.

Después del primer arranque exitoso, el firmware almacena estos valores en NVS.
Los siguientes flasheos normales pueden seguir usando la copia de NVS sin
compilar las credenciales dentro del firmware.

## Payload de provisioning BLE

Durante el primer provisioning Wi-Fi, el firmware expone dos endpoints BLE para
configurar MQTT:

```text
mqtt-config
custom-data
```

Ambos endpoints aceptan el mismo payload JSON:

```json
{
  "enabled": true,
  "broker_uri": "mqtt://192.168.3.10:1883",
  "username": "esp32",
  "password": "YOUR_PASSWORD",
  "topic": "smart-environment-sensor/indoor/state",
  "publish_interval_ms": 10000
}
```

Solo se requiere `broker_uri` cuando MQTT está habilitado y no existe una
configuración MQTT previa. Establecer `"enabled": false` mantiene MQTT
deshabilitado sin requerir una URI de broker. Los campos opcionales omitidos
conservan su valor actual o utilizan los valores predeterminados del proyecto.

El endpoint `custom-data` es compatible con la opción `--custom_data` de
`esp_prov.py` de Espressif.

La aplicación oficial Android de provisioning BLE de Espressif configura Wi-Fi,
pero no muestra campos MQTT personalizados. Para el flujo normal de
configuración desde teléfono u ordenador, utilizar el portal web.

## Topic

```text
smart-environment-sensor/bme680/state  (valor inicial BME680)
smart-environment-sensor/sht30/state   (valor inicial SHT30)
```

El topic es editable en la pestaña MQTT del portal y se persiste en NVS. Para
dos dispositivos se recomienda identificar la ubicación, por ejemplo
`smart-environment-sensor/indoor/state` y
`smart-environment-sensor/outdoor/state`.

## Payload

BME680:

```json
{
  "temperature_c": 24.32,
  "humidity_percent": 50.44,
  "pressure_hpa": 1011.62
}
```

SHT30:

```json
{
  "temperature_c": 24.32,
  "humidity_percent": 50.44
}
```

## Unidades

```text
temperature_c    degrees Celsius
humidity_percent relative humidity percent
pressure_hpa     hectopascals
```

## Comportamiento de publicación

MQTT publica el snapshot más reciente de `sensor_service`. No debe acceder
directamente al driver seleccionado ni al bus I²C. La variante SHT30 omite
`pressure_hpa`; no publica un cero ni un valor simulado.

El intervalo inicial de publicación MQTT es de 10 segundos, independiente del
intervalo de muestreo del sensor.

La reconexión MQTT es independiente de Matter. Un fallo del broker o de la
Raspberry Pi no debe detener el muestreo ni el funcionamiento de Matter.

La prueba inicial en hardware confirmó la publicación de telemetría en un
broker Mosquitto ejecutándose en la Raspberry Pi, con autenticación mediante
usuario y contraseña.

La infraestructura de observabilidad de la Raspberry Pi se mantiene en el
repositorio separado
[`smartInfrastructure`](https://github.com/tucho235/smartInfrastructure)
Utiliza Telegraf para suscribirse a MQTT, escribir los campos JSON en InfluxDB y
provisionar dashboards de Grafana.

## Prueba desde la Raspberry Pi

Desde el host de la Raspberry Pi donde se ejecuta el broker:

```bash
sudo docker exec -it mosquitto \
  mosquitto_sub -h localhost -p 1883 -u esp32 -P 'YOUR_PASSWORD' \
  -t 'smart-environment-sensor/#' -v
```

Telemetría esperada:

```text
smart-environment-sensor/bme680/state {"temperature_c":24.32,"humidity_percent":50.44,"pressure_hpa":1011.62}
```
