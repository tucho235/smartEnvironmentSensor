#include "matter_device.h"

#include "esp_log.h"
#include "matter_config.h"
#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_MATTER
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <inttypes.h>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "memory_diagnostics.h"
#include "sensor_service.h"

#include <app/clusters/pressure-measurement-server/PressureMeasurementCluster.h>
#include <app/clusters/relative-humidity-measurement-server/RelativeHumidityMeasurementCluster.h>
#include <app/clusters/temperature-measurement-server/TemperatureMeasurementCluster.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <data_model/esp_matter_data_model.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <esp_matter.h>
#include <setup_payload/OnboardingCodesUtil.h>
#endif

namespace {
constexpr const char *TAG = "matter";

#if CONFIG_APP_ENABLE_MATTER

int16_t clamp_i16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(value);
}

int16_t temperature_c_to_matter(float temperature_c)
{
    return clamp_i16(static_cast<int32_t>(std::lround(temperature_c * 100.0f)));
}

#if !CONFIG_APP_MATTER_TEMPERATURE_ONLY && !CONFIG_APP_MATTER_THERMOSTAT_ONLY
uint16_t clamp_u16(int32_t value)
{
    if (value > UINT16_MAX) {
        return UINT16_MAX;
    }
    if (value < 0) {
        return 0;
    }
    return static_cast<uint16_t>(value);
}

uint16_t humidity_percent_to_matter(float humidity_percent)
{
    return clamp_u16(static_cast<int32_t>(std::lround(humidity_percent * 100.0f)));
}

int16_t pressure_hpa_to_matter(float pressure_hpa)
{
    return clamp_i16(static_cast<int32_t>(std::lround(pressure_hpa)));
}
#endif

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

uint16_t s_temperature_endpoint_id = 0;
#if !CONFIG_APP_MATTER_TEMPERATURE_ONLY && !CONFIG_APP_MATTER_THERMOSTAT_ONLY
uint16_t s_humidity_endpoint_id = 0;
uint16_t s_pressure_endpoint_id = 0;
#endif
#if CONFIG_APP_MATTER_ENABLE_POWER_SOURCE
uint16_t s_power_source_endpoint_id = 0;
#endif
TaskHandle_t s_update_task_handle = nullptr;
std::atomic<bool> s_commissioning_active{false};

void matter_event_cb(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        s_commissioning_active = false;
        ESP_LOGI(TAG, "Matter commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        s_commissioning_active = true;
        ESP_LOGI(TAG, "Matter commissioning session started");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        s_commissioning_active = false;
        ESP_LOGI(TAG, "Matter commissioning session stopped");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        s_commissioning_active = true;
        ESP_LOGI(TAG, "Matter commissioning window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Matter commissioning window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Matter fabric removed");
        auto &commissioning_manager = chip::Server::GetInstance().GetCommissioningWindowManager();
        constexpr auto timeout = chip::System::Clock::Seconds16(300);
        if (!commissioning_manager.IsCommissioningWindowOpen()) {
            CHIP_ERROR err = commissioning_manager.OpenBasicCommissioningWindow(
                timeout, chip::CommissioningWindowAdvertisement::kDnssdOnly);
            if (err != CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "Failed to reopen Matter commissioning window");
            }
        }
        break;
    }
    default:
        break;
    }
}

esp_err_t matter_identification_cb(identification::callback_type_t type,
                                   uint16_t endpoint_id,
                                   uint8_t effect_id,
                                   uint8_t effect_variant,
                                   void *)
{
    ESP_LOGI(TAG,
             "Matter identify callback: type=%u endpoint=%u effect=%u variant=%u",
             type,
             endpoint_id,
             effect_id,
             effect_variant);
    return ESP_OK;
}

esp_err_t matter_attribute_update_cb(attribute::callback_type_t, uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *, void *)
{
    return ESP_OK;
}

esp_err_t update_temperature_measurement(uint16_t endpoint_id, int16_t centi_celsius)
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);

    auto *cluster_iface = esp_matter::data_model::provider::get_instance().registry().Get(
        chip::app::ConcreteClusterPath(endpoint_id, TemperatureMeasurement::Id));
    if (cluster_iface == nullptr) {
        ESP_LOGW(TAG, "TemperatureMeasurement cluster not found: endpoint=%u", endpoint_id);
        return ESP_ERR_NOT_FOUND;
    }

    auto *temperature_cluster =
        static_cast<chip::app::Clusters::TemperatureMeasurementCluster *>(cluster_iface);
    CHIP_ERROR err =
        temperature_cluster->SetMeasuredValue(chip::app::DataModel::Nullable<int16_t>(centi_celsius));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG,
                 "TemperatureMeasurement update failed: endpoint=%u value=%d err=%" CHIP_ERROR_FORMAT,
                 endpoint_id,
                 centi_celsius,
                 err.Format());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TemperatureMeasurement set: endpoint=%u value=%d", endpoint_id, centi_celsius);
    return ESP_OK;
}

template <typename ClusterClass, typename ValueType>
esp_err_t update_code_driven_measurement(uint16_t endpoint_id,
                                         chip::ClusterId cluster_id,
                                         const char *name,
                                         chip::app::DataModel::Nullable<ValueType> value)
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);

    auto *cluster_iface = esp_matter::data_model::provider::get_instance().registry().Get(
        chip::app::ConcreteClusterPath(endpoint_id, cluster_id));
    if (cluster_iface == nullptr) {
        ESP_LOGW(TAG, "%s cluster not found: endpoint=%u", name, endpoint_id);
        return ESP_ERR_NOT_FOUND;
    }

    CHIP_ERROR err = static_cast<ClusterClass *>(cluster_iface)->SetMeasuredValue(value);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG,
                 "%s update failed: endpoint=%u err=%" CHIP_ERROR_FORMAT,
                 name,
                 endpoint_id,
                 err.Format());
        return ESP_FAIL;
    }

    return ESP_OK;
}

#if CONFIG_APP_MATTER_THERMOSTAT_ONLY || !CONFIG_APP_MATTER_TEMPERATURE_ONLY
esp_err_t update_measurement(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t value)
{
    esp_err_t err = attribute::update(endpoint_id, cluster_id, attribute_id, &value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Matter attribute update failed: endpoint=%u cluster=0x%08" PRIx32 " attribute=0x%08" PRIx32 " err=%s",
                 endpoint_id,
                 cluster_id,
                 attribute_id,
                 esp_err_to_name(err));
    }
    return err;
}
#endif

void matter_update_task(void *)
{
    uint32_t last_sequence = 0;
    uint32_t updates_since_diagnostics = 0;
    memory_diagnostics_log(TAG, "Matter update task started");

    while (true) {
        SensorSnapshot snapshot = {};
        esp_err_t err = sensor_service_get_latest(snapshot);
        if (err == ESP_OK && snapshot.sequence != last_sequence) {
            last_sequence = snapshot.sequence;

#if CONFIG_APP_MATTER_THERMOSTAT_ONLY
            update_measurement(s_temperature_endpoint_id,
                               Thermostat::Id,
                               Thermostat::Attributes::LocalTemperature::Id,
                               esp_matter_nullable_int16(nullable<int16_t>(temperature_c_to_matter(snapshot.sample.temperature_c))));
#else
            int16_t temp_val = temperature_c_to_matter(snapshot.sample.temperature_c);
            update_temperature_measurement(s_temperature_endpoint_id, temp_val);

#if !CONFIG_APP_MATTER_TEMPERATURE_ONLY
            update_code_driven_measurement<chip::app::Clusters::RelativeHumidityMeasurementCluster>(
                s_humidity_endpoint_id,
                RelativeHumidityMeasurement::Id,
                "RelativeHumidityMeasurement",
                chip::app::DataModel::Nullable<uint16_t>(humidity_percent_to_matter(snapshot.sample.humidity_percent)));

            update_code_driven_measurement<chip::app::Clusters::PressureMeasurementCluster>(
                s_pressure_endpoint_id,
                PressureMeasurement::Id,
                "PressureMeasurement",
                chip::app::DataModel::Nullable<int16_t>(pressure_hpa_to_matter(snapshot.sample.pressure_hpa)));
#endif
#endif

            updates_since_diagnostics++;
            if (updates_since_diagnostics >= 12) {
                updates_since_diagnostics = 0;
                memory_diagnostics_log(TAG, "Matter update task periodic");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_MATTER_UPDATE_INTERVAL_MS));
    }
}

esp_err_t create_sensor_endpoints(node_t *node)
{
    SensorSnapshot snapshot = {};
    const bool has_snapshot = sensor_service_get_latest(snapshot) == ESP_OK;

#if CONFIG_APP_MATTER_THERMOSTAT_ONLY
    endpoint_t *thermostat_endpoint = esp_matter::endpoint::create(node, ENDPOINT_FLAG_NONE, nullptr);
    if (thermostat_endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter thermostat endpoint");
        return ESP_FAIL;
    }

    esp_matter::cluster::descriptor::config_t descriptor_config;
    cluster_t *descriptor_cluster =
        esp_matter::cluster::descriptor::create(thermostat_endpoint, &descriptor_config, CLUSTER_FLAG_SERVER);
    if (descriptor_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter thermostat descriptor cluster");
        return ESP_FAIL;
    }

    constexpr uint32_t thermostat_device_type_id = 0x0301;
    constexpr uint8_t thermostat_device_type_version = 1;
    esp_err_t err = add_device_type(thermostat_endpoint, thermostat_device_type_id, thermostat_device_type_version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add Matter thermostat device type");
        return ESP_FAIL;
    }

    esp_matter::cluster::identify::config_t identify_config;
    cluster_t *identify_cluster =
        esp_matter::cluster::identify::create(thermostat_endpoint, &identify_config, CLUSTER_FLAG_SERVER);
    if (identify_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter thermostat identify cluster");
        return ESP_FAIL;
    }

    esp_matter::endpoint::thermostat::config_t thermostat_config;
    thermostat_config.thermostat.control_sequence_of_operation =
        static_cast<uint8_t>(Thermostat::ControlSequenceOfOperationEnum::kCoolingAndHeating);
    thermostat_config.thermostat.system_mode =
        static_cast<uint8_t>(Thermostat::SystemModeEnum::kOff);
    thermostat_config.thermostat.feature_flags =
        esp_matter::cluster::thermostat::feature::heating::get_id() |
        esp_matter::cluster::thermostat::feature::cooling::get_id();
    if (has_snapshot) {
        thermostat_config.thermostat.local_temperature =
            nullable<int16_t>(temperature_c_to_matter(snapshot.sample.temperature_c));
    }

    cluster_t *thermostat_cluster =
        esp_matter::cluster::thermostat::create(thermostat_endpoint,
                                                &thermostat_config.thermostat,
                                                CLUSTER_FLAG_SERVER);
    if (thermostat_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter thermostat cluster");
        return ESP_FAIL;
    }

    esp_matter::cluster::thermostat_user_interface_configuration::config_t ui_config;
    cluster_t *ui_cluster =
        esp_matter::cluster::thermostat_user_interface_configuration::create(thermostat_endpoint,
                                                                             &ui_config,
                                                                             CLUSTER_FLAG_SERVER);
    if (ui_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter thermostat UI cluster");
        return ESP_FAIL;
    }

    s_temperature_endpoint_id = endpoint::get_id(thermostat_endpoint);

    ESP_LOGI(TAG,
             "Matter thermostat endpoint created: endpoint=%u device_type=0x0301 version=1",
             s_temperature_endpoint_id);
    return ESP_OK;
#else
    temperature_sensor::config_t temperature_config;
    temperature_config.temperature_measurement.min_measured_value = nullable<int16_t>(-4000);
    temperature_config.temperature_measurement.max_measured_value = nullable<int16_t>(8500);
    if (has_snapshot) {
        temperature_config.temperature_measurement.measured_value =
            nullable<int16_t>(temperature_c_to_matter(snapshot.sample.temperature_c));
    }

#if CONFIG_APP_MATTER_TEMPERATURE_ONLY
    endpoint_t *temperature_endpoint = esp_matter::endpoint::create(node, ENDPOINT_FLAG_NONE, nullptr);
    if (temperature_endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter temperature endpoint");
        return ESP_FAIL;
    }

    esp_matter::cluster::descriptor::config_t descriptor_config;
    cluster_t *descriptor_cluster =
        esp_matter::cluster::descriptor::create(temperature_endpoint, &descriptor_config, CLUSTER_FLAG_SERVER);
    if (descriptor_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter temperature descriptor cluster");
        return ESP_FAIL;
    }

    constexpr uint32_t temperature_sensor_device_type_id = 0x0302;
    constexpr uint8_t temperature_sensor_device_type_version = 1;
    esp_err_t err = add_device_type(temperature_endpoint,
                                    temperature_sensor_device_type_id,
                                    temperature_sensor_device_type_version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add Matter temperature sensor device type");
        return ESP_FAIL;
    }

    esp_matter::cluster::identify::config_t identify_config;
    cluster_t *identify_cluster =
        esp_matter::cluster::identify::create(temperature_endpoint, &identify_config, CLUSTER_FLAG_SERVER);
    if (identify_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter temperature identify cluster");
        return ESP_FAIL;
    }

    cluster_t *temperature_cluster =
        esp_matter::cluster::temperature_measurement::create(temperature_endpoint,
                                                             &temperature_config.temperature_measurement,
                                                             CLUSTER_FLAG_SERVER);
    if (temperature_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter temperature measurement cluster");
        return ESP_FAIL;
    }

    s_temperature_endpoint_id = endpoint::get_id(temperature_endpoint);

    ESP_LOGI(TAG,
             "Matter temperature endpoint created: endpoint=%u device_type=0x0302 version=1",
             s_temperature_endpoint_id);
    return ESP_OK;
#else
    humidity_sensor::config_t humidity_config;
    humidity_config.relative_humidity_measurement.min_measured_value = nullable<uint16_t>(0);
    humidity_config.relative_humidity_measurement.max_measured_value = nullable<uint16_t>(10000);
    if (has_snapshot) {
        humidity_config.relative_humidity_measurement.measured_value =
            nullable<uint16_t>(humidity_percent_to_matter(snapshot.sample.humidity_percent));
    }

    pressure_sensor::config_t pressure_config;
    pressure_config.pressure_measurement.min_measured_value = nullable<int16_t>(300);
    pressure_config.pressure_measurement.max_measured_value = nullable<int16_t>(1100);
    if (has_snapshot) {
        pressure_config.pressure_measurement.measured_value =
            nullable<int16_t>(pressure_hpa_to_matter(snapshot.sample.pressure_hpa));
    }

#if CONFIG_APP_MATTER_SEPARATE_SENSOR_ENDPOINTS
    endpoint_t *temperature_endpoint = temperature_sensor::create(node, &temperature_config, ENDPOINT_FLAG_NONE, nullptr);
    if (temperature_endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter temperature endpoint");
        return ESP_FAIL;
    }
    s_temperature_endpoint_id = endpoint::get_id(temperature_endpoint);

    endpoint_t *humidity_endpoint = humidity_sensor::create(node, &humidity_config, ENDPOINT_FLAG_NONE, nullptr);
    if (humidity_endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter humidity endpoint");
        return ESP_FAIL;
    }
    s_humidity_endpoint_id = endpoint::get_id(humidity_endpoint);

    endpoint_t *pressure_endpoint = pressure_sensor::create(node, &pressure_config, ENDPOINT_FLAG_NONE, nullptr);
    if (pressure_endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter pressure endpoint");
        return ESP_FAIL;
    }
    s_pressure_endpoint_id = endpoint::get_id(pressure_endpoint);

    ESP_LOGI(TAG,
             "Matter sensor endpoints created: temperature=%u humidity=%u pressure=%u",
             s_temperature_endpoint_id,
             s_humidity_endpoint_id,
             s_pressure_endpoint_id);

    return ESP_OK;
#else
    endpoint_t *environment_endpoint = temperature_sensor::create(node, &temperature_config, ENDPOINT_FLAG_NONE, nullptr);
    if (environment_endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter environment endpoint");
        return ESP_FAIL;
    }
    s_temperature_endpoint_id = endpoint::get_id(environment_endpoint);

    esp_err_t err = add_device_type(environment_endpoint,
                                    humidity_sensor::get_device_type_id(),
                                    humidity_sensor::get_device_type_version());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add Matter humidity device type");
        return ESP_FAIL;
    }
    cluster_t *humidity_cluster =
        cluster::relative_humidity_measurement::create(environment_endpoint,
                                                       &humidity_config.relative_humidity_measurement,
                                                       CLUSTER_FLAG_SERVER);
    if (humidity_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter humidity cluster");
        return ESP_FAIL;
    }
    s_humidity_endpoint_id = s_temperature_endpoint_id;

    err = add_device_type(environment_endpoint,
                          pressure_sensor::get_device_type_id(),
                          pressure_sensor::get_device_type_version());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add Matter pressure device type");
        return ESP_FAIL;
    }
    cluster_t *pressure_cluster = cluster::pressure_measurement::create(environment_endpoint,
                                                                        &pressure_config.pressure_measurement,
                                                                        CLUSTER_FLAG_SERVER);
    if (pressure_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter pressure cluster");
        return ESP_FAIL;
    }
    s_pressure_endpoint_id = s_temperature_endpoint_id;

    ESP_LOGI(TAG,
             "Matter environment endpoint created: endpoint=%u temperature=%u humidity=%u pressure=%u",
             s_temperature_endpoint_id,
             s_temperature_endpoint_id,
             s_humidity_endpoint_id,
             s_pressure_endpoint_id);

    return ESP_OK;
#endif
#endif
#endif
}

esp_err_t create_power_source_endpoint(node_t *node)
{
#if CONFIG_APP_MATTER_ENABLE_POWER_SOURCE
    esp_matter::endpoint::power_source::config_t power_source_config;
    power_source_config.power_source.status =
        static_cast<uint8_t>(PowerSource::PowerSourceStatusEnum::kActive);
    power_source_config.power_source.order = 0;
    std::snprintf(power_source_config.power_source.description,
                  sizeof(power_source_config.power_source.description),
                  "USB power");
    power_source_config.power_source.feature_flags =
        esp_matter::cluster::power_source::feature::battery::get_id();
    power_source_config.power_source.features.battery.bat_charge_level =
        static_cast<uint8_t>(PowerSource::BatChargeLevelEnum::kOk);
    power_source_config.power_source.features.battery.bat_replacement_needed = false;
    power_source_config.power_source.features.battery.bat_replaceability =
        static_cast<uint8_t>(PowerSource::BatReplaceabilityEnum::kNotReplaceable);

    endpoint_t *power_source_endpoint =
        esp_matter::endpoint::power_source::create(node, &power_source_config, ENDPOINT_FLAG_NONE, nullptr);
    if (power_source_endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter power source endpoint");
        return ESP_FAIL;
    }
    s_power_source_endpoint_id = endpoint::get_id(power_source_endpoint);

    cluster_t *power_source_cluster = esp_matter::cluster::get(power_source_endpoint, PowerSource::Id);
    if (power_source_cluster == nullptr) {
        ESP_LOGE(TAG, "Failed to resolve Matter power source cluster");
        return ESP_FAIL;
    }

    esp_matter::cluster::power_source::attribute::create_bat_present(power_source_cluster, true);
    esp_matter::cluster::power_source::attribute::create_bat_percent_remaining(power_source_cluster,
                                                                               nullable<uint8_t>(200),
                                                                               nullable<uint8_t>(0),
                                                                               nullable<uint8_t>(200));

    ESP_LOGI(TAG, "Matter power source endpoint created: endpoint=%u battery=100%%", s_power_source_endpoint_id);
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Matter power source endpoint disabled by build configuration");
    return ESP_OK;
#endif
}

#endif // CONFIG_APP_ENABLE_MATTER
} // namespace

esp_err_t matter_device_start()
{
#if CONFIG_APP_ENABLE_MATTER
    MatterConfig config = {};
    esp_err_t err = matter_config_load(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load Matter configuration: %s", esp_err_to_name(err));
        return err;
    }
    if (!config.enabled) {
        ESP_LOGI(TAG, "Matter integration disabled by runtime configuration");
        return ESP_ERR_NOT_SUPPORTED;
    }

    node::config_t node_config;
    node_t *node = node::create(&node_config, matter_attribute_update_cb, matter_identification_cb);
    if (node == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    err = create_sensor_endpoints(node);
    if (err != ESP_OK) {
        return err;
    }

    err = create_power_source_endpoint(node);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_matter::start(matter_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    BaseType_t created = xTaskCreate(matter_update_task,
                                     "matter_update",
                                     3072,
                                     nullptr,
                                     4,
                                     &s_update_task_handle);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Matter update task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Matter device started");
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Matter integration disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool matter_device_is_commissioning_active()
{
#if CONFIG_APP_ENABLE_MATTER
    return s_commissioning_active.load();
#else
    return false;
#endif
}
