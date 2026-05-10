# Инструменты разработчика iDryer Link

## tools/fake_moonraker.py

Локальный WebSocket-сервер, имитирующий Moonraker (Klipper) — для проверки
Moonraker-интеграции iHeater Link без реального принтера. Шлёт лестницу
`VIRTUAL_CHAMBER.target` (70 → 65 → 60 → 55 → 50 → 55 → 60 → 65, шаг 30 с).

Подробности: [`tools/fake_moonraker.README.md`](fake_moonraker.README.md).

```bash
python3 tools/fake_moonraker.py
```

Настройка в портале: Moonraker → host=`<IP мака>`, port=`7125`, path=`/websocket`.

---

## tools/fake_bambu/

Локальный TLS-MQTT broker (mosquitto) + publisher.py, имитирующий принтер
Bambu Lab — для проверки Bambu Reader без реального принтера. Лестница
по 30 с: AMS → vt_tray → AMS-HT → FINISH с разными `tray_type`.

Подробности: [`tools/fake_bambu/README.md`](fake_bambu/README.md).

```bash
brew install mosquitto
cd tools/fake_bambu && ./gen_cert.sh
mosquitto -c mosquitto.conf      # терминал A
python3 publisher.py             # терминал B
```

Настройка в портале: Bambu → ip=`<IP мака>`, serial=`FAKE_BAMBU_001`,
lan=`12345678`.

---

## tools/mock_portal.py

Мок backend-портала для тестирования cloud-флоу без реального сервера.

**Запуск:**
```bash
pip install flask python-socketio eventlet
python3 tools/mock_portal.py
```

Сервер слушает `http://0.0.0.0:5050`.

**Настройка прошивки:**
```ini
build_flags =
  -DIDRYER_API_BASE="http://<host>:5050/api"
  -DMQTT_USE_TLS=0
```

**API:**
- `POST /api/devices/provision` — токен `mock-token-{serial}`
- `POST /api/devices/register` — PIN `12345678`
- `GET  /api/devices/check-claim/{token}` — автоматически подтверждает при первом вызове

---

## extra_scripts/copy_firmware.py

Post-build скрипт. Копирует артефакты сборки:

- `firmware.bin`, `bootloader.bin`, `partitions.bin`, `boot_app0.bin`
- локально: `firmware/<board>/`
- в flasher-portal: `/Users/ruslanpavlucenko/Projects/iDryerPortal/flasher-portal/firmware/link/<slot>/<board>/`

Slot (`prod` / `stage`) определяется по имени окружения.

---

## extra_scripts/stage_auto_claim.py

Post-upload скрипт для staging. После прошивки читает Serial, ждёт `CLAIM_PIN:<pin>:<expires>` и автоматически заявляет устройство через staging API.

Подробнее: [STAGING.md](../STAGING.md).
