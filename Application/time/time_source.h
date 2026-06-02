#ifndef TIME_SOURCE_H
#define TIME_SOURCE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file time_source.h
 * @brief Monotonic wall clock seeded once from SNTP.
 *
 * The device has no battery-backed RTC, so we sync UTC once over the network
 * (SNTP via the ESP-01) and then extrapolate using the FreeRTOS tick. UTC is
 * the source of truth (used for the inspection logged_at); local time is only
 * derived for on-screen display.
 */

/* Plant-local offset from UTC for display, in seconds. Africa/Tunis is UTC+1
 * year-round (no DST). Override via a build flag if the plant moves. */
#ifndef TIME_SOURCE_LOCAL_OFFSET_SEC
#define TIME_SOURCE_LOCAL_OFFSET_SEC  (3600)
#endif

/** Seed the clock from an absolute UTC epoch (seconds since 1970-01-01). */
void time_source_set_utc(uint32_t epoch_utc);

/**
 * Seed the clock from an asctime string as returned by AT+CIPSNTPTIME?,
 * e.g. "Thu Aug 04 14:48:05 2022". Interpreted as UTC (configure SNTP tz=0).
 * Rejects pre-2021 timestamps (module not synced yet).
 *
 * @return true if parsed and accepted.
 */
bool time_source_set_from_asctime(const char *asctime_utc);

/** True once a plausible time has been set. */
bool time_source_is_valid(void);

/** Current UTC epoch (seconds), or 0 when not valid. */
uint32_t time_source_now_utc(void);

/**
 * Format an explicit UTC epoch as ISO-8601 "YYYY-MM-DDTHH:MM:SSZ".
 * Use this for a timestamp captured earlier (e.g. when a part was inspected,
 * so an offline-queued part keeps its real time). epoch_utc == 0 yields 0.
 * @return characters written (excl. NUL), or 0 if epoch is 0 / buffer too small.
 */
int time_source_format_epoch_iso8601(uint32_t epoch_utc, char *buf, int len);

/**
 * Format the current time as ISO-8601 UTC "YYYY-MM-DDTHH:MM:SSZ".
 * @return characters written (excl. NUL), or 0 if not valid / buffer too small.
 */
int time_source_format_iso8601(char *buf, int len);

/** Format current plant-local time as "HH:MM". @return chars written or 0. */
int time_source_format_local_hm(char *buf, int len);

/** Format current plant-local date as "DD/MM/YYYY". @return chars written or 0. */
int time_source_format_local_date(char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SOURCE_H */
