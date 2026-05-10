#include "controller/RmtOutputAdapter.h"

#include <string.h>

namespace iheaterlink {

namespace {
// Аппаратный формат rmt_item32_t (32-битное слово) в ESP32:
//   duration0 : 15  | level0 : 1  | duration1 : 15  | level1 : 1   = 32 бита
// На каждое полуслово отводится РОВНО 15 бит счётчика длительности —
// это диктует чип, изменить нельзя.
//
// Откуда 32767: максимум беззнакового 15-битного числа = 2^15 − 1 = 32767.
// При clk_div=80 один тик RMT = 1 мкс, значит потолок длительности
// одного полуслова ≈ 32.767 мс. Всё, что длиннее (например, SYNC LOW 120 мс),
// режется на несколько items.
constexpr uint32_t RMT_FIELD_MAX_US = 32767;
} // namespace // namespace

// Запускает RMT-драйвер и FreeRTOS-таск периодической отправки фреймов на STM32.
void RmtOutputAdapter::begin() {
  lastPulseCode_ = encode(lastCommand_);

  if (config_.outputPin == 0xFF) {
    return;
  }

  rmt_config_t cfg = {};
  cfg.rmt_mode = RMT_MODE_TX;
  cfg.channel = config_.channel;
  cfg.gpio_num = static_cast<gpio_num_t>(config_.outputPin);
  cfg.clk_div = 80; // 80 МГц / 80 = 1 МГц, 1 тик = 1 мкс
  cfg.mem_block_num = config_.memBlockNum;
  cfg.tx_config.loop_en = false;
  cfg.tx_config.carrier_en = false;
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

  if (rmt_config(&cfg) != ESP_OK) {
    return;
  }
  if (rmt_driver_install(config_.channel, 0, 0) != ESP_OK) {
    return;
  }

  rmt_set_tx_thr_intr_en(config_.channel, false, 0);

  initialized_ = true;
  rebuildFrame(lastPulseCode_);

  // Выделенный FreeRTOS-таск: период RMT не зависит от блокировок main loop
  // (WiFi/MQTT/HA могут подвешивать loopTask на секунды — это убивало тайминг).
  xTaskCreate(&RmtOutputAdapter::rmtTaskEntry, "rmt_tx", 3072, this,
              tskIDLE_PRIORITY + 2, &rmtTask_);
}

// Сохраняет команду и пересчитывает пульс-код для следующего фрейма.
void RmtOutputAdapter::apply(const ControllerOutputCommand &command) {
  lastCommand_ = command;
  lastPulseCode_ = encode(command);
}

ControllerOutputCommand RmtOutputAdapter::getLastCommand() const {
  return lastCommand_;
}

// Кодирует команду в число импульсов: offCode для Off, иначе температура квантуется в min–max.
uint8_t RmtOutputAdapter::encode(const ControllerOutputCommand &command) const {
  if (command.mode == ControllerOutputMode::Off) {
    return config_.offCode;
  }

  int target = static_cast<int>(lroundf(command.targetTempC));
  if (target < config_.minTempC) {
    target = config_.minTempC;
  }
  if (target > config_.maxTempC) {
    target = config_.maxTempC;
  }

  const int base = config_.minTempC;
  const int step = config_.stepTempC > 0 ? config_.stepTempC : 5;
  const int quantized = base + (((target - base) + (step / 2)) / step) * step;
  return static_cast<uint8_t>(quantized);
}

// Собирает RMT-фрейм: SYNC LOW + N импульсов (pulseCode) + маркер конца.
//
// Один rmt_item32_t — это два полуслова подряд:
//   { level0, duration0,   level1, duration1 }
// Каждое поле длительности — 15 бит (≤ 32767 мкс при тике 1 мкс).
//
// Структура кадра, передаваемого STM32:
//   [SYNC LOW ~120 мс] → [HIGH 2 мс | LOW 2 мс] × pulseCode → [end marker]
void RmtOutputAdapter::rebuildFrame(uint8_t pulseCode) {
  size_t idx = 0;

  // 1. SYNC LOW.
  // syncLowUs (по умолчанию 120 000 мкс) не помещается в одно 15-битное поле,
  // поэтому набираем несколько items, в каждом ОБА полуслова с level=0.
  // На каждой итерации забираем максимум 2×RMT_FIELD_MAX_US из остатка.
  uint32_t remaining = config_.syncLowUs;
  while (remaining > 0 && idx < MAX_ITEMS - 1) {
    const uint32_t d0 =
        remaining > RMT_FIELD_MAX_US ? RMT_FIELD_MAX_US : remaining;
    remaining -= d0;
    const uint32_t d1 =
        remaining > RMT_FIELD_MAX_US ? RMT_FIELD_MAX_US : remaining;
    remaining -= d1;

    items_[idx].level0 = 0;
    items_[idx].duration0 = d0;
    items_[idx].level1 = 0;
    items_[idx].duration1 = d1;
    ++idx;
  }

  // 2. Счётные импульсы: один импульс = один item.
  // Полуслово 0 — HIGH длительностью pulseHighUs,
  // полуслово 1 — LOW  длительностью pulseLowUs.
  // STM32 считает нарастающие фронты после SYNC; их количество = pulseCode.
  for (uint8_t i = 0; i < pulseCode && idx < MAX_ITEMS - 1; ++i) {
    items_[idx].level0 = 1;
    items_[idx].duration0 = config_.pulseHighUs;
    items_[idx].level1 = 0;
    items_[idx].duration1 = config_.pulseLowUs;
    ++idx;
  }

  // 3. End marker — item с val=0 (обе длительности = 0).
  // Сигнал драйверу RMT «передача завершена»; линия уходит в idle (LOW).
  items_[idx].val = 0;
  ++idx;

  itemCount_ = idx;
  builtForCode_ = pulseCode;
}

// Будит FreeRTOS-таск и отправляет фрейм немедленно, не дожидаясь таймера.
void RmtOutputAdapter::forceFrame() {
  if (!isEnabled() || !rmtTask_)
    return;
  xTaskNotifyGive(rmtTask_);
}

// Точка входа FreeRTOS-таска (static) — делегирует в rmtTaskLoop.
void RmtOutputAdapter::rmtTaskEntry(void *arg) {
  static_cast<RmtOutputAdapter *>(arg)->rmtTaskLoop();
}

// Основной цикл таска: отправлять фрейм каждые framePeriodMs или по
// уведомлению.
void RmtOutputAdapter::rmtTaskLoop() {
  const TickType_t period = pdMS_TO_TICKS(config_.framePeriodMs);
  for (;;) {
    sendFrame();
    ulTaskNotifyTake(pdTRUE, period);
  }
}

// Перестроить фрейм если код изменился, затем отправить через RMT.
void RmtOutputAdapter::sendFrame() {
  if (builtForCode_ != lastPulseCode_) {
    rebuildFrame(lastPulseCode_);
  }

  const uint32_t now = millis();
  const uint32_t dt = lastSentMs_ ? (now - lastSentMs_) : 0;
  lastSentMs_ = now;
  ++seqNum_;
  // Serial.printf("[RMT] #%lu t=%lu dt=%lu ms code=%u items=%u\n",
  //               (unsigned long)seqNum_,
  //               (unsigned long)now, (unsigned long)dt,
  //               (unsigned)lastPulseCode_, (unsigned)itemCount_);

  rmt_write_items(config_.channel, items_, static_cast<int>(itemCount_), false);
}

} // namespace iheaterlink
