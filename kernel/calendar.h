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

/* Maximum number of date notes tracked at once */
#define CALENDAR_MAX_NOTES  32

/* A single date note: year/month/day → VFS paths */
typedef struct {
    int  year;
    int  month;  /* 1-12 */
    int  day;    /* 1-31 */
    char path[128];     /* ramfs temp path:  /notes/YYYY-MM-DD.txt  */
    char persist[16];   /* FAT16 8.3 name:   N_MMDD.TXT             */
    bool used;
    bool saved;         /* true once saved to persistent storage    */
} calendar_note_t;

typedef struct {
    int view_month;   /* 1-12: month currently being viewed */
    int view_year;    /* Year currently being viewed */
    int today_day;    /* Actual current day (for highlighting) */
    int today_month;  /* Actual current month */
    int today_year;   /* Actual current year */
    bool visible;     /* Whether the popup is shown */

    /* Date notes */
    calendar_note_t notes[CALENDAR_MAX_NOTES];
} calendar_state_t;

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

/**
 * calendar_prev_month - Navigate to previous month
 */
void calendar_prev_month(calendar_state_t *cal);

/**
 * calendar_next_month - Navigate to next month
 */
void calendar_next_month(calendar_state_t *cal);

/**
 * calendar_has_note - Check if a date has a note
 * @return: pointer to the note entry, or NULL
 */
calendar_note_t *calendar_has_note(calendar_state_t *cal,
                                   int year, int month, int day);

/**
 * calendar_create_note - Create a note file for a date
 *
 * Creates /notes/YYYY-MM-DD.txt via VFS (mkdir /notes if needed)
 * and records it in the calendar state.
 * @return: pointer to the note entry, or NULL on failure
 */
calendar_note_t *calendar_create_note(calendar_state_t *cal,
                                      int year, int month, int day);

/**
 * calendar_delete_note - Delete a note file for a date
 *
 * Removes the VFS file and clears the calendar entry.
 * @return: 0 on success, negative on error
 */
int calendar_delete_note(calendar_state_t *cal,
                         int year, int month, int day);

/**
 * calendar_build_note_path - Build the ramfs temp path for a date note
 */
void calendar_build_note_path(int year, int month, int day,
                              char *buf, int bufsize);

/**
 * calendar_build_persist_name - Build FAT16 8.3 filename "N_MMDD.TXT"
 */
void calendar_build_persist_name(int month, int day,
                                 char *buf, int bufsize);

/**
 * calendar_scan_notes - Scan /home/ for existing persistent note files
 *
 * Called when the calendar popup opens to discover notes saved
 * to FAT16 on previous sessions.
 */
void calendar_scan_notes(calendar_state_t *cal);

/**
 * calendar_mark_saved - Mark a note as persisted (dot shows)
 */
void calendar_mark_saved(calendar_state_t *cal,
                         int year, int month, int day);

#endif
