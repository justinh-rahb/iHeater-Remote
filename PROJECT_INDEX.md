# iHeater Link — Project Index

Индексный файл для навигации агентов. Отражает актуальное состояние проекта на 2026-03-25.

---

## Что это за проект

**iHeater Link** — прошивка для ESP32-C3, которая:

1. Принимает состояние печати от принтера Bambu Lab по локальному MQTT (TLS, port 8883).
2. Определяет тип материала → выбирает целевую температуру камеры из menu.
3. Передаёт команду на контроллер iHeater (STM32F042) через pulse protocol по одному GPIO.
4. Публикует телеметрию и конфиг в портал iDryer через облачный MQTT.

---

## Полный flow устройства

```
[Browser] --(Improv WiFi / WebSerial)--> [ESP32] -- WiFi setup + claiming
[Portal]  --(claim PIN via WebSerial)-->  [ESP32] -- device registered on portal
[Bambu printer] --(local MQTT TLS)-->     [ESP32 BambuSourceAdapter]
                                               |
                                          [IHeaterLinkProfile] -- material -> temp
                                               |
                                          [PulseOutputAdapter] -- GPIO pulse -> STM32
                                               |
                                          [Portal publisher]  -- MQTT -> iDryer portal
```

### Claiming (web installer)

Браузер открывает **install.idryer.org** (web installer), подключается к ESP32 через WebSerial:

1. Браузер отправляет Improv WiFi credentials → ESP32 подключается к WiFi.
2. Браузер отправляет `START_CLAIM` → ESP32 запрашивает PIN у backend.
3. Backend отвечает PIN → ESP32 выводит `CLAIM_PIN:<pin>:<expires>` в Serial → браузер читает.
4. Браузер передаёт PIN на портал → устройство привязывается.
5. После привязки устройство появляется на портале как обычное MQTT-устройство.

Ключевые места claiming в коде:
- `src/main.cpp:152` — `handleWebSerialCommand` / `START_CLAIM`
- `src/main.cpp:132` — `onWebClaimPin` → `CLAIM_PIN:...` в Serial
- `src/IdryerDevice.h:56` — `requestClaimProcess()`

---

## Структура src/

```
src/
  main.cpp                  -- точка входа, Improv WiFi, claiming, loop
  IdryerDevice.h/.cpp       -- фасад: UART + cloud + Bambu + pulse output
  WsServer.h/.cpp           -- локальный WebSocket мост
  version.h                 -- версия прошивки
  external/
    IExternalSourceAdapter.h    -- интерфейс внешнего источника
    ExternalSourceState.h       -- состояние: isPrinting, material, temperature
    BambuSourceAdapter.h/.cpp   -- MQTT TLS клиент к принтеру Bambu
  controller/
    IControllerOutputAdapter.h  -- интерфейс выхода на контроллер
    ControllerOutputCommand.h   -- команда: enabled + targetTempC
    PulseOutputAdapter.h/.cpp   -- pulse protocol encoder (Link-side, DONE)
    AnalogTriggerOutputAdapter.h/.cpp -- устаревший analog вариант (не используется)
  iheater/
    IHeaterLinkProfile.h/.cpp   -- material -> temp resolver + decision maker
```

---

## Pulse Protocol (Link -> STM32)

Спецификация: `IHEATER_LINK_PULSE_PROTOCOL.md`

| Параметр | Значение |
|---|---|
| Физический канал | один GPIO ESP32 -> TH2/PB1 на STM32 |
| Idle | LOW |
| SYNC LOW | 120 ms |
| PULSE HIGH | 2 ms |
| PULSE LOW | 2 ms |
| Frame period | 1000 ms |

Коды (количество импульсов после SYNC):
- `10` = OFF
- `45/50/55/60/65/70/75/80` = целевая температура в °C

**ВАЖНО**: `IHEATER_TRIGGER_OUTPUT_PIN` сейчас = `0xFF` (не назначен).
Hardware mapping GPIO ESP32 -> TH2 ещё не зафиксирован.

---

## Статус реализации

### DONE (ESP32/Link side)

- [x] `BambuSourceAdapter` — подключение к Bambu local MQTT (TLS), парсинг print report, AMS, material
- [x] `IHeaterLinkProfile` — маппинг материала на температуру из `MaterialTemperatureSettings`
- [x] `PulseOutputAdapter` — encoder pulse protocol, state machine (SyncLow/PulseHigh/PulseLow/Idle)
- [x] Menu для iHeater Link (`menu/menu_v2.yaml`) — Portal, Claim, Bambu, Auto heat, материалы
- [x] Claiming flow (Improv WiFi + WebSerial START_CLAIM/CLAIM_PIN)
- [x] Fail-safe: при stale Bambu state (>15s без данных) → команда OFF
- [x] HA-gating: без `ha_enabled=true` автозапуск не выполняется

### TODO (ESP32/Link side)

- [ ] **`IHEATER_TRIGGER_OUTPUT_PIN`** — добавить `-DIHEATER_TRIGGER_OUTPUT_PIN=<N>` в `build_flags` в `platformio.ini` (сейчас fallback = `0xFF`, адаптер отключен)
- [ ] **Portal telemetry publish** — в telemetry публиковать `humidity: 0`; backend требует это поле, без него payload будет отклонён
- [ ] **Portal state publish** — `status`, `telemetry`, `config` в правильном формате
- [ ] `PortalStateAdapter` или аналог — слой публикации iHeater-совместимого статуса

### TODO (STM32 iHeater side)

- [ ] **Pulse decoder на STM32** — читать TH2/PB1 как GPIO/EXTI, считать импульсы, декодировать команду
- [ ] Режим `iHeater Link` — при получении pulse frame: OFF или target temp
- [ ] Watchdog: если валидные кадры не приходят >= 3s → перейти в OFF

Ключевые точки STM32 для правок:
- `docs/iHeater_stm32f042/Core/Inc/main.h:78` — пин PB1/TH2
- `docs/iHeater_stm32f042/Core/Src/main.c:802` — ADC setup (заменить на GPIO/EXTI)
- `docs/iHeater_stm32f042/Core/Src/main.c:526` — trigger input path
- `docs/iHeater_stm32f042/Core/Src/main.c:555` — trigger state machine

### TODO (Portal)

- [ ] Backend endpoint `POST /devices/{id}/configure-bambu` (payload: mode, host, accessCode, serial)
- [ ] Frontend: заменить HA-block на Bambu settings UI
- [ ] Проверить что device settings page работает с новым menu iHeater Link
- [ ] Подробнее: `PORTAL_TASKS.md`

---

## Ключевые файлы планирования

| Файл | Содержание |
|---|---|
| `IHEATER_LINK_IMPLEMENTATION_PLAN.md` | Полный план + текущий статус всех компонентов |
| `IHEATER_LINK_PULSE_PROTOCOL.md` | Спецификация pulse protocol |
| `PORTAL_TASKS.md` | Задачи для исполнителя портала |
| `_private_AGENTS.md` | Coding style, naming, workflow |
| `_private_LIBRARY_ARCHITECTURE.md` | Архитектура idryer-protocol библиотеки |

---

## Зависимости (lib_deps)

- `knolleary/PubSubClient` — MQTT (и для портала, и для Bambu)
- `bblanchon/ArduinoJson` — JSON парсинг Bambu MQTT payloads
- `densaugeo/base64` — для MQTT auth
- `Improv-WiFi-Library` — WiFi provisioning через браузер
- `links2004/WebSockets` — локальный WS мост
- `lib/idryer-protocol` — submodule: UART, облачный MQTT, cloud state machine
- `lib/idryer-menu` — submodule: menu YAML -> C++ generated headers

---

## Окружения сборки (platformio.ini)

| Env | Назначение |
|---|---|
| `esp32c3-prod` | ESP32-C3 DevKit, production сервер |
| `esp32c3-super-mini-prod` | Super Mini форм-фактор, production |
| `xiao-esp32s3-prod` | XIAO ESP32-S3, production |
| `waveshare-esp32s3-zero-prod` | Waveshare Zero, production |
| `esp32-c3-stage` | Stage сервер |
| `esp32c3-super-mini-stage` | Super Mini, stage |

---

## Следующий конкретный шаг (MVP unblock)

**Приоритет 1**: зафиксировать `IHEATER_TRIGGER_OUTPUT_PIN` в `platformio.ini` (`build_flags -DIHEATER_TRIGGER_OUTPUT_PIN=X`).

**Приоритет 2**: реализовать STM32 pulse decoder на TH2/PB1 (GPIO/EXTI) — без этого ESP32 side бесполезен.

**Приоритет 3**: добавить `humidity: 0` в telemetry publish — без этого портал не принимает телеметрию.
