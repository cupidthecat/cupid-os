/**
 * @file cupidc_bundle.h
 * @brief Compile a list of sources into a single ISO inside Cupid OS.
 */

#ifndef CUPID_BOOTSTRAP_CUPIDC_BUNDLE_H
#define CUPID_BOOTSTRAP_CUPIDC_BUNDLE_H

#include "cupidc_engine.h"
#include "guest_job.h"

ctool_status_t cupid_guest_compile_bundle(ctool_invocation_t *invocation,
                                          const ctool_c_engine_request_t *profile,
                                          cupid_syscall_table_t *sys);

#endif
