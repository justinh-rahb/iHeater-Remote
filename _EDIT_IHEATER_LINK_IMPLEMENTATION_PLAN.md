# iHeater Link Implementation Plan

## Статус документа

Этот файл теперь совмещает:

- исходный план;
- текущий статус реализации;
- пометки по устаревшим решениям.

Легенда:

- ✅ сделано
- 🟡 сделано частично
- ⏳ не сделано
- ~~зачёркнуто~~ = устаревшая версия решения

## Текущий статус проекта

- ✅ отдельное рабочее меню `iHeater Link` переведено на [menu](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/menu)
- ✅ `menu_v2.yaml` переделан под отдельное устройство `iHeater Link`, а не под сушилку
- ✅ добавлен `BambuSourceAdapter` с `local MQTT` подключением к принтеру
- ✅ добавлен `iHeater Link` profile/resolver для выбора температуры по материалу
- ✅ вместо analog-концепции зафиксирован и реализован `pulse protocol` `Link -> STM32`
- 🟡 Link-side `ControllerOutputAdapter` сделан, но pin hardware mapping ещё не зафиксирован
- ✅ menu temperatures теперь реально участвуют в control logic, а не живут отдельной копией
- ✅ HA gating сведён к одному правилу: без `ha_enabled=true` автозапуск не выполняется
- ✅ добавлен fail-safe на устаревшее `Bambu` состояние: при stale source нагрев выключается
- 🟡 публикация в портал сохраняется в общей архитектуре, но специальная `iHeater` telemetry/state логика ещё не доведена до конца
- ⏳ STM32 decoder под pulse protocol ещё не реализован
- ⏳ портал пока не менялся, только подготовлен план
- ⏳ `humidity: 0` в совместимой telemetry ещё не доведён до финального publish flow

## Цель

Реализовать режим `iHeater Link` в рамках текущей архитектуры `iDryer Link`, чтобы:

1. получать состояние печати и параметры задания от внешнего источника;
2. преобразовывать их в команду для контроллера `iHeater`;
3. публиковать в портал уже совместимые `info / telemetry / status / config / config/delta`;
4. использовать отдельное локальное меню Link под сценарий термокамеры.

Документ фиксирует рабочий MVP и дальнейшее развитие.

Базовый целевой сценарий этого документа:

- источник данных = принтеры `Bambu Lab`;
- основной способ интеграции = `local MQTT / Developer Mode`;
- cloud-вариант рассматривается только как вторичный этап.

## Главные решения

### 1. Портал не ломаем

Для MVP `iHeater Link` должен остаться совместимым с текущим MQTT-конвейером портала.

Это значит:

- Link публикует в те же топики `idryer/{serial}/...`;
- меню продолжает отдаваться через `config` и `config/delta`;
- статус и телеметрия должны быть в существующем формате;
- портал на первом этапе получает только минимальные доработки.

Основание в коде:

- MQTT backend слушает фиксированные device-topics: [mqtt-client.service.ts](/Users/ruslanpavlucenko/Projects/iDryerPortal/backend/src/mqtt-telemetry/mqtt-client.service.ts#L81)
- меню устройства принимается как generic config pipeline: [config.handler.ts](/Users/ruslanpavlucenko/Projects/iDryerPortal/backend/src/mqtt-telemetry/handlers/config.handler.ts#L115)
- UI меню строится динамически: [DeviceSettings.tsx](/Users/ruslanpavlucenko/Projects/iDryerPortal/frontend/src/pages/DeviceSettings.tsx#L61), [DynamicMenu.tsx](/Users/ruslanpavlucenko/Projects/iDryerPortal/frontend/src/components/DynamicMenu.tsx#L121)

### 2. Меню делаем отдельным внутри репозитория Link `✅`

Для `iHeater Link` используется отдельное рабочее меню внутри этого репозитория.

Рабочий каталог меню для `iHeater Link`:

- [menu](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/menu)

Правило:

- все дальнейшие правки menu для `iHeater Link` вносить только в [menu](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/menu);
- генерация menu-файлов и дальнейший workflow должны быть привязаны только к [menu](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/menu).

Решение:

- использовать отдельный локальный menu package внутри этого репозитория;
- переключить `iHeater Link` на собственный источник menu-файлов;
- сохранить существующий генератор и YAML workflow, но для отдельного набора меню.

Причина:

- у `iHeater Link` другое назначение;
- меню будет небольшим и отдельным;
- рабочий menu-source должен быть однозначным и локальным для этого проекта.

Текущий статус:

- ✅ меню уже вынесено в отдельный `iHeater Link` YAML
- ✅ текущий `menu_v2.yaml` больше не является меню сушилки
- 🟡 в menu runtime пока остаётся технический хвост `units_count / language`, потому что общий shared parser ожидает их последними пунктами
- ⏳ перенос полного menu pipeline в `lib/idryer-menu` сознательно отложен как отдельная структурная задача

### 3. Протокол Link -> контроллер делаем минимальным `🟡`

Для MVP используем существующий вход `TH2 / TRIGGER` на контроллере `iHeater rev1.1`:

- `PB1`
- `ADC_IN9` / тот же физический вход, но теперь рассматривается как цифровой канал для pulse protocol на стороне STM32
- уже участвует в trigger логике

Основание:

- пин и вход: [main.h](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater_stm32f042/Core/Inc/main.h#L78)
- ADC channel setup: [main.c](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater_stm32f042/Core/Src/main.c#L802)
- trigger path: [main.c](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater_stm32f042/Core/Src/main.c#L524), [main.c](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater_stm32f042/Core/Src/main.c#L555)

### 4. HA-архитектуру переиспользуем под Bambu `🟡`

В текущем Link уже есть рабочий шаблон второго MQTT-клиента и подписок:

- MQTT-клиент HA: [ha_mqtt_client.cpp](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/lib/idryer-protocol/src/mqtt/ha_mqtt_client.cpp)
- подписки на внешние команды: [ha_publisher.cpp](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/lib/idryer-protocol/src/cloud/ha_publisher.cpp#L518)

Решение:

- не писать внешний-source слой с нуля;
- использовать ту же схему как основу для адаптера `BambuSourceAdapter`.

Текущий статус:

- ✅ `BambuSourceAdapter` уже добавлен в `src/external`
- 🟡 это не прямое переиспользование HA-класса, а новый adapter в `src/`, построенный по тому же паттерну второго MQTT-клиента
- ✅ для MVP явно зафиксирован только `local_mqtt` путь; остальные mode пока не поддерживаются runtime

## MVP архитектура

```text
Bambu printer
    ->
Bambu local MQTT / Developer Mode
    ->
iHeater Link
    - ExternalSourceAdapter
    - Material/Profile Resolver
    - ControllerOutputAdapter
    - Portal Publisher
    ->
iHeater controller
    ->
heater/fan control
```

### Поток данных

1. `Bambu` принтер публикует статус печати, материал и сопутствующие данные в локальный MQTT.
2. Link определяет:
   - печать активна или нет;
   - тип материала;
   - требуемую температуру камеры;
   - доп. признаки вроде старта/остановки/преднагрева.
3. Link на основе своего menu выбирает целевую температуру для материала.
4. Link кодирует эту температуру в минимальный сигнал для контроллера.
5. Контроллер включает или выключает нагрев по этому сигналу.
6. Link публикует в портал собственные `status/telemetry/config`.

На текущем этапе под "внешним источником" в документе везде подразумевается именно `Bambu printer`.

## Минимальный протокол Link -> controller

### Выбранный вариант для MVP

~~Не использовать UART, SWD или отдельный цифровой интерфейс.~~

~~Использовать:~~

- ~~один существующий вход `TH2`;~~
- ~~аналоговый уровень от ESP32;~~
- ~~простой RC-фильтр на выходе ESP32 PWM;~~
- ~~минимальную интерпретацию на STM32.~~

Новая версия решения:

- ✅ использовать один существующий вход `TH2`
- ✅ Link передаёт команду пачкой цифровых импульсов
- ✅ на STM32 вход должен быть переведён из ADC-логики в `GPIO/EXTI` режим декодирования импульсов
- ✅ отдельная спецификация вынесена в [IHEATER_LINK_PULSE_PROTOCOL.md](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/IHEATER_LINK_PULSE_PROTOCOL.md)

### Что именно кодируем

На MVP Link передаёт не "температуру стола", а целевую температуру камеры.

~~Предлагаемый смысл analog-значений:~~

- ~~`0..20` -> нагрев выключен~~
- ~~`30..85` -> целевая температура камеры в градусах~~

Актуальная версия:

- ✅ кодируется количество импульсов в кадре
- ✅ `10` = `OFF`
- ✅ `45/50/55/60/65/70/75/80` = целевая температура
- ✅ Link-side encoder уже реализован
- ⏳ STM32-side decoder ещё не сделан

### Почему это лучше фиксированного trigger mode

Текущий `iHeater` умеет trigger only через пороги и фиксированный `TRIGGER_MODE`, этого недостаточно для меню вида:

- ABS = 60
- PLA = 45
- PETG = 50
- ASA = 65

Если оставить старую trigger-модель без изменений, menu температур в Link не сможет реально управлять температурой камеры.

Поэтому нужен минимальный firmware change на STM32:

- ~~читать `TH2` как внешний temperature command в режиме `iHeater Link`;~~
- ~~а не только как thermistor-trigger.~~

Актуальная версия:

- ⏳ читать `TH2` как цифровой pulse input в режиме `iHeater Link`
- ⏳ декодировать pulse-frame и превращать его в `OFF` или `target temp`

## Изменения по проектам

## A. iHeater Link

### Новые подсистемы

Нужно добавить:

- ✅ `ExternalSourceAdapter`
- ✅ `BambuSourceAdapter`
- ✅ `ControllerOutputAdapter`
- ⏳ `PortalStateAdapter` или аналогичный слой публикации iHeater-совместимого статуса

### Предлагаемая структура

```text
src/
  external/
    IExternalSourceAdapter.h
    BambuSourceAdapter.h/.cpp
    ExternalSourceState.h
  controller/
    IControllerOutputAdapter.h
    PulseOutputAdapter.h/.cpp
  iheater/
    IHeaterLinkProfile.h/.cpp
```

Статус:

- ✅ `src/external/*` добавлено
- ✅ `src/controller/PulseOutputAdapter.*` добавлен
- ✅ `src/iheater/IHeaterLinkProfile.*` добавлен
- ⏳ `IHeaterMenuModel` отдельно не выделялся, пока не нужен

Идея:

- внешний источник ничего не знает о портале;
- выход на контроллер ничего не знает о Bambu;
- профиль `iHeater Link` связывает их и формирует device state.

### Menu для Link `✅`

Актуальная минимальная версия menu:

- ✅ `Portal`
- ✅ `Claim`
- ✅ `Bambu`
- ✅ `Auto heat`
- ✅ `PLA`
- ✅ `PETG`
- ✅ `ABS`
- ✅ `ASA`
- ✅ `PC`
- ✅ `PA / Nylon`
- ✅ `Other material`

Что пока не сделано:

- ⏳ fallback policy как отдельный menu пункт
- ⏳ ручной override
- ⏳ тестовый режим выхода на контроллер
- ⏳ отображение последнего распознанного материала
- ⏳ отображение последней принятой target temperature

Опционально позже:

- профиль для TPU
- общая коррекция `+/-`
- задержка старта нагрева
- пост-охлаждение после окончания печати

### Поведение Link `🟡`

Link должен уметь:

- ✅ определять материал из внешнего источника;
- ✅ если материал неизвестен, брать дефолт;
- ✅ если печать активна, передавать target temp;
- ✅ если печать завершена или idle, передавать off;
- ⏳ публиковать в портал расширенное `iHeater` состояние пока не делали

### Интеграция с порталом `⏳`

Для портала Link должен выглядеть как normal MQTT device.

Минимально нужно публиковать:

- `info`
- `telemetry`
- `status`
- `config`
- `config/delta`
- `events`

Замечание:

backend сейчас требует `humidity` в telemetry, даже если устройству она не нужна: [telemetry.handler.ts](/Users/ruslanpavlucenko/Projects/iDryerPortal/backend/src/mqtt-telemetry/handlers/telemetry.handler.ts#L41).

На MVP есть 2 варианта:

- публиковать фиктивную `humidity`
- или отдельно доработать backend, чтобы `humidity` была optional для `iHeater Link`

Для MVP проще сначала публиковать совместимое значение.

Актуальная договорённость:

- ✅ backend пока не меняем
- ⏳ в telemetry публиковать `humidity: 0`
- ⏳ UI портала пока не трогаем, только держим задачи в плане

## B. Контроллер iHeater

### Что оставить как есть

Не трогать:

- PID и основной heating loop
- air/heater thermistors
- силовую часть
- fan logic

### Что изменить минимально `⏳`

~~Добавить режим внешнего входа на базе уже существующего `TH2` как analog command input.~~

Новая версия:

- ⏳ добавить режим внешнего pulse input на базе того же `TH2`

~~Предлагаемое analog-поведение:~~

- ~~если включён режим `external_input_mode`, канал `TH2` не интерпретируется как термистор печатного стола;~~
- ~~вместо этого он трактуется как command temperature;~~
- ~~если значение `<= 20`, контроллер идёт в stop;~~
- ~~если значение `>= 30`, контроллер включает нагрев до этой температуры.~~

Актуальная версия:

- ⏳ если включён режим `iHeater Link`, канал `TH2` не интерпретируется как thermistor/legacy trigger
- ⏳ вместо этого STM32 ждёт sync + pulse frame
- ⏳ по завершении кадра обновляет `OFF` или целевую температуру

Технически правки локализуются вокруг:

- чтения `temperatures[2]`
- trigger path
- выбора `air_target`

Ключевые точки:

- ADC и фильтрация: [main.c](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater_stm32f042/Core/Src/main.c#L328)
- текущий trigger input: [main.c](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater_stm32f042/Core/Src/main.c#L526)
- trigger state machine: [main.c](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater_stm32f042/Core/Src/main.c#L555)

### Что не делать на MVP

Не делать:

- UART между Link и STM32
- ~~цифровой pulse/frequency protocol~~
- отдельную новую плату
- новый разъём
- переработку пользовательского UI контроллера

Поправка:

- ✅ цифровой pulse protocol уже выбран как основной MVP вариант
- ⏳ frequency protocol по-прежнему не делаем

## C. Портал `⏳`

Этот блок нужен отдельно для исполнителя по порталу.

### Что нужно для MVP

1. ⏳ Принять `iHeater Link` как ещё одно MQTT-совместимое устройство без новой архитектуры.
2. ⏳ Убедиться, что экран настроек работает с новым menu.
3. ⏳ Добавить минимальное брендирование и нейминг в UI.

Текущая договорённость:

- ✅ отдельный документ для портала подготовлен: [PORTAL_TASKS.md](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/PORTAL_TASKS.md)
- ⏳ сами правки в портал пока не делаем

### Что желательно сделать

1. Проверить, что frontend helper'ы для `units/lang` используют актуальный generated contract меню `iHeater Link`: [DeviceSettings.tsx](/Users/ruslanpavlucenko/Projects/iDryerPortal/frontend/src/pages/DeviceSettings.tsx#L38), [menu_meta.h](/Users/ruslanpavlucenko/Projects/iDryerProject/docs/iHeater-link/lib/idryer-menu/src/menu_meta.h#L849)
2. Сделать `humidity` optional для некоторых device profiles.
3. Добавить отображение:
   - внешний источник;
   - текущий материал;
   - requested chamber temp;
   - состояние связи с принтером.

### Что можно отложить

- отдельную продуктовую модель устройства в БД;
- отдельный раздел аналитики;
- кэш полной config-модели в backend;
- специальный экран для material presets.

## Этапы

## Этап 1. Архитектурный MVP `🟡`

Сделать:

- ✅ отдельное menu для `iHeater Link`
- ✅ `BambuSourceAdapter` на базе второго MQTT-клиента
- ~~`AnalogTriggerOutputAdapter`~~
- ✅ `PulseOutputAdapter`
- ⏳ публикацию статусов в портал
- ⏳ минимальную правку STM32 под pulse protocol on TH2

Результат:

- 🟡 принтер может быть источником данных для Link
- ✅ Link видит материал
- ✅ Link выбирает target temp из menu
- ⏳ контроллер ещё не греет через новый протокол, пока нет STM32 decoder
- ⏳ портал ещё не видит финальное состояние `iHeater Link`

## Этап 2. Стабилизация `⏳`

Сделать:

- fallback policy если источник потерян;
- более аккуратную модель состояний;
- логирование событий и ошибок;
- корректную stop/recovery семантику;
- portal polish.

## Этап 3. Отложенное развитие `🟡`

Сделать позже:

- ~~отдельный цифровой протокол Link -> STM32~~
- больше источников кроме Bambu
- cloud source adapters
- более богатое material/profile menu

Поправка:

- ✅ цифровой pulse protocol уже переехал в MVP

## Отдельные примечания

### По Bambu

Первым источником берём `Bambu local MQTT`.

Cloud-интеграцию держим как отдельный, более рискованный этап.

Причина:

- локальный MQTT лучше соответствует задаче минимального протокола;
- не требует завязывать MVP на нестабильный reverse-engineered cloud flow;
- хорошо ложится на уже существующий паттерн второго MQTT-клиента в Link.

## Внешние ссылки и основания

Ниже ссылки, на которые опирается выбор `Bambu local MQTT` как основного сценария.

### Официальные материалы Bambu Lab

- Bambu Lab, `Firmware Update Introducing New Authorization Control System`, 16 января 2025  
  <https://blog.bambulab.com/firmware-update-introducing-new-authorization-control-system-2/>
- Bambu Lab, `Updates and Third-Party Integration with Bambu Connect`, 20 января 2025  
  <https://blog.bambulab.com/updates-and-third-party-integration-with-bambu-connect/>

Что важно для проекта:

- Bambu отдельно оставляет статусные данные доступными;
- для current models был заявлен `Developer Mode`;
- в `Developer Mode` оставляются открытыми `MQTT`, `live stream` и `FTP`.

### Независимое подтверждение позиции Bambu

- The Verge, `Here’s what Bambu will — and won’t — promise after its controversial 3D printer update`, 22 января 2025  
  <https://www.theverge.com/2025/1/21/24349031/bambu-3d-printer-update-authentication-filament-subscription-lock-answers>

Что важно для проекта:

- для текущих моделей Bambu публично подтвердил сохранение `Developer Mode`;
- отдельно упомянута локальная доступность `MQTT`, `livestream` и `FTP` для current models;
- для будущих моделей такой гарантии заранее не дано.

### Community / reverse-engineered материалы

- OpenBambuAPI  
  <https://github.com/Doridian/OpenBambuAPI>
- Bambu cloud community gist  
  <https://gist.github.com/syuchan1005/9aee026ff77cf9745950de188b26346c>

Назначение этих ссылок:

- использовать как справочный материал по локальному и cloud-поведению;
- не считать cloud API стабильной основой для MVP;
- не строить архитектуру этапа 1 вокруг reverse-engineered cloud flow.

### По безопасности

Если внешний источник потерян:

- Link должен переводить контроллер в `off`;
- это поведение должно быть явной настройкой menu;
- дефолт для MVP: `fail-safe = off`.

### По совместимости

`iHeater Link` не должен ломать текущий `iDryer Link`.

Решение:

- новый профиль устройства;
- отдельное menu;
- отдельный adapter stack;
- без правок существующего dryer flow сверх необходимого рефакторинга.

## Итоговая рекомендация

Делать в таком порядке:

1. отдельное menu внутри этого репозитория;
2. `BambuSourceAdapter` на базе HA MQTT pattern;
3. ~~analog output на `TH2`;~~
4. `pulse protocol` на `TH2`;
5. минимальная STM32-доработка под pulse decode;
6. совместимая публикация в портал;
7. отдельно выдать задачи исполнителю по порталу.

Актуальный статус по этим шагам:

1. ✅ сделано
2. ✅ сделано
3. ✅ analog-вариант отменён
4. ✅ Link-side pulse protocol сделан
5. ⏳ не сделано
6. ⏳ не сделано
7. ✅ сделано

Это даёт самый короткий путь к работающему устройству без лишней архитектурной перестройки.
