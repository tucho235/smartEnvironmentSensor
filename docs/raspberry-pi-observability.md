# Raspberry Pi Observability Stack

This stack receives MQTT telemetry from the ESP32-C3, stores it in InfluxDB, and
shows it in Grafana.

```text
ESP32-C3 -> Mosquitto -> Telegraf -> InfluxDB -> Grafana
```

Mosquitto can keep running in the existing `/opt/stacks/mqtt` stack.

If Grafana and InfluxDB are already running on the Raspberry Pi, do not start
the full stack below. Use the Telegraf-only stack instead so only one new
container is added.

## Files

```text
examples/raspberry-pi/observability/
├── docker-compose.yml
├── .env.example
├── telegraf/telegraf.conf
└── grafana/
    ├── dashboards/smart-environment-sensor.json
    └── provisioning/
        ├── dashboards/smart-environment.yml
        └── datasources/influxdb.yml
```

## Existing InfluxDB And Grafana

Recommended when the Raspberry Pi already has InfluxDB and Grafana running.
This adds only Telegraf:

```text
examples/raspberry-pi/telegraf-mqtt-to-influx/
├── docker-compose.yml
├── .env.example
└── telegraf.conf
```

Create a stack directory:

```bash
sudo mkdir -p /opt/stacks/smart-env-telegraf
sudo chown -R "$USER":"$USER" /opt/stacks/smart-env-telegraf
```

Copy the example files into that directory:

```bash
cp -R examples/raspberry-pi/telegraf-mqtt-to-influx/. /opt/stacks/smart-env-telegraf/
cd /opt/stacks/smart-env-telegraf
```

Create the local environment file:

```bash
cp .env.example .env
nano .env
```

Set:

```text
INFLUX_URL
INFLUX_TOKEN
INFLUX_ORG
INFLUX_BUCKET
MQTT_SERVER
MQTT_USERNAME
MQTT_PASSWORD
MQTT_TOPIC
```

If InfluxDB and Mosquitto publish ports on the Raspberry Pi host, these defaults
usually work:

```text
INFLUX_URL=http://host.docker.internal:8086
MQTT_SERVER=tcp://host.docker.internal:1883
```

If Telegraf should join the same Docker network as the existing InfluxDB stack,
add that external network to `docker-compose.yml` and use the InfluxDB service
name in `INFLUX_URL`, for example:

```text
INFLUX_URL=http://influxdb:8086
```

Start Telegraf:

```bash
docker compose up -d
docker compose logs -f telegraf
```

Then add an InfluxDB datasource in the existing Grafana instance, or import the
dashboard JSON from:

```text
examples/raspberry-pi/observability/grafana/dashboards/smart-environment-sensor.json
```

## Install On The Raspberry Pi

Use this section only when creating a new InfluxDB + Grafana stack from scratch.

Create a stack directory:

```bash
sudo mkdir -p /opt/stacks/smart-env-observability
sudo chown -R "$USER":"$USER" /opt/stacks/smart-env-observability
```

Copy the example files into that directory:

```bash
cp -R examples/raspberry-pi/observability/. /opt/stacks/smart-env-observability/
cd /opt/stacks/smart-env-observability
```

Create the local environment file:

```bash
cp .env.example .env
nano .env
```

Replace every `CHANGE_ME` value. Keep `INFLUXDB_BUCKET=smart_environment` unless
the Grafana dashboard queries are also updated.

If Mosquitto runs on the same Raspberry Pi and exposes port `1883`, keep:

```text
MQTT_SERVER=tcp://host.docker.internal:1883
```

Set `MQTT_USERNAME` and `MQTT_PASSWORD` to the same credentials already accepted
by the Mosquitto broker. The `.env` file is intentionally ignored by Git.

Start the stack:

```bash
docker compose up -d
docker compose ps
```

Open Grafana:

```text
http://<raspberry-pi-ip>:3000
```

Log in with `GRAFANA_ADMIN_USER` and `GRAFANA_ADMIN_PASSWORD` from `.env`. The
`Smart Environment Sensor` dashboard should appear under the `Smart Environment`
folder after Grafana finishes provisioning.

## Validate MQTT Input

Confirm the broker still receives ESP32-C3 telemetry:

```bash
sudo docker exec -it mosquitto \
  mosquitto_sub -h localhost -p 1883 -u esp32 -P 'YOUR_PASSWORD' \
  -t 'smart-environment-sensor/#' -v
```

Check Telegraf logs:

```bash
docker compose logs -f telegraf
```

Telegraf should subscribe to MQTT and write to InfluxDB. If credentials or the
topic are wrong, this log is the first place to look.

## Validate InfluxDB Data

From the observability stack directory:

```bash
set -a
. ./.env
set +a

docker compose exec influxdb influx query \
  'from(bucket: "smart_environment") |> range(start: -15m) |> filter(fn: (r) => r._measurement == "environment")' \
  --org "$INFLUXDB_ORG" \
  --token "$INFLUXDB_ADMIN_TOKEN"
```

Expected fields:

```text
temperature_c
humidity_percent
pressure_hpa
```

## Operations

Restart only the observability stack:

```bash
docker compose restart
```

Stop the stack without deleting data:

```bash
docker compose down
```

Stop the stack and delete InfluxDB/Grafana persisted data:

```bash
docker compose down -v
```

Use `down -v` only when intentionally resetting the historical database and
Grafana state.
