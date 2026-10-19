# ENTime - Протокол команд

Все команды передаются в формате JSON через Serial (UART0) или BLE NUS. Ответы также в JSON.

---

## Событийные пакеты (не JSON)

При наступлении события устройство отправляет стартовый или финишный пакет
в зависимости от типа модуля. Отправка идёт во все подключённые сериалы
(UART0 и BluetoothSerial).

**Формат времени:** `HH:MM:SS,mmm`

**Стартовый пакет (START):**
```
$10:10:10,123;-3345#
```
- Время берётся из события.
- `correction` — поправка в миллисекундах, которую нужно прибавить или вычесть из
  текущего времени, чтобы получить ближайшее время с нулём секунд.
- Если `abs(correction)` превышает `MAX_CORRECTION_MS`, поправка не отправляется.

**Пример без поправки (START):**
```
$10:10:35,123#
```

**Финишный пакет (FINISH):**
```
F10:10:10,123#
```
- Время берётся из события.

**Бип пакет (BEEP):**
```
B10:10:56#
```
- Отправляется каждую минуту на 56-й секунде.
- Отправляется только стартовым модулем.

**Голосовой пакет (VOICE):**
```
V10:10:15#
```
- Отправляется каждую минуту на 15-й секунде.
- Отправляется только стартовым модулем.

---

## Список команд

- [ping](#ping---проверка-связи)
- [time](#time---получить-текущее-время)
- [status](#status---получить-статус-устройства)
- [wifi](#wifi---управление-wifi)
- [calibrate](#calibrate---калибровка-rtc)
- [touch_calibrate](#touch_calibrate---калибровка-touch)
- [sync_source](#sync_source---переключить-источник-синхронизации)
- [sync_ntp](#sync_ntp---синхронизация-rtc-по-ntp)
- [save_config](#save_config---сохранить-конфигурацию)
- [load_config](#load_config---загрузить-конфигурацию)
- [factory_reset](#factory_reset---сброс-к-заводским-настройкам)

---

## Сопоставление запросов и ответов

Для сопоставления запросов и ответов используется опциональное поле `id` (идентификатор запроса).

**Как это работает:**

1. Клиент может включить поле `id` в запрос (любое значение: число, строка и т.д.)
2. Устройство копирует это поле `id` в ответ
3. Клиент сопоставляет ответ с запросом по значению `id`

**Пример:**

```json
// Запрос 1
{"cmd": "time", "id": 1}

// Запрос 2 (отправлен сразу после первого)
{"cmd": "status", "id": 2}

// Ответ на запрос 1
{
  "cmd": "time",
  "id": 1,
  "time": 1703169600123456,
  "source": "gps",
  "accuracy_us": 50,
  "status": "ok"
}

// Ответ на запрос 2
{
  "cmd": "status",
  "id": 2,
  "gps": {
    "fix": true,
    "satellites_used": 8,
    "satellites_view": 12
  },
  "status": "ok"
}
```

**Важно:**
- Поле `id` опционально — если оно не указано в запросе, оно не будет в ответе
- Если указано `id` в запросе, оно всегда будет в ответе
- Значение `id` может быть любым (число, строка, null)
- Рекомендуется использовать последовательные числа или уникальные строки для удобства отслеживания

**Без `id`:**

Если `id` не указан, клиент должен отправлять команды последовательно и ждать ответа перед отправкой следующей команды (см. рекомендации по интеграции).

---

## Команды (Request)

### `ping` - Проверка связи

**Запрос:**
```json
{"cmd": "ping", "id": 1}
```

**Ответ:**
```json
{
  "cmd": "pong",
  "id": 1,
  "status": "ok"
}
```

**Параметры ответа:**
- `cmd` — всегда `"pong"` в ответе
- `status` — всегда `"ok"` (команда всегда успешна)
- `id` — копируется из запроса, если указан

**Примечание:** Простая команда для проверки связи с устройством. Всегда возвращает успешный ответ.

---

### `time` - Получить текущее время

**Запрос:**
```json
{"cmd": "time", "id": 1}
```

**Ответ:**
```json
{
  "cmd": "time",
  "id": 1,
  "time": 1703169600123456,
  "source": "gps",
  "accuracy_us": 50,
  "status": "ok"
}
```

**Параметры ответа:**
- `time` — текущее время в микросекундах с UNIX epoch
- `source` — источник времени: `"gps"` или `"rtc"`
- `accuracy_us` — точность в микросекундах (±)
- `status` — `"ok"`, `"warning"`, `"error"`

---

### `status` - Получить статус устройства

**Запрос:**
```json
{"cmd": "status", "id": 2}
```

**Ответ:**
```json
{
  "cmd": "status",
  "id": 2,
  "device": {
    "name": "ENTIME",
    "number": 1,
    "type": "start"
  },
  "firmware": {
    "version": "0.1.0",
    "build_date": "2024-01-01"
  },
  "system": {
    "uptime_s": 12345,
    "free_heap_bytes": 142336,
    "reset_reason": "power_on"
  },
  "wifi": {
    "state": "connected",
    "rssi": -62,
    "ip": "192.168.1.10",
    "ssid": "MyWiFi"
  },
  "ble": {
    "state": "advertising",
    "clients": 0
  },
  "rtc": {
    "ready": true,
    "lost_power": false,
    "last_sync_ms": 600000,
    "temperature_c": 24.5
  },
  "gps": {
    "state": "searching",
    "fix_age_ms": 120000,
    "nmea_age_ms": 350,
    "gsv_age_ms": 410,
    "utc_age_ms": 900,
    "pps_age_ms": 120,
    "fix": true,
    "satellites_used": 8,
    "satellites_view": 12,
    "pps_signal": true,
    "enabled": true
  },
  "sync": {
    "last_ms": 5000,
    "state": "gps_ok",
    "accuracy_us": 50,
    "source": "gps"
  },
  "touch": {
    "ready": true,
    "enabled": true,
    "calibrated": false
  },
  "storage": {
    "used_pct": 42,
    "ok": true
  },
  "power": {
    "battery_voltage": 5.00
  },
  "status": "ok"
}
```

**Параметры:**
- `device.name` — имя устройства
- `device.number` — номер устройства
- `device.type` — тип модуля: `"start"` или `"finish"`
- `firmware.version` — версия прошивки
- `firmware.build_date` — дата сборки
- `system.uptime_s` — время работы в секундах
- `system.free_heap_bytes` — свободная куча
- `system.reset_reason` — причина последней перезагрузки: `"power_on"`, `"software"`, `"panic"`, `"int_wdt"`, `"task_wdt"`, `"wdt"`, `"deep_sleep"`, `"brownout"`, `"sdio"`, `"unknown"`
- `wifi.state` — состояние WiFi: `"off"`, `"connecting"`, `"connected"`, `"error"`
- `wifi.rssi` — уровень сигнала WiFi в dBm
- `wifi.ip` — IP адрес (если подключено)
- `wifi.ssid` — SSID текущей сети (если подключено)
- `ble.state` — состояние BLE: `"off"`, `"advertising"`, `"connected"`
- `ble.clients` — количество активных BLE клиентов
- `rtc.ready` — готов ли RTC
- `rtc.lost_power` — потеря питания RTC
- `rtc.time_valid` — можно ли доверять времени RTC как источнику timestamp
- `rtc.last_sync_ms` — время с последней установки RTC (мс)
- `rtc.temperature_c` — температура датчика RTC
- `gps.state` — состояние GPS: `"off"`, `"searching"`, `"active"`
- `gps.fix_age_ms` — давность последнего фикса в мс
- `gps.nmea_age_ms` — давность последнего обработанного NMEA-предложения в мс
- `gps.gsv_age_ms` — давность последнего обработанного `$GPGSV`/`$GNGSV` в мс
- `gps.utc_age_ms` — давность последнего обновления UTC из NMEA в мс
- `gps.pps_age_ms` — давность последнего PPS фронта в мс
- `gps.fix` — есть ли свежая GPS фиксация
- `gps.satellites_used` — количество спутников, используемых fix/MicroNMEA
- `gps.satellites_view` — количество свежих спутников из GSV skyplot
- `gps.pps_signal` — получен ли PPS сигнал от GPS
- `gps.enabled` — включен ли GPS в настройках при загрузке
- `sync.last_ms` — время последней синхронизации (мс)
- `sync.state` — состояние синхронизации: `"gps_ok"`, `"gps_degraded"`, `"rtc_ok"`, `"rtc_degraded"`, `"nosync"`
- `sync.accuracy_us` — оценка точности синхронизации (мкс)
- `sync.source` — текущий источник: `"gps"` или `"rtc"`
- `touch.ready` — инициализирован ли touch HAL
- `touch.enabled` — включен ли touch во входных настройках
- `touch.calibrated` — применена ли валидная калибровка touch
- `storage.used_pct` — использование хранилища в %
- `storage.ok` — статус хранилища (true если ok)
- `power.battery_voltage` — напряжение питания (опционально)

---

### `wifi` - Управление WiFi

**Запрос (включить):**
```json
{"cmd": "wifi", "enable": true, "ssid": "MyWiFi", "passwd": "secret", "id": 12}
```

**Запрос (включить без смены сети):**
```json
{"cmd": "wifi", "enable": true, "id": 13}
```

**Запрос (отключить):**
```json
{"cmd": "wifi", "enable": false, "id": 14}
```

**Ответ:**
```json
{
  "cmd": "wifi",
  "id": 12,
  "state": "enabled",
  "status": "ok"
}
```

**Ответ (ошибка):**
```json
{
  "cmd": "wifi",
  "id": 12,
  "status": "error",
  "error_code": 205,
  "error_message": "WiFi start failed"
}
```

**Ответ (ошибка остановки):**
```json
{
  "cmd": "wifi",
  "id": 14,
  "status": "error",
  "error_code": 206,
  "error_message": "WiFi stop timeout"
}
```

**Параметры ответа:**
- `state` — `"enabled"` или `"disabled"`

---

### `calibrate` - Калибровка RTC

**Запрос:**
```json
{"cmd": "calibrate", "offset": 0.5, "id": 5}
```

**Параметры:**
- `offset` — смещение в ppm для DS3231 aging offset (шаг ~0.1 ppm, диапазон около -12.8..12.7 ppm)

**Ответ:**
```json
{
  "cmd": "calibrate",
  "id": 5,
  "previous_offset": 0.2,
  "new_offset": 0.5,
  "estimated_error_us": 10,
  "status": "ok"
}
```

**Параметры ответа:**
- `previous_offset` — предыдущее значение aging offset (ppm)
- `new_offset` — примененное значение (ppm, с учетом округления)
- `estimated_error_us` — оценка текущей ошибки синхронизации (мкс)

---

### `touch_calibrate` - Калибровка touch

Команда запускает UI-сценарий калибровки touch на дисплее, сохраняет результат в `NVS` и сразу применяет новую калибровку. Используется для повторной калибровки после первичной настройки.

**Запрос:**
```json
{"cmd": "touch_calibrate", "id": 6}
```

**Ответ (успех):**
```json
{
  "cmd": "touch_calibrate",
  "id": 6,
  "status": "ok",
  "saved_keys": 6,
  "cal_valid": true,
  "calibration": [3800, 220, 3700, 240, 7]
}
```

**Ответ (ошибка):**
```json
{
  "cmd": "touch_calibrate",
  "id": 6,
  "status": "error",
  "error_code": 202,
  "error_message": "Touch calibration failed"
}
```

**Параметры ответа:**
- `saved_keys` — количество обновленных ключей в NVS
- `cal_valid` — флаг валидной калибровки
- `calibration` — массив коэффициентов калибровки из 5 значений

---

### `sync_source` - Переключить источник синхронизации

**Запрос (на GPS):**
```json
{"cmd": "sync_source", "source": "gps"}
```

**Запрос (авто):**
```json
{"cmd": "sync_source", "source": "auto"}
```

**Запрос (на RTC):**
```json
{"cmd": "sync_source", "source": "rtc"}
```

**Ответ:**
```json
{
  "cmd": "sync_source",
  "active_source": "rtc",
  "reason": "no_gps_signal",
  "timestamp": 1703169600123456,
  "status": "ok"
}
```

**Параметры ответа:**
- `active_source` — активный источник синхронизации: `"gps"`, `"rtc"`, `"none"`
- `reason` — причина выбора источника: `"ok"`, `"no_gps_signal"`, `"auto"`
- `timestamp` — текущий UTC timestamp (мкс) или 0 если время недоступно

---

### `sync_ntp` - Синхронизация RTC по NTP

**Запрос:**
```json
{"cmd": "sync_ntp", "id": 8}
```

**Ответ (успех):**
```json
{
  "cmd": "sync_ntp",
  "id": 8,
  "status": "ok",
  "rtc_time": 1703169600,
  "ntp_servers": ["ru.pool.ntp.org", "time.google.com", "time.cloudflare.com"],
  "sync_duration_ms": 2500
}
```

**Ответ (ошибка):**
```json
{
  "cmd": "sync_ntp",
  "id": 8,
  "status": "error",
  "error_code": 204,
  "error_message": "WiFi not connected"
}
```

**Параметры ответа:**
- `status` — `"ok"`, `"warning"`, `"error"`
- `rtc_time` — время, записанное в RTC (UNIX timestamp в секундах, UTC)
- `ntp_servers` — список используемых NTP серверов (в порядке приоритета)
- `sync_duration_ms` — длительность синхронизации в миллисекундах
- `error_code` — код ошибки (при status="error")
- `error_message` — описание ошибки

**Требования:**
- WiFi должен быть подключён
- RTC должен быть инициализирован

**Примечание:** Команда выполняет точную синхронизацию RTC по NTP с выравниванием на границу секунды. Используются серверы из настроек `sync.ntp1`, `sync.ntp2`, `sync.ntp3`.

---

### `save_config` - Сохранить конфигурацию

**Запрос:**
```json
{
  "cmd": "save_config",
  "id": 9,
  "data": {
    "device": {
      "name": "ENTime-Lab",
      "number": 1,
      "type": 1,
      "timezone_offset_min": 180
    },
    "sync": {
      "auto": true,
      "source": 0,
      "ntp1": "ru.pool.ntp.org",
      "ntp2": "time.google.com",
      "ntp3": "time.cloudflare.com"
    },
    "wifi": {
      "active": false,
      "ssid": "",
      "passwd": ""
    },
    "gps": {
      "enabled": true
    },
    "touch": {
      "enabled": true,
      "cal_valid": false,
      "calibration": [0, 0, 0, 0, 0]
    }
  }
}
```

**Ответ:**
```json
{
  "cmd": "save_config",
  "id": 9,
  "saved_keys": 4,
  "reboot_needed": true,
  "storage_usage_percent": 15,
  "status": "ok"
}
```

**Параметры ответа:**
- `reboot_needed` — требуется ли перезагрузка для применения изменений

**Примечание:** При сохранении можно указывать только нужные группы или параметры внутри групп. Остальные останутся без изменений. См. [SETTINGS.md](SETTINGS.md) для подробного описания всех настроек. Для touch поддерживаются оба формата: `touch.calibration` (массив из 5 значений) и совместимый формат `touch.cal0..touch.cal4`.

**Типы и `null`:** Отсутствующее поле сохраняет прежнее значение. Явный `null` недопустим для `data`, известных групп, параметров и элементов `touch.calibration`: возвращается ошибка `103`, изменения не применяются. Отсутствующее `data` возвращает ошибку `102`. `null` не означает сброс к значениям по умолчанию. Если переданы оба формата калибровки, применяется массив, но типы переданных `cal0..cal4` также проверяются.

**Пароль Wi-Fi:** `wifi.passwd` используется только для записи. Если поле отсутствует, сохранённый пароль остаётся прежним; строка заменяет пароль; пустая строка `""` очищает пароль (открытая сеть). Явный `null` возвращает ошибку `103`, как и для остальных настроек.

**Атомарность:** Операция `save_config` является атомарной - все указанные группы и параметры валидируются перед применением. Если хотя бы одна группа не прошла валидацию, изменения не применяются и возвращается ошибка. Это гарантирует, что либо все изменения применяются успешно, либо настройки остаются без изменений.

---

### `load_config` - Загрузить конфигурацию

**Запрос:**
```json
{"cmd": "load_config", "id": 10}
```

**Ответ:**
```json
{
  "cmd": "load_config",
  "id": 10,
  "data": {
    "device": {
      "name": "ENTime-Lab",
      "number": 1,
      "type": 1,
      "timezone_offset_min": 180
    },
    "sync": {
      "auto": true,
      "source": 0,
      "ntp1": "ru.pool.ntp.org",
      "ntp2": "time.google.com",
      "ntp3": "time.cloudflare.com"
    },
    "wifi": {
      "active": false,
      "ssid": ""
    },
    "gps": {
      "enabled": true
    },
    "touch": {
      "enabled": true,
      "cal_valid": false,
      "calibration": [0, 0, 0, 0, 0]
    }
  },
  "status": "ok"
}
```

**Примечание:** Все настройки организованы в иерархическую структуру по функциональным категориям. См. [SETTINGS.md](SETTINGS.md) для подробного описания всех параметров.

Поле `data.wifi.passwd` отсутствует в ответе независимо от того, установлен пароль или используется открытая сеть. При отправке полученного `data` обратно через `save_config` сохранённый пароль не изменяется.

---

### `factory_reset` - Сброс к заводским настройкам

**Запрос:**
```json
{"cmd": "factory_reset", "id": 11}
```

**Ответ:**
```json
{
  "cmd": "factory_reset",
  "id": 11,
  "message": "Device will reset in 2 seconds",
  "status": "ok"
}
```

---

## Ответы (Response)

### Общая структура ответа

```json
{
  "cmd": "command_name",
  "id": 123,  // опционально, копируется из запроса
  "status": "ok|warning|error",
  "error_code": 0,
  "error_message": "",
  "timestamp": 1703169600123456,
  "...": "command_specific_fields"
}
```

### Коды ошибок

| Код | Статус | Описание |
|-----|--------|---------|
| 0 | ok | Успешно |
| 1 | warning | Предупреждение, команда выполнена частично |
| 100 | error | Неизвестная команда |
| 101 | error | Неверный формат JSON |
| 102 | error | Отсутствует обязательный параметр |
| 103 | error | Недопустимое значение параметра |
| 104 | error | Кадр команды превышает размер буфера |
| 200 | error | GPS не инициализирован |
| 201 | error | RTC не инициализирован |
| 202 | error | Ошибка сохранения в Preferences |
| 203 | error | Ошибка при синхронизации времени |
| 204 | error | WiFi не подключен |
| 205 | error | Ошибка запуска WiFi |
| 206 | error | Таймаут остановки WiFi |

---

## Примеры сценариев

### Сценарий 0: Неверный формат JSON

```json
// Запрос (невалидный JSON)
{"cmd": "ping", "id": 1

// Ответ
{
  "cmd": "",
  "status": "error",
  "error_code": 101,
  "error_message": "Invalid JSON"
}
```

### Сценарий 1: Запрос текущего времени с проверкой точности

```json
// Запрос
{"cmd": "time", "id": 1}

// Ответ
{
  "cmd": "time",
  "id": 1,
  "time": 1703169600123456,
  "source": "gps",
  "accuracy_us": 50,
  "status": "ok"
}
```

### Сценарий 2: Проверка статуса и калибровка если нужна

```json
// 1. Запрос статуса
{"cmd": "status", "id": 1}

// Ответ: RTC дрейфит
{
  "cmd": "status",
  "id": 1,
  "sync_source": "rtc",
  "rtc": {
    "last_sync_ms": 1800000
  },
  "status": "warning"
}

// 2. Калибровка
{"cmd": "calibrate", "offset": 0.8, "id": 2}

// Ответ
{
  "cmd": "calibrate",
  "id": 2,
  "previous_offset": 0.2,
  "new_offset": 0.8,
  "status": "ok"
}
```

### Сценарий 3: Потеря GPS, fallback на RTC

```json
// Статус: GPS сигнал потерян
{
  "gps_fix": false,
  "sync_source": "rtc",
  "reason": "no_gps_signal",
  "status": "warning"
}

// При восстановлении GPS
{
  "gps_fix": true,
  "pps_signal": true,
  "sync_source": "gps",
  "status": "ok"
}
```

---

## Асинхронные события

Устройство может отправлять асинхронные уведомления (без запроса):

```json
// Потеря GPS
{"event": "gps_lost", "timestamp": 1703169600123456}

// Восстановление GPS
{"event": "gps_acquired", "satellites_used": 8, "satellites_view": 12, "timestamp": 1703169600123456}

// Ошибка RTC
{"event": "rtc_error", "error_code": 201, "timestamp": 1703169600123456}

// Переключение источника синхронизации
{"event": "sync_source_changed", "new_source": "rtc", "reason": "gps_timeout"}
```

---

## Рекомендации по интеграции

1. **Timeouts:** Ждите ответ максимум 100мс
2. **Retry logic:** При timeout повторите команду до 3 раз
3. **Queue commands:** Отправляйте по одной команде за раз, ждите ответа
4. **Monitor events:** Слушайте асинхронные события для быстрого реагирования
5. **Validate JSON:** Используйте JSON schema для валидации
