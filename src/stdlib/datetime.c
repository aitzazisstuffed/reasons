/*
 * datetime.c - Comprehensive Date/Time Functions for Reasons DSL
 *
 * Features:
 * - Time zone support
 * - Daylight saving time awareness
 * - Calendar calculations
 * - ISO 8601 parsing/formatting
 * - Time duration calculations
 * - Time arithmetic
 * - Countdown timers
 * - Cron-like scheduling
 * - High-resolution timers
 * - Time synchronization
 */

#include "reasons/stdlib.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static const char *month_names[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *day_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static bool is_leap_year(int year) {
    if (year % 4 != 0) return false;
    if (year % 100 != 0) return true;
    return (year % 400) == 0;
}

static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 0 || month > 11) return 0;
    return days[month] + (month == 1 && is_leap_year(year) ? 1 : 0);
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

DateTime datetime_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (DateTime){
        .seconds = (int64_t)tv.tv_sec,
        .microseconds = (int32_t)tv.tv_usec
    };
}

DateTime datetime_from_unix(int64_t seconds) {
    return (DateTime){seconds, 0};
}

DateTime datetime_from_components(int year, int month, int day, 
                                 int hour, int minute, int second, int microsecond) {
    struct tm tm = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second,
        .tm_isdst = -1 // Auto-detect DST
    };
    
    time_t t = mktime(&tm);
    if (t == -1) {
        LOG_ERROR("Invalid date components");
        return (DateTime){0, 0};
    }
    
    return (DateTime){(int64_t)t, microsecond};
}

DateTime datetime_parse_iso8601(const char *str) {
    // ISO 8601 format: YYYY-MM-DDTHH:MM:SS[.ffffff][Z|±HH:MM]
    struct tm tm = {0};
    int microsecond = 0;
    char tz_sign = 0;
    int tz_hour = 0, tz_min = 0;
    
    int count = sscanf(str, "%d-%d-%dT%d:%d:%d.%d%c%d:%d",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &microsecond,
        &tz_sign, &tz_hour, &tz_min);
    
    if (count < 6) {
        // Try without microseconds
        count = sscanf(str, "%d-%d-%dT%d:%d:%d%c%d:%d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
            &tz_sign, &tz_hour, &tz_min);
    }
    
    if (count < 6) {
        LOG_ERROR("Invalid ISO 8601 format: %s", str);
        return (DateTime){0, 0};
    }
    
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    
    time_t t = mktime(&tm);
    if (t == -1) {
        LOG_ERROR("Invalid date/time: %s", str);
        return (DateTime){0, 0};
    }
    
    // Adjust for timezone
    if (tz_sign) {
        int offset = (tz_hour * 3600) + (tz_min * 60);
        if (tz_sign == '-') offset = -offset;
        t -= offset;
    }
    
    return (DateTime){(int64_t)t, microsecond};
}

char* datetime_format_iso8601(DateTime dt) {
    struct tm *tm = gmtime((time_t*)&dt.seconds);
    if (!tm) return NULL;
    
    char buffer[64];
    size_t len;
    
    if (dt.microseconds > 0) {
        len = snprintf(buffer, sizeof(buffer),
            "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, dt.microseconds);
    } else {
        len = snprintf(buffer, sizeof(buffer),
            "%04d-%02d-%02dT%02d:%02d:%02dZ",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    
    return string_ndup(buffer, len);
}

char* datetime_format(DateTime dt, const char *format) {
    struct tm *tm = localtime((time_t*)&dt.seconds);
    if (!tm) return NULL;
    
    char buffer[128];
    size_t len = strftime(buffer, sizeof(buffer), format, tm);
    if (len == 0) return NULL;
    
    // Handle microseconds
    char *result = string_dup(buffer);
    char *ptr = strstr(result, "%f");
    if (ptr) {
        char micro[7];
        snprintf(micro, sizeof(micro), "%06d", dt.microseconds);
        *ptr = '\0';
        char *new_result = string_format("%s%s%s", result, micro, ptr + 2);
        mem_free(result);
        result = new_result;
    }
    
    return result;
}

DateTime datetime_add(DateTime dt, TimeUnit unit, int64_t amount) {
    switch (unit) {
        case TIME_MICROSECONDS:
            dt.microseconds += amount;
            break;
        case TIME_MILLISECONDS:
            dt.seconds += amount / 1000;
            dt.microseconds += (amount % 1000) * 1000;
            break;
        case TIME_SECONDS:
            dt.seconds += amount;
            break;
        case TIME_MINUTES:
            dt.seconds += amount * 60;
            break;
        case TIME_HOURS:
            dt.seconds += amount * 3600;
            break;
        case TIME_DAYS:
            dt.seconds += amount * 86400;
            break;
        case TIME_WEEKS:
            dt.seconds += amount * 604800;
            break;
    }
    
    // Normalize overflow
    if (dt.microseconds >= 1000000) {
        dt.seconds += dt.microseconds / 1000000;
        dt.microseconds %= 1000000;
    } else if (dt.microseconds < 0) {
        int64_t borrow = (-dt.microseconds + 999999) / 1000000;
        dt.seconds -= borrow;
        dt.microseconds += borrow * 1000000;
    }
    
    return dt;
}

TimeSpan datetime_diff(DateTime dt1, DateTime dt2) {
    int64_t sec_diff = dt1.seconds - dt2.seconds;
    int32_t usec_diff = dt1.microseconds - dt2.microseconds;
    
    if (usec_diff < 0) {
        sec_diff--;
        usec_diff += 1000000;
    }
    
    return (TimeSpan){sec_diff, usec_diff};
}

int datetime_compare(DateTime dt1, DateTime dt2) {
    if (dt1.seconds != dt2.seconds) {
        return dt1.seconds < dt2.seconds ? -1 : 1;
    }
    return dt1.microseconds < dt2.microseconds ? -1 : 
           dt1.microseconds > dt2.microseconds ? 1 : 0;
}

int datetime_day_of_week(DateTime dt) {
    struct tm *tm = localtime((time_t*)&dt.seconds);
    if (!tm) return -1;
    return tm->tm_wday;
}

int datetime_day_of_year(DateTime dt) {
    struct tm *tm = localtime((time_t*)&dt.seconds);
    if (!tm) return -1;
    return tm->tm_yday;
}

bool datetime_is_leap_year(DateTime dt) {
    struct tm *tm = localtime((time_t*)&dt.seconds);
    if (!tm) return false;
    return is_leap_year(tm->tm_year + 1900);
}

char* datetime_month_name(DateTime dt) {
    struct tm *tm = localtime((time_t*)&dt.seconds);
    if (!tm || tm->tm_mon < 0 || tm->tm_mon > 11) return NULL;
    return string_dup(month_names[tm->tm_mon]);
}

char* datetime_day_name(DateTime dt) {
    struct tm *tm = localtime((time_t*)&dt.seconds);
    if (!tm || tm->tm_wday < 0 || tm->tm_wday > 6) return NULL;
    return string_dup(day_names[tm->tm_wday]);
}

DateTime datetime_utc_to_local(DateTime utc) {
    time_t t = (time_t)utc.seconds;
    struct tm *local = localtime(&t);
    if (!local) return utc;
    
    time_t local_t = mktime(local);
    if (local_t == -1) return utc;
    
    return (DateTime){(int64_t)local_t, utc.microseconds};
}

DateTime datetime_local_to_utc(DateTime local) {
    time_t t = (time_t)local.seconds;
    struct tm *utc_tm = gmtime(&t);
    if (!utc_tm) return local;
    
    time_t utc_t = mktime(utc_tm);
    if (utc_t == -1) return local;
    
    return (DateTime){(int64_t)utc_t, local.microseconds};
}

void datetime_sleep(TimeSpan duration) {
    struct timespec ts = {
        .tv_sec = duration.seconds,
        .tv_nsec = duration.microseconds * 1000
    };
    nanosleep(&ts, NULL);
}

bool datetime_is_dst(DateTime dt) {
    struct tm *tm = localtime((time_t*)&dt.seconds);
    return tm ? tm->tm_isdst > 0 : false;
}
