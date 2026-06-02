#include "time/time_source.h"
#include "cmsis_os2.h"

#include <stdio.h>
#include <string.h>

/* Clock state: UTC epoch captured at a known tick, extrapolated from there. */
static volatile bool     s_valid     = false;
static volatile uint32_t s_baseEpoch = 0U;   /* UTC seconds at s_baseTick     */
static volatile uint32_t s_baseTick  = 0U;   /* osKernelGetTickCount() value  */

static uint32_t ms_per_tick(void)
{
    uint32_t f = osKernelGetTickFreq();
    return (f != 0U) ? (1000U / f) : 1U;
}

/* days_from_civil / civil_from_days: Howard Hinnant's public-domain algorithms,
 * valid for the full proleptic Gregorian range. */
static int32_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153U * (m > 2 ? m - 3U : m + 9U) + 2U) / 5U + d - 1U;
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + (int)doe - 719468;
}

static void civil_from_days(int32_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int yy = (int)yoe + era * 400;
    unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    unsigned mp = (5U * doy + 2U) / 153U;
    *d = doy - (153U * mp + 2U) / 5U + 1U;
    *m = mp < 10U ? mp + 3U : mp - 9U;
    *y = yy + (*m <= 2U);
}

static void epoch_to_parts(uint32_t epoch, int *Y, int *Mo, int *D,
                           int *h, int *mi, int *s)
{
    uint32_t days = epoch / 86400U;
    uint32_t secs = epoch % 86400U;
    *h = (int)(secs / 3600U);
    *mi = (int)((secs % 3600U) / 60U);
    *s = (int)(secs % 60U);
    unsigned m, d;
    civil_from_days((int32_t)days, Y, &m, &d);
    *Mo = (int)m;
    *D = (int)d;
}

void time_source_set_utc(uint32_t epoch_utc)
{
    s_baseEpoch = epoch_utc;
    s_baseTick  = osKernelGetTickCount();
    s_valid     = (epoch_utc > 0U);
}

bool time_source_is_valid(void)
{
    return s_valid;
}

uint32_t time_source_now_utc(void)
{
    if (!s_valid) {
        return 0U;
    }
    uint32_t elapsed_ms = (osKernelGetTickCount() - s_baseTick) * ms_per_tick();
    return s_baseEpoch + elapsed_ms / 1000U;
}

static int month_from_abbrev(const char *mon)
{
    static const char *const names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    for (int i = 0; i < 12; ++i) {
        if (strncmp(mon, names[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

bool time_source_set_from_asctime(const char *asctime_utc)
{
    if (asctime_utc == NULL) {
        return false;
    }

    /* "Www Mmm dd hh:mm:ss yyyy" — the leading weekday is skipped. */
    char mon[4] = {0};
    int day = 0, hh = 0, mm = 0, ss = 0, year = 0;
    if (sscanf(asctime_utc, "%*s %3s %d %d:%d:%d %d",
               mon, &day, &hh, &mm, &ss, &year) != 6) {
        return false;
    }

    int month = month_from_abbrev(mon);
    if (month == 0 || year < 2021 || day < 1 || day > 31 ||
        hh > 23 || mm > 59 || ss > 60) {
        return false;
    }

    int32_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
    uint32_t epoch = (uint32_t)days * 86400U +
                     (uint32_t)hh * 3600U + (uint32_t)mm * 60U + (uint32_t)ss;
    time_source_set_utc(epoch);
    return true;
}

int time_source_format_epoch_iso8601(uint32_t epoch_utc, char *buf, int len)
{
    if (epoch_utc == 0U || buf == NULL || len < 21) {
        return 0;
    }
    int Y, Mo, D, h, mi, s;
    epoch_to_parts(epoch_utc, &Y, &Mo, &D, &h, &mi, &s);
    return snprintf(buf, (size_t)len, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                    Y, Mo, D, h, mi, s);
}

int time_source_format_iso8601(char *buf, int len)
{
    if (!s_valid) {
        return 0;
    }
    return time_source_format_epoch_iso8601(time_source_now_utc(), buf, len);
}

int time_source_format_local_hm(char *buf, int len)
{
    if (!s_valid || buf == NULL || len < 6) {
        return 0;
    }
    int Y, Mo, D, h, mi, s;
    epoch_to_parts(time_source_now_utc() + TIME_SOURCE_LOCAL_OFFSET_SEC,
                   &Y, &Mo, &D, &h, &mi, &s);
    return snprintf(buf, (size_t)len, "%02d:%02d", h, mi);
}

int time_source_format_local_date(char *buf, int len)
{
    if (!s_valid || buf == NULL || len < 11) {
        return 0;
    }
    int Y, Mo, D, h, mi, s;
    epoch_to_parts(time_source_now_utc() + TIME_SOURCE_LOCAL_OFFSET_SEC,
                   &Y, &Mo, &D, &h, &mi, &s);
    return snprintf(buf, (size_t)len, "%02d/%02d/%04d", D, Mo, Y);
}
