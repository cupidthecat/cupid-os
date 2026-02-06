/*
 * cupidscript_jobs.c - Background job management for CupidScript
 *
 * Manages background jobs started with & operator.
 * Tracks job status and prints completion notifications.
 */

#include "cupidscript_jobs.h"
#include "process.h"
#include "string.h"
#include "memory.h"
#include "math.h"
#include "../drivers/serial.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Initialize job table
 * ══════════════════════════════════════════════════════════════════════ */
void job_table_init(job_table_t *table) {
    memset(table, 0, sizeof(job_table_t));
    table->job_count = 0;
    table->next_job_id = 1;
    table->last_bg_pid = 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Add a new running job
 * ══════════════════════════════════════════════════════════════════════ */
int job_add(job_table_t *table, uint32_t pid, const char *command) {
    if (table->job_count >= MAX_JOBS) {
        KERROR("CupidScript: too many jobs (max %d)", MAX_JOBS);
        return -1;
    }

    job_t *job = &table->jobs[table->job_count];
    job->pid = pid;
    job->job_id = table->next_job_id++;
    job->state = JOB_RUNNING;
    job->exit_code = 0;

    /* Copy command string */
    int i = 0;
    while (command[i] && i < 255) {
        job->command[i] = command[i];
        i++;
    }
    job->command[i] = '\0';

    table->last_bg_pid = pid;
    table->job_count++;

    KDEBUG("CupidScript: added job [%d] PID %u: %s",
           job->job_id, pid, command);

    return job->job_id;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Check for completed jobs
 * ══════════════════════════════════════════════════════════════════════ */
void job_check_completed(job_table_t *table,
                         void (*print_fn)(const char *)) {
    for (int i = 0; i < table->job_count; i++) {
        job_t *job = &table->jobs[i];
        if (job->state == JOB_RUNNING) {
            /* Check if process is still alive */
            int status = process_get_state(job->pid);
            if (status < 0 || status == 4 /* TERMINATED */) {
                job->state = JOB_DONE;
                job->exit_code = 0;

                if (print_fn) {
                    /* Print completion: [1]+  Done  command */
                    char buf[320];
                    int p = 0;
                    buf[p++] = '[';
                    /* int to string for job_id */
                    char tmp[8];
                    int ti = 0;
                    int jid = job->job_id;
                    if (jid == 0) { tmp[ti++] = '0'; }
                    else {
                        while (jid > 0 && ti < 7) {
                            tmp[ti++] = (char)('0' + (jid % 10));
                            jid /= 10;
                        }
                    }
                    while (ti > 0) buf[p++] = tmp[--ti];
                    buf[p++] = ']';
                    buf[p++] = '+';
                    buf[p++] = ' ';
                    buf[p++] = ' ';

                    /* "Done" */
                    const char *done = "Done                    ";
                    int di = 0;
                    while (done[di]) buf[p++] = done[di++];

                    /* command */
                    int ci = 0;
                    while (job->command[ci] && p < 318) {
                        buf[p++] = job->command[ci++];
                    }
                    buf[p++] = '\n';
                    buf[p] = '\0';

                    print_fn(buf);
                }
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  List all active jobs
 * ══════════════════════════════════════════════════════════════════════ */
void job_list(job_table_t *table, bool show_pids,
              void (*print_fn)(const char *)) {
    if (!print_fn) return;

    for (int i = 0; i < table->job_count; i++) {
        job_t *job = &table->jobs[i];
        if (job->state == JOB_DONE) continue;

        char buf[320];
        int p = 0;

        /* [job_id] */
        buf[p++] = '[';
        char tmp[8];
        int ti = 0;
        int jid = job->job_id;
        if (jid == 0) { tmp[ti++] = '0'; }
        else {
            while (jid > 0 && ti < 7) {
                tmp[ti++] = (char)('0' + (jid % 10));
                jid /= 10;
            }
        }
        while (ti > 0) buf[p++] = tmp[--ti];
        buf[p++] = ']';

        /* Optional PID */
        if (show_pids) {
            buf[p++] = ' ';
            buf[p++] = ' ';
            ti = 0;
            uint32_t pid = job->pid;
            if (pid == 0) { tmp[ti++] = '0'; }
            else {
                while (pid > 0 && ti < 7) {
                    tmp[ti++] = (char)('0' + (pid % 10));
                    pid /= 10;
                }
            }
            while (ti > 0) buf[p++] = tmp[--ti];
        }

        /* State */
        buf[p++] = ' ';
        buf[p++] = ' ';
        const char *state_str = (job->state == JOB_RUNNING) ? "Running"
                              : (job->state == JOB_STOPPED) ? "Stopped"
                              : "Done";
        int si = 0;
        while (state_str[si]) buf[p++] = state_str[si++];

        /* Padding + command */
        for (int pad = 0; pad < 17 - si; pad++) buf[p++] = ' ';
        int ci = 0;
        while (job->command[ci] && p < 318) {
            buf[p++] = job->command[ci++];
        }
        buf[p++] = '\n';
        buf[p] = '\0';

        print_fn(buf);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Find job by ID or PID
 * ══════════════════════════════════════════════════════════════════════ */

job_t *job_find_by_id(job_table_t *table, int job_id) {
    for (int i = 0; i < table->job_count; i++) {
        if (table->jobs[i].job_id == job_id) {
            return &table->jobs[i];
        }
    }
    return NULL;
}

job_t *job_find_by_pid(job_table_t *table, uint32_t pid) {
    for (int i = 0; i < table->job_count; i++) {
        if (table->jobs[i].pid == pid) {
            return &table->jobs[i];
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Parse job spec: %1 → 1, %2 → 2, etc.
 * ══════════════════════════════════════════════════════════════════════ */
int job_parse_spec(const char *spec) {
    if (!spec || spec[0] != '%') return -1;

    int val = 0;
    int i = 1;
    while (spec[i] >= '0' && spec[i] <= '9') {
        val = val * 10 + (spec[i] - '0');
        i++;
    }
    return val;
}
