# Virtual Chamber (Klipper + WebSocket + ESP)

## Что это

Способ получить из G-code значение температуры камеры (`M141 / M191`)  
без обязательного наличия heater_generic chamber.

Работает в двух режимах:
- без сенсора — только target
- с сенсором — target + реальная температура

---

## Что получится

M141 S50 → target = 50 → ESP включает  
M141 S0  → target = 0  → ESP выключает  

---

# 1. Настройка Klipper

Добавить:

[include virtual_chamber.cfg]

---

## virtual_chamber.cfg

[gcode_macro VIRTUAL_CHAMBER]
variable_target: 0
variable_temperature: -1
variable_has_sensor: 0
gcode:

[gcode_macro M141]
gcode:
  {% set t = params.S|default(0)|float %}
  SET_GCODE_VARIABLE MACRO=VIRTUAL_CHAMBER VARIABLE=target VALUE={t}

[gcode_macro M191]
gcode:
  {% set t = params.S|default(0)|float %}
  SET_GCODE_VARIABLE MACRO=VIRTUAL_CHAMBER VARIABLE=target VALUE={t}

[gcode_macro CLEAR_VIRTUAL_CHAMBER]
gcode:
  SET_GCODE_VARIABLE MACRO=VIRTUAL_CHAMBER VARIABLE=target VALUE=0
  SET_GCODE_VARIABLE MACRO=VIRTUAL_CHAMBER VARIABLE=temperature VALUE=-1
  SET_GCODE_VARIABLE MACRO=VIRTUAL_CHAMBER VARIABLE=has_sensor VALUE=0

---

# 2. Опционально: сенсор

Найди в printer.cfg строку:

[temperature_sensor XXX] или [heater_generic XXX]

И подставь сюда:

printer["XXX"].temperature

---

#[delayed_gcode UPDATE_VIRTUAL_CHAMBER_TEMP]
#initial_duration: 1.0
#gcode:
#  {% set t = printer["heater_generic chamber"].temperature|float %}
#  SET_GCODE_VARIABLE MACRO=VIRTUAL_CHAMBER VARIABLE=temperature VALUE={t}
#  SET_GCODE_VARIABLE MACRO=VIRTUAL_CHAMBER VARIABLE=has_sensor VALUE=1
#  UPDATE_DELAYED_GCODE ID=UPDATE_VIRTUAL_CHAMBER_TEMP DURATION=2.0

---

# 3. Проверка

M141 S50

---

# 4. WebSocket

{
  "jsonrpc": "2.0",
  "method": "printer.objects.subscribe",
  "params": {
    "objects": {
      "gcode_macro VIRTUAL_CHAMBER": ["target","temperature","has_sensor"]
    }
  },
  "id": 1
}

---

# 5. Логика ESP

target > 0 → ON  
target == 0 → OFF
