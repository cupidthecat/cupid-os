#ifndef CUPID_TOOLCHAIN_HOST_ADAPTER_H
#define CUPID_TOOLCHAIN_HOST_ADAPTER_H

#include "ctool.h"

typedef struct {
  ctool_string_t root;
} ctool_host_adapter_t;

/* native_root is an explicit platform path owned by the caller and must
 * remain valid until every job using this adapter has closed. */
ctool_status_t ctool_host_adapter_init(ctool_host_adapter_t *adapter,
                                       const char *native_root);
ctool_job_config_t ctool_host_job_config(ctool_host_adapter_t *adapter,
                                         ctool_limits_t limits);

#endif
