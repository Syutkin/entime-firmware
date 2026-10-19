#include "time_sync.h"
#include "config.h"
#include "esp_log.h"
#include "hal/gps/gps.h"
#include "hal/rtc/rtc.h"
#include "storage/settings.h"
#include "pps_isr.h"
#include <RTClib.h>
#include <esp_timer.h>
#include <sys/time.h>

static const char *TAG = "TimeSync";

static TimeSyncStatus s_status{};
static int64_t        s_last_synced_pps_us = 0;
static TimeSyncStateCallback s_state_callback = nullptr;
static TimeSyncState s_last_state = TimeSyncState::NONE;

// Основная модель времени:
//   UTC(us) = anchor_utc_us + (esp_timer_us - anchor_esp_us)
//
// ISR события фиксирует esp_timer_us, а этот модуль позже переводит его в UTC.
// Поэтому точность метки события зависит от качества anchor, а не от того,
// насколько ровно сейчас выставлены системные часы через settimeofday().

// Системные часы не трогаем при микродрейфе меньше 1 ms.
static constexpr int64_t kJitterAdjustThresholdUs = 1000;

// Даже если offset малый, периодически переустанавливаем системные часы,
// чтобы gettimeofday() не уходил далеко от текущего anchor.
static constexpr int64_t kMaxHoldoffUs = 30000000;

// Максимально допустимый разнос PPS и NMEA sentence-start для выбора UTC секунды.
// Почти ±1s допустимы, но мертвая зона около ровно 1s обрабатывается отдельно.
static constexpr int64_t kPhaseWindowUs = 990000;

// RTC fallback: период проверки RTC и обновления грубой UTC привязки.
static constexpr int64_t kRtcResyncPeriodUs = 10000000; // 10 секунд
static int64_t s_last_rtc_resync_us = 0;
static constexpr uint8_t kRtcFallbackWarmupTicks = 3;
static constexpr int64_t kRtcFallbackLargeStepGuardUs = 100000;     // 100 ms
static constexpr uint8_t kRtcFallbackLargeStepMaxSkips = 3;
static constexpr int64_t kRtcFallbackInitialAnchorMaxAgeUs = 50000; // 50 ms
static constexpr int64_t kRtcLossHoldoverMaxAgeUs = 30000000;       // 30 s

// GPS/PPS validation.
static constexpr int64_t kNmeaFreshnessUs = 1500000;                // 1.5 s
static constexpr int64_t kGpsCandidateJumpGuardUs = 5000000;        // 5 s
static constexpr uint32_t kMinValidUnixSec = MIN_VALID_UNIX_SEC;    // 2020-01-01 00:00:00 UTC
static constexpr int64_t kPpsIntervalToleranceUs = 150000;          // 150 ms
static constexpr int64_t kPpsIntervalRebaselineGapUs = 5000000;     // 5 s
static constexpr int64_t kGpsLossHoldoverMaxAgeUs = 10000000;       // 10 s

// Анти-±1s защита NMEA/PPS: сравниваем кандидата только со свежим GPS anchor.
static constexpr int64_t kAnchorFreshnessUs = 3000000; // 3 секунды
// RTC/system holdover не подтверждает номер UTC секунды GPS. При расхождении
// больше половины секунды ждём несколько независимых согласованных NMEA.
static constexpr uint8_t kGpsUtcStepConfirmations = 3;
static bool s_gps_utc_confirmed = false;
struct GpsUtcCandidate {
  uint32_t utc_sec = 0;
  uint32_t nmea_utc_sec = 0;
  int64_t pps_esp_us = 0;
  int64_t nmea_esp_us = 0;
  uint8_t confirmations = 0;
};
static GpsUtcCandidate s_gps_utc_candidate{};

// Дисциплина RTC по PPS: rtc.setTime() планируется на следующий PPS edge.
static constexpr int64_t kRtcPpsSyncPeriodUs   = 10LL * 60LL * 1000000LL; // 10 минут
static constexpr int64_t kRtcPpsAlignWindowUs  = 3000; // 3 ms
static int64_t s_last_rtc_pps_sync_us = 0;
static bool    s_rtc_pps_pending = false;
static uint32_t s_rtc_pps_target_sec = 0;
static uint32_t s_rtc_pps_target_count = 0;

// Оценка точности: базовые допуски на джиттер и задержку источников.
static constexpr int64_t kPpsIsrJitterUs     = 20;    // ISR + чтение таймера
static constexpr int64_t kRtcBaseJitterUs    = 1500;  // SQW ISR + редкий I2C reanchor
static constexpr int64_t kGpsPhaseResidualUs = 200;   // остаточная неопределенность PPS<->UTC после phase lock
static constexpr int64_t kRtcAgingCapUs      = 10000; // максимум +10ms штрафа за "просрочку" тика

// GPS/PPS рабочее состояние.
static uint32_t s_last_pps_count = 0;
static bool     s_have_nmea = false;
static uint32_t s_last_nmea_utc_sec = 0;
static int64_t  s_last_nmea_esp_us  = 0;

// RTC/SQW fallback state.
// s_rtc_anchor_sqw_count связывает anchor_utc_us с конкретным SQW tick count:
// так на следующих SQW тиках можно строить UTC без I2C чтения RTC.
static uint32_t s_last_sqw_count = 0;
static uint32_t s_rtc_anchor_sqw_count = 0; // sqw_count в момент set_anchor(RTC,...)
static int64_t  s_last_sqw_edge_us = 0; // последний принятый фронт доверенного RTC
static constexpr int64_t kSqwAgeWindowUs = 900000; // если обработали позже — лучше не переякориваться
// Raw counter считает все GPIO фронты. Его приращение должно соответствовать
// времени от RTC anchor, иначе лишний/пропущенный фронт станет UTC секундой.
static constexpr int64_t kSqwCountToleranceUs = 200000;
static bool s_in_rtc_fallback = false;
static bool s_have_rtc_anchor = false;
static bool s_rtc_trust_failed = false;
// Внешняя установка RTC меняет UTC секунду и может изменить фазу SQW.
static int64_t s_last_observed_rtc_sync_us = 0;
static bool s_rtc_reanchor_after_write = false;

// Измеренный при живом PPS сдвиг RTC SQW относительно UTC-секунд.
// UTC момент SQW edge = ближайшая PPS UTC секунда + s_rtc_sqw_utc_offset_us.
static bool s_have_rtc_sqw_utc_offset = false;
static int64_t s_rtc_sqw_utc_offset_us = 0;

// Флаги служебных логов и guard'ов, чтобы не спамить одинаковыми сообщениями.
static bool s_logged_no_sqw = false;
static bool s_prev_sqw_locked = false;
static bool s_prev_have_sqw_edge = false; // чтобы логировать "signal acquired" или "signal lost"
static bool s_logged_sqw_warmup = false;
static bool s_logged_rtc_only = false;
static bool s_log_rtc_fallback_delta = false;
static bool s_rtc_fallback_guard_active = false;
static bool s_rtc_fallback_guard_logged = false;
static bool s_rtc_fallback_guard_failed = false;
static uint8_t s_rtc_fallback_large_step_skips = 0;
static uint8_t s_rtc_fallback_ticks = 0;
static bool s_logged_rtc_anchor_wait = false;

struct PpsSqwSample {
  bool has_sample = false;
  bool sqw_locked = false;
  int64_t sqw_utc_offset_us = 0;
};

static bool is_auto_sync_enabled() {
  return settings.getSync().auto_sync;
}

static void notify_state_change_if_needed() {
  TimeSyncState state = time_sync_state();
  if (state == s_last_state)
    return;
  s_last_state = state;
  if (s_state_callback)
    s_state_callback(state);
}

static bool gps_time_to_unix(uint32_t &unix_sec) {
  if (!gps.isReady())
    return false;

  auto &nmea = gps.nmea();
  if (!nmea.isValid())
    return false;

  if (!gps.utcFresh(kNmeaFreshnessUs))
    return false;

  uint16_t year   = nmea.getYear();
  uint8_t  month  = nmea.getMonth();
  uint8_t  day    = nmea.getDay();
  uint8_t  hour   = nmea.getHour();
  uint8_t  minute = nmea.getMinute();
  uint8_t  second = nmea.getSecond();

  if (year < 2020 || month == 0 || day == 0 || hour > 23 || minute > 59 || second > 60)
    return false;

  DateTime dt(year, month, day, hour, minute, second);
  unix_sec = dt.unixtime();
  return unix_sec > 0;
}

// Единственная точка смены anchor для esp_timer -> UTC.
// GPS anchor привязан к PPS edge, RTC anchor привязан к SQW edge.
static void set_anchor(TimeSource src, int64_t anchor_utc_us, int64_t anchor_esp_us) {
  s_status.source = src;
  s_status.anchor_utc_us = anchor_utc_us;
  s_status.anchor_esp_us = anchor_esp_us;
  if (src == TimeSource::RTC) {
    s_have_rtc_anchor = true;
    s_gps_utc_confirmed = false;
    s_gps_utc_candidate = GpsUtcCandidate{};
  } else {
    s_have_rtc_anchor = false;
    s_rtc_anchor_sqw_count = 0;
  }
}

static bool have_rtc_time_anchor() {
  return s_have_rtc_anchor &&
         (s_status.anchor_utc_us != 0) &&
         (s_status.anchor_esp_us != 0);
}

static void observe_external_rtc_write() {
  const int64_t sync_us = rtc.lastSyncUs();
  if (sync_us == s_last_observed_rtc_sync_us)
    return;

  s_last_observed_rtc_sync_us = sync_us;
  s_rtc_reanchor_after_write = true;
  s_rtc_anchor_sqw_count = 0;
  s_last_rtc_resync_us = 0;
  s_have_rtc_sqw_utc_offset = false;
  s_rtc_sqw_utc_offset_us = 0;
  s_rtc_pps_pending = false;

  // Прежний RTC anchor относится к уже заменённому времени. GPS anchor
  // остаётся независимым источником UTC и не сбрасывается при записи RTC.
  if (have_rtc_time_anchor()) {
    set_anchor(TimeSource::NONE, 0, 0);
    s_status.synced = false;
    s_last_sqw_edge_us = 0;
  }
}

static bool have_recent_rtc_holdover_anchor() {
  if (!have_rtc_time_anchor())
    return false;

  const int64_t reference_us = (s_last_sqw_edge_us != 0)
      ? s_last_sqw_edge_us
      : s_status.anchor_esp_us;
  const int64_t age_us = esp_timer_get_time() - reference_us;
  return age_us >= 0 && age_us <= kRtcLossHoldoverMaxAgeUs;
}

static bool rtc_time_is_trusted(uint32_t rtc_sec) {
  return rtc.isReady() &&
         !rtc.lostPower() &&
         rtc_sec >= kMinValidUnixSec;
}

static bool have_recent_gps_holdover_anchor() {
  if (s_status.source != TimeSource::GPS_PPS ||
      s_status.anchor_utc_us == 0 ||
      s_status.anchor_esp_us == 0 ||
      s_status.last_pps_timestamp_us == 0) {
    return false;
  }

  int64_t age_us = esp_timer_get_time() - s_status.last_pps_timestamp_us;
  return age_us >= 0 && age_us <= kGpsLossHoldoverMaxAgeUs;
}

// При временной потере PPS/SQW не сбрасываем sync сразу, если есть уже
// построенный RTC anchor или разрешенный свежий GPS holdover. В RTC-only
// режиме GPS holdover запрещен policy, даже если старый GPS anchor еще свежий.
static void set_fallback_status_from_anchor(bool allow_gps_holdover = true) {
  if (have_recent_rtc_holdover_anchor()) {
    s_status.source = TimeSource::RTC;
    s_status.synced = true;
  } else if (allow_gps_holdover && have_recent_gps_holdover_anchor()) {
    s_status.synced = true;
  } else {
    s_status.source = TimeSource::NONE;
    s_status.synced = false;
  }
}

static bool utc_us_to_nearest_second(int64_t utc_us, uint32_t &utc_sec_out) {
  int64_t sec = (utc_us + 500000LL) / 1000000LL;
  if (sec < (int64_t)kMinValidUnixSec || sec > (int64_t)UINT32_MAX)
    return false;

  utc_sec_out = (uint32_t)sec;
  return true;
}

// PPS остается точной секундной фазой даже без свежего NMEA. В этом случае
// номер UTC секунды берем из текущего anchor, а если его нет - из системного
// времени с поправкой на возраст PPS. Результат округляется к ближайшей секунде.
static bool estimate_pps_utc_from_holdover(int64_t pps_esp_us, uint32_t &utc_sec_out) {
  const bool have_precise_anchor =
      (s_status.source == TimeSource::GPS_PPS &&
       s_status.anchor_utc_us != 0 &&
       s_status.anchor_esp_us != 0) ||
      (s_status.source == TimeSource::RTC &&
       have_rtc_time_anchor() &&
       s_rtc_anchor_sqw_count != 0);

  if (have_precise_anchor) {
    int64_t utc_us = 0;
    if (time_sync_esp_to_utc_us(pps_esp_us, utc_us) &&
        utc_us_to_nearest_second(utc_us, utc_sec_out)) {
      return true;
    }
  }

  timeval tv{};
  gettimeofday(&tv, nullptr);
  int64_t now_us = esp_timer_get_time();
  int64_t age_us = now_us - pps_esp_us;
  if (age_us < 0) age_us = 0;

  int64_t system_utc_us =
      (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec - age_us;
  return utc_us_to_nearest_second(system_utc_us, utc_sec_out);
}

static int64_t round_to_nearest_second_us(int64_t utc_us) {
  if (utc_us >= 0)
    return ((utc_us + 500000LL) / 1000000LL) * 1000000LL;
  return ((utc_us - 500000LL) / 1000000LL) * 1000000LL;
}

// Переносит грубую UTC оценку SQW edge на фазу, измеренную от GPS PPS.
// Если SQW приходит не ровно на UTC секунде, fallback должен anchor'иться
// не на "целую секунду", а на "UTC секунда + измеренный SQW offset".
static int64_t apply_known_sqw_utc_offset(int64_t sqw_utc_est_us) {
  if (!s_have_rtc_sqw_utc_offset)
    return sqw_utc_est_us;

  int64_t pps_second_us =
      round_to_nearest_second_us(sqw_utc_est_us - s_rtc_sqw_utc_offset_us);
  return pps_second_us + s_rtc_sqw_utc_offset_us;
}

static bool system_time_at_esp_time_us(int64_t esp_us, int64_t &utc_us_out) {
  if (!is_auto_sync_enabled())
    return false;

  timeval tv{};
  gettimeofday(&tv, nullptr);
  // UTC и esp_timer должны относиться к одному моменту. Timestamp вызывающего
  // кода мог быть снят до блокирующего чтения RTC по I2C.
  const int64_t now_us = esp_timer_get_time();
  if (tv.tv_sec < (time_t)kMinValidUnixSec)
    return false;

  int64_t age_us = now_us - esp_us;
  if (age_us < 0)
    return false;

  utc_us_out = (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec - age_us;
  return utc_us_out >= (int64_t)kMinValidUnixSec * 1000000LL;
}

// Отбрасывает одиночные ложные PPS фронты. Неправдоподобный PPS не должен
// менять GPS anchor, системные часы и измеренный PPS/SQW offset.
static bool pps_interval_is_plausible(int64_t pps_esp_us,
                                      int64_t &period_us_out,
                                      int64_t &period_error_us_out) {
  period_us_out = 0;
  period_error_us_out = 0;

  if (s_status.last_pps_timestamp_us == 0)
    return true;

  int64_t period_us = pps_esp_us - s_status.last_pps_timestamp_us;
  period_us_out = period_us;

  if (period_us <= 0)
    return false;

  // После настоящей потери PPS первый новый lock становится новым baseline.
  if (period_us > kPpsIntervalRebaselineGapUs)
    return true;

  int64_t periods = (period_us + 500000LL) / 1000000LL;
  if (periods < 1)
    periods = 1;

  period_error_us_out = period_us - periods * 1000000LL;
  return llabs(period_error_us_out) <= kPpsIntervalToleranceUs;
}

// Измеряет кандидат offset между GPS PPS и RTC SQW. Само значение принимается
// позже, только если этот PPS прошел interval guard и SQW уже locked.
static PpsSqwSample log_pps_sqw_delta(int64_t pps_esp_us, uint32_t pps_count) {
  PpsSqwSample sample{};
  int64_t sqw_edge_us = 0;
  uint32_t sqw_count = 0;
  if (!rtc_sqw_get_raw(sqw_edge_us, sqw_count)) {
    ESP_LOGD(TAG, "PPS/SQW delta unavailable: no SQW edge (pps=%lld, pps_cnt=%lu)",
             (long long)pps_esp_us, (unsigned long)pps_count);
    return sample;
  }
  // Фронт до установки RTC не описывает новую фазу SQW.
  if (sqw_edge_us <= rtc.lastSyncUs())
    return sample;

  int64_t raw_delta_us = pps_esp_us - sqw_edge_us;
  int64_t phase_delta_us = raw_delta_us;
  if (phase_delta_us > 500000LL) {
    phase_delta_us -= 1000000LL;
  } else if (phase_delta_us < -500000LL) {
    phase_delta_us += 1000000LL;
  }

  int64_t sqw_utc_offset_us = -phase_delta_us;
  bool sqw_locked = rtc_sqw_is_locked();
  sample.has_sample = true;
  sample.sqw_locked = sqw_locked;
  sample.sqw_utc_offset_us = sqw_utc_offset_us;

  ESP_LOGV(TAG,
           "PPS/SQW candidate: delta_us=%lld raw_delta_us=%lld candidate_sqw_utc_offset_us=%lld locked=%d (pps=%lld cnt=%lu, sqw=%lld cnt=%lu)",
           (long long)phase_delta_us,
           (long long)raw_delta_us,
           (long long)sqw_utc_offset_us,
           (int)sqw_locked,
           (long long)pps_esp_us,
           (unsigned long)pps_count,
           (long long)sqw_edge_us,
           (unsigned long)sqw_count);
  return sample;
}

// Сохраняет рабочий PPS/SQW offset для будущего RTC fallback.
static bool accept_pps_sqw_offset(const PpsSqwSample &sample, int64_t pps_esp_us, uint32_t pps_count) {
  if (!sample.has_sample || !sample.sqw_locked)
    return false;

  if (s_have_rtc_sqw_utc_offset &&
      sample.sqw_utc_offset_us == s_rtc_sqw_utc_offset_us) {
    return true;
  }

  s_rtc_sqw_utc_offset_us = sample.sqw_utc_offset_us;
  s_have_rtc_sqw_utc_offset = true;
  ESP_LOGV(TAG,
           "RTC SQW offset accepted: sqw_utc_offset_us=%lld pps=%lld cnt=%lu",
           (long long)s_rtc_sqw_utc_offset_us,
           (long long)pps_esp_us,
           (unsigned long)pps_count);
  return true;
}

// Подтверждаем новую UTC шкалу по независимым NMEA и ходу времени между PPS.
static bool confirm_gps_utc_step(int64_t pps_esp_us, uint32_t utc_sec) {
  if (s_gps_utc_candidate.confirmations != 0 &&
      (s_last_nmea_esp_us == s_gps_utc_candidate.nmea_esp_us ||
       s_last_nmea_utc_sec == s_gps_utc_candidate.nmea_utc_sec)) {
    return false; // Повторное использование одной NMEA не является подтверждением.
  }

  const int64_t elapsed_us = pps_esp_us - s_gps_utc_candidate.pps_esp_us;
  const int64_t utc_elapsed_us =
      ((int64_t)utc_sec - (int64_t)s_gps_utc_candidate.utc_sec) * 1000000LL;
  const bool consistent = s_gps_utc_candidate.confirmations != 0 &&
      elapsed_us > 0 && elapsed_us <= kAnchorFreshnessUs &&
      s_last_nmea_esp_us > s_gps_utc_candidate.nmea_esp_us &&
      utc_elapsed_us > 0 &&
      llabs(utc_elapsed_us - elapsed_us) <= kPpsIntervalToleranceUs;

  const uint8_t confirmations = consistent
      ? s_gps_utc_candidate.confirmations + 1 : 1;
  s_gps_utc_candidate.utc_sec = utc_sec;
  s_gps_utc_candidate.nmea_utc_sec = s_last_nmea_utc_sec;
  s_gps_utc_candidate.pps_esp_us = pps_esp_us;
  s_gps_utc_candidate.nmea_esp_us = s_last_nmea_esp_us;
  s_gps_utc_candidate.confirmations = confirmations;

  return confirmations >= kGpsUtcStepConfirmations;
}

/**
 * Определяем, какая UTC секунда соответствует данному PPS.
 *
 * nmea_esp_us - это timestamp начала NMEA предложения, которое обновило UTC.
 *
 * delta = pps_esp_us - nmea_esp_us
 *  delta >= 0  => NMEA пришло ДО PPS => PPS = nmea_utc_sec + 1
 *  delta <  0  => NMEA пришло ПОСЛЕ PPS => PPS = nmea_utc_sec
 *
 * Порог по модулю delta нужен, чтобы не привязаться к старому/задержанному
 * NMEA. Если alignment не проходит, GPS PPS все равно может обновить anchor
 * через estimate_pps_utc_from_holdover(), но состояние будет GPS_DEGRADED.
 */
static bool align_pps_utc(int64_t pps_esp_us, uint32_t &pps_utc_sec_out, int64_t &phase_delta_us_out) {
  if (!s_have_nmea) {
    s_gps_utc_candidate = GpsUtcCandidate{};
    return false;
  }

  int64_t now_us = esp_timer_get_time();
  int64_t nmea_age_us = now_us - s_last_nmea_esp_us;
  if (nmea_age_us < 0 || nmea_age_us > kNmeaFreshnessUs) {
    s_gps_utc_candidate = GpsUtcCandidate{};
    return false;
  }

  // Знак delta выбирает UTC-секунду PPS, поэтому здесь нельзя применять
  // независимый фильтр по предыдущим NMEA-секундам: это может дать ошибку ±1s.
  int64_t delta = pps_esp_us - s_last_nmea_esp_us;
  phase_delta_us_out = delta;

  // Мертвая зона около ±1 секунды: здесь знак delta становится слишком хрупким,
  // и можно ошибиться ровно на одну UTC секунду.
  static constexpr int64_t kPhaseDeadbandUs = 5000; // 5 ms, можно 3000..10000

  int64_t abs_delta = llabs(delta);

  if (abs_delta >= (1000000LL - kPhaseDeadbandUs)) {
    s_gps_utc_candidate = GpsUtcCandidate{};
    return false;
  }

  if (abs_delta > kPhaseWindowUs) {
    s_gps_utc_candidate = GpsUtcCandidate{};
    return false;
  }

  if (delta >= 0) {
    pps_utc_sec_out = s_last_nmea_utc_sec + 1;
  } else {
    pps_utc_sec_out = s_last_nmea_utc_sec;
  }

  // Jump guard защищает только ранее подтверждённую GPS UTC. RTC и GPS_PPS,
  // полученный округлением RTC/system, могут ошибаться на секунды и минуты.
  const bool confirmed_gps_anchor = s_status.source == TimeSource::GPS_PPS &&
                                    s_gps_utc_confirmed;
  int64_t current_us = 0;
  if (s_status.synced && time_sync_esp_to_utc_us(pps_esp_us, current_us)) {
    int64_t candidate_us = (int64_t)pps_utc_sec_out * 1000000LL;
    int64_t diff_us = llabs(candidate_us - current_us);
    if (confirmed_gps_anchor && diff_us > kGpsCandidateJumpGuardUs) {
      s_gps_utc_candidate = GpsUtcCandidate{};
      return false;
    }
    if (!confirmed_gps_anchor && diff_us > 500000LL &&
        !confirm_gps_utc_step(pps_esp_us, pps_utc_sec_out)) {
      return false;
    }
  }
  s_gps_utc_candidate = GpsUtcCandidate{};

  // Анти-±1s коррекция: если свежий GPS anchor увереннее, чем знак delta,
  // поправляем выбранную NMEA секунду на один шаг.
  bool anchor_fresh = false;
  if (confirmed_gps_anchor && s_status.last_pps_timestamp_us != 0) {
    int64_t now_us = esp_timer_get_time();
    int64_t age_us = now_us - s_status.last_pps_timestamp_us;
    anchor_fresh = age_us >= 0 && age_us < kAnchorFreshnessUs;
  }

  if (anchor_fresh) {
    int64_t est_utc_us = 0;
    if (time_sync_esp_to_utc_us(pps_esp_us, est_utc_us)) {
      int64_t candidate_us = (int64_t)pps_utc_sec_out * 1000000LL;
      int64_t diff = candidate_us - est_utc_us;
      if (diff > 500000 && diff < 1500000) {
        pps_utc_sec_out -= 1;
      } else if (diff < -500000 && diff > -1500000) {
        pps_utc_sec_out += 1;
      }
    }
  }
  return true;
}

bool time_sync_esp_to_utc_us(int64_t esp_us, int64_t &utc_us_out) {
  if (s_status.source == TimeSource::NONE || s_status.anchor_esp_us == 0 || s_status.anchor_utc_us == 0)
    return false;

  if (s_status.source == TimeSource::RTC &&
      rtc.lastSyncUs() != s_last_observed_rtc_sync_us)
    return false;

  if (s_status.source == TimeSource::RTC &&
      (s_rtc_trust_failed || !rtc_sqw_is_locked()) &&
      !have_recent_rtc_holdover_anchor()) {
    return false;
  }

  utc_us_out = s_status.anchor_utc_us + (esp_us - s_status.anchor_esp_us);
  return true;
}

// Быстрый boot sync от RTC. Идеальный вариант - дождаться свежего SQW edge и
// связать RTC секунду именно с ним. Если edge не пришел, ставим системное
// время по rtc.unixTime() без фазовой гарантии, чтобы устройство имело хоть
// какие-то часы на UI до GPS/RTC fallback.
//
// Важно: системное время выставляем из RTC даже при lostPower(), но trusted
// anchor для timestamp событий создаем только если RTC time действительно
// доверенный.
static bool set_system_time_from_rtc_on_second_edge(uint32_t timeout_ms = 1500) {
  if (!rtc.isReady()) return false;

  const int64_t t0_us = esp_timer_get_time();
  int64_t  edge_us = 0;
  uint32_t edge_count = 0;

  // Ждем свежий SQW edge, а не просто факт наличия старого edge в ISR буфере.
  while ((esp_timer_get_time() - t0_us) < (int64_t)timeout_ms * 1000LL) {
    int64_t  e_us = 0;
    uint32_t c = 0;
    if (rtc_sqw_get_raw(e_us, c)) {
      int64_t now_us = esp_timer_get_time();
      if ((now_us - e_us) >= 0 && (now_us - e_us) < 200000) { // <200ms
        edge_us = e_us;
        edge_count = c;
        break;
      }
    }
    // Короткая пауза, чтобы boot wait не крутил busy-loop на полной скорости.
    esp_rom_delay_us(100);
  }

  if (edge_us == 0) {
    uint32_t sec0 = rtc.unixTime();
    timeval tv{ (time_t)sec0, 0 };
    settimeofday(&tv, nullptr);

    if (!rtc_time_is_trusted(sec0)) {
      ESP_LOGW(TAG,
               "Boot RTC clock loaded for UI only: rtc=%lu (lost_power=%d)",
               (unsigned long)sec0,
               (int)rtc.lostPower());
      return false;
    }

    int64_t now_us = esp_timer_get_time();
    set_anchor(TimeSource::RTC, (int64_t)sec0 * 1000000LL, now_us);

    s_status.source = TimeSource::RTC;
    s_status.synced = true;
    s_status.last_sync_us = now_us;
    s_status.last_offset_us = 0;
    s_status.last_utc_second = sec0;

    ESP_LOGW(TAG, "Boot RTC trusted anchor: SQW edge timeout, using rtc=%lu",
             (unsigned long)sec0);
    return true;
  }

  // На фронте читаем секунду RTC. Предполагаем, что RTC unixTime() уже
  // соответствует этому SQW edge.
  uint32_t rtc_sec = rtc.unixTime();

  // Ставим системные часы ровно на границу секунды RTC.
  timeval tv{ (time_t)rtc_sec, 0 };
  settimeofday(&tv, nullptr);

  if (!rtc_time_is_trusted(rtc_sec)) {
    ESP_LOGW(TAG,
             "Boot RTC clock loaded for UI only: rtc=%lu sqw_cnt=%lu (lost_power=%d)",
             (unsigned long)rtc_sec,
             (unsigned long)edge_count,
             (int)rtc.lostPower());
    return false;
  }

  // Создаем RTC anchor и связываем его с конкретным SQW counter.
  set_anchor(TimeSource::RTC, (int64_t)rtc_sec * 1000000LL, edge_us);
  s_last_sqw_count = edge_count;
  s_rtc_anchor_sqw_count = edge_count;
  s_last_sqw_edge_us = edge_us;
  s_last_rtc_resync_us = esp_timer_get_time();

  s_status.source = TimeSource::RTC;
  s_status.synced = true;
  s_status.last_sync_us = edge_us;
  s_status.last_offset_us = 0;
  s_status.last_utc_second = rtc_sec;

  ESP_LOGI(TAG, "Boot RTC sync: aligned to SQW edge (rtc=%lu, sqw_cnt=%lu)",
           (unsigned long)rtc_sec, (unsigned long)edge_count);

  return true;
}

void time_sync_begin() {
  s_status = TimeSyncStatus{};
  s_last_synced_pps_us = 0;
  s_gps_utc_confirmed = false;
  s_gps_utc_candidate = GpsUtcCandidate{};

  s_last_pps_count = 0;
  s_have_nmea = false;
  s_last_nmea_utc_sec = 0;
  s_last_nmea_esp_us = 0;

  s_last_rtc_resync_us = 0;
  s_last_rtc_pps_sync_us = 0;
  s_rtc_pps_pending = false;
  s_rtc_pps_target_sec = 0;
  s_rtc_pps_target_count = 0;

  s_last_sqw_count = 0;
  s_rtc_anchor_sqw_count = 0;
  s_last_sqw_edge_us = 0;
  s_have_rtc_anchor = false;
  s_rtc_trust_failed = false;
  s_last_observed_rtc_sync_us = rtc.lastSyncUs();
  s_rtc_reanchor_after_write = false;
  s_have_rtc_sqw_utc_offset = false;
  s_rtc_sqw_utc_offset_us = 0;
  rtc_sqw_begin(RTC_SQW_PIN, FALLING);

  s_in_rtc_fallback = false;
  s_logged_no_sqw = false;
  s_prev_sqw_locked = false;
  s_prev_have_sqw_edge = false;
  s_logged_sqw_warmup = false;
  s_logged_rtc_only = false;
  s_log_rtc_fallback_delta = false;
  s_rtc_fallback_guard_active = false;
  s_rtc_fallback_guard_logged = false;
  s_rtc_fallback_guard_failed = false;
  s_rtc_fallback_large_step_skips = 0;
  s_rtc_fallback_ticks = 0;
  s_logged_rtc_anchor_wait = false;

  // Boot phase: если GPS еще не готов, RTC может дать стартовые часы для UI.
  // Основная точная синхронизация все равно будет построена позже от PPS/SQW.
  // Это делаем даже при auto_sync=false: boot load не равен дальнейшей
  // автодисциплине системного времени.
  if (rtc.isReady()) {
    bool trusted_anchor = set_system_time_from_rtc_on_second_edge(1500);
    ESP_LOGI(TAG, "Boot time from RTC: %s",
             trusted_anchor ? "trusted anchor" : "system clock only");
  }

  notify_state_change_if_needed();
}

void time_sync_set_state_callback(TimeSyncStateCallback callback) {
  s_state_callback = callback;
  s_last_state = time_sync_state();
  if (s_state_callback)
    s_state_callback(s_last_state);
}

void time_sync_update() {
  observe_external_rtc_write();
  const bool auto_sync_enabled = is_auto_sync_enabled();
  const uint8_t sync_source = settings.getSync().source;
  const bool allow_gps = (sync_source != 2);
  if (allow_gps) {
    s_logged_rtc_only = false;
  }

  // ---------------------------------------------------------------------------
  // 1) NMEA: берем UTC секунду и timestamp начала предложения, которое ее дало.
  // ---------------------------------------------------------------------------
  uint32_t gps_utc = 0;
  if (allow_gps && gps_time_to_unix(gps_utc)) {
    s_status.gps_time_valid = true;

    int64_t nmea_arrival_us = 0;
    if (!gps.lastUtcUpdateSentenceStartUs(nmea_arrival_us)) {
      nmea_arrival_us = esp_timer_get_time();
    }

    // Статус обновляем всегда: это диагностика последнего видимого GPS UTC.
    s_status.last_nmea_utc_sec = gps_utc;

    // Для phase alignment важен только момент, когда UTC секунда реально
    // изменилась. Повторные чтения той же NMEA секунды не должны двигать метку.
    if (!s_have_nmea || gps_utc != s_last_nmea_utc_sec) {
      s_have_nmea = true;
      s_last_nmea_utc_sec = gps_utc;
      s_last_nmea_esp_us  = nmea_arrival_us;

      // Только диагностика PPS ISR; расчет UTC секунды PPS делает align_pps_utc().
      pps_set_gps_utc_second(gps_utc);
    }
  } else {
    s_status.gps_time_valid = false;
    s_have_nmea = false;
  }

  // ---------------------------------------------------------------------------
  // 2) PPS lock status. При потере PPS отменяем отложенное rtc.setTime().
  // ---------------------------------------------------------------------------
  s_status.pps_locked = allow_gps && pps_is_locked();
  if (!s_status.pps_locked) {
    // PPS пропал -> отменяем запланированную установку RTC по "следующему PPS"
    s_rtc_pps_pending = false;
    s_gps_utc_candidate = GpsUtcCandidate{};
  }

  // ---------------------------------------------------------------------------
  // 3) Нет PPS: удерживаем последний GPS anchor либо переходим на RTC+SQW.
  // ---------------------------------------------------------------------------
  if (!s_status.pps_locked) {
    s_status.phase_aligned = false;

    if (!rtc.isReady()) {
      s_rtc_trust_failed = true;
      s_rtc_anchor_sqw_count = 0;
      set_fallback_status_from_anchor(allow_gps);
      notify_state_change_if_needed();
      return;
    }

    int64_t sqw_edge_us = 0;
    uint32_t sqw_count  = 0;

    bool have_edge = rtc_sqw_get_raw(sqw_edge_us, sqw_count);
    bool locked    = rtc_sqw_is_locked();

    // Логируем только переходы signal/lock, чтобы fallback не спамил каждую loop.
    if (have_edge != s_prev_have_sqw_edge) {
      s_prev_have_sqw_edge = have_edge;
      if (have_edge) ESP_LOGI(TAG, "RTC SQW signal acquired");
      else           ESP_LOGW(TAG, "RTC SQW signal lost");
    }

    if (locked != s_prev_sqw_locked) {
      s_prev_sqw_locked = locked;
      if (locked) ESP_LOGI(TAG, "RTC SQW lock acquired");
      else        ESP_LOGW(TAG, "RTC SQW lock lost");
    }

    // 3a) RTC есть, но SQW edge еще не видим: продолжаем holdover, если он есть.
    if (!have_edge) {
      s_rtc_anchor_sqw_count = 0;
      set_fallback_status_from_anchor(allow_gps);

      if (!s_logged_no_sqw) {
        s_logged_no_sqw = true;
        if (s_status.synced) {
          ESP_LOGW(TAG, "RTC ready but SQW no signal -> holding last anchor");
        } else {
          ESP_LOGW(TAG, "RTC ready but SQW no signal -> NOSYNC");
        }
      }
      s_logged_sqw_warmup = false;
      notify_state_change_if_needed();
      return;
    }

    // После явной установки RTC календарное время относится только к новым
    // SQW фронтам. Старый ISR sample нельзя связать с новой UTC секундой.
    if (s_rtc_reanchor_after_write && sqw_edge_us <= s_last_observed_rtc_sync_us) {
      set_fallback_status_from_anchor(allow_gps);
      notify_state_change_if_needed();
      return;
    }

    // 3b) SQW signal есть, но lock еще набирается: не строим новый RTC anchor.
    if (!locked) {
      // Anchor ещё пригоден для holdover, но raw counter через потерю lock
      // переносить нельзя: в нём могли остаться лишние/пропущенные фронты.
      s_rtc_anchor_sqw_count = 0;
      s_logged_no_sqw = false;

      if (!s_logged_sqw_warmup) {
        s_logged_sqw_warmup = true;
        ESP_LOGI(TAG, "RTC SQW warmup: signal present, waiting lock...");
      }

      set_fallback_status_from_anchor(allow_gps);
      notify_state_change_if_needed();
      return;
    }

    // 3c) SQW locked: можно строить RTC anchor и дисциплинировать системное время.
    s_logged_no_sqw = false;
    s_logged_sqw_warmup = false;

    // Одноразовая инициализация guard'ов при входе в RTC fallback.
    if (!s_in_rtc_fallback) {
      s_in_rtc_fallback = true;
      s_log_rtc_fallback_delta = true;
      s_rtc_fallback_guard_active = true;
      s_rtc_fallback_guard_logged = false;
      s_rtc_fallback_guard_failed = false;
      s_rtc_fallback_large_step_skips = 0;
      s_rtc_fallback_ticks = 0;
      s_logged_rtc_anchor_wait = false;

      // rtc_sec нужен только для диагностического лога.
      uint32_t rtc_sec = rtc.unixTime();
      if (allow_gps) {
        ESP_LOGW(TAG, "GPS/PPS lost -> RTC+SQW fallback. rtc=%lu", (unsigned long)rtc_sec);
      } else if (!s_logged_rtc_only) {
        ESP_LOGI(TAG, "RTC-only mode (sync.source=2). rtc=%lu", (unsigned long)rtc_sec);
        s_logged_rtc_only = true;
      }
    }

    // RTC fallback работает по фронтам SQW, а не по частоте loop().
    if (sqw_count != s_last_sqw_count) {
      s_last_sqw_count = sqw_count;
      if (s_rtc_fallback_ticks < 255) s_rtc_fallback_ticks++;

      int64_t now_us = esp_timer_get_time();
      int64_t age_us = now_us - sqw_edge_us;
      if (age_us < 0) age_us = 0;

      // Если loop увидел SQW слишком поздно, лучше пропустить этот tick:
      // иначе чтение rtc/system time может попасть уже в соседнюю секунду.
      if (age_us <= kSqwAgeWindowUs) {

        // -----------------------------------------------------------------------
        // 3.1) RTC reanchor.
        //
        // Системное UTC используем только для первичной привязки: до потери
        // PPS оно было дисциплинировано GPS. При непрерывном SQW номер секунды
        // продолжаем по счетчику, иначе дрейф ESP возвращался бы в RTC anchor.
        // При auto_sync=false грубую секунду периодически сверяем с самим RTC.
        // -----------------------------------------------------------------------
        const bool have_rtc_anchor = have_rtc_time_anchor();

        // Loop мог не увидеть ложный фронт и временную потерю lock. Поэтому
        // проверяем и уже locked SQW, прежде чем считать UTC по raw counter.
        if (have_rtc_anchor && s_rtc_anchor_sqw_count != 0) {
          const uint32_t counted_seconds = sqw_count - s_rtc_anchor_sqw_count;
          const int64_t counted_us = (int64_t)counted_seconds * 1000000LL;
          const int64_t elapsed_us = sqw_edge_us - s_status.anchor_esp_us;
          if (elapsed_us < 0 || llabs(counted_us - elapsed_us) > kSqwCountToleranceUs) {
            ESP_LOGW(TAG, "RTC SQW count discontinuity: counted_us=%lld elapsed_us=%lld; rebuilding anchor",
                     (long long)counted_us, (long long)elapsed_us);
            s_rtc_anchor_sqw_count = 0;
          }
        }

        bool need_reanchor =
            !have_rtc_anchor || s_rtc_trust_failed ||
            (s_rtc_anchor_sqw_count == 0) ||
            (s_last_rtc_resync_us == 0) ||
            ((now_us - s_last_rtc_resync_us) >= kRtcResyncPeriodUs);


        bool allow_reanchor_now = true;
        if (s_rtc_fallback_guard_active &&
            s_rtc_fallback_ticks <= kRtcFallbackWarmupTicks &&
            age_us > kRtcFallbackInitialAnchorMaxAgeUs) {
          allow_reanchor_now = false;
        }

        if (need_reanchor && !allow_reanchor_now) {
          if (!s_logged_rtc_anchor_wait) {
            ESP_LOGW(TAG, "RTC fallback: waiting reanchor window (tick=%u, age_us=%lld)",
                     (unsigned)s_rtc_fallback_ticks, (long long)age_us);
            s_logged_rtc_anchor_wait = true;
          }
          set_fallback_status_from_anchor(allow_gps);
          notify_state_change_if_needed();
          return;
        }

        if (need_reanchor && allow_reanchor_now) {
          if (!rtc.timeValid(kMinValidUnixSec)) {
            s_rtc_trust_failed = true;
            if (!s_logged_rtc_anchor_wait) {
              ESP_LOGW(TAG,
                       "RTC fallback: RTC time is not trusted (lost_power=%d, min_unix=%lu)",
                       (int)rtc.lostPower(),
                       (unsigned long)kMinValidUnixSec);
              s_logged_rtc_anchor_wait = true;
            }
            set_fallback_status_from_anchor(allow_gps);
            notify_state_change_if_needed();
            return;
          }

          int64_t rtc_utc_us = 0;
          const char *anchor_source = "system";

          if (!s_rtc_reanchor_after_write && auto_sync_enabled &&
              have_rtc_anchor && s_rtc_anchor_sqw_count != 0) {
            const uint32_t seconds = sqw_count - s_rtc_anchor_sqw_count;
            rtc_utc_us = s_status.anchor_utc_us + (int64_t)seconds * 1000000LL;
            anchor_source = "sqw";
          } else if (!s_rtc_reanchor_after_write &&
                     system_time_at_esp_time_us(sqw_edge_us, rtc_utc_us)) {
            rtc_utc_us = apply_known_sqw_utc_offset(rtc_utc_us);
            anchor_source = s_have_rtc_sqw_utc_offset ? "system+sqw_offset" : "system";
          } else {
            uint32_t rtc_sec = rtc.unixTime();
            rtc_utc_us = (int64_t)rtc_sec * 1000000LL;
            if (s_have_rtc_sqw_utc_offset) {
              rtc_utc_us += s_rtc_sqw_utc_offset_us;
            }
            anchor_source = s_have_rtc_sqw_utc_offset ? "rtc+sqw_offset" : "rtc";

            // Холодный RTC fallback может ошибиться на ±1s из-за того, когда именно
            // rtc.unixTime() обновляется относительно SQW. Если есть старый anchor,
            // выбираем ближайшую соседнюю секунду к уже идущему времени.
            int64_t est_utc_us = 0;
            if (!s_rtc_reanchor_after_write &&
                time_sync_esp_to_utc_us(sqw_edge_us, est_utc_us)) {
              int64_t diff = rtc_utc_us - est_utc_us;
              if (diff > 500000 && diff < 1500000)            rtc_utc_us -= 1000000LL;
              else if (diff < -500000 && diff > -1500000)     rtc_utc_us += 1000000LL;
            }
          }

          // timeValid()/unixTime() могут занять миллисекунды. Возраст фронта
          // для target UTC и окна допустимости считаем после всех I2C чтений.
          now_us = esp_timer_get_time();
          age_us = now_us - sqw_edge_us;
          if (age_us < 0 || age_us > kSqwAgeWindowUs) {
            set_fallback_status_from_anchor(allow_gps);
            notify_state_change_if_needed();
            return;
          }

          if (!have_rtc_anchor || s_rtc_anchor_sqw_count == 0) {
            ESP_LOGI(TAG,
                     "RTC fallback anchor from %s: utc=%lld sqw=%lld age_us=%lld offset_us=%lld",
                     anchor_source,
                     (long long)rtc_utc_us,
                     (long long)sqw_edge_us,
                     (long long)age_us,
                     (long long)(s_have_rtc_sqw_utc_offset ? s_rtc_sqw_utc_offset_us : 0));
          }

          set_anchor(TimeSource::RTC, rtc_utc_us, sqw_edge_us);
          s_rtc_anchor_sqw_count = sqw_count;
          s_last_rtc_resync_us = now_us;
          s_rtc_trust_failed = false;
          s_rtc_reanchor_after_write = false;
          s_logged_rtc_anchor_wait = false;
        }

        const bool have_valid_rtc_anchor =
            s_have_rtc_anchor &&
            (s_status.anchor_utc_us != 0) &&
            (s_status.anchor_esp_us != 0) &&
            (s_rtc_anchor_sqw_count != 0);

        if (!have_valid_rtc_anchor) {
          if (!s_logged_rtc_anchor_wait) {
            ESP_LOGW(TAG, "RTC fallback: waiting valid RTC anchor (tick=%u, age_us=%lld)",
                     (unsigned)s_rtc_fallback_ticks, (long long)age_us);
            s_logged_rtc_anchor_wait = true;
          }
          set_fallback_status_from_anchor(allow_gps);
          notify_state_change_if_needed();
          return;
        }

        // -----------------------------------------------------------------------
        // 3.2) На каждый SQW tick строим UTC по счетчику без I2C:
        //    edge_utc = anchor_utc + (sqw_count - anchor_sqw_count)*1s
        // -----------------------------------------------------------------------
        int32_t d = (int32_t)(sqw_count - s_rtc_anchor_sqw_count); // signed delta
        int64_t edge_utc_us = s_status.anchor_utc_us + (int64_t)d * 1000000LL;

        // Метки событий должны интерполироваться от последнего SQW, а не от
        // anchor десятисекундной давности. Тогда дрейф esp_timer ограничен
        // интервалом между соседними фронтами и не накапливается в UTC.
        set_anchor(TimeSource::RTC, edge_utc_us, sqw_edge_us);
        s_rtc_anchor_sqw_count = sqw_count;
        // Недоверенный RTC не должен продлевать holdover одними фронтами SQW.
        s_last_sqw_edge_us = sqw_edge_us;

        // target_us - UTC оценка на момент текущей обработки loop().
        int64_t target_us = edge_utc_us + age_us;

        // -----------------------------------------------------------------------
        // 3.3) Дисциплина системных часов от RTC target.
        //
        // Метки событий используют anchor напрямую, но системные часы нужны
        // остальному коду и как источник грубой секунды при holdover.
        // -----------------------------------------------------------------------
        timeval current_tv{};
        gettimeofday(&current_tv, nullptr);
        int64_t current_us =
            (int64_t)current_tv.tv_sec * 1000000LL + (int64_t)current_tv.tv_usec;

        int64_t delta_us = target_us - current_us;

        bool need_adjust = !s_status.synced ||
                          (delta_us < -kJitterAdjustThresholdUs || delta_us > kJitterAdjustThresholdUs) ||
                          ((now_us - s_status.last_sync_us) > kMaxHoldoffUs);

        if (s_log_rtc_fallback_delta) {
          ESP_LOGI(TAG, "RTC fallback initial delta_us=%lld (target=%lld, current=%lld, age_us=%lld)",
                   (long long)delta_us, (long long)target_us, (long long)current_us, (long long)age_us);
          s_log_rtc_fallback_delta = false;
        }

        bool allow_adjust = true;
        if (auto_sync_enabled && s_rtc_fallback_guard_active) {
          if (s_rtc_fallback_ticks < kRtcFallbackWarmupTicks) {
            allow_adjust = false;
          } else if (llabs(delta_us) > kRtcFallbackLargeStepGuardUs) {
            allow_adjust = false;
            if (s_rtc_fallback_large_step_skips < 255) {
              s_rtc_fallback_large_step_skips++;
            }
            if (!s_rtc_fallback_guard_logged) {
              ESP_LOGW(TAG,
                       "RTC fallback guard: skip large delta_us=%lld (tick=%u, age_us=%lld)",
                       (long long)delta_us,
                       (unsigned)s_rtc_fallback_ticks,
                       (long long)age_us);
              s_rtc_fallback_guard_logged = true;
            }
            if (s_rtc_fallback_large_step_skips >= kRtcFallbackLargeStepMaxSkips) {
              bool first_failure = !s_rtc_fallback_guard_failed;
              s_rtc_fallback_guard_failed = true;
              if (first_failure) {
                ESP_LOGW(TAG,
                         "RTC fallback guard failed: delta_us=%lld after %u skips",
                         (long long)delta_us,
                         (unsigned)s_rtc_fallback_large_step_skips);
              }
            }
          } else {
            s_rtc_fallback_guard_active = false;
            s_rtc_fallback_guard_logged = false;
            s_rtc_fallback_guard_failed = false;
            s_rtc_fallback_large_step_skips = 0;
            ESP_LOGI(TAG,
                     "RTC fallback guard cleared: delta_us=%lld after %u SQW ticks",
                     (long long)delta_us,
                     (unsigned)s_rtc_fallback_ticks);
          }
        }

        if (auto_sync_enabled && s_rtc_fallback_guard_failed) {
          s_status.source = TimeSource::NONE;
          s_status.synced = false;
          s_status.last_offset_us = delta_us;
          notify_state_change_if_needed();
          return;
        }

        if (need_adjust && allow_adjust) {
          if (auto_sync_enabled) {
            int64_t sec64  = target_us / 1000000LL;
            int64_t usec64 = target_us % 1000000LL;
            if (usec64 < 0) { usec64 += 1000000LL; sec64 -= 1; }

            timeval tv{};
            tv.tv_sec  = (time_t)sec64;
            tv.tv_usec = (suseconds_t)usec64;
            settimeofday(&tv, nullptr);

            s_status.last_sync_us    = now_us;
            s_status.last_offset_us  = delta_us;
            s_status.last_utc_second = (uint32_t)tv.tv_sec;
          } else {
            s_status.last_offset_us = delta_us;
          }
        } else if (!allow_adjust) {
          s_status.last_offset_us = delta_us;
        }
      }
    }

    if (auto_sync_enabled && s_rtc_fallback_guard_failed) {
      s_status.source = TimeSource::NONE;
      s_status.synced = false;
      notify_state_change_if_needed();
      return;
    }

    set_fallback_status_from_anchor(allow_gps);
    notify_state_change_if_needed();
    return;
  }

  // ---------------------------------------------------------------------------
  // 4) PPS locked: обрабатываем только новый raw PPS edge.
  // ---------------------------------------------------------------------------
  int64_t  pps_time_us = 0;
  uint32_t pps_count   = 0;
  if (!pps_get_raw(pps_time_us, pps_count)) {
    s_gps_utc_candidate = GpsUtcCandidate{};
    s_status.phase_aligned = false;
    notify_state_change_if_needed();
    return;
  }

  // Тот же PPS edge может быть прочитан много раз между loop() итерациями.
  if (pps_count == s_last_pps_count) {
    notify_state_change_if_needed();
    return;
  }
  // Запись планируется строго на следующий raw PPS. Любой новый счётчик,
  // отличный от целевого, означает пропуск цели или сброс счётчика. Отменяем
  // старую заявку, чтобы на принятом PPS можно было запланировать следующую.
  // Сравнение на неравенство сохраняет это правило и при uint32_t rollover.
  if (s_rtc_pps_pending && pps_count != s_rtc_pps_target_count) {
    s_rtc_pps_pending = false;
  }
  s_last_pps_count = pps_count;
  PpsSqwSample pps_sqw_sample = log_pps_sqw_delta(pps_time_us, pps_count);

  // Дополнительная защита от повторной дисциплины тем же timestamp.
  if (pps_time_us == s_last_synced_pps_us) {
    notify_state_change_if_needed();
    return;
  }

  int64_t pps_period_us = 0;
  int64_t pps_period_error_us = 0;
  if (!pps_interval_is_plausible(pps_time_us, pps_period_us, pps_period_error_us)) {
    s_gps_utc_candidate = GpsUtcCandidate{};
    ESP_LOGW(TAG,
             "PPS rejected: bad interval period_us=%lld error_us=%lld last_accepted=%lld pps=%lld cnt=%lu candidate_sqw_utc_offset_us=%lld discarded=%d",
             (long long)pps_period_us,
             (long long)pps_period_error_us,
             (long long)s_status.last_pps_timestamp_us,
             (long long)pps_time_us,
             (unsigned long)pps_count,
             (long long)(pps_sqw_sample.has_sample ? pps_sqw_sample.sqw_utc_offset_us : 0),
             (int)pps_sqw_sample.has_sample);
    notify_state_change_if_needed();
    return;
  }

  // ---------------------------------------------------------------------------
  // 5) Выбираем UTC секунду для PPS.
  //
  // Свежий NMEA дает GPS_OK и phase_aligned=true. Если NMEA stale/invalid,
  // PPS все равно остается точной секундной фазой: номер секунды выводим из
  // предыдущего anchor или системного времени и помечаем GPS_DEGRADED.
  // ---------------------------------------------------------------------------
  uint32_t utc_second = 0;
  int64_t  phase_delta_us = 0;
  bool phase_aligned = align_pps_utc(pps_time_us, utc_second, phase_delta_us) && utc_second != 0;
  if (!phase_aligned) {
    s_status.phase_aligned = false;
    s_status.last_phase_delta_us = phase_delta_us;

    if (!estimate_pps_utc_from_holdover(pps_time_us, utc_second) || utc_second == 0) {
      ESP_LOGW(TAG, "PPS received but cannot align with NMEA or holdover (delta=%lld us, have_nmea=%d)",
               (long long)phase_delta_us, (int)s_have_nmea);
      notify_state_change_if_needed();
      return;
    }
  } else {
    s_status.phase_aligned = true;
    s_status.last_phase_delta_us = phase_delta_us;
  }

  // GPS anchor всегда ставится ровно на PPS edge. Даже в GPS_DEGRADED это
  // сохраняет миллисекундную фазу меток событий, если номер секунды выведен.
  (void)accept_pps_sqw_offset(pps_sqw_sample, pps_time_us, pps_count);
  const bool preserve_confirmed_utc = s_status.source == TimeSource::GPS_PPS &&
                                      s_gps_utc_confirmed;
  set_anchor(TimeSource::GPS_PPS, (int64_t)utc_second * 1000000LL, pps_time_us);
  s_gps_utc_confirmed = phase_aligned || preserve_confirmed_utc;

  // Одноразовая очистка RTC fallback guard'ов при возврате на GPS/PPS.
  if (s_in_rtc_fallback) {
    s_in_rtc_fallback = false;
    s_rtc_fallback_guard_active = false;
    s_rtc_fallback_guard_logged = false;
    s_rtc_fallback_guard_failed = false;
    s_rtc_fallback_large_step_skips = 0;
    s_rtc_fallback_ticks = 0;
    s_logged_rtc_anchor_wait = false;
    ESP_LOGI(TAG, "GPS/PPS restored -> GPS_PPS mode");
  }

  // ---------------------------------------------------------------------------
  // 6) Дисциплина RTC и системных часов.
  //
  // set_anchor() уже сделал главное для меток событий. settimeofday() нужен
  // для совместимости с кодом, который читает gettimeofday(), и для holdover.
  // ---------------------------------------------------------------------------
  int64_t now_us = esp_timer_get_time();
  int64_t age_us = now_us - pps_time_us;
  if (age_us < 0) age_us = 0;

  // RTC ставим на заранее запланированный "следующий PPS", чтобы I2C setTime()
  // соответствовал границе UTC секунды, а не текущему произвольному моменту.
  if (s_rtc_pps_pending && pps_count == s_rtc_pps_target_count) {
    // При физическом пропуске PPS счётчик может совпасть с целевым позже:
    // прежний target_sec тогда уже не соответствует этому фронту.
    if (auto_sync_enabled && age_us <= kRtcPpsAlignWindowUs && rtc.isReady() &&
        utc_second == s_rtc_pps_target_sec) {
      rtc.setTime(s_rtc_pps_target_sec);
      // Эта запись уже привязана к принятому GPS PPS, её не следует считать
      // внешней коррекцией при следующем вызове time_sync_update().
      s_last_observed_rtc_sync_us = rtc.lastSyncUs();
      ESP_LOGI(TAG, "RTC set from PPS: %lu (aligned, age %lld us)",
               (unsigned long)s_rtc_pps_target_sec, (long long)age_us);
      s_last_rtc_pps_sync_us = now_us;
    }
    s_rtc_pps_pending = false;
  }

  // rtc.setTime() может занять миллисекунды по I2C, поэтому после него заново
  // считаем возраст PPS перед settimeofday().
  now_us = esp_timer_get_time();
  age_us = now_us - pps_time_us;
  if (age_us < 0) age_us = 0;

  int64_t target_us = (int64_t)utc_second * 1000000LL + age_us;

  timeval current_tv{};
  gettimeofday(&current_tv, nullptr);
  int64_t current_us = (int64_t)current_tv.tv_sec * 1000000LL + (int64_t)current_tv.tv_usec;

  int64_t delta_us = target_us - current_us;

  bool need_adjust = !s_status.synced ||
                     (delta_us < -kJitterAdjustThresholdUs || delta_us > kJitterAdjustThresholdUs) ||
                     ((now_us - s_status.last_sync_us) > kMaxHoldoffUs);

  if (need_adjust) {
    if (auto_sync_enabled) {
      int64_t sec64  = target_us / 1000000LL;
      int64_t usec64 = target_us % 1000000LL;
      if (usec64 < 0) { usec64 += 1000000LL; sec64 -= 1; }

      timeval tv{};
      tv.tv_sec  = (time_t)sec64;
      tv.tv_usec = (suseconds_t)usec64;

      settimeofday(&tv, nullptr);

      s_status.last_sync_us     = now_us;
      s_status.last_utc_second  = (uint32_t)tv.tv_sec;
      s_last_synced_pps_us      = pps_time_us;

      ESP_LOGI(TAG,
               "System time synced: %ld.%06ld (delta %lld us, age %lld us, pps_utc %lu, phase %lld us, pps %lld, cnt %lu)",
               (long)tv.tv_sec, (long)tv.tv_usec,
               (long long)delta_us, (long long)age_us,
               (unsigned long)utc_second,
               (long long)phase_delta_us,
               (long long)pps_time_us,
               (unsigned long)pps_count);
    }
    s_status.last_offset_us = delta_us;
  }

  s_status.synced                = true;
  s_status.last_pps_timestamp_us = pps_time_us;
  s_status.last_offset_us        = delta_us;
  s_status.last_utc_second       = utc_second;

  // Планируем следующую установку RTC только при настоящем GPS_OK: PPS есть,
  // NMEA свежий, номер UTC секунды подтвержден.
  if (!s_rtc_pps_pending &&
      s_status.phase_aligned &&
      s_status.gps_time_valid &&
      auto_sync_enabled &&
      (s_last_rtc_pps_sync_us == 0 ||
       (now_us - s_last_rtc_pps_sync_us) >= kRtcPpsSyncPeriodUs)) {
    s_rtc_pps_pending = true;
    s_rtc_pps_target_sec = utc_second + 1;
    s_rtc_pps_target_count = pps_count + 1;
  }

  notify_state_change_if_needed();
}

TimeSyncStatus time_sync_status() { return s_status; }

TimeSyncState time_sync_state() {
  if (!s_status.synced) return TimeSyncState::NONE;

  // Пока PPS locked, UI/диагностика должны показывать GPS режим: GPS_OK при
  // свежем NMEA alignment, GPS_DEGRADED при PPS anchor без свежего NMEA.
  if (s_status.pps_locked) {
    return s_status.phase_aligned ? TimeSyncState::GPS_OK : TimeSyncState::GPS_DEGRADED;
  }

  if (s_status.source == TimeSource::RTC) {
    if (!s_rtc_trust_failed && rtc_sqw_is_locked())
      return TimeSyncState::RTC_OK;
    return have_recent_rtc_holdover_anchor() ? TimeSyncState::RTC_DEGRADED
                                             : TimeSyncState::NONE;
  }

  if (s_status.source == TimeSource::GPS_PPS) {
    // Короткий holdover после PPS loss: время еще идет от последнего GPS anchor.
    return TimeSyncState::GPS_DEGRADED;
  }

  return TimeSyncState::NONE;
}

int64_t time_sync_estimate_accuracy_us() {
  if (!s_status.synced)
    return -1;

  const bool auto_sync = is_auto_sync_enabled();
  const int64_t now_us = esp_timer_get_time();

  switch (s_status.source) {

    case TimeSource::GPS_PPS: {
      // Строгую accuracy отдаем только для GPS_OK. В GPS_DEGRADED метка
      // события может оставаться пригодной по holdover, но номер секунды уже не
      // подтвержден свежим PPS/NMEA alignment.
      if (!s_status.pps_locked || !s_status.phase_aligned)
        return -1;

      // База: ISR/таймер + остаток phase модели
      int64_t acc = kPpsIsrJitterUs + kGpsPhaseResidualUs;

      // "Свежесть" PPS якоря: esp_timer дрейфует, даём мягкий штраф
      int64_t age_us = now_us - s_status.last_pps_timestamp_us;
      if (age_us < 0) age_us = 0;

      // допустим дрейф ~1..2 us/сек (консервативно 2 us/сек)
      acc += (age_us / 1000000LL) * 2;

      // Если авто-дисциплина выключена — системные часы могут быть с большим offset
      // Тогда accuracy должна честно это показать.
      if (!auto_sync) {
        acc += llabs(s_status.last_offset_us);
      }

      return acc;
    }

    case TimeSource::RTC: {
      if (s_rtc_trust_failed || !rtc_sqw_is_locked())
        return -1;

      if (!rtc.timeValid(kMinValidUnixSec))
        return -1;

      // RTC_OK: метка события строится по SQW edge и anchor. I2C чтение RTC
      // участвует только при редком reanchor, не на каждом событии.
      int64_t acc = kRtcBaseJitterUs;

      // Если авто-дисциплина выключена — добавляем реальный offset
      if (!auto_sync) {
        acc += llabs(s_status.last_offset_us);
        return acc;
      }

      // Мягкое ухудшение, если давно не было актуального SQW edge/anchor обновления
      // (например loop сильно тормозит). Не "1ms за секунду", а аккуратнее и с потолком.
      int64_t age_us = (s_last_sqw_edge_us != 0) ? (now_us - s_last_sqw_edge_us)
                                                 : (now_us - s_status.anchor_esp_us);
      if (age_us < 0) age_us = 0;

      if (age_us > 1000000LL) {
        // +1ms за каждые 5 секунд "просрочки" после первой секунды
        int64_t extra_ms = (age_us - 1000000LL) / 5000000LL;
        int64_t extra_us = extra_ms * 1000LL;

        if (extra_us > kRtcAgingCapUs) extra_us = kRtcAgingCapUs;
        acc += extra_us;
      }

      return acc;
    }

    default:
      return -1;
  }
}
