/**
 * calendar.c - Calendar Math & Time/Date Formatting
 *
 * Implements calendar calculations and time/date string formatting
 * for the taskbar clock and interactive calendar popup.
 */

#include "calendar.h"
#include "string.h"

/* ── Name tables ──────────────────────────────────────────────────── */

static const char *month_abbr[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *month_full[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *weekday_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

/* ── Internal helpers ─────────────────────────────────────────────── */

/**
 * int_to_str - Convert a small unsigned integer to a string
 *
 * Does NOT zero-pad. Writes at most `maxlen` digits.
 * Returns the number of characters written.
 */
static int int_to_str(int val, char *buf, int maxlen) {
    char tmp[12];
    int len = 0;
    if (val < 0) val = 0;
    if (val == 0) {
        tmp[len++] = '0';
    } else {
        while (val > 0 && len < 11) {
            tmp[len++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    /* Reverse into buf */
    int out = 0;
    for (int i = len - 1; i >= 0 && out < maxlen; i--) {
        buf[out++] = tmp[i];
    }
    return out;
}

/**
 * int_to_str_pad2 - Convert integer to zero-padded 2-digit string
 */
static int int_to_str_pad2(int val, char *buf) {
    if (val < 0) val = 0;
    if (val > 99) val = 99;
    buf[0] = (char)('0' + (val / 10));
    buf[1] = (char)('0' + (val % 10));
    return 2;
}

/**
 * str_append - Append a string, return number of characters written
 */
static int str_append(char *buf, int pos, int bufsize, const char *src) {
    int i = 0;
    while (src[i] && pos + i < bufsize - 1) {
        buf[pos + i] = src[i];
        i++;
    }
    return i;
}

/* ── Formatting functions ─────────────────────────────────────────── */

void format_time_12hr(const rtc_time_t *time, char *buf, int bufsize) {
    if (bufsize < 10) { buf[0] = '\0'; return; }

    int hr = (int)time->hour;
    const char *ampm = "AM";
    int display_hr;

    if (hr == 0) {
        display_hr = 12;
        ampm = "AM";
    } else if (hr < 12) {
        display_hr = hr;
        ampm = "AM";
    } else if (hr == 12) {
        display_hr = 12;
        ampm = "PM";
    } else {
        display_hr = hr - 12;
        ampm = "PM";
    }

    int pos = 0;
    pos += int_to_str(display_hr, buf + pos, bufsize - pos);
    buf[pos++] = ':';
    pos += int_to_str_pad2((int)time->minute, buf + pos);
    buf[pos++] = ' ';
    pos += str_append(buf, pos, bufsize, ampm);
    buf[pos] = '\0';
}

void format_time_12hr_sec(const rtc_time_t *time, char *buf, int bufsize) {
    if (bufsize < 14) { buf[0] = '\0'; return; }

    int hr = (int)time->hour;
    const char *ampm = "AM";
    int display_hr;

    if (hr == 0) {
        display_hr = 12;
        ampm = "AM";
    } else if (hr < 12) {
        display_hr = hr;
        ampm = "AM";
    } else if (hr == 12) {
        display_hr = 12;
        ampm = "PM";
    } else {
        display_hr = hr - 12;
        ampm = "PM";
    }

    int pos = 0;
    pos += int_to_str(display_hr, buf + pos, bufsize - pos);
    buf[pos++] = ':';
    pos += int_to_str_pad2((int)time->minute, buf + pos);
    buf[pos++] = ':';
    pos += int_to_str_pad2((int)time->second, buf + pos);
    buf[pos++] = ' ';
    pos += str_append(buf, pos, bufsize, ampm);
    buf[pos] = '\0';
}

void format_date_short(const rtc_date_t *date, char *buf, int bufsize) {
    if (bufsize < 8) { buf[0] = '\0'; return; }

    int pos = 0;
    const char *mon = get_month_abbr(date->month);
    pos += str_append(buf, pos, bufsize, mon);
    buf[pos++] = ' ';
    pos += int_to_str((int)date->day, buf + pos, bufsize - pos);
    buf[pos] = '\0';
}

void format_date_full(const rtc_date_t *date, char *buf, int bufsize) {
    if (bufsize < 40) { buf[0] = '\0'; return; }

    int pos = 0;
    const char *wday = get_weekday_name(date->weekday);
    pos += str_append(buf, pos, bufsize, wday);
    buf[pos++] = ',';
    buf[pos++] = ' ';

    const char *mon = get_month_full(date->month);
    pos += str_append(buf, pos, bufsize, mon);
    buf[pos++] = ' ';

    pos += int_to_str((int)date->day, buf + pos, bufsize - pos);
    buf[pos++] = ',';
    buf[pos++] = ' ';

    pos += int_to_str((int)date->year, buf + pos, bufsize - pos);
    buf[pos] = '\0';
}

/* ── Name lookup ──────────────────────────────────────────────────── */

const char *get_month_abbr(uint8_t month) {
    if (month < 1 || month > 12) return "???";
    return month_abbr[month - 1];
}

const char *get_month_full(uint8_t month) {
    if (month < 1 || month > 12) return "Unknown";
    return month_full[month - 1];
}

const char *get_weekday_name(uint8_t weekday) {
    if (weekday > 6) return "Unknown";
    return weekday_names[weekday];
}

/* ── Calendar math ────────────────────────────────────────────────── */

bool is_leap_year(int year) {
    if (year % 400 == 0) return true;
    if (year % 100 == 0) return false;
    if (year % 4 == 0)   return true;
    return false;
}

int get_days_in_month(int month, int year) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

int get_first_weekday(int month, int year) {
    /* Zeller's congruence for the 1st of the month */
    int m = month;
    int y = year;

    if (m < 3) {
        m += 12;
        y--;
    }

    int k = y % 100;
    int j = y / 100;
    int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    /* Convert from Zeller result (0=Saturday) to 0=Sunday */
    int day = ((h + 6) % 7);
    if (day < 0) day += 7;
    return day;
}

/* ── Calendar popup navigation ────────────────────────────────────── */

void calendar_prev_month(calendar_state_t *cal) {
    cal->view_month--;
    if (cal->view_month < 1) {
        cal->view_month = 12;
        cal->view_year--;
    }
}

void calendar_next_month(calendar_state_t *cal) {
    cal->view_month++;
    if (cal->view_month > 12) {
        cal->view_month = 1;
        cal->view_year++;
    }
}
