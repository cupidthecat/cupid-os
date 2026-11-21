/**
 * @file cupidc_engine.h
 * @brief Define the request ABI of the freestanding CupidC engine.
 */

#ifndef CUPID_C_ENGINE_H
#define CUPID_C_ENGINE_H

#include "cupidc_emit.h"

#define CTOOL_C_ENGINE_ABI 1u

typedef struct {
    ctool_u32 abi;
    ctool_u32 bytes;
    ctool_job_config_t config;
    ctool_invocation_request_t invocation;
    ctool_c_pp_request_t preprocessor;
    ctool_c_parse_request_t parser;
} ctool_c_engine_request_t;

/* The caller owns all views for the synchronous invocation. Early ABI errors
 * leave result untouched. Valid requests use the ordinary job transaction. */
ctool_status_t ctool_c_engine_compile(const ctool_c_engine_request_t *request,
                                      ctool_invocation_result_t *result);

#endif
