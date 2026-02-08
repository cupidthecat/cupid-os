/*
 * cupidscript_jobs.h - Background job management for CupidScript
 */
#ifndef CUPIDSCRIPT_JOBS_H
#define CUPIDSCRIPT_JOBS_H

#include "types.h"

/* Forward declaration */
typedef struct script_context script_context_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Constants
 * ══════════════════════════════════════════════════════════════════════ */
#define MAX_JOBS 8

/* ══════════════════════════════════════════════════════════════════════
 *  Job state
 * ══════════════════════════════════════════════════════════════════════ */
typedef enum {
    JOB_NONE,
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Job entry
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    uint32_t    pid;            /* Process ID */
    int         job_id;         /* Job number for %1, %2 etc. */
    char        command[256];   /* Command string for display */
    job_state_t state;
    int         exit_code;
} job_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Job table
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    job_t    jobs[MAX_JOBS];
    int      job_count;
    int      next_job_id;
    uint32_t last_bg_pid;     /* $! variable */
} job_table_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════ */

/* Initialize job table */
void job_table_init(job_table_t *table);

/* Add a new running job */
int job_add(job_table_t *table, uint32_t pid, const char *command);

/* Check for completed jobs and print notifications */
void job_check_completed(job_table_t *table,
                         void (*print_fn)(const char *));

/* List all active jobs */
void job_list(job_table_t *table, bool show_pids,
              void (*print_fn)(const char *));

/* Find job by job_id */
job_t *job_find_by_id(job_table_t *table, int job_id);

/* Find job by PID */
job_t *job_find_by_pid(job_table_t *table, uint32_t pid);

/* Parse job spec (%1 → 1) */
int job_parse_spec(const char *spec);

#endif /* CUPIDSCRIPT_JOBS_H */
