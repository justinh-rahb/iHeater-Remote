# iDryer.h — финальный дизайн (WIP, удалить после реализации)

Цель — обёртка-фасад в стиле ESP-IDF / STM32 HAL над существующим SDK.
Программист прошивки заполняет структуры, не лезет под капот.

Источник правды протокола — `mqtt_contract.yaml`. Структуры `Config`,
`Telemetry`, `Status`, `Request` **генерируются** из него (4-й генератор
рядом с существующими тремя).

---

## Решения по обсуждению (2026-05-02)

| # | Вопрос | Решение |
|---|---|---|
| **Q1** | Откуда брать структуру | **Генерировать из yaml.** Флаги `Config.has*` маскируют поля. |
| **Q2** | Интеграции (HA/Bambu/Moonraker) | **Compile-time флаги в Config**: `allowHa`/`allowBambu`/`allowMoonraker`. Что выключено — не компилируется (экономия памяти). Конфигурацию интеграций задаёт пользователь через портал, прошивка только наблюдает статус. |
| **Q3** | Bambu/Moonraker Reader (нагрев на iHeater) | **Снято** — это специфика iHeater, не общий API. Решим позже. |
| **Q4** | `commands/set` через `Request`? | **НЕТ.** `Request` сужен до бизнес-действий: `Start / Stop / Find / ClearErrors`. Все настройки меню (`set`/`get_config`) обрабатывает SDK через `MenuBridge` молча, прошивке не приходит. Просто читает обновлённое значение из своей структуры. |
| **Q5** | Local-WS и MQTT — один `onRequest`? | **Да.** Source transparent для прошивки. |
| **Q6** | main.cpp ≈ 5 строк? | **Да.** Всё (WiFi/Improv/MQTT/UART/integrations/menu/local-WS) под фасадом. |
| **Q7** | UART-only kinds в API? | **Отложено.** Решим при миграции сушилки. На текущем этапе — внутренний слой. |
| **Q8** | Namespace | **`iDryer::`** — все публичные имена под этим префиксом. |

### Дополнительные решения

| Вопрос | Решение |
|---|---|
| `Telemetry` и `Status` — раздельные структуры или единая `Report`? | **Раздельные** — это диктует контракт (два разных MQTT-топика с разной частотой). |
| `Config` — в конструкторе или в `begin(cfg)`? | **В конструкторе.** Объект сразу типизирован, нельзя использовать `link` до задания конфига. Конфиг у нас всегда compile-time константа. |
| Per-unit поля — массивы `[N]` или `std::vector`? | **Массивы `[N]`** где `N` — `MAX_UNITS = 4`. Zero-overhead, без heap, контракт фиксирует `unitsCount ≤ 4`. |
| API-стиль: структура с designated init или метод с default args? | **По задаче.** Структура — для init-один-раз / много полей (5+) / часто пропускаемые поля (`Config`). Метод с default args — для ad-hoc вызовов с 3-4 полями (`raiseEvent`). НЕ применять один паттерн ко всему. |

---

## Финальный API

```cpp
#pragma once
#include <stdint.h>
#include <functional>

namespace iDryer {

// Текущий протокол поддерживает максимум 4 юнита.
inline constexpr uint8_t MAX_UNITS = 4;

// ── Перечисления (генерируются из yaml.enums) ────────────────────────
enum class DeviceType : uint8_t { Dryer, Heater, IHeaterLink };
enum class UnitMode   : uint8_t { Idle, Drying, Storage, Find, Error };
enum class EventKind  : uint8_t { Info, Warning, Error };

// ── 1. Конфигурация (транспортный уровень) ───────────────────────────
//   Объявляет ЧТО устройство умеет принимать/передавать.
//   После begin() SDK сам знает: какие топики publish-ить, какие команды
//   subscribe-ить, какие UART kind'ы валидны для этого устройства.
//   Поля has* — флаги вкл/выкл функциональности.
struct Config {
    DeviceType deviceType;
    uint8_t    unitsCount;          // 1..MAX_UNITS

    // Что устройство отдаёт (телеметрия / статус)
    bool hasAirTemp;
    bool hasAirHumidity;
    bool hasHeaterTemp;
    bool hasHeaterPower;
    bool hasFanStatus;
    bool hasScales;
    bool hasRfid;

    // Какие интеграции компилируются в прошивку (compile-time)
    bool allowHa;
    bool allowBambu;
    bool allowMoonraker;

    // Период автоотправки
    uint32_t telemetryPeriodMs;     // 1000 default
    uint32_t statusPeriodMs;        // 5000 default
};

// ── 2. Структуры исходящих данных ────────────────────────────────────
//   Прошивка пишет поля в loop(), SDK сам шлёт по таймеру из Config.

struct Telemetry {
    float    airTempC[MAX_UNITS];
    float    airHumidityPct[MAX_UNITS];
    float    heaterTempC[MAX_UNITS];
    float    heaterPower01[MAX_UNITS];   // 0..1
    bool     fanOn[MAX_UNITS];
    uint16_t weightG[MAX_UNITS];
};

struct Status {
    UnitMode mode[MAX_UNITS];
    float    targetTempC[MAX_UNITS];
    uint32_t durationS[MAX_UNITS];
    uint32_t elapsedS[MAX_UNITS];
};

// ── 3. Структура входящих команд ─────────────────────────────────────
//   Только бизнес-действия, на которые ОБЯЗАНА реагировать прошивка.
//   Настройки (commands/set, get_config) — НЕ сюда; их применяет SDK сам.
enum class RequestKind : uint8_t {
    Start,           // commands/drying — начать нагрев
    Stop,            // commands/stop   — остановить
    Find,            // commands/find   — мигни/пикни (для поиска)
    ClearErrors,     // commands/clear_errors
};

struct Request {
    RequestKind kind;
    uint8_t     unitId;             // 0..unitsCount-1
};

// ── 4. Состояние интеграций (наблюдательно) ──────────────────────────
enum class IntegrationKind  : uint8_t { Ha, Bambu, Moonraker };
enum class IntegrationState : uint8_t { Inactive, Connecting, Active, Error };

struct IntegrationStatus {
    IntegrationKind  kind;
    IntegrationState state;
};

// ── 5. Фасад ─────────────────────────────────────────────────────────
class Link {
public:
    explicit Link(const Config& cfg);
    ~Link();

    // Поднять всё внутри (WiFi, MQTT, UART, integrations, local-WS).
    bool begin();

    // Крутить в Arduino loop() — единственный обязательный тик.
    void loop();

    // Исходящее: пиши в эти структуры — SDK шлёт по таймеру из Config.
    Telemetry telemetry;
    Status    status;

    // Принудительная немедленная отправка (помимо таймера).
    void publishTelemetryNow();
    void publishStatusNow();

    // Событие — отправляется сразу, не периодика.
    // Поля по контракту: severity (kind), event_code, message, unitId.
    // Default args покрывают типичный случай "ошибка + текст".
    void raiseEvent(EventKind   kind,
                    const char* message,
                    uint16_t    code   = 0,
                    uint8_t     unitId = 0xFF);   // 0xFF = device-wide

    // Подписка на входящие команды (Start/Stop/Find/ClearErrors).
    using RequestCallback = std::function<void(const Request&)>;
    void onRequest(RequestCallback cb);

    // Опционально: статус интеграций (для UI/логов прошивки).
    using IntegrationStatusCallback = std::function<void(const IntegrationStatus&)>;
    void onIntegrationStatus(IntegrationStatusCallback cb);

    // Состояние для UI/диагностики
    bool isOnline() const;          // подключено к порталу
    const char* serial() const;     // hardware serial

private:
    struct Impl;
    Impl* impl_;
};

} // namespace iDryer
```

---

## Целевой `main.cpp` (≈ 30 строк вместо 330)

```cpp
#include <iDryer.h>

// 1. Объявить ЧТО умеет твоё устройство.
static const iDryer::Config CFG = {
    .deviceType        = iDryer::DeviceType::Dryer,
    .unitsCount        = 1,

    .hasAirTemp        = true,
    .hasAirHumidity    = true,
    .hasHeaterTemp     = true,
    .hasHeaterPower    = true,
    .hasFanStatus      = true,
    .hasScales         = false,
    .hasRfid           = true,

    .allowHa           = true,
    .allowBambu        = false,
    .allowMoonraker    = false,

    .telemetryPeriodMs = 1000,
    .statusPeriodMs    = 5000,
};

static iDryer::Link link(CFG);

void setup() {
    link.begin();

    link.onRequest([](const iDryer::Request& r) {
        switch (r.kind) {
            case iDryer::RequestKind::Start:       myStartDrying(r.unitId); break;
            case iDryer::RequestKind::Stop:        myStop(r.unitId);        break;
            case iDryer::RequestKind::Find:        myBeep();                break;
            case iDryer::RequestKind::ClearErrors: myClearFault();          break;
        }
    });
}

void loop() {
    link.loop();

    // твои датчики → структура (SDK сам отправит по таймеру)
    link.telemetry.airTempC[0]      = mySht31.readTemp();
    link.telemetry.airHumidityPct[0]= mySht31.readHum();
    link.telemetry.heaterTempC[0]   = myThermistor.readTemp();
    link.telemetry.heaterPower01[0] = myPid.output();
    link.telemetry.fanOn[0]         = myFan.isOn();

    link.status.mode[0]        = myMode;
    link.status.targetTempC[0] = myPid.setpoint();

    if (overheatDetected) {
        link.raiseEvent(iDryer::EventKind::Error, "overheat U1", 0x0042, 0);
    }
}
```

Это всё. Ни ImprovWiFi, ни MQTT setup, ни LinkIntegrationsManager wiring — всё внутри.

---

## Что генерируется из yaml (новый `gen_idryer_api_h.py`)

| Из yaml | В iDryer.h |
|---|---|
| `enums.DeviceType` | `enum class DeviceType` |
| `enums.UnitMode` | `enum class UnitMode` |
| `enums.IntegrationState` | `enum class IntegrationState` |
| `enums.EventKind` | `enum class EventKind` |
| `payloads.TelemetryPayload.fields.*` | `struct Telemetry` поля + соответствующий `bool has*` в Config |
| `payloads.StatusPayload.fields.*` | `struct Status` поля |
| `messages` с suffix=commands/{drying,stop,find,clear_errors} | `enum class RequestKind` |

Маппинг **yaml-поле ↔ Config.has-флаг**: каждое поле `Telemetry` / `Status`
получает соответствующий флаг. Если `Config.hasAirTemp = false` — поле
`telemetry.airTempC` всё равно есть в структуре (compile-time fixed layout),
но SDK его **не публикует** в MQTT (не включает в JSON payload). Это
единый универсальный способ сказать «этого датчика у меня нет».

---

## Скрытые слои (то что фасад прячет)

```
┌─────────────────────────────────────────┐
│   user firmware (main.cpp) ≈ 30 строк   │
└────────────┬────────────────────────────┘
             │ iDryer::Link (фасад)
┌────────────▼────────────────────────────┐
│  WiFi + Improv provisioning             │
│  CloudStateMachine (claim flow)         │
│  HttpApi (provision/register/check)     │
│  MqttClient (portal MQTT, time-sync)    │
│  IdryerRuntime (command routing)        │
│  MenuBridge + NVS (commands/set)        │
│  LinkIntegrationsManager + 3 клиента    │
│  LocalAccess WS server (port 81)        │
│  UART transport (если есть RP2040)      │
└─────────────────────────────────────────┘
```

Программист прошивки эти слои **никогда не вызывает напрямую**.

---

## План имплементации

1. **Codegen `gen_idryer_api_h.py`** → `_generated/iDryer.h` из yaml.
2. **`iDryer.cpp`** в `lib/idryer-core/src/` — обёртка над существующими
   классами (Impl-pimpl). Без переписывания внутренностей.
3. **Подменить `iHeater-link/src/main.cpp`** на новый API.
   Старый wire-up (s_wifi, s_credentials, s_http, ... s_runtime) удалить.
4. **Подменить продуктовые callback'и** (`iheaterlink::handleCommand`) —
   маппинг RequestKind → product handler.
5. **Удалить мёртвое**: `command_routing.cpp` сжимается до RequestKind-маппинга.
6. **Удалить этот документ** + `DEV_NOTES_FEATURE_FLAGS.md` (плановый).

---

## Открытые мелочи (не блокеры)

- **Имя файла**: `iDryer.h` или `IDryer.h`? Я за `iDryer.h` (читается как «iDryer dot h», как у Adafruit/Arduino все библиотеки в lower-camelCase).
- **`pimpl` или inline**: у `Link` приватное состояние большое (десяток объектов SDK). Pimpl скрывает их от include-юзера. Без pimpl — пользователь подтянет MQTT/UART includes транзитивно. Я за pimpl.
- **Thread-safety**: ESP32 single-thread Arduino, но если кто-то начнёт писать в `link.telemetry` из FreeRTOS task — будет race. Документировать «писать только из loop()» или класть mutex? Я за документировать (mutex дорого, race в float не критичен).

---

## Лог обсуждения

- 2026-04-30: первый набросок API.
- 2026-05-02: пользователь сформулировал виденье через transport-init / publish-struct / receive-struct, стиль ESP-IDF/HAL. Документ восстановлен из transcript.
- 2026-05-02 (позже): закрыты Q1-Q8. Q3 снят, Q7 отложен. Финальное API зафиксировано.
