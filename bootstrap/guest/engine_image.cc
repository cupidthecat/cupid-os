#include "engine_image.h"

#define ELF_PT_GNU_STACK 0x6474e551u

ctool_status_t ctool_c_engine_plan_image(ctool_job_t *job, const ctool_source_t *source,
                                         ctool_u32 base, ctool_u32 capacity,
                                         ctool_c_engine_image_t *plan) {
    if (!plan)
        return CTOOL_ERR_INVALID_ARGUMENT;
    *plan = (ctool_c_engine_image_t){0};
    if (!job || !source || !capacity || capacity > 0xffffffffu - base)
        return CTOOL_ERR_INVALID_ARGUMENT;

    ctool_elf32_object_t object;
    ctool_status_t status = ctool_elf32_read(job, source, &object);
    if (status != CTOOL_OK)
        return status;
    if (object.file_type != CTOOL_ELF32_ET_EXEC)
        return CTOOL_ERR_INPUT;

    for (ctool_u32 i = 1; i < object.symbol_count; i++) {
        if (object.symbols[i].placement == CTOOL_ELF32_SYMBOL_UNDEFINED)
            return CTOOL_ERR_INPUT;
    }

    ctool_u32 end = base;
    ctool_u32 limit = base + capacity;
    ctool_bool executable_entry = CTOOL_FALSE;
    for (ctool_u32 i = 0; i < object.program_header_count; i++) {
        const ctool_elf32_program_header_t *segment = &object.program_headers[i];
        if (segment->type == ELF_PT_GNU_STACK && segment->file_size == 0u &&
            segment->memory_size == 0u &&
            !(segment->flags & ~(CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W)))
            continue;

        ctool_u32 start = segment->virtual_address;
        if (segment->type != CTOOL_ELF32_PT_LOAD || start < end || start >= limit ||
            segment->memory_size == 0u || segment->memory_size > limit - start ||
            (segment->flags & ~(CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)))
            return CTOOL_ERR_INPUT;
        if ((segment->flags & CTOOL_ELF32_PF_X) && object.entry_point >= start &&
            object.entry_point - start < segment->file_size)
            executable_entry = CTOOL_TRUE;
        end = start + segment->memory_size;
    }
    if (!executable_entry)
        return CTOOL_ERR_INPUT;

    *plan = (ctool_c_engine_image_t){base, end - base, object.entry_point,
                                     object.program_headers, object.program_header_count};
    return CTOOL_OK;
}

ctool_status_t ctool_c_engine_install_image(const ctool_c_engine_image_t *plan,
                                            ctool_mut_bytes_t destination) {
    if (!plan || !plan->segments || !plan->size || !destination.data)
        return CTOOL_ERR_INVALID_ARGUMENT;
    if (destination.size < plan->size)
        return CTOOL_ERR_LIMIT;

    for (ctool_u32 i = 0; i < plan->size; i++)
        destination.data[i] = 0u;
    for (ctool_u32 i = 0; i < plan->segment_count; i++) {
        const ctool_elf32_program_header_t *segment = &plan->segments[i];
        if (segment->type == CTOOL_ELF32_PT_LOAD) {
            ctool_u8 *target = destination.data + (segment->virtual_address - plan->base);
            for (ctool_u32 j = 0; j < segment->file_size; j++)
                target[j] = segment->contents.data[j];
        }
    }
    return CTOOL_OK;
}
