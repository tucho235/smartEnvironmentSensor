# Wi-Fi

El firmware incluye una capa no bloqueante de estación Wi-Fi con provisioning BLE.

Las credenciales Wi-Fi no se compilan dentro del firmware. Se envían mediante
BLE cuando el dispositivo no tiene credenciales Wi-Fi almacenadas en NVS.

## Primer provisioning

Flashear el firmware y mantener abierto el monitor serie:

```bash
cd firmware
idf.py -p /dev/cu.usbmodem112301 flash monitor
```

En el primer arranque, o después de borrar las credenciales Wi-Fi, una build sin
Matter activo en tiempo de ejecución inicia el provisioning BLE.

Cuando el servicio Matter está activo en tiempo de ejecución, la configuración
Wi-Fi inicial la gestiona Matter Network Commissioning. Utilizar la aplicación de
un controlador Matter, por ejemplo SmartThings, para enviar la red Wi-Fi durante
el commissioning Matter. El flujo de provisioning BLE del proyecto queda como
alternativa para builds o configuraciones de tiempo de ejecución donde Matter
está desactivado.

En el flujo Matter, la capa Wi-Fi local inicia el modo estación, pero no inicia
conexiones ni reconexiones. Mantiene disponible la pila de red de ESP-IDF y los
observadores de eventos mientras ESP-Matter controla la asociación con el punto
de acceso.

Para el flujo de provisioning BLE, el registro esperado es:

```text
BLE Wi-Fi provisioning started
BLE provisioning device name: SMENV_...
```

Utilizar la aplicación de provisioning BLE de Espressif o un cliente compatible.
Buscar el nombre del dispositivo BLE mostrado en el monitor serie y enviar desde
la aplicación el SSID y la contraseña Wi-Fi local.

Cuando la telemetría MQTT está incluida en la build, el firmware también expone
endpoints de configuración MQTT durante el provisioning BLE:

```text
mqtt-config
custom-data
```

Consultar [`docs/mqtt.md`](mqtt.md) para ver el payload JSON.

Registro esperado tras un provisioning exitoso:

```text
Received Wi-Fi credentials over BLE
Wi-Fi provisioning successful
Wi-Fi connected, IP=...
```

Después de conectar Wi-Fi, el firmware inicia el portal local de configuración:

```text
Configuration portal started on http://<device-ip>/
```

Abrir la IP mostrada en un navegador para configurar MQTT y los ajustes Matter
de tiempo de ejecución sin recompilar el firmware.

ESP-IDF Wi-Fi almacena las credenciales en NVS. Los siguientes arranques las
reutilizan automáticamente.

## Re-provisioning

Todavía no hay un botón físico de reprovisioning conectado. Por ahora, borrar la
flash para eliminar las credenciales guardadas y volver al provisioning BLE:

```bash
cd firmware
idf.py -p /dev/cu.usbmodem112301 erase-flash
idf.py -p /dev/cu.usbmodem112301 flash monitor
```

El trabajo futuro de hardware deberá definir un GPIO seguro para un botón u otra
condición explícita que permita borrar las credenciales sin reflashear.

## Proof of possession opcional

El provisioning BLE utiliza protocomm security 1 de ESP-IDF. Durante el
desarrollo inicial, el proof of possession está vacío por defecto.

Para establecer un proof of possession local:

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
BLE provisioning proof of possession
```

Este valor se escribe en el archivo local `firmware/sdkconfig`, ignorado por
Git. No agregar proofs of possession, SSID ni contraseñas a
`sdkconfig.defaults`.

## Comportamiento en tiempo de ejecución

Si existen credenciales, el firmware inicia el modo estación Wi-Fi normal. En el
flujo Matter, ESP-Matter controla las conexiones y reconexiones; en otro caso,
la capa Wi-Fi local se reconecta cinco segundos después de una desconexión.

Si Wi-Fi se desconecta, el firmware programa intentos de reconexión sin bloquear
el muestreo del sensor.

Si no existen credenciales, inicia el provisioning BLE y la tarea del sensor
continúa ejecutándose mientras el dispositivo espera la configuración Wi-Fi.
