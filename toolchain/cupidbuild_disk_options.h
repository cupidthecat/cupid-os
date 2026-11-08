#ifndef CUPIDBUILD_DISK_OPTIONS_H
#define CUPIDBUILD_DISK_OPTIONS_H

#include "cupidbuild_disk_integer.h"

typedef struct {
  const char *data;
  size_t size;
} cupidbuild_disk_argument_t;

typedef struct {
  cupidbuild_disk_argument_t source;
  cupidbuild_disk_argument_t destination;
} cupidbuild_disk_argument_stage_t;

typedef struct {
  cupidbuild_disk_argument_t seed_manifest;
  cupidbuild_disk_argument_t image;
  cupidbuild_disk_argument_t bootloader;
  cupidbuild_disk_argument_t kernel;
  cupidbuild_disk_integer_t hdd_mb;
  cupidbuild_disk_integer_t fat_start_lba;
  const cupidbuild_disk_argument_stage_t *stages;
  size_t stage_count;
  unsigned int force_format;
} cupidbuild_disk_options_view_t;

typedef struct cupidbuild_disk_options cupidbuild_disk_options_t;

typedef enum {
  CUPIDBUILD_DISK_OPTIONS_OK = 0,
  CUPIDBUILD_DISK_OPTIONS_HELP = 1,
  CUPIDBUILD_DISK_OPTIONS_SYNTAX = 2,
  CUPIDBUILD_DISK_OPTIONS_ARGUMENT = 3,
  CUPIDBUILD_DISK_OPTIONS_MEMORY = 4,
  CUPIDBUILD_DISK_OPTIONS_CAPACITY = 5,
  CUPIDBUILD_DISK_OPTIONS_ENCODING = 6
} cupidbuild_disk_options_status_t;

/* Interpret the arguments following the image command. Grammar 0 retains
 * Python 3.12's eager ambiguity and complete negative-number pattern; grammar
 * 1 retains Python 3.14's deferred ambiguity and negative-number prefix.
 * Basename profile 0 is POSIX and 1 is Windows. Select Unicode 15 or 16.
 * argv strings are borrowed until close; stages and WAD destinations are
 * owned. Geometry validation follows final repeated-value selection in the
 * eventual command caller. This operation observes no filesystem state.
 * Failure and help clear the owner output. Inputs and outputs are disjoint. */
cupidbuild_disk_options_status_t cupidbuild_disk_options_open(
    size_t argc, const char *const *argv, unsigned int unicode_major,
    uint32_t digit_limit, unsigned int grammar_profile,
    unsigned int basename_profile, uint32_t stage_capacity,
    cupidbuild_disk_options_t **owner_out);
cupidbuild_disk_options_status_t cupidbuild_disk_options_view(
    const cupidbuild_disk_options_t *owner, cupidbuild_disk_options_view_t *view_out);
cupidbuild_disk_options_status_t cupidbuild_disk_options_stage(
    const cupidbuild_disk_options_t *owner, size_t index,
    cupidbuild_disk_argument_stage_t *stage_out);
void cupidbuild_disk_options_close(cupidbuild_disk_options_t *owner);

#endif
