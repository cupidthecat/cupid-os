/**
 * calendar.h - Calendar Math & Time/Date Formatting
 *
 * Provides calendar calculations (leap year, days in month,
 * first weekday) and time/date formatting for display in the
 * taskbar clock and calendar popup.
 */

#ifndef CALENDAR_H
#define CALENDAR_H

#include "types.h"
#include "../drivers/rtc.h"

/* ── Calendar popup state ─────────────────────────────────────────── */
typedef struct {
    int view_month;   /* 1-12: month currently being viewed */
    int view_year;    /* Year currently being viewed */
    int today_day;    /* Actual current day (for highlighting) */
    int today_month;  /* Actual current month */
    int today_year;   /* Actual current year */
    bool visible;     /* Whether the popup is shown */
} calendar_state_t;

/* ── Formatting functions ─────────────────────────────────────────── */

/**
 * format_time_12hr - Format time as "H:MM AM/PM"
 *
 * @param time:    Pointer to RTC time (24-hour format)
 * @param buf:     Output buffer
 * @param bufsize: Size of output buffer
 */
void format_time_12hr(const rtc_time_t *time, char *buf, int bufsize);

/**
 * format_time_12hr_sec - Format time as "H:MM:SS AM/PM"
 *
 * @param time:    Pointer to RTC time (24-hour format)
 * @param buf:     Output buffer
 * @param bufsize: Size of output buffer
 */
void format_time_12hr_sec(const rtc_time_t *time, char *buf, int bufsize);

/**
 * format_date_short - Format date as "Feb 6"
 *
 * @param date:    Pointer to RTC date
 * @param buf:     Output buffer
 * @param bufsize: Size of output buffer
 */
void format_date_short(const rtc_date_t *date, char *buf, int bufsize);

/**
 * format_date_full - Format date as "Thursday, February 6, 2026"
 *
 * @param date:    Pointer to RTC date
 * @param buf:     Output buffer
 * @param bufsize: Size of output buffer
 */
void format_date_full(const rtc_date_t *date, char *buf, int bufsize);

/* ── Name lookup ──────────────────────────────────────────────────── */

/**
 * get_month_abbr - Get abbreviated month name (e.g. "Feb")
 * @param month: 1-12
 */
const char *get_month_abbr(uint8_t month);

/**
 * get_month_full - Get full month name (e.g. "February")
 * @param month: 1-12
 */
const char *get_month_full(uint8_t month);

/**
 * get_weekday_name - Get full weekday name (e.g. "Thursday")
 * @param weekday: 0=Sunday, 6=Saturday
 */
const char *get_weekday_name(uint8_t weekday);

/* ── Calendar math ────────────────────────────────────────────────── */

/**
 * is_leap_year - Check if a year is a leap year
 */
bool is_leap_year(int year);

/**
 * get_days_in_month - Get number of days in a month (handles leap years)
 * @param month: 1-12
 * @param year:  Full year
 */
int get_days_in_month(int month, int year);

/**
 * get_first_weekday - Get the weekday of the 1st of a given month
 * @param month: 1-12
 * @param year:  Full year
 * @return: 0=Sunday, 6=Saturday
 */
int get_first_weekday(int month, int year);

/* ── Calendar popup navigation ────────────────────────────────────── */

/**
 * calendar_prev_month - Navigate to previous month
 */
void calendar_prev_month(calendar_state_t *cal);

/**
 * calendar_next_month - Navigate to next month
 */
void calendar_next_month(calendar_state_t *cal);

#endif
