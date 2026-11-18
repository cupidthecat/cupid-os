#include "cupidc_engine.h"

static ctool_status_t compile_body(ctool_invocation_t *invocation, void *context) {
    const ctool_c_engine_request_t *request = (const ctool_c_engine_request_t *)context;
    ctool_c_pp_result_t tape = {0};
    ctool_c_translation_unit_t unit = {0};
    ctool_status_t status;

    status = ctool_c_preprocess(invocation->job, invocation->input, &request->preprocessor, &tape);
    if (status == CTOOL_OK) {
        status = ctool_c_parse(invocation->job, &tape, &request->parser, &unit);
    }
    if (status == CTOOL_OK) {
        status = ctool_c_emit_object(invocation->job, &unit, invocation->output);
    }
    return status;
}

ctool_status_t ctool_c_engine_compile(const ctool_c_engine_request_t *request,
                                      ctool_invocation_result_t *result) {
    ctool_c_engine_request_t local;
    if (request == (const ctool_c_engine_request_t *)0 ||
        result == (ctool_invocation_result_t *)0 || request->abi != CTOOL_C_ENGINE_ABI ||
        request->bytes != sizeof(*request) || request->preprocessor.mode != request->parser.mode ||
        request->preprocessor.gnu_extensions != request->parser.gnu_extensions) {
        return CTOOL_ERR_INVALID_ARGUMENT;
    }
    local = *request;
    return ctool_invoke(&local.config, &local.invocation, compile_body, &local, result);
}
