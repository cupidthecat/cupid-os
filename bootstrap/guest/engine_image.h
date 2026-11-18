#ifndef CUPID_C_ENGINE_IMAGE_H
#define CUPID_C_ENGINE_IMAGE_H

#include "elf32.h"

typedef struct {
    ctool_u32 base;
    ctool_u32 size;
    ctool_u32 entry;
    const ctool_elf32_program_header_t *segments;
    ctool_u32 segment_count;
} ctool_c_engine_image_t;

/* An image plan borrows the source bytes and ELF metadata from its job.
 * Copy it only while both are live. The install destination must be disjoint
 * from those borrowed views and start at the mapped image base. */
ctool_status_t ctool_c_engine_plan_image(ctool_job_t *job, const ctool_source_t *source,
                                         ctool_u32 base, ctool_u32 capacity,
                                         ctool_c_engine_image_t *plan);
ctool_status_t ctool_c_engine_install_image(const ctool_c_engine_image_t *plan,
                                            ctool_mut_bytes_t destination);

#endif
