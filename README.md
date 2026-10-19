# Entime Firmware

Прошивка для ESP32-S3 устройства ENTime: старт/финиш модули для точной фиксации событий, синхронизация времени по GPS PPS/RTC (с INA226 для мониторинга питания) и управление через JSON-команды по Serial/BLE.

## Что делает

- Захватывает внешние события по прерыванию и формирует точные временные метки.
- Отправляет событийные пакеты START/FINISH/BEEP/VOICE по Serial и BLE.
- Синхронизирует время по GPS PPS + NMEA, с fallback на RTC DS3231 (SQW 1 Hz).
- Может синхронизировать RTC по NTP через WiFi командой `sync_ntp`.
- Рисует UI на TFT: статус-бар с временем и иконками, лог загрузки, отображение событий.
- Поддерживает touch HAL: события `Press/Move/Release`, polling, debounce и очередь событий.
- Работает как BLE NUS (UART over BLE) с сервисами Battery и Device Info.
- Хранит настройки в NVS и управляется JSON-протоколом.

## Режимы START/FINISH

Роль устройства задается настройкой `device.type` (см. `doc/SETTINGS.md`):
- `START` — стартовый модуль.
- `FINISH` — финишный модуль.

Влияние на пакеты:
- `START` отправляет стартовые пакеты с возможной поправкой (`$HH:MM:SS,mmm;correction#`), а также периодические `BEEP` и `VOICE` пакеты.
- `FINISH` отправляет только финишные пакеты (`FHH:MM:SS,mmm#`).

Подробный формат пакетов и правила отправки — в `doc/PROTOCOL.md`.

## Аппаратная часть (по умолчанию)

- ESP32-S3 DevKitC-1
- TFT ILI9341 (SPI, пины см. `platformio.ini`)
- Touch контроллер (через `TFT_eSPI`, `TOUCH_CS` в `platformio.ini`)
- GPS NEO-M8N (UART1 + PPS)
- RTC DS3231 (I2C + SQW 1 Hz)
- INA226 (I2C, мониторинг питания)
- Внешний вход события (GPIO15, FALLING, pull-up)

Пины заданы в `src/config.h`:
- GPS: RX=4, TX=5, PPS=6
- RTC/I2C: SDA=8, SCL=9, SQW=7
- External Interrupt: GPIO15

## Протокол и настройки

- [PROTOCOL.md](doc/PROTOCOL.md) — формат событийных пакетов и JSON-команд
- [SETTINGS.md](doc/SETTINGS.md) — структура настроек, валидация и дефолты
- [TIMESYNC.md](doc/TIMESYNC.md) — алгоритм GPS/RTC синхронизации времени
- [BOM.md](doc/BOM.md) — список компонентов
- [TODO.md](TODO.md) — ближайшие задачи

## Лицензия

MIT, см. [LICENSE](LICENSE).

## Требования

- [PlatformIO](https://platformio.org/)
- ESP32-S3 DevKitC (см. [BOM.md](doc/BOM.md) для полной конфигурации)

## Быстрый старт

Сборка:
```bash
pio run
```

Прошивка:
```bash
pio run -t upload
```

Монитор порта:
```bash
pio device monitor
```

## Тесты

Host-тесты без платы ESP32:

```bash
python3 test/run_host_tests.py
```

Структура и запуск с UBSan описаны в [test/README.md](test/README.md).

