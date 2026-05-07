# Bambu LAN MQTT — спецификация для iHeater

Документ описывает протокол связи iHeater-link ↔ принтер Bambu Lab по локальной сети,
на основе которого реализуется клиент в `lib/idryer-core` (фаза следующая).

Эталоном принят рабочий референс `bHeaterWebAi/components/Bambu/Bambu.c` (автор протестировал
на живом принтере). Этот документ согласован с тремя независимыми источниками — расхождения
вынесены в отдельный раздел в конце.

## Источники

| Источник | Назначение | Ссылка |
|---|---|---|
| **bHeaterWebAi** | Рабочая C-реализация на ESP-IDF (esp-mqtt + cJSON), 337 строк | локально: `Bambu.c`, `Bambu.h`, `SampleReport.json` |
| **OpenBambuAPI** | Реверс-инженерная спецификация протокола | <https://github.com/Doridian/OpenBambuAPI/blob/main/mqtt.md> |
| **ha-bambulab** | Production-парсер Home Assistant (`pybambu`) | <https://github.com/greghesp/ha-bambulab/blob/main/custom_components/bambu_lab/pybambu/models.py> |
| **iHeater-link (текущее)** | Существующий клиент в репо | `lib/idryer-protocol/src/cloud/bambu_client.cpp` (512 строк), `BAMBU_MQTT_FIELDS.md` (352 строки) |

## 1. Подключение

LAN-режим. Облачный режим (`us.mqtt.bambulab.com`) для iHeater вне scope.

| Параметр | Значение |
|---|---|
| Транспорт | `mqtts://<printer_ip>:8883` |
| TLS | обязателен, сертификат самоподписанный (insecure-режим) |
| Username | `bblp` (хардкод) |
| Password | LAN Access Code из меню принтера (8 цифр) |
| Buffer | **≥ 32 КБ** на приём (полный `push_status` с AMS — 4–8 КБ, на пиках больше) |
| QoS | 1 для subscribe и publish |

Параметры от пользователя: IP принтера, серийный номер, access code. Хранение — NVS (как сейчас в `appConfig`).

## 2. Топики

```
device/<SERIAL>/report     ← подписка  (принтер → клиент)
device/<SERIAL>/request    → публикация (клиент → принтер)
```

`<SERIAL>` — серийник принтера (например, `01P00A123456789`), берётся из конфигурации.
Формирование топиков — `Bambu.c:124–130` (`build_topics`).

## 3. Стартовая последовательность

После события `MQTT_EVENT_CONNECTED` (`Bambu.c:267–288`):

1. Подписаться на `device/<SERIAL>/report` с QoS 1.
2. Опубликовать в `device/<SERIAL>/request` запрос полного снимка состояния (`pushall`):

   ```json
   {
     "pushing": {
       "sequence_id": "0",
       "command": "pushall",
       "version": 1,
       "push_target": 1
     }
   }
   ```

   > **P1-серия** в штатном режиме шлёт только изменившиеся поля. Без `pushall` первый принятый
   > отчёт будет неполным (нет AMS, нет `tray_type`).
   >
   > Полный формат с `version`/`push_target` — рекомендация OpenBambuAPI. bHeaterWebAi
   > использует короткий вариант (см. раздел расхождений п. 1).

   > **Не отправляйте `pushall` чаще одного раза в 5 минут** — на P1P это вызывает лаги
   > (источник: OpenBambuAPI; официального подтверждения от Bambu Lab нет, но факт
   > наблюдается всеми сторонними клиентами).

## 4. Парсинг отчёта

Корневая структура входящего сообщения:

```json
{
  "print": {
    "command": "push_status",
    "gcode_state": "IDLE",
    "ams": {
      "tray_now": "255",
      "ams": [ { "id": "0", "tray": [ { "id": "0", "tray_type": "PLA" } ] } ]
    },
    "vt_tray": { "id": "254", "tray_type": "" }
  }
}
```

Все поля приходят как **строки**, даже числовые (`tray_now`, `id`, `humidity`, `temp`).
Пример: `"tray_now": "254"`, не `254`. См. `SampleReport.json:90`.

Из всего отчёта iHeater читает три поля:

### 4.1 Состояние печати

`print.gcode_state` — строка. Возможные значения (источник: ha-bambulab `const.py`):

```
IDLE | PREPARE | RUNNING | PAUSE | FINISH | FAILED | INIT | OFFLINE | SLICING | UNKNOWN
```

Сравнение в C/C++ — с **верхним** регистром, как приходит в JSON.

Реакция iHeater:

```
gcode_state == "RUNNING" || "PREPARE"  → нагрев разрешён
иначе                                   → нагрев OFF
```

### 4.2 Активный лоток (`tray_now`)

`print.ams.tray_now` — строка с числом. Полная таблица интерпретации (источник:
ha-bambulab `models.py:2775–2788`):

| Значение | Смысл | Откуда брать `tray_type` |
|---|---|---|
| `"255"` | Нет активного трея | — (материал отсутствует, нагрев OFF) |
| `"254"` | Внешняя катушка (vt_tray) | `print.vt_tray.tray_type` |
| `"128"–"135"` (`>= 80` hex 0x80–0x87) | AMS HT (8 одиночных лотков) | `ams_index = tray_now`, `tray_index = 0` → `print.ams.ams[ams_index].tray[0].tray_type` |
| `"0"–"15"` | Стандартный AMS / AMS Lite | `ams_index = tray_now >> 2`, `tray_index = tray_now & 0x3` → `print.ams.ams[ams_index].tray[tray_index].tray_type` |

Псевдокод выбора:

```c
int n = atoi(tray_now_str);
if (n == 255)              { /* нет трея */ }
else if (n == 254)         { /* vt_tray */ }
else if (n >= 0x80)        { /* AMS HT: ams[n].tray[0] */ }
else                       { /* стандарт: ams[n>>2].tray[n&3] */ }
```

**Пояснения к коду:**

- **`atoi(tray_now_str)`** — `atoi` (ascii-to-integer) превращает строку в число.
  В JSON `tray_now` приходит как строка (`"254"`), а сравнивать удобнее с числом
  (`n == 254`). При нечисловом значении `atoi` вернёт `0`.

- **`254`, `255`** — записаны в десятичной системе. Это конкретные значения,
  закреплённые протоколом Bambu: `255` = «нет трея», `254` = «внешняя катушка».

- **`0x80`** — то же число, что и `128`, но записано в шестнадцатеричной системе
  (`0x` — префикс hex). Hex используется потому, что Bambu кодирует тип AMS через
  **старший (7-й) бит** байта `tray_now`:

  ```
   бит:    7  6  5  4  3  2  1  0
          ┌──┬─────────────┬─────┐
          │HT│   ams_id    │tray │   ← раскладка байта tray_now
          └──┴─────────────┴─────┘
           ▲       ▲          ▲
           │       │          └── индекс слота внутри блока (2 бита, 0..3)
           │       └── индекс блока AMS (зарезервировано до 5 бит, реально 0..3)
           └── 0 = обычный AMS / AMS Lite
               1 = AMS HT
  ```

  Примеры:

  ```
  0  0000 0000  ← обычный AMS №0, слот 0          (десятичное 0)
  6  0000 0110  ← обычный AMS №1, слот 2          (десятичное 6 = 1×4+2)
  15 0000 1111  ← обычный AMS №3, слот 3          (десятичное 15 = 3×4+3)

  128 1000 0000 ← AMS HT, слот 0                  (0x80)
  131 1000 0011 ← AMS HT, слот 3                  (0x83)
  135 1000 0111 ← AMS HT, слот 7                  (0x87)

  254 1111 1110 ← внешняя катушка (vt_tray)       (0xFE)
  255 1111 1111 ← нет активного трея              (0xFF)
  ```

  Поэтому `n >= 0x80` — это «старший бит установлен», то есть «AMS HT» (плюс
  спец-значения `0xFE` и `0xFF`, которые мы отсекаем выше по `if`).

- **`n >> 2`** — сдвиг вправо на 2 бита. Эквивалентен делению на 4 (целочисленному).
  Для обычного AMS «выкусывает» поле `ams_id`:

  ```
  tray_now = 6 = 0000 0110
                       ▲▲
                       └┴── tray_id = 2

  6 >> 2  = 0000 0001 → 1    (это и есть ams_id)
  ```

- **`n & 0x3`** — побитовое И с маской `0x3` (двоичное `11`). Оставляет младшие 2 бита,
  обнуляя всё остальное. Для обычного AMS «выкусывает» поле `tray_id`:

  ```
  n     = 0000 0110  (= 6)
  0x3   = 0000 0011  (маска: «оставь только младшие 2 бита»)
  ─────────────────
  n&0x3 = 0000 0010  (= 2, это и есть tray_id)
  ```

  Можно было бы написать `n / 4` и `n % 4` — результат тот же. Bitwise-форма принята
  потому, что протокол изначально кладёт `ams_id` и `tray_id` в **биты одного байта**,
  и сдвиги/маски в коде повторяют структуру байта.

- **Порядок веток `if/else`** важен: сначала отсекаем спец-значения `255` и `254`,
  потом ветку AMS HT (`>= 0x80`), и только в `else` — обычный AMS. Иначе `n == 254`
  попадёт в ветку AMS HT (т. к. `254 >= 0x80`) и парсер полезет читать несуществующий
  `ams[254]`.

### 4.3 Тип материала

Поле `tray_type` (строка), известные значения: `"PLA"`, `"PETG"`, `"ABS"`, `"ASA"`, `"PC"`,
`"PA"`, `"PA-CF"`, `"PET-CF"`, `"PVA"`, `"HIPS"`, `"TPU"`, `""` (пустой трей).

Подробный код производителя — в `tray_info_idx` (`"GFA00"`, `"GFB99"` и т. д.) — для
определения температуры сушки в iHeater **не используется**, нужен только
человекочитаемый `tray_type`.

## 5. Жизненный цикл

```
BambuConnect()
  ├─ проверка appConfig.host / serial / accessCode (Bambu.c:38)
  ├─ build_topics()
  ├─ esp_mqtt_client_init(uri, "bblp", accessCode, buf=32768)
  ├─ esp_mqtt_client_register_event(...)
  └─ esp_mqtt_client_start()

MQTT_EVENT_CONNECTED      → subscribe(report) + publish(pushall)
MQTT_EVENT_DATA           → parse_report() → обновить appConfig.{status, material}
MQTT_EVENT_DISCONNECTED   → mqtt_connected = false
                            (esp-mqtt сам делает reconnect с экспоненциальным backoff)

BambuDisconnect()         → stop() + destroy() + clear flags
```

FreeRTOS-таск создаётся автоматически библиотекой `esp_mqtt_client_start()` — отдельно
писать `xTaskCreate` не нужно. Все наши колбэки (`mqtt_event_handler`, парсинг отчётов)
вызываются из этого библиотечного таска.

## 6. Связь материал → температура

В bHeaterWebAi таблица «материал → °C» хранится в NVS как linked list (`Core.c:11–36`),
редактируется через web-форму. Функция `GetTemp(material)` — линейный поиск.

В iHeater-link уже есть аналогичный механизм через `idryer-menu`
(`materialTempFromMenu(...)`, см. `BAMBU_MQTT_FIELDS.md` раздел 8). При портировании
**заново таблицу не реализовываем** — используем существующий menu-pipeline.

## 7. Зависимости

| Компонент | Версия в bHeaterWebAi | Аналог в iHeater-link |
|---|---|---|
| MQTT-клиент | `espressif/mqtt` (esp-mqtt 1.0.0) | Arduino `PubSubClient` (используется сейчас в `bambu_client.cpp`) |
| Парсер JSON | `espressif/cjson` 1.7.19 | `ArduinoJson` (используется в проекте) или cJSON через PlatformIO |
| TLS | mbedTLS (через esp-mqtt) | `WiFiClientSecure` (Arduino) |

Целевой чип в bHeaterWebAi — ESP32-C3 на ESP-IDF. У iHeater-link — ESP32 на Arduino-фреймворке.
Поэтому **прямого копирования кода не будет** — переносим логику и структуру, реализацию
адаптируем под Arduino-стек либо переходим на esp-mqtt.

---

## 8. Расхождения между источниками

Все расхождения проверены по конкретным строкам кода. Приоритет в реализации iHeater
обозначен в графе «Решение».

### 8.1 Формат запроса `pushall`

| Источник | Payload |
|---|---|
| **bHeaterWebAi** (`Bambu.c:285`) | `{"pushing": {"sequence_id": "0", "command": "pushall"}}` — без `version`/`push_target` |
| **OpenBambuAPI** (раздел `pushing.pushall`) | `{"pushing": {"sequence_id": "0", "command": "pushall", "version": 1, "push_target": 1}}` |
| **iHeater-link текущий** (`bambu_client.cpp:321–331` через `requestPushAll()`) | Полный вариант с `version`/`push_target` |

**Что думаю.** Оба варианта работают на текущих прошивках Bambu — короткий проверен
автором bHeaterWebAi на живом принтере, полный приведён в каноне OpenBambuAPI и
используется ha-bambulab. Для совместимости с будущими прошивками **выбираю полный
формат** — он явно описывает таргет и версию протокола.

### 8.2 Тип значения `tray_now` в JSON

| Источник | Тип |
|---|---|
| **SampleReport.json:90** | строка: `"tray_now": "255"` |
| **bHeaterWebAi** (`Bambu.c:193–195`) | проверка `cJSON_IsString` → `atoi` |
| **OpenBambuAPI** | строка |
| **ha-bambulab** (`models.py:2770`) | `int(...)` без явной проверки типа — Python преобразует |

**Что думаю.** Реальные принтеры присылают строку. Парсер должен:
- сначала проверить `cJSON_IsString` → `atoi`;
- если будущая прошивка пришлёт число — `cJSON_IsNumber` → `valueint`.

Защита от обоих типов — три лишних строки кода, но снимает риск регрессии.

### 8.3 Обработка `tray_now == 255`

| Источник | Поведение |
|---|---|
| **bHeaterWebAi** (`Bambu.c:247–250`) | падает в ветку `else { tray_now=...}`, `tray_type_str` остаётся `"N/A"` — корректно по факту, но **без явной семантики** |
| **OpenBambuAPI** | явно: 255 = no tray |
| **ha-bambulab** (`models.py:2775–2778`) | явно: 255 → `ams_index=255, tray_index=255`, материал не выбирается |

**Что думаю.** Явная ветка `if (n == 255) → no_material` обязательна. Иначе при
дополнении кода (например, валидации индекса AMS) можно случайно превратить «нет трея»
в «ошибка парсинга». Семантика «255 = нет активного трея» — устойчивая инвариантность
протокола.

### 8.4 AMS HT (`tray_now >= 0x80`)

| Источник | Поведение |
|---|---|
| **bHeaterWebAi** (`Bambu.c:215–246`) | **НЕ обрабатывает.** При `tray_now = 128` пойдёт в ветку `0..253` → `ams_id = 32, tray_id = 0` → out-of-bounds на массиве `ams[]` (обычно 1–4 элемента) |
| **OpenBambuAPI** | упоминает AMS HT, но без детальной формулы |
| **ha-bambulab** (`models.py:2780–2784`) | `ams_index = tray_now`, `tray_index = 0` |
| **iHeater-link `BAMBU_MQTT_FIELDS.md`** (раздел 9) | формула задокументирована, но **в коде `bambu_client.cpp` не реализована** |

**Что думаю.** Для AMS HT нужна отдельная ветка `else if (n >= 0x80)`. Без неё на новых
принтерах с AMS HT клиент будет читать материал с неправильного слота или вылетать.
В реализации iHeater **обязательно покрыть**.

### 8.5 Размер MQTT-буфера

| Источник | Буфер |
|---|---|
| **bHeaterWebAi** (`Bambu.c:66`) | `.buffer.size = 32768` (esp-mqtt) |
| **iHeater-link текущий** (`bambu_client.h:147`) | `kTlsBufferSize = 4096` (TLS буфер); PubSubClient по умолчанию `MQTT_MAX_PACKET_SIZE = 256` |

**Что думаю.** Полный `push_status` с AMS — 4–8 КБ. На X1C с AMS 2 единицы и заполненными
лотками встречал отчёты до 12 КБ. С `MQTT_MAX_PACKET_SIZE = 256` PubSubClient **молча
выбрасывает сообщение** — приёмный буфер переполняется до его обработки. Это вероятная
причина, почему текущий клиент в iHeater-link «парсит, но иногда ничего не приходит».

Минимум для надёжной работы:
- TLS буфер: ≥ 16 КБ;
- MQTT-буфер: ≥ 16 КБ (через `client.setBufferSize(16384)` для PubSubClient или
  через `.buffer.size` для esp-mqtt).

При переходе на esp-mqtt (рекомендую — см. п. 8.7) — ставить 32 КБ как в bHeaterWebAi.

### 8.6 Парсинг `tray_type` в существующем клиенте iHeater-link

`lib/idryer-protocol/src/cloud/bambu_client.cpp:431–454` — берёт первый непустой
`tray_type` из `ams[0].tray[]` без проверки `tray_now`.

**Последствия:**
- Активен AMS 1, слот 2 (`tray_now = "6"`), материал PETG → код прочитает PLA из ams[0].tray[1] (первого непустого).
- Активна внешняя катушка (`tray_now = "254"`) → код всё равно прочитает первый AMS-трей.

**Что думаю.** Это **функциональная регрессия**, уже задокументированная в
`BAMBU_MQTT_FIELDS.md` раздел 9. В новой реализации iHeater-link исправляем по
формуле из 8.4 + п. 4.2 этого документа.

### 8.7 Выбор MQTT-стека: esp-mqtt vs PubSubClient

| Аспект | esp-mqtt (bHeaterWebAi) | PubSubClient (iHeater-link сейчас) |
|---|---|---|
| Большие сообщения | поддержка из коробки (`buffer.size`) | по умолчанию 256 байт, требует `setBufferSize` |
| TLS | mbedTLS встроен | через `WiFiClientSecure` |
| Reconnect | встроен с backoff | руками (есть в `bambu_client.cpp`) |
| Event-driven | да (`mqtt_event_handler`) | поллинг через `client.loop()` |
| Зависимости PlatformIO | требует `framework = espidf` или `arduino, espidf` | работает на чистом arduino |

**Что думаю.** Если оставаться на чистом arduino-фреймворке — **PubSubClient с
`setBufferSize(16384)`** допустим (потеря по сравнению с esp-mqtt — отсутствие нативного
backoff и поллинг вместо event-driven, но всё это уже реализовано в текущем клиенте).
Если переходим на dual-framework `arduino, espidf` (проект уже использует FreeRTOS-таски,
шаг небольшой) — **esp-mqtt предпочтительнее**: меньше своего кода, надёжнее на больших
отчётах. **Решение оставляю за тобой** — это архитектурный выбор, не протокольный.

### 8.8 Сводная таблица: что менять в новой реализации

| № | Что | Источник истины | Где править / реализовывать |
|---|---|---|---|
| 1 | Полный `pushall` с `version`/`push_target` | OpenBambuAPI | в новой либе сразу правильно |
| 2 | `tray_now` как строка → `atoi` (с фолбэком на int) | SampleReport.json | в новой либе |
| 3 | Явная ветка `tray_now == 255` → no material | ha-bambulab | в новой либе |
| 4 | Ветка `tray_now >= 0x80` для AMS HT | ha-bambulab | в новой либе **обязательно** |
| 5 | Буфер ≥ 16 КБ (PubSubClient) или 32 КБ (esp-mqtt) | bHeaterWebAi + опыт | в новой либе |
| 6 | Корректный выбор активного трея через `tray_now` | bHeaterWebAi + ha-bambulab | заменяет регрессию из `bambu_client.cpp:431–454` |
| 7 | Состояния `gcode_state` — сравнение с uppercase | ha-bambulab `const.py` | в новой либе |
| 8 | Выбор MQTT-стека (esp-mqtt vs PubSubClient) | архитектурное решение | требует подтверждения автора |

---

## 9. Что НЕ переносим из bHeaterWebAi

- **GPIO-выход на нагреватель** (`main.c:42–70`, `heat_task`) — у них duty-cycle PWM на
  пине, у нас точный счётный протокол через RMT (`RmtOutputAdapter`). Логика принципиально
  другая, RMT-вариант лучше.
- **Web-форма + Improv BLE** для конфигурации — у нас конфиг приходит от портала через
  отдельный MQTT (`idryer/<SERIAL>/commands/link_integration`, см.
  `BAMBU_MQTT_FIELDS.md` раздел 10).
- **NVS-таблица материалов** — у нас уже есть `idryer-menu`.
- **Таблица соответствия `material → temp` в NVS** — заменяется на
  `materialTempFromMenu(tray_type)` из существующего pipeline.

## 10. Из bHeaterWebAi берём

- Структуру MQTT-клиента: connect → subscribe(report) → publish(pushall) → parse(report).
- Алгоритм извлечения `tray_type` из активного трея (с доработками из раздела 8).
- Размер буфера 32 КБ.
- Простую event-driven архитектуру (без своего таска).

## 11. Открытые вопросы

1. Стек MQTT: остаёмся на PubSubClient или переходим на esp-mqtt? (см. 8.7)
2. Управление подключением: сохраняем существующий `BambuClient` из `idryer-protocol`
   и патчим парсинг, или пишем заново в `idryer-core`? (формулировка задачи указывает
   на второй вариант)
3. Нужна ли совместимость с уже задеплоенными устройствами с текущим клиентом?
   (если да — значения NVS-конфига должны переноситься 1:1)

---

## Приложение A. Структура `print.ams` (из SampleReport.json)

Сокращённо, оставлены только поля, релевантные для iHeater:

```json
{
  "print": {
    "ams": {
      "tray_now": "255",
      "ams": [
        {
          "id": "0",
          "humidity": "4",
          "temp": "22.7",
          "tray": [
            { "id": "0" },
            { "id": "1", "tray_type": "PLA", "tray_info_idx": "GFA00" },
            { "id": "2", "tray_type": "PLA", "tray_info_idx": "GFA05" },
            { "id": "3", "tray_type": "PLA", "tray_info_idx": "GFL00" }
          ]
        }
      ]
    },
    "vt_tray": {
      "id": "254",
      "tray_type": ""
    },
    "gcode_state": "IDLE"
  }
}
```

Полный пример — `SampleReport.json` в исходниках bHeaterWebAi (231 строка).

## Приложение B. Карта файлов референса

| Файл | Назначение | Строки |
|---|---|---|
| `Bambu.h` | Public API: `BambuConnect/Disconnect/Publish/IsConnected` | 32 |
| `Bambu.c` | MQTT-клиент + парсер `report` | 337 |
| `SampleReport.json` | Реальный отчёт принтера в IDLE | 231 |
| `Core.c` (фрагмент) | NVS-таблица материалов, `GetTemp(material)` | — |
| `main.c` | GPIO + heat_task (НЕ переносим) | 116 |
