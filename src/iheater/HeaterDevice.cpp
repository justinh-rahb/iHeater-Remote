/**
 * @file HeaterDevice.cpp
 * @brief Реализация фасада iHeater Link — без UART.
 */

#if defined(ESP32) || defined(ESP_PLATFORM)

#include "iheater/HeaterDevice.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <Arduino.h>
#include <WiFi.h>

#include <idryer_protocol.h>
#include <hal/hal_types.h>

#include "version.h"

using namespace DryerUart;
using namespace idryer;
using namespace idryer::hal;

namespace iheaterlink
{
    namespace
    {
        /// MAC ESP32 → "DEVICE_aabbccddeeff" (префикс + 12 lowercase hex).
        /// Формат валидирует backend /devices/provision: "DEVICE_<mac>" для ESP32 Link.
        void readEspMacSerial(char *out, size_t outSize)
        {
            if (!out || outSize < 20)
                return;
            uint8_t mac[6] = {0};
            WiFi.macAddress(mac);
            snprintf(out, outSize, "DEVICE_%02x%02x%02x%02x%02x%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    } // namespace

    // =============================================================================
    // КОНСТРУКТОР
    // =============================================================================

    HeaterDevice::HeaterDevice(idryer::IWifiManager *wifi,
                               idryer::IHttpClient *http,
                               idryer::ICredentialStore *store,
                               const char *apiBaseUrl)
        : wifi_(wifi),
          http_(http),
          store_(store),
          api_(http, apiBaseUrl ? apiBaseUrl : IDRYER_API_BASE),
          cloud_(wifi, store, &api_, &mqtt_),
          publisher_(&mqtt_),
          integrationsStore_(),
          integrations_(&mqtt_, &integrationsStore_),
          controllerOutput_(RmtOutputConfig{}),
          linkSink_(&controllerOutput_),
          cmdHandler_(&linkSink_)
    {
    }

    // =============================================================================
    // ИНИЦИАЛИЗАЦИЯ
    // =============================================================================

    void HeaterDevice::begin()
    {
        HAL_LOG_INFO("HEATER", "Initializing...");

        cloud_.setStateChangeCallback(onCloudStateChange, this);
        cloud_.setClaimPinCallback(onClaimPin, this);
        cloud_.setClaimCompleteCallback(onClaimComplete, this);
        cloud_.setUnclaimedCallback(onUnclaimed, this);

        mqtt_.setCommandCallback([this](const char *cmd, JsonObjectConst data)
                                 { handleMqttCommand(cmd, data); });

        cmdHandler_.setTimeSyncCallback([](const char *ts, void *ctx)
                                        { static_cast<HeaterDevice *>(ctx)->syncTimeFromBackend(ts); },
                                        this);

        // Маршрутизация commands/link_integration и commands/bambu_apply → менеджер.
        cmdHandler_.setLinkIntegrationsManager(&integrations_);

        controllerOutput_.begin();
        if (!controllerOutput_.isEnabled())
        {
            HAL_LOG_WARN("HEATER", "RMT output disabled: IHEATER_TRIGGER_OUTPUT_PIN is not configured");
        }

        // В качестве serialNumber для облака и HA-ClientId используем MAC ESP.
        char mac[24] = {0};
        readEspMacSerial(mac, sizeof(mac));
        if (mac[0] != '\0')
        {
            cloud_.setMcuSerial(mac);
            integrations_.setHaClientId(mac);
        }

        // Тип устройства — LinkII (специализированный LINK для iHeater).
        // Влияет на режим Bambu (Reader) внутри менеджера.
        integrations_.setDeviceType(DeviceType::LinkII);

        // Колбэки из менеджера → локальное применение на pulse output.
        integrations_.setVirtualChamberCallback(
            [this](const cloud::VirtualChamberData &data) { onVirtualChamberUpdate(data); });
        integrations_.setBambuPrinterStatusCallback(
            [this](const cloud::BambuPrinterStatus &status) { onBambuPrinterStatusUpdate(status); });

        integrationsStore_.begin();
        integrations_.begin();

        cloud_.begin();

        HAL_LOG_INFO("HEATER", "Initialized, serial=%s", cloud_.getIdentity().serialNumber);
    }

    // =============================================================================
    // ГЛАВНЫЙ ЦИКЛ
    // =============================================================================

    void HeaterDevice::loop()
    {
        cloud_.loop();
        integrations_.loop();
        controllerOutput_.loop();

        if (cloud_.isOnline())
        {
            publishInfoOnce();
            publishLinkTelemetryIfNeeded();
        }
    }

    // =============================================================================
    // CLAIM
    // =============================================================================

    bool HeaterDevice::requestClaimProcess()
    {
        return cloud_.requestClaim();
    }

    // =============================================================================
    // ОБРАБОТКА MQTT-КОМАНД
    // =============================================================================

    void HeaterDevice::handleMqttCommand(const char *command, JsonObjectConst data)
    {
        // Все команды (drying/stop/link_integration/bambu_apply/...) идут через CommandHandler.
        // CommandHandler сам маршрутизирует link_integration/bambu_apply в LinkIntegrationsManager,
        // остальное — в LinkCommandSink (локальное применение на controllerOutput_).
        cmdHandler_.handleMqttCommand(command, data);
    }

    void HeaterDevice::handleExternalCommand(const char *command, JsonObjectConst data)
    {
        handleMqttCommand(command, data);
    }

    // =============================================================================
    // КОЛБЭКИ ОТ ИНТЕГРАЦИЙ
    // =============================================================================

    void HeaterDevice::onVirtualChamberUpdate(const cloud::VirtualChamberData &data)
    {
        // target == 0 ИЛИ объект VIRTUAL_CHAMBER не виден → выключаем нагрев.
        // target > 0 → подаём setpoint на pulse output.
        ControllerOutputCommand cmd{};
        if (!data.available || data.target <= 0.0f)
        {
            cmd.mode = ControllerOutputMode::Off;
            cmd.targetTempC = 0.0f;
        }
        else
        {
            cmd.mode = ControllerOutputMode::TargetTemperature;
            cmd.targetTempC = data.target;
        }
        controllerOutput_.apply(cmd);

        HAL_LOG_INFO("HEATER",
                     "VIRTUAL_CHAMBER: available=%d target=%.1f temp=%.1f hasSensor=%d → output=%s",
                     data.available ? 1 : 0,
                     data.target,
                     data.temperature,
                     data.hasSensor ? 1 : 0,
                     cmd.mode == ControllerOutputMode::Off ? "OFF" : "ON");
    }

    void HeaterDevice::onBambuPrinterStatusUpdate(const cloud::BambuPrinterStatus &status)
    {
        // Bambu Reader (для X1C с датчиком камеры): chamberTarget = setpoint.
        // Если chamberTarget == 0 — принтер не задаёт нагрев камеры; держим OFF.
        // Не дёргаем apply если активен Moonraker — там приоритетный канал;
        // но лёгкого способа узнать кто активен здесь нет, полагаемся на
        // "одна активная интеграция" (политика Manager) — callback приходит
        // только от активной.
        ControllerOutputCommand cmd{};
        if (status.chamberTarget <= 0.0f)
        {
            cmd.mode = ControllerOutputMode::Off;
            cmd.targetTempC = 0.0f;
        }
        else
        {
            cmd.mode = ControllerOutputMode::TargetTemperature;
            cmd.targetTempC = status.chamberTarget;
        }
        controllerOutput_.apply(cmd);

        HAL_LOG_INFO("HEATER",
                     "BAMBU status: state=%s chamberTarget=%.1f chamberTemp=%.1f tray=%s → output=%s",
                     status.gcodeState,
                     status.chamberTarget,
                     status.chamberTemp,
                     status.trayType,
                     cmd.mode == ControllerOutputMode::Off ? "OFF" : "ON");
    }

    // =============================================================================
    // ПУБЛИКАЦИЯ INFO (один раз после первого online)
    // =============================================================================

    void HeaterDevice::publishInfoOnce()
    {
        if (infoPublished_)
            return;

        UnitConfig units[1] = {};
        units[0].unitId = 0;
        units[0].capabilities = 0;
        memset(units[0].scales, 0xFF, sizeof(units[0].scales));
        memset(units[0].rfid, 0xFF, sizeof(units[0].rfid));

        const char *hwVersion = "LINK-v1";
        const char *fwVersion = VERSION_STR;
        const uint32_t workTimeCounter = millis() / 1000u;

        char mcuSerial[24] = {0};
        readEspMacSerial(mcuSerial, sizeof(mcuSerial));

        const uint8_t deviceType = static_cast<uint8_t>(DeviceType::LinkII);

        if (publisher_.publishInfo(1, units, hwVersion, fwVersion,
                                   workTimeCounter, mcuSerial, deviceType))
        {
            infoPublished_ = true;
            HAL_LOG_INFO("HEATER", "Published info: hw=%s fw=%s serial=%s deviceType=%u",
                         hwVersion, fwVersion, mcuSerial, deviceType);
        }
    }

    // =============================================================================
    // ТЕЛЕМЕТРИЯ LINK (каждые 5 секунд)
    // =============================================================================

    void HeaterDevice::publishLinkTelemetryIfNeeded()
    {
        const uint32_t now = millis();
        if (now - lastTelemetryAt_ < 5000u)
            return;
        lastTelemetryAt_ = now;

        if (!mqtt_.isConnected())
            return;

        StaticJsonDocument<512> doc;
        doc["deviceType"] = "link_ii";
        doc["active"] = cloud::activeIntegrationToString(integrations_.getActive());

        // Moonraker snapshot
        const cloud::MoonrakerStatus &ms = integrations_.moonrakerStatus();
        JsonObject moon = doc.createNestedObject("moonraker");
        moon["printerState"] = ms.printerState;
        moon["progress"] = ms.progress;
        moon["chamberTarget"] = ms.chamberTarget;
        moon["chamberTemp"] = ms.chamberTemperature;
        moon["hasSensor"] = ms.chamberHasSensor;

        // Bambu snapshot
        const cloud::BambuPrinterStatus &bs = integrations_.bambuPrinterStatus();
        JsonObject bam = doc.createNestedObject("bambu");
        bam["gcodeState"] = bs.gcodeState;
        bam["chamberTarget"] = bs.chamberTarget;
        bam["chamberTemp"] = bs.chamberTemp;
        bam["tray"] = bs.trayType;

        // Выход на нагреватель (намерение ESP — STM32 ничего не возвращает).
        const ControllerOutputCommand out = controllerOutput_.getLastCommand();
        const bool heating = (out.mode == ControllerOutputMode::TargetTemperature);
        doc["outputMode"] = static_cast<int>(out.mode);
        doc["targetTempC"] = out.targetTempC;
        doc["pulseCode"] = controllerOutput_.getLastPulseCode();

        // iDryer-совместимый блок units[] — чтобы portal.telemetry.handler
        // сохранял историю и строил графики штатным путём.
        // temperature: температура камеры от активного источника (moonraker с датчиком > bambu).
        // humidity: 0 — на iHeater Link нет датчика влажности.
        // heaterPower: 0/100 как намерение ESP, реальную мощность STM32 не отдаёт.
        float chamberTemp = 0.0f;
        if (ms.chamberHasSensor && ms.chamberTemperature > 0.0f) {
            chamberTemp = ms.chamberTemperature;
        } else if (bs.chamberTemp > 0.0f) {
            chamberTemp = bs.chamberTemp;
        }
        JsonArray units = doc.createNestedArray("units");
        JsonObject u1 = units.createNestedObject();
        u1["unitId"] = "U1";
        u1["temperature"] = chamberTemp;
        u1["humidity"] = 0;
        u1["heaterPower"] = heating ? 100 : 0;
        u1["fanStatus"] = false;

        doc["rssi"] = WiFi.RSSI();
        doc["uptime"] = millis() / 1000u;

        mqtt_.publishTelemetry(doc);
    }

    // =============================================================================
    // СИНХРОНИЗАЦИЯ ВРЕМЕНИ
    // =============================================================================

    void HeaterDevice::syncTimeFromBackend(const char *timestamp)
    {
        if (!timestamp)
            return;

        struct tm tm = {0};
        char cleanTimestamp[32];
        strncpy(cleanTimestamp, timestamp, sizeof(cleanTimestamp) - 1);
        cleanTimestamp[sizeof(cleanTimestamp) - 1] = '\0';

        char *dot = strchr(cleanTimestamp, '.');
        if (dot)
        {
            char *z = strchr(dot, 'Z');
            if (z)
            {
                *dot = 'Z';
                *(dot + 1) = '\0';
            }
        }

        if (strptime(cleanTimestamp, "%Y-%m-%dT%H:%M:%SZ", &tm) != nullptr)
        {
            time_t t = mktime(&tm);
            struct timeval tv = {0};
            tv.tv_sec = t;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
            HAL_LOG_INFO("HEATER", "Time synced: %s", timestamp);
        }
        else
        {
            HAL_LOG_WARN("HEATER", "Failed to parse timestamp: %s", timestamp);
        }
    }

    // =============================================================================
    // STATIC CALLBACKS
    // =============================================================================

    void HeaterDevice::onCloudStateChange(cloud::CloudState oldState,
                                          cloud::CloudState newState,
                                          void *ctx)
    {
        auto *self = static_cast<HeaterDevice *>(ctx);

        HAL_LOG_INFO("HEATER", "Cloud: %s -> %s",
                     cloud::cloudStateToString(oldState),
                     cloud::cloudStateToString(newState));

        if (newState == cloud::CloudState::Online)
        {
            self->publisher_.resetInfoPublished();
            self->infoPublished_ = false;
            self->integrations_.publishStatus();
        }
    }

    void HeaterDevice::onClaimPin(const char *pin, uint32_t expiresInSeconds, void *ctx)
    {
        auto *self = static_cast<HeaterDevice *>(ctx);

        HAL_LOG_INFO("HEATER", "Claim PIN: %s (expires in %ds)", pin, expiresInSeconds);

        if (self->userClaimPinCallback_)
        {
            self->userClaimPinCallback_(pin, expiresInSeconds);
        }
    }

    void HeaterDevice::onClaimComplete(const char *deviceId, void *ctx)
    {
        (void)ctx;
        HAL_LOG_INFO("HEATER", "Claim complete: deviceId=%s", deviceId);
    }

    void HeaterDevice::onUnclaimed(void *ctx)
    {
        (void)ctx;
        HAL_LOG_WARN("HEATER", "Device not claimed");
    }

} // namespace iheaterlink

#endif // ESP32 || ESP_PLATFORM
