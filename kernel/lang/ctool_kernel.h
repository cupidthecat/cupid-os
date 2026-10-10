#ifndef CUPID_TOOLCHAIN_KERNEL_ADAPTER_H
#define CUPID_TOOLCHAIN_KERNEL_ADAPTER_H

#include "ctool.h"

ctool_job_config_t ctool_kernel_job_config(ctool_limits_t limits);
void ctool_kernel_selftest(void);

#endif
