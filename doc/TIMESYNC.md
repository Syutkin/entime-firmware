# Time Sync Algorithm

## Цель

Подсистема синхронизации времени нужна не для того, чтобы просто показывать
системные часы ESP32. Главная цель - точно перевести timestamp события,
захваченный в прерывании через `esp_timer_get_time()`, в UTC/local time с
миллисекундной точностью.

Базовая модель:

- событие фиксируется в ISR как `esp_timestamp_us`;
- GPS PPS или RTC SQW дают точную границу секунды на той же шкале
  `esp_timer`;
- NMEA, RTC или системное время дают номер UTC-секунды для этой границы;
- дальше любое событие переводится формулой:

```text
utc_us = anchor_utc_us + (event_esp_us - anchor_esp_us)
```

`anchor_esp_us` всегда должен быть timestamp'ом секундного фронта, когда это
возможно. Именно это даёт фазовую точность.

## Источники времени

### GPS PPS

GPS PPS - основной и самый точный источник секундной фазы.

PPS ISR сохраняет:

- `pps_time_us` - момент фронта PPS по `esp_timer`;
- `pps_count` - счётчик PPS фронтов.

PPS lock означает, что входные импульсы идут с периодом около 1 секунды и
сигнал считается стабильным.

### GPS NMEA

NMEA даёт UTC-время, то есть номер секунды. Сам по себе NMEA не считается
точной фазой, потому что строка приходит по UART с задержкой.

GPS слой сохраняет timestamp начала NMEA-предложения, на котором реально
обновился UTC tuple. `time_sync_update()` использует это для привязки NMEA
секунды к ближайшему PPS.

### RTC SQW

RTC DS3231 используется как fallback. Его SQW 1 Hz даёт секундный фронт,
аналогично PPS, но менее точно.

RTC I2C чтение (`rtc.unixTime()`) даёт номер секунды. SQW ISR даёт фазу этой
секунды на шкале `esp_timer`.

SQW фронт не обязан совпадать по фазе с GPS PPS. Когда одновременно видны PPS и
locked SQW, код измеряет `PPS - SQW` и сохраняет `sqw_utc_offset_us` - UTC
смещение SQW edge относительно ближайшей целой PPS/UTC секунды.

### Системное время

Системное время через `gettimeofday()` не является источником фазовой точности.
Оно используется:

- для оценки offset'а между target UTC и системными часами;
- для `settimeofday()`, если `auto_sync=true` (boot-загрузка часов из RTC
  является отдельным исключением);
- как fallback для номера секунды PPS, когда PPS есть, но свежего NMEA нет и
  текущего точного anchor ещё нет.

`auto_sync=false` запрещает дисциплинировать системные часы, но не должен
ломать внутренний `esp_timer -> UTC` anchor.

## Основные состояния

`TimeSource` показывает, откуда взят текущий anchor:

- `NONE` - anchor отсутствует;
- `GPS_PPS` - anchor построен по PPS edge;
- `RTC` - anchor построен по RTC SQW edge.

`TimeSyncState` показывает рабочий режим:

- `NONE` - времени нет;
- `GPS_OK` - PPS locked и есть свежий PPS/NMEA phase alignment;
- `GPS_DEGRADED` - PPS locked без свежего NMEA alignment или короткий holdover
  после потери PPS; timestamp событий продолжает строиться от GPS/PPS anchor;
- `RTC_OK` - RTC SQW locked и anchor валиден;
- `RTC_DEGRADED` - RTC есть, но SQW ещё не locked, при этом старый RTC anchor
  может оставаться пригодным в течение короткого holdover окна.

Ключевой инвариант: `synced=true` означает, что `time_sync_esp_to_utc_us()`
должен уметь построить UTC по текущему anchor.

## Инициализация

`time_sync_begin()` сбрасывает внутреннее состояние, счётчики PPS/SQW, NMEA
кэш, guard flags и запускает ISR RTC SQW.

Если RTC готов, код при boot всегда пытается выставить системное время от RTC,
даже при `auto_sync=false`. Это нужно, чтобы UI и код, читающий системные часы,
видели хоть какие-то часы до GPS/NTP.

1. ждёт свежий SQW edge;
2. читает `rtc.unixTime()`;
3. делает `settimeofday()` на границу секунды;
4. если RTC time trusted (`!lostPower()` и Unix time >= 2020-01-01), создаёт
   RTC anchor: `anchor_utc_us = rtc_sec * 1e6`,
   `anchor_esp_us = sqw_edge_us`.

Если SQW edge не дождались, используется fallback: читается `rtc.unixTime()` и
системные часы выставляются относительно текущего `esp_timer`. Trusted RTC
anchor в этом режиме создаётся только если RTC time valid. Если RTC потерял
питание или время явно старое, системные часы всё равно загружаются для UI, но
`sync.state` остаётся `NONE`, и timestamp событий не считаются валидными до
GPS/NTP или доверенного RTC anchor.

## Основной цикл `time_sync_update()`

### 1. Обновление NMEA UTC

Если GPS разрешён настройкой `sync.source`, код пытается получить текущую UTC
секунду из MicroNMEA.

UTC принимается только если:

- GPS готов;
- NMEA валиден и свежий;
- UTC update из NMEA свежий;
- дата/время правдоподобны (год не раньше 2020).

Если UTC принят:

- `gps_time_valid=true`;
- `last_nmea_utc_sec` обновляется для диагностики;
- при смене UTC секунды сохраняются:
  - `s_last_nmea_utc_sec`;
  - `s_last_nmea_esp_us` - timestamp начала NMEA-предложения, которое обновило
    UTC.

Если GPS/NMEA невалиден или UTC update устарел, `s_have_nmea=false`.

### 2. Проверка PPS lock

`pps_is_locked()` определяет, стабилен ли PPS. Если PPS не locked:

- отменяется отложенная установка RTC по следующему PPS;
- включается guard для будущего GPS relock;
- управление переходит в RTC fallback.

Если PPS locked, код остаётся в GPS ветке.

## GPS PPS режим

GPS режим обрабатывает только новые PPS импульсы. Старый `pps_count` или уже
обработанный `pps_time_us` игнорируются.

### Raw PPS guard

Не каждый GPIO edge, попавший в PPS ISR, сразу становится рабочим PPS.

Перед построением GPS anchor код проверяет интервал от последнего принятого PPS:

- интервал должен быть близок к целому числу секунд;
- допустимое отклонение - 150 ms;
- длинный разрыв больше 5 s считается новым baseline после настоящей потери и
  восстановления PPS lock.

Если интервал плохой, PPS отбрасывается до PPS/NMEA alignment, `set_anchor()` и
`settimeofday()`. Рабочий `sqw_utc_offset_us` по такому PPS не обновляется.

### PPS + fresh NMEA alignment

`align_pps_utc()` определяет, какая UTC секунда соответствует текущему PPS.

Используются:

- последняя NMEA UTC секунда;
- timestamp NMEA UTC update;
- timestamp текущего PPS.

Расчёт:

```text
delta = pps_esp_us - nmea_esp_us

delta >= 0  => NMEA пришло до PPS, PPS = nmea_utc_sec + 1
delta <  0  => NMEA пришло после PPS, PPS = nmea_utc_sec
```

Защиты:

- NMEA должна быть свежей;
- `abs(delta)` должен попадать в доверенное окно;
- зона около ровно +/-1 секунды считается опасной и отбрасывается;
- jump guard в 5 секунд применяется только к anchor, UTC которого ранее
  подтверждена NMEA, а не к произвольному RTC/system holdover;
- коррекция кандидата на +/-1 секунду по свежему anchor также разрешена только
  для такой подтверждённой GPS шкалы.

Источник `GPS_PPS` сам по себе не означает подтверждение UTC: первая секунда
могла быть получена округлением RTC или системного времени. Внутренний признак
`s_gps_utc_confirmed` различает эти случаи. Он сохраняется при непрерывном
PPS holdover от ранее подтверждённого GPS, сбрасывается при создании RTC anchor
и при `time_sync_begin()`, не наследуется от отсутствующего anchor.

Если новый NMEA/PPS кандидат расходится с ещё не подтверждённой RTC/holdover
шкалой больше чем на 500 мс, смена UTC требует трёх независимых согласованных
обновлений NMEA. Для каждого подтверждения должны измениться NMEA UTC и
timestamp предложения, а приращение UTC должно соответствовать времени между
PPS с допуском 150 мс. Между подтверждениями допускается не более 3 секунд.
Одна NMEA, использованная для двух соседних PPS, не считается дважды.

Невалидный/stale NMEA или недопустимая фаза при обработке PPS, отвергнутый
raw PPS и потеря PPS lock сбрасывают накопление подтверждений; противоречивый
кандидат начинает новую последовательность. Пока подтверждений недостаточно,
работает прежний holdover и состояние остаётся `GPS_DEGRADED`.
После третьего подтверждения правильная GPS секунда заменяет RTC/holdover даже
при ошибке в секунды или минуты. При `auto_sync=false` исправляется внутренний
UTC anchor, системные часы не меняются.

Если существующая шкала уже согласуется с NMEA в пределах 500 мс, либо рабочего
anchor ещё нет, дополнительное ожидание не требуется. Это сохраняет быстрый
обычный захват GPS.

Если alignment успешен:

- `phase_aligned=true`;
- `source=GPS_PPS`;
- `anchor_utc_us = utc_second * 1e6`;
- `anchor_esp_us = pps_time_us`;
- состояние становится `GPS_OK`.

### PPS holdover без fresh NMEA

Если PPS locked, но PPS/NMEA alignment не прошёл, PPS всё равно остаётся
точным секундным edge. Код не прекращает обновлять anchor сразу.

Вместо раннего отказа он пытается получить UTC секунду PPS через holdover:

1. сначала по текущему точному anchor:
   - `GPS_PPS`, если уже был GPS PPS anchor;
   - `RTC`, если был валидный RTC SQW anchor;
2. если такого anchor нет, по системному времени:

```text
system_utc_at_pps = gettimeofday() - (esp_now - pps_esp_us)
```

Полученная оценка округляется до ближайшей UTC секунды, потому что PPS является
границей секунды. Время раньше 2020-01-01 отбрасывается как явно невалидное.

Если holdover успешен:

- `phase_aligned=false`;
- `source=GPS_PPS`;
- anchor обновляется на текущий PPS;
- `time_sync_state()` возвращает `GPS_DEGRADED`;
- `time_sync_esp_to_utc_us()` продолжает работать от свежего PPS edge.

Если не удалось получить UTC секунду ни из NMEA, ни из holdover/system time,
PPS не используется для нового anchor, и состояние только обновляет диагностику.

### Дисциплина системного времени

После построения PPS anchor код считает target UTC на текущий момент:

```text
target_us = utc_second * 1e6 + age_us
```

`age_us` - сколько времени прошло от PPS edge до обработки в loop.

Затем сравнивается target с `gettimeofday()`:

```text
delta_us = target_us - current_system_time_us
```

Если `auto_sync=true`, системное время корректируется через `settimeofday()`,
когда:

- система ещё не была synced;
- offset больше jitter-порога;
- прошло слишком много времени с последней коррекции.

Если `auto_sync=false`, `settimeofday()` не вызывается, но internal anchor и
`last_offset_us` всё равно обновляются.

### GPS relock guard

После потери PPS и последующего возвращения GPS действует warmup/large-step
guard. Он не даёт сразу применить большой скачок системного времени.

Guard влияет только на дисциплину системных часов при `auto_sync=true`. PPS
anchor для timestamp событий всё равно может обновляться.

## RTC fallback

RTC fallback используется, когда PPS не locked.

### Нет RTC или SQW

Если RTC не готов, новый RTC anchor создать нельзя. Если GPS разрешён policy
`sync.source` и перед потерей PPS был свежий GPS/PPS anchor, он временно
остаётся рабочим holdover anchor. Если до этого был свежий RTC anchor, он тоже
может временно удерживать синхронизацию. Иначе состояние становится `NONE`. В
RTC-only режиме (`sync.source=2`) GPS holdover не используется.

Если RTC готов, но SQW edge ещё не виден, код тоже не создаёт новый RTC anchor.
Приоритет удержания: свежий старый RTC anchor, затем разрешённый свежий GPS/PPS
holdover anchor. В первом случае состояние может быть `RTC_DEGRADED`, во втором
timestamp событий продолжает строиться от GPS anchor. Без такого anchor
состояние становится `NONE`.

Старый RTC anchor без locked SQW удерживается не бесконечно: текущее окно
holdover составляет 30 секунд от последнего обработанного SQW edge, а если edge
никогда не обрабатывался - от времени создания RTC anchor.

### SQW warmup

Если SQW signal есть, но lock ещё не набран:

- код не создаёт новый anchor;
- если старый RTC anchor ещё попадает в holdover окно, состояние может быть
  `RTC_DEGRADED`;
- если RTC anchor ещё нет, но свежий GPS/PPS holdover anchor валиден и GPS
  разрешён policy, состояние остаётся синхронизированным в degraded режиме;
- если anchor нет, состояние становится `NONE`.

### SQW locked

При стабильном SQW код работает по новым SQW фронтам:

1. берёт `sqw_edge_us` и `sqw_count`;
2. перед reanchor проверяет доверенность RTC: `!lostPower()` и Unix time не
   ниже минимального порога;
3. если `auto_sync=true`, берёт системное время через `gettimeofday()` и
   пересчитывает его назад к `sqw_edge_us`;
4. если системное время не используется или невалидно, использует
   `rtc.unixTime()` как холодный fallback;
5. если известен `sqw_utc_offset_us`, переносит UTC на фазу SQW edge;
6. дальше строит UTC секунды по счётчику SQW:

```text
edge_utc_us = anchor_utc_us + (sqw_count - anchor_sqw_count) * 1e6
target_us   = edge_utc_us + age_us
```

Например, если SQW приходит на 354 ms после GPS PPS, то UTC на SQW edge
считается как `целая_UTC_секунда + 354000 us`, а не как ровная граница секунды.

Это позволяет не читать RTC по I2C каждую секунду.

RTC anchor считается валидным только если есть:

- `s_have_rtc_anchor`;
- ненулевые `anchor_utc_us` и `anchor_esp_us`;
- `s_rtc_anchor_sqw_count`.

### RTC fallback guards

На входе в RTC fallback включается warmup и large-step guard:

- первые SQW ticks могут не применяться для коррекции системного времени;
- слишком большой delta не применяется сразу;
- после нескольких подряд больших delta guard переводит состояние в `NONE`,
  чтобы не выглядеть синхронизированным при подозрительном времени.

При `auto_sync=false` guard не блокирует внутренний RTC anchor, потому что
системные часы специально не дисциплинируются.

## Дисциплина RTC по PPS

Когда GPS PPS режим стабилен и `auto_sync=true`, код периодически планирует
установку RTC на следующий PPS:

1. на текущем PPS вычисляется `s_rtc_pps_target_sec = utc_second + 1`;
2. запоминается ожидаемый `pps_count + 1`;
3. на следующем PPS, если возраст PPS находится в допустимом окне, вызывается
   `rtc.setTime(target_sec)`.

Это удерживает RTC близко к GPS времени для будущего fallback.

## Конвертация timestamp события

События фиксируются отдельно: ISR сохраняет `esp_timestamp_us`.

Когда событие нужно отправить/обработать, вызывается:

```cpp
time_sync_esp_to_utc_us(esp_timestamp_us, utc_us)
```

Функция не читает GPS, RTC или системные часы. Она только применяет текущий
anchor:

```text
utc_us = anchor_utc_us + (esp_timestamp_us - anchor_esp_us)
```

Поэтому точность события зависит от качества последнего anchor:

- лучший случай: anchor от GPS PPS;
- fallback: anchor от RTC SQW;
- нет anchor: событие получает `success=false`.

## Оценка точности

`time_sync_estimate_accuracy_us()` возвращает грубую оценку ошибки:

- для `GPS_PPS`: ISR jitter + остаточная PPS/NMEA неопределённость + мягкий
  штраф за возраст anchor;
- для `GPS_PPS` оценка доступна только в состоянии `GPS_OK`
  (`pps_locked=true` и `phase_aligned=true`); для `GPS_DEGRADED`
  возвращается `-1`, потому что UTC секунда не подтверждена свежим
  PPS/NMEA alignment;
- для `RTC`: остаточный допуск SQW ISR/обработки loop/reanchor + мягкое
  ухудшение при старом SQW edge;
- для `RTC` оценка недоступна (`-1`), если RTC не прошел проверку
  `time_valid` (`lostPower` или время ниже минимального порога);
- при `auto_sync=false` дополнительно учитывается `last_offset_us`, потому что
  системные часы могут отличаться от internal anchor.

Эта оценка диагностическая. Для timestamp событий главным источником точности
является то, что event ISR и PPS/SQW ISR используют одну шкалу `esp_timer`.

## Краткая схема переходов

```text
PPS locked + fresh NMEA alignment
  => GPS_OK, anchor = GPS PPS edge + NMEA UTC second

PPS locked + no fresh NMEA + holdover/system UTC second available
  => GPS_DEGRADED, anchor = GPS PPS edge + derived UTC second

PPS not locked + recent GPS/PPS anchor + RTC SQW not ready yet + GPS allowed
  => GPS_DEGRADED, conversion continues from last GPS/PPS anchor

PPS not locked + sync.source=RTC + no valid RTC anchor
  => NONE, GPS holdover is ignored by source policy

PPS not locked + RTC SQW locked
  => RTC_OK, anchor = RTC SQW edge + RTC UTC second

PPS not locked + RTC SQW warmup/loss + recent old RTC anchor exists
  => RTC_DEGRADED, conversion may continue from old RTC anchor for up to 30s

No usable GPS PPS or RTC anchor
  => NONE, event timestamp conversion fails
```

## Практический смысл `auto_sync`

`auto_sync` управляет дальнейшей дисциплиной системных часов после boot:

- `true`: код вызывает `settimeofday()` и периодически дисциплинирует систему;
- `false`: системные часы не корректируются в рабочем цикле, но при boot всё
  равно однократно загружаются из RTC, если RTC доступен.

В обоих режимах подсистема должна продолжать поддерживать internal anchor для
`time_sync_esp_to_utc_us()`. Это важно, потому что timestamp событий не должен
зависеть от того, разрешено ли менять системные часы ESP32.
