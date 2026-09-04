# Wi-Fi

The firmware includes a non-blocking Wi-Fi station layer with BLE provisioning.

Wi-Fi credentials are not compiled into the firmware. They are sent over BLE
when the device has no Wi-Fi credentials stored in NVS.

## First Provisioning

Flash the firmware and keep the serial monitor open:

```bash
cd firmware
idf.py -p /dev/cu.usbmodem112301 flash monitor
```

On first boot, or after Wi-Fi credentials are erased, the MQTT-only firmware
starts BLE provisioning.

When the Matter runtime service is enabled, initial Wi-Fi setup is handled by
Matter Network Commissioning instead. Use the Matter controller app, for example
SmartThings, to send the Wi-Fi network during Matter commissioning. The project
BLE provisioning flow is kept as a fallback for builds or runtime configurations
where Matter is disabled.

In the Matter runtime path, the local Wi-Fi layer starts station mode but does
not initiate connection or reconnection attempts. It keeps the ESP-IDF network
stack and event observers available while ESP-Matter controls access point
association.

Expected log:

```text
BLE Wi-Fi provisioning started
BLE provisioning device name: SMENV_...
```

Use the Espressif BLE provisioning app or a compatible provisioning client.
Search for the BLE device name printed in the serial monitor and send the local
Wi-Fi SSID/password from the app.

The firmware also exposes MQTT configuration endpoints during BLE provisioning:

```text
mqtt-config
custom-data
```

See `docs/mqtt.md` for the JSON payload.

Expected successful provisioning log:

```text
Received Wi-Fi credentials over BLE
Wi-Fi provisioning successful
Wi-Fi connected, IP=...
```

After Wi-Fi connects, the firmware starts the local configuration portal:

```text
Configuration portal started on http://<device-ip>/
```

Open the printed IP in a browser to configure MQTT and Matter runtime settings
without recompiling the firmware.

The credentials are stored by ESP-IDF Wi-Fi in NVS. Future boots reuse the saved
credentials automatically.

## Re-Provisioning

There is no physical reprovisioning button wired yet. For now, erase flash to
clear saved credentials and return to BLE provisioning:

```bash
cd firmware
idf.py -p /dev/cu.usbmodem112301 erase-flash
idf.py -p /dev/cu.usbmodem112301 flash monitor
```

Future hardware work should define a safe button GPIO or another explicit reset
condition for clearing Wi-Fi credentials without reflashing.

## Optional Proof Of Possession

BLE provisioning uses ESP-IDF protocomm security 1. During initial development
the proof of possession is empty by default.

To set a local proof of possession:

```bash
cd firmware
idf.py menuconfig
```

Then open:

```text
Smart Environment Sensor Configuration
```

Set:

```text
BLE provisioning proof of possession
```

This value is written to local `firmware/sdkconfig`, which is ignored by Git. Do
not add proofs of possession, SSIDs, or passwords to `sdkconfig.defaults`.

## Runtime Behavior

If credentials exist, the firmware starts normal Wi-Fi station mode.

If Wi-Fi disconnects, the firmware schedules reconnect attempts without blocking
sensor sampling.

If credentials do not exist, BLE provisioning starts and the sensor task keeps
running while the device waits for Wi-Fi configuration.
