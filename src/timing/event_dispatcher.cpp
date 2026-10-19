#include "event_dispatcher.h"
#include "hal/ble/nus_service.h"
#include "timing/event_isr.h"
#include "timing/event_timestamp.h"
#include "ui/main_area.h"
#include <Arduino.h>
#include <time.h>

void sendTimedPacket(char header, const struct tm &tm) {
  char timebuf[9];
  String packet;
  snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
           tm.tm_sec);
  packet.reserve(11);
  packet += header;
  packet += timebuf;
  packet += PACKET_ENDER;
  packet += '\n';
  Serial.print(packet);
  nusService.print(packet);
}

static void sendEventPacket(const EventTimestampData &data) {
  if (!data.success)
    return;

  char packet[BleNusService::kMaxPendingPacketSize];
  int len = 0;

  if (data.module_type == ModuleType::START) {
    int32_t correction_ms = data.correction_ms;
    int32_t abs_corr = (correction_ms < 0) ? -correction_ms : correction_ms;
    if (abs_corr > (int32_t)MAX_CORRECTION_MS) {
      len = snprintf(packet, sizeof(packet), "%c%s%c\n", START_HEADER,
                     data.local_time_str, PACKET_ENDER);
    } else {
      len = snprintf(packet, sizeof(packet), "%c%s;%ld%c\n", START_HEADER,
                     data.local_time_str, (long)correction_ms, PACKET_ENDER);
    }
  } else {
    len = snprintf(packet, sizeof(packet), "%c%s%c\n", FINISH_HEADER,
                   data.local_time_str, PACKET_ENDER);
  }

  if (len <= 0)
    return;

  size_t send_len =
      (len < (int)sizeof(packet)) ? (size_t)len : (sizeof(packet) - 1);
  // Вызывается только после освобождения pending slot в dispatcher.
  if (!nusService.queuePacket(reinterpret_cast<const uint8_t *>(packet), send_len))
    return;
  Serial.write(packet, send_len);
  nusService.sendPendingPacket();
}

void event_dispatcher_handle_event_isr(ModuleType module_type,
                                       MainArea &mainArea) {
  static int64_t last_start_event_us = 0;
  int64_t t_esp_us = 0;

  // Пока пакет не принят BLE, следующий timestamp сохраняется в ISR slot.
  if (!nusService.sendPendingPacket())
    return;

  if (event_isr_get(t_esp_us)) {
    if (module_type == ModuleType::START) {
      if (last_start_event_us != 0 &&
          (t_esp_us - last_start_event_us) <
              (int64_t)START_EVENT_DELAY_US) {
        return;
      }
      last_start_event_us = t_esp_us;
    }
    EventTimestampData data = event_timestamp_process(t_esp_us, module_type);
    sendEventPacket(data);
    mainArea.displayEventTimestamp(data);
  }
}
