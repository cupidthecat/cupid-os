/**
 * calendar.c - Calendar Math & Time/Date Formatting
 *
 * Implements calendar calculations and time/date string formatting
 * for the taskbar clock and interactive calendar popup.
 */

#include "calendar.h"
#include "string.h"
#include "vfs.h"

/* Use 2D char arrays (not pointer arrays) so the string data is stored
 * inline in .rodata as one contiguous block, avoiding linker issues
 * where pointer targets in .rodata.str1.1 subsections get dropped. */

static const char month_abbr[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char month_full[][10] = {
    "January",  "February", "March",    "April",
    "May",      "June",     "July",     "August",
    "September","October",  "November", "December"
};

static const char weekday_names[][10] = {
    "Sunday",   "Monday",   "Tuesday",  "Wednesday",
    "Thursday", "Friday",   "Saturday"
};

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

/**
 * calendar_build_note_path - Build "/notes/YYYY-MM-DD.txt" (ramfs temp)
 */
void calendar_build_note_path(int year, int month, int day,
                              char *buf, int bufsize) {
    if (bufsize < 24) { buf[0] = '\0'; return; }

    int pos = 0;
    /* "/notes/" */
    const char *prefix = "/notes/";
    while (*prefix && pos < bufsize - 1) buf[pos++] = *prefix++;

    /* Year (4 digits) */
    buf[pos++] = (char)('0' + (year / 1000) % 10);
    buf[pos++] = (char)('0' + (year / 100) % 10);
    buf[pos++] = (char)('0' + (year / 10) % 10);
    buf[pos++] = (char)('0' + year % 10);
    buf[pos++] = '-';

    /* Month (2 digits) */
    buf[pos++] = (char)('0' + (month / 10));
    buf[pos++] = (char)('0' + (month % 10));
    buf[pos++] = '-';

    /* Day (2 digits) */
    buf[pos++] = (char)('0' + (day / 10));
    buf[pos++] = (char)('0' + (day % 10));

    /* ".txt" */
    buf[pos++] = '.';
    buf[pos++] = 't';
    buf[pos++] = 'x';
    buf[pos++] = 't';
    buf[pos] = '\0';
}

/**
 * calendar_build_persist_name - Build FAT16 8.3 filename "n_mmdd.txt"
 *
 * FAT16 is root-directory-only so we use a flat naming scheme.
 * Names are lowercase to match what fat16_enumerate_root returns.
 * The full persistent VFS path is "/home/n_mmdd.txt".
 */
void calendar_build_persist_name(int month, int day,
                                 char *buf, int bufsize) {
    if (bufsize < 12) { buf[0] = '\0'; return; }
    int pos = 0;
    buf[pos++] = 'n';
    buf[pos++] = '_';
    buf[pos++] = (char)('0' + (month / 10));
    buf[pos++] = (char)('0' + (month % 10));
    buf[pos++] = (char)('0' + (day / 10));
    buf[pos++] = (char)('0' + (day % 10));
    buf[pos++] = '.';
    buf[pos++] = 't';
    buf[pos++] = 'x';
    buf[pos++] = 't';
    buf[pos] = '\0';
}

calendar_note_t *calendar_has_note(calendar_state_t *cal,
                                   int year, int month, int day) {
    for (int i = 0; i < CALENDAR_MAX_NOTES; i++) {
        if (cal->notes[i].used &&
            cal->notes[i].year == year &&
            cal->notes[i].month == month &&
            cal->notes[i].day == day) {
            return &cal->notes[i];
        }
    }
    return (calendar_note_t *)0;
}

calendar_note_t *calendar_create_note(calendar_state_t *cal,
                                      int year, int month, int day) {
    /* Already exists? */
    calendar_note_t *existing = calendar_has_note(cal, year, month, day);
    if (existing) return existing;

    /* Find a free slot */
    calendar_note_t *slot = (calendar_note_t *)0;
    for (int i = 0; i < CALENDAR_MAX_NOTES; i++) {
        if (!cal->notes[i].used) {
            slot = &cal->notes[i];
            break;
        }
    }
    if (!slot) return (calendar_note_t *)0; /* Full */

    /* Build temp path in ramfs */
    char path[128];
    calendar_build_note_path(year, month, day, path, (int)sizeof(path));

    /* Ensure /notes directory exists in ramfs */
    vfs_mkdir("/notes");

    /* Create the temp file with an initial header line */
    int fd = vfs_open(path, O_WRONLY | O_CREAT);
    if (fd < 0) return (calendar_note_t *)0;

    /* Write a header */
    char header[64];
    int hpos = 0;
    const char *pfx = "Note for ";
    while (*pfx) header[hpos++] = *pfx++;

    /* Month name */
    const char *mname = get_month_full((uint8_t)month);
    while (*mname) header[hpos++] = *mname++;
    header[hpos++] = ' ';

    /* Day */
    if (day >= 10) header[hpos++] = (char)('0' + (day / 10));
    header[hpos++] = (char)('0' + (day % 10));
    header[hpos++] = ',';
    header[hpos++] = ' ';

    /* Year */
    header[hpos++] = (char)('0' + (year / 1000) % 10);
    header[hpos++] = (char)('0' + (year / 100) % 10);
    header[hpos++] = (char)('0' + (year / 10) % 10);
    header[hpos++] = (char)('0' + year % 10);
    header[hpos++] = '\n';
    header[hpos] = '\0';

    vfs_write(fd, header, (uint32_t)hpos);
    vfs_close(fd);

    /* Record in calendar state */
    slot->year  = year;
    slot->month = month;
    slot->day   = day;
    slot->used  = true;
    slot->saved = false; /* Not yet persisted - no dot */

    /* Store ramfs temp path */
    int k = 0;
    while (path[k] && k < 127) { slot->path[k] = path[k]; k++; }
    slot->path[k] = '\0';

    /* Store FAT16 8.3 persistent name */
    calendar_build_persist_name(month, day,
                                slot->persist, (int)sizeof(slot->persist));

    return slot;
}

int calendar_delete_note(calendar_state_t *cal,
                         int year, int month, int day) {
    calendar_note_t *note = calendar_has_note(cal, year, month, day);
    if (!note) return -1;

    /* Delete from ramfs (temp) */
    vfs_unlink(note->path);

    /* Delete from FAT16 (persistent) if it was saved */
    if (note->saved && note->persist[0]) {
        char persist_path[128];
        int p = 0;
        const char *pfx = "/home/";
        while (*pfx) persist_path[p++] = *pfx++;
        int k = 0;
        while (note->persist[k] && p < 127) persist_path[p++] = note->persist[k++];
        persist_path[p] = '\0';
        vfs_unlink(persist_path);
    }

    /* Clear the slot */
    note->used = false;
    note->saved = false;
    note->year = 0;
    note->month = 0;
    note->day = 0;
    note->path[0] = '\0';
    note->persist[0] = '\0';

    return 0;
}

void calendar_mark_saved(calendar_state_t *cal,
                         int year, int month, int day) {
    calendar_note_t *note = calendar_has_note(cal, year, month, day);
    if (note) {
        note->saved = true;
    }
}

/**
 * calendar_scan_notes - Scan /home/ for existing n_mmdd.txt files
 *
 * Called when the calendar popup is opened to discover notes that
 * were previously saved to persistent FAT16 storage.
 * FAT16 readdir returns lowercase names.
 */
void calendar_scan_notes(calendar_state_t *cal) {
    int fd = vfs_open("/home", O_RDONLY);
    if (fd < 0) return;

    vfs_dirent_t ent;
    while (vfs_readdir(fd, &ent) > 0) {
        /* Match pattern: n_mmdd.txt (lowercase from FAT16 readdir) */
        if (ent.name[0] != 'n' || ent.name[1] != '_') continue;
        if (ent.name[6] != '.' || ent.name[10] != '\0') continue;
        if (ent.name[7] != 't' || ent.name[8] != 'x' || ent.name[9] != 't')
            continue;

        /* Parse month and day */
        int m = (ent.name[2] - '0') * 10 + (ent.name[3] - '0');
        int d = (ent.name[4] - '0') * 10 + (ent.name[5] - '0');
        if (m < 1 || m > 12 || d < 1 || d > 31) continue;

        /* Use the calendar's current view year for scanned notes */
        int y = cal->view_year;

        /* Already tracked? */
        if (calendar_has_note(cal, y, m, d)) continue;

        /* Find a free slot and register it */
        for (int i = 0; i < CALENDAR_MAX_NOTES; i++) {
            if (!cal->notes[i].used) {
                cal->notes[i].year  = y;
                cal->notes[i].month = m;
                cal->notes[i].day   = d;
                cal->notes[i].used  = true;
                cal->notes[i].saved = true; /* Already on disk */

                /* Build ramfs temp path */
                calendar_build_note_path(y, m, d,
                    cal->notes[i].path,
                    (int)sizeof(cal->notes[i].path));

                /* Copy the 8.3 name */
                int k = 0;
                while (ent.name[k] && k < 15) {
                    cal->notes[i].persist[k] = ent.name[k];
                    k++;
                }
                cal->notes[i].persist[k] = '\0';
                break;
            }
        }
    }
    vfs_close(fd);
}
