/**
 * rtc.c - Real-Time Clock (RTC) Driver
 *
 * Reads time and date from the CMOS Real-Time Clock hardware via
 * I/O ports 0x70 (index) and 0x71 (data).
 *
 * CMOS Register Map:
 *   0x00 - Seconds       0x04 - Hours        0x08 - Month
 *   0x02 - Minutes       0x07 - Day of month  0x09 - Year
 *   0x0A - Status Reg A  0x0B - Status Reg B
 *
 * Handles BCD-to-binary conversion and NMI masking during reads.
 */

#include "rtc.h"
#include "../kernel/ports.h"
#include "../kernel/kernel.h"
#include "../drivers/serial.h"

/* ── CMOS Ports ───────────────────────────────────────────────────── */
#define CMOS_INDEX  0x70
#define CMOS_DATA   0x71

/* ── CMOS Registers ───────────────────────────────────────────────── */
#define RTC_REG_SECONDS  0x00
#define RTC_REG_MINUTES  0x02
#define RTC_REG_HOURS    0x04
#define RTC_REG_DAY      0x07
#define RTC_REG_MONTH    0x08
#define RTC_REG_YEAR     0x09
#define RTC_REG_STATUS_A 0x0A
#define RTC_REG_STATUS_B 0x0B

/* ── Century register (if available) ──────────────────────────────── */
#define RTC_REG_CENTURY  0x32

/* ── Internal helpers ─────────────────────────────────────────────── */

/**
 * cmos_read - Read a single CMOS register
 *
 * Disables NMI by setting bit 7 of the index port, then reads
 * the data register.
 *
 * @param reg: CMOS register index (0x00-0x3F)
 * @return: Value read from the register
 */
static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_INDEX, (uint8_t)(0x80 | reg));  /* Disable NMI + select register */
    return inb(CMOS_DATA);
}

/**
 * bcd_to_bin - Convert BCD-encoded value to binary
 *
 * BCD format: upper nibble = tens digit, lower nibble = ones digit.
 * Example: 0x59 → 59
 *
 * @param bcd: BCD-encoded value
 * @return: Binary value
 */
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

/**
 * rtc_is_update_in_progress - Check if an RTC update is in progress
 *
 * Status Register A bit 7 indicates an update cycle is happening.
 * We should not read time/date during an update.
 *
 * @return: true if update is in progress
 */
static bool rtc_is_update_in_progress(void) {
    return (cmos_read(RTC_REG_STATUS_A) & 0x80) != 0;
}

/**
 * get_weekday - Calculate day of week using Zeller's congruence
 *
 * Returns 0=Sunday, 1=Monday, ..., 6=Saturday
 *
 * @param day:   Day of month (1-31)
 * @param month: Month (1-12)
 * @param year:  Full year (e.g. 2026)
 * @return: Day of week (0=Sunday)
 */
static uint8_t get_weekday(uint8_t day, uint8_t month, uint16_t year) {
    int m = (int)month;
    int y = (int)year;

    /* Zeller's congruence: adjust Jan/Feb to be months 13/14 of prev year */
    if (m < 3) {
        m += 12;
        y--;
    }

    int k = y % 100;
    int j = y / 100;
    int h = ((int)day + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    /* Convert from Zeller result (0=Saturday) to 0=Sunday */
    int dow = ((h + 6) % 7);
    if (dow < 0) dow += 7;
    return (uint8_t)dow;
}

/* ── Public API ───────────────────────────────────────────────────── */

void rtc_init(void) {
    rtc_time_t time;
    rtc_date_t date;

    rtc_read_time(&time);
    rtc_read_date(&date);

    if (rtc_validate_time(&time) && rtc_validate_date(&date)) {
        KINFO("RTC: %u-%02u-%02u %02u:%02u:%02u",
              (unsigned)date.year, (unsigned)date.month, (unsigned)date.day,
              (unsigned)time.hour, (unsigned)time.minute, (unsigned)time.second);
    } else {
        KWARN("RTC: invalid data (time or date out of range)");
    }
}

void rtc_read_time(rtc_time_t *time) {
    uint8_t sec, min, hr;
    uint8_t last_sec, last_min, last_hr;
    uint8_t status_b;

    /* Wait for any in-progress update to finish */
    while (rtc_is_update_in_progress())
        ;

    /* Read the time registers */
    sec = cmos_read(RTC_REG_SECONDS);
    min = cmos_read(RTC_REG_MINUTES);
    hr  = cmos_read(RTC_REG_HOURS);

    /* Read again until we get two consecutive identical reads
     * to ensure we didn't read during an update boundary */
    do {
        last_sec = sec;
        last_min = min;
        last_hr  = hr;

        while (rtc_is_update_in_progress())
            ;

        sec = cmos_read(RTC_REG_SECONDS);
        min = cmos_read(RTC_REG_MINUTES);
        hr  = cmos_read(RTC_REG_HOURS);
    } while (sec != last_sec || min != last_min || hr != last_hr);

    /* Check if values are in BCD mode (Status Register B, bit 2) */
    status_b = cmos_read(RTC_REG_STATUS_B);

    if (!(status_b & 0x04)) {
        /* BCD mode — convert to binary */
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr  = bcd_to_bin((uint8_t)(hr & 0x7F));  /* Mask out AM/PM bit */
    } else {
        hr = (uint8_t)(hr & 0x7F);
    }

    /* Handle 12-hour mode (Status Register B bit 1) */
    if (!(status_b & 0x02)) {
        /* 12-hour mode: bit 7 of hour register = PM */
        uint8_t raw_hr = cmos_read(RTC_REG_HOURS);
        bool pm = (raw_hr & 0x80) != 0;
        if (hr == 12) {
            hr = pm ? 12 : 0;
        } else if (pm) {
            hr = (uint8_t)(hr + 12);
        }
    }

    time->second = sec;
    time->minute = min;
    time->hour   = hr;
}

void rtc_read_date(rtc_date_t *date) {
    uint8_t day, mon, yr;
    uint8_t last_day, last_mon, last_yr;
    uint8_t status_b;
    uint8_t century = 20; /* Default century */

    /* Wait for any in-progress update to finish */
    while (rtc_is_update_in_progress())
        ;

    /* Read date registers */
    day = cmos_read(RTC_REG_DAY);
    mon = cmos_read(RTC_REG_MONTH);
    yr  = cmos_read(RTC_REG_YEAR);

    /* Double-read for consistency */
    do {
        last_day = day;
        last_mon = mon;
        last_yr  = yr;

        while (rtc_is_update_in_progress())
            ;

        day = cmos_read(RTC_REG_DAY);
        mon = cmos_read(RTC_REG_MONTH);
        yr  = cmos_read(RTC_REG_YEAR);
    } while (day != last_day || mon != last_mon || yr != last_yr);

    /* Check BCD mode */
    status_b = cmos_read(RTC_REG_STATUS_B);

    if (!(status_b & 0x04)) {
        /* BCD mode — convert to binary */
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        yr  = bcd_to_bin(yr);
    }

    /* Try to read century register (not always reliable) */
    {
        uint8_t cent_raw = cmos_read(RTC_REG_CENTURY);
        if (!(status_b & 0x04)) {
            cent_raw = bcd_to_bin(cent_raw);
        }
        if (cent_raw >= 19 && cent_raw <= 25) {
            century = cent_raw;
        }
    }

    date->day   = day;
    date->month = mon;
    date->year  = (uint16_t)(century * 100 + yr);

    /* Calculate weekday */
    date->weekday = get_weekday(day, mon, date->year);
}

bool rtc_validate_time(const rtc_time_t *time) {
    return (time->hour < 24 &&
            time->minute < 60 &&
            time->second < 60);
}

bool rtc_validate_date(const rtc_date_t *date) {
    static const uint8_t days_in_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (date->month < 1 || date->month > 12) return false;
    if (date->day < 1)   return false;
    if (date->year < 1970 || date->year > 2099) return false;

    uint8_t max_days = days_in_month[date->month - 1];
    /* Leap year check for February */
    if (date->month == 2) {
        bool leap = (date->year % 4 == 0 &&
                     (date->year % 100 != 0 || date->year % 400 == 0));
        if (leap) max_days = 29;
    }

    return (date->day <= max_days);
}

uint32_t rtc_get_epoch_seconds(void) {
    rtc_time_t time;
    rtc_date_t date;

    rtc_read_time(&time);
    rtc_read_date(&date);

    if (!rtc_validate_time(&time) || !rtc_validate_date(&date)) {
        return 0;
    }

    /* Days from years since 1970 */
    uint32_t days = 0;
    for (uint16_t y = 1970; y < date.year; y++) {
        bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        days += leap ? 366 : 365;
    }

    /* Days from months this year */
    static const uint16_t mdays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    if (date.month >= 1 && date.month <= 12) {
        days += mdays[date.month - 1];
    }
    /* Add leap day if past February in a leap year */
    if (date.month > 2) {
        bool leap = (date.year % 4 == 0 &&
                     (date.year % 100 != 0 || date.year % 400 == 0));
        if (leap) days++;
    }
    days += (uint32_t)(date.day - 1);

    return days * 86400 + (uint32_t)time.hour * 3600 +
           (uint32_t)time.minute * 60 + (uint32_t)time.second;
}
