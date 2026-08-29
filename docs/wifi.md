# Wi-Fi

The firmware includes a non-blocking Wi-Fi station layer.

Wi-Fi credentials are configured through ESP-IDF project configuration and are
stored in the local `firmware/sdkconfig` file. That file is ignored by Git and
must not be committed.

Configure credentials:

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
Wi-Fi SSID
Wi-Fi password
```

Build and flash:

```bash
idf.py build
idf.py -p /dev/cu.usbmodem112301 flash monitor
```

Expected successful connection log:

```text
Wi-Fi station started
Wi-Fi connected, IP=...
```

If no SSID is configured, Wi-Fi is skipped and the sensor task keeps running.
If Wi-Fi disconnects, the firmware schedules reconnect attempts without blocking
sensor sampling.
