/**
 * rtc.h - Real-Time Clock (RTC) Driver Interface
 *
 * Provides access to the CMOS Real-Time Clock hardware for reading
 * the current time and date. The RTC is accessed through CMOS ports
 * 0x70 (index) and 0x71 (data).
 */

#ifndef RTC_H
#define RTC_H

#include "../kernel/types.h"

/* ── Data structures ──────────────────────────────────────────────── */

typedef struct {
    uint8_t second;  /* 0-59 */
    uint8_t minute;  /* 0-59 */
    uint8_t hour;    /* 0-23 (24-hour format internally) */
} rtc_time_t;

typedef struct {
    uint8_t  day;     /* 1-31 */
    uint8_t  month;   /* 1-12 */
    uint16_t year;    /* Full year (e.g. 2026) */
    uint8_t  weekday; /* 0=Sunday, 6=Saturday */
} rtc_date_t;

/* ── Public API ───────────────────────────────────────────────────── */

/**
 * rtc_init - Initialize the RTC driver
 *
 * Performs a test read to verify the RTC is present and functional.
 */
void rtc_init(void);

/**
 * rtc_read_time - Read current time from CMOS RTC
 *
 * @param time: Pointer to rtc_time_t to fill with current time
 */
void rtc_read_time(rtc_time_t *time);

/**
 * rtc_read_date - Read current date from CMOS RTC
 *
 * Reads day, month, year and calculates the weekday.
 *
 * @param date: Pointer to rtc_date_t to fill with current date
 */
void rtc_read_date(rtc_date_t *date);

/**
 * rtc_validate_time - Check if an rtc_time_t has valid values
 *
 * @param time: Pointer to time structure to validate
 * @return: true if valid, false otherwise
 */
bool rtc_validate_time(const rtc_time_t *time);

/**
 * rtc_validate_date - Check if an rtc_date_t has valid values
 *
 * @param date: Pointer to date structure to validate
 * @return: true if valid, false otherwise
 */
bool rtc_validate_date(const rtc_date_t *date);

#endif
