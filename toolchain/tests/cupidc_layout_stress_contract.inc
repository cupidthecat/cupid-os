/**
 * @file cupidc_layout_stress_contract.inc
 * @brief Check layout at scale and against active kernel records.
 */

static int type_layouts_equal(const ctool_c_type_layout_t *left,
                              const ctool_c_type_layout_t *right) {
    return left->size == right->size && left->alignment == right->alignment &&
                   left->is_complete_object == right->is_complete_object &&
                   left->is_object == right->is_object && left->is_integer == right->is_integer &&
                   left->is_signed == right->is_signed
               ? 1
               : 0;
}

static int member_layouts_equal(const ctool_c_member_layout_t *left,
                                const ctool_c_member_layout_t *right) {
    return left->byte_offset == right->byte_offset && left->bit_offset == right->bit_offset &&
                   left->bit_width == right->bit_width && left->size == right->size &&
                   left->alignment == right->alignment
               ? 1
               : 0;
}

static int check_active_shapes(void) {
    enum {
        P_U8,
        P_CHAR,
        P_I32,
        P_U32,
        P_ENUM,
        P_VOID,
        P_VOID_POINTER,
        P_U8_512,
        P_CHAR_32,
        P_CPU_CONTEXT,
        P_PROCESS,
        P_COUNT
    };

    enum { S_VOID, S_FUNCTION, S_FUNCTION_POINTER, S_TABLE, S_COUNT };

    enum {
        C_U8,
        C_U32,
        C_U64,
        C_VOID,
        C_VOID_POINTER,
        C_FUNCTION,
        C_FUNCTION_POINTER,
        C_U8_74,
        C_RECORD,
        C_ALIGNED,
        C_COUNT
    };

    enum { D_U8, D_U16, D_U64, D_RECORD, D_COUNT };

    ctool_c_type_node_t process_types[P_COUNT];
    ctool_c_record_member_t process_members[30];
    ctool_c_type_node_t syscall_types[S_COUNT];
    ctool_c_record_member_t syscall_members[103];
    ctool_c_type_node_t percpu_types[C_COUNT];
    ctool_c_record_member_t percpu_members[19];
    ctool_c_type_node_t e1000_types[D_COUNT];
    ctool_c_record_member_t e1000_members[6];
    ctool_c_layout_request_t request;
    ctool_c_layout_result_t process_result;
    ctool_c_layout_result_t syscall_result;
    ctool_c_layout_result_t percpu_result;
    ctool_c_layout_result_t e1000_result;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("adversarial", &adapter, &job) != 0) {
        return 1;
    }
    (void)memset(process_types, 0, sizeof(process_types));
    (void)memset(process_members, 0, sizeof(process_members));
    process_types[P_U8] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/kernel/core/process.h", 63u);
    process_types[P_CHAR] = type_node(CTOOL_C_TYPE_CHAR, "/kernel/core/process.h", 75u);
    process_types[P_I32] = type_node(CTOOL_C_TYPE_SIGNED_INT, "/kernel/core/process.h", 25u);
    process_types[P_U32] = type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/kernel/core/process.h", 49u);
    process_types[P_ENUM] = type_node(CTOOL_C_TYPE_ENUM, "/kernel/core/process.h", 25u);
    process_types[P_ENUM].referenced_type = P_I32;
    process_types[P_VOID] = type_node(CTOOL_C_TYPE_VOID, "/kernel/core/process.h", 68u);
    process_types[P_VOID_POINTER] = type_node(CTOOL_C_TYPE_POINTER, "/kernel/core/process.h", 68u);
    process_types[P_VOID_POINTER].referenced_type = P_VOID;
    process_types[P_U8_512] = type_node(CTOOL_C_TYPE_ARRAY, "/kernel/core/process.h", 66u);
    process_types[P_U8_512].referenced_type = P_U8;
    process_types[P_U8_512].element_count = 512u;
    process_types[P_CHAR_32] = type_node(CTOOL_C_TYPE_ARRAY, "/kernel/core/process.h", 75u);
    process_types[P_CHAR_32].referenced_type = P_CHAR;
    process_types[P_CHAR_32].element_count = 32u;
    process_types[P_CPU_CONTEXT] = type_node(CTOOL_C_TYPE_RECORD, "/kernel/core/process.h", 55u);
    process_types[P_CPU_CONTEXT].record_complete = CTOOL_TRUE;
    process_types[P_CPU_CONTEXT].first_member = 0u;
    process_types[P_CPU_CONTEXT].member_count = 16u;
    for (index = 0u; index < 16u; index++) {
        process_members[index] =
            record_member("register", P_U32, "/kernel/core/process.h", 56u + index / 4u, 0u);
    }
    process_types[P_PROCESS] = type_node(CTOOL_C_TYPE_RECORD, "/kernel/core/process.h", 62u);
    process_types[P_PROCESS].record_complete = CTOOL_TRUE;
    process_types[P_PROCESS].first_member = 16u;
    process_types[P_PROCESS].member_count = 14u;
    process_members[16] = record_member("pid", P_U32, "/kernel/core/process.h", 63u, 0u);
    process_members[17] = record_member("state", P_ENUM, "/kernel/core/process.h", 64u, 0u);
    process_members[18] =
        record_member("context", P_CPU_CONTEXT, "/kernel/core/process.h", 65u, 0u);
    process_members[19] = record_member("fp_state", P_U8_512, "/kernel/core/process.h", 66u, 0u);
    process_members[19].explicit_alignment = 16u;
    process_members[20] =
        record_member("stack_base", P_VOID_POINTER, "/kernel/core/process.h", 68u, 0u);
    process_members[21] = record_member("stack_size", P_U32, "/kernel/core/process.h", 69u, 0u);
    process_members[22] = record_member("image_base", P_U32, "/kernel/core/process.h", 70u, 0u);
    process_members[23] = record_member("image_size", P_U32, "/kernel/core/process.h", 71u, 0u);
    process_members[24] =
        record_member("image_ownership", P_ENUM, "/kernel/core/process.h", 72u, 0u);
    process_members[25] =
        record_member("image_lease_generation", P_U32, "/kernel/core/process.h", 73u, 0u);
    process_members[26] = record_member("domain", P_ENUM, "/kernel/core/process.h", 74u, 0u);
    process_members[27] = record_member("name", P_CHAR_32, "/kernel/core/process.h", 75u, 0u);
    process_members[28] = record_member("on_cpu", P_U8, "/kernel/core/process.h", 76u, 0u);
    process_members[29] = record_member("last_cpu", P_U8, "/kernel/core/process.h", 79u, 0u);

    request = layout_request(process_types, P_COUNT, process_members, ARRAY_COUNT(process_members),
                             "/kernel/core/process.h");
    status = ctool_c_layout_types(job, &request, &process_result);
    if (status != CTOOL_OK ||
        expect_type("adversarial", &process_result, P_CPU_CONTEXT,
                    (expected_type_layout_t){64u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0 ||
        expect_type("adversarial", &process_result, P_PROCESS,
                    (expected_type_layout_t){656u, 16u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0 ||
        expect_member("adversarial", &process_result, 19u, 80u, 0u, 0u, 512u, 16u) != 0 ||
        expect_member("adversarial", &process_result, 27u, 620u, 0u, 0u, 32u, 1u) != 0 ||
        expect_member("adversarial", &process_result, 29u, 653u, 0u, 0u, 1u, 1u) != 0) {
        (void)fprintf(stderr, "adversarial: process_t ABI oracle differs\n");
        ctool_job_close(job);
        return 1;
    }

    (void)memset(syscall_types, 0, sizeof(syscall_types));
    (void)memset(syscall_members, 0, sizeof(syscall_members));
    syscall_types[S_VOID] = type_node(CTOOL_C_TYPE_VOID, "/kernel/core/syscall.cc", 174u);
    syscall_types[S_FUNCTION] = type_node(CTOOL_C_TYPE_FUNCTION, "/kernel/core/syscall.cc", 174u);
    syscall_types[S_FUNCTION].referenced_type = S_VOID;
    syscall_types[S_FUNCTION].has_prototype = CTOOL_TRUE;
    syscall_types[S_FUNCTION_POINTER] =
        type_node(CTOOL_C_TYPE_POINTER, "/kernel/core/syscall.cc", 174u);
    syscall_types[S_FUNCTION_POINTER].referenced_type = S_FUNCTION;
    syscall_types[S_TABLE] = type_node(CTOOL_C_TYPE_RECORD, "/user/cupid.h", 100u);
    syscall_types[S_TABLE].record_complete = CTOOL_TRUE;
    syscall_types[S_TABLE].member_count = ARRAY_COUNT(syscall_members);
    for (index = 0u; index < ARRAY_COUNT(syscall_members); index++) {
        syscall_members[index] =
            record_member("function", S_FUNCTION_POINTER, "/user/cupid.h", 101u + index, 0u);
    }
    request = layout_request(syscall_types, S_COUNT, syscall_members, ARRAY_COUNT(syscall_members),
                             "/user/cupid.h");
    status = ctool_c_layout_types(job, &request, &syscall_result);
    if (status != CTOOL_OK ||
        expect_type("adversarial", &syscall_result, S_TABLE,
                    (expected_type_layout_t){412u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0) {
        (void)fprintf(stderr, "adversarial: syscall table ABI oracle differs\n");
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < ARRAY_COUNT(syscall_members); index++) {
        if (expect_member("adversarial", &syscall_result, index, index * 4u, 0u, 0u, 4u, 4u) != 0) {
            ctool_job_close(job);
            return 1;
        }
    }
    if (expect_type("adversarial", &process_result, P_PROCESS,
                    (expected_type_layout_t){656u, 16u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0 ||
        expect_member("adversarial", &process_result, 19u, 80u, 0u, 0u, 512u, 16u) != 0) {
        (void)fprintf(stderr, "adversarial: later operation invalidated prior result\n");
        ctool_job_close(job);
        return 1;
    }

    (void)memset(percpu_types, 0, sizeof(percpu_types));
    (void)memset(percpu_members, 0, sizeof(percpu_members));
    percpu_types[C_U8] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/kernel/smp/percpu.h", 12u);
    percpu_types[C_U32] = type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/kernel/smp/percpu.h", 18u);
    percpu_types[C_U64] = type_node(CTOOL_C_TYPE_UNSIGNED_LONG_LONG, "/kernel/smp/percpu.h", 19u);
    percpu_types[C_VOID] = type_node(CTOOL_C_TYPE_VOID, "/kernel/smp/percpu.h", 23u);
    percpu_types[C_VOID_POINTER] = type_node(CTOOL_C_TYPE_POINTER, "/kernel/smp/percpu.h", 11u);
    percpu_types[C_VOID_POINTER].referenced_type = C_VOID;
    percpu_types[C_FUNCTION] = type_node(CTOOL_C_TYPE_FUNCTION, "/kernel/smp/percpu.h", 23u);
    percpu_types[C_FUNCTION].referenced_type = C_VOID;
    percpu_types[C_FUNCTION].has_prototype = CTOOL_TRUE;
    percpu_types[C_FUNCTION_POINTER] = type_node(CTOOL_C_TYPE_POINTER, "/kernel/smp/percpu.h", 23u);
    percpu_types[C_FUNCTION_POINTER].referenced_type = C_FUNCTION;
    percpu_types[C_U8_74] = type_node(CTOOL_C_TYPE_ARRAY, "/kernel/smp/percpu.h", 32u);
    percpu_types[C_U8_74].referenced_type = C_U8;
    percpu_types[C_U8_74].element_count = 74u;
    percpu_types[C_RECORD] = type_node(CTOOL_C_TYPE_RECORD, "/kernel/smp/percpu.h", 10u);
    percpu_types[C_RECORD].record_complete = CTOOL_TRUE;
    percpu_types[C_RECORD].member_count = ARRAY_COUNT(percpu_members);
    percpu_types[C_ALIGNED] = type_node(CTOOL_C_TYPE_ALIGNED, "/kernel/smp/percpu.h", 33u);
    percpu_types[C_ALIGNED].referenced_type = C_RECORD;
    percpu_types[C_ALIGNED].explicit_alignment = 64u;

    percpu_members[0] = record_member("self_ptr", C_VOID_POINTER, "/kernel/smp/percpu.h", 11u, 0u);
    percpu_members[1] = record_member("cpu_id", C_U8, "/kernel/smp/percpu.h", 12u, 0u);
    percpu_members[2] = record_member("apic_id", C_U8, "/kernel/smp/percpu.h", 13u, 0u);
    percpu_members[3] = record_member("bootstrap", C_U8, "/kernel/smp/percpu.h", 14u, 0u);
    percpu_members[4] = record_member("online", C_U8, "/kernel/smp/percpu.h", 15u, 0u);
    percpu_members[5] = record_member("current", C_VOID_POINTER, "/kernel/smp/percpu.h", 16u, 0u);
    percpu_members[6] = record_member("idle", C_VOID_POINTER, "/kernel/smp/percpu.h", 17u, 0u);
    percpu_members[7] = record_member("bkl_depth", C_U32, "/kernel/smp/percpu.h", 18u, 0u);
    percpu_members[8] = record_member("preempt_count", C_U64, "/kernel/smp/percpu.h", 19u, 0u);
    percpu_members[9] =
        record_member("idle_stack_top", C_VOID_POINTER, "/kernel/smp/percpu.h", 20u, 0u);
    percpu_members[10] = record_member("bkl_eflags_saved", C_U32, "/kernel/smp/percpu.h", 21u, 0u);
    percpu_members[11] = record_member("current_pid", C_U32, "/kernel/smp/percpu.h", 22u, 0u);
    percpu_members[12] =
        record_member("call_fn", C_FUNCTION_POINTER, "/kernel/smp/percpu.h", 23u, 0u);
    percpu_members[13] = record_member("call_arg", C_VOID_POINTER, "/kernel/smp/percpu.h", 24u, 0u);
    percpu_members[14] = record_member("call_pending", C_U8, "/kernel/smp/percpu.h", 25u, 0u);
    percpu_members[15] = record_member("call_done", C_U8, "/kernel/smp/percpu.h", 26u, 0u);
    percpu_members[16] = record_member("reschedule_pending", C_U8, "/kernel/smp/percpu.h", 27u, 0u);
    percpu_members[17] = record_member("interrupt_depth", C_U8, "/kernel/smp/percpu.h", 28u, 0u);
    percpu_members[18] = record_member("_pad", C_U8_74, "/kernel/smp/percpu.h", 32u, 0u);

    request = layout_request(percpu_types, C_COUNT, percpu_members, ARRAY_COUNT(percpu_members),
                             "/kernel/smp/percpu.h");
    status = ctool_c_layout_types(job, &request, &percpu_result);
    if (status != CTOOL_OK ||
        expect_type("adversarial", &percpu_result, C_RECORD,
                    (expected_type_layout_t){128u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0 ||
        expect_type("adversarial", &percpu_result, C_ALIGNED,
                    (expected_type_layout_t){128u, 64u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0 ||
        expect_member("adversarial", &percpu_result, 8u, 20u, 0u, 0u, 8u, 4u) != 0 ||
        expect_member("adversarial", &percpu_result, 12u, 40u, 0u, 0u, 4u, 4u) != 0 ||
        expect_member("adversarial", &percpu_result, 18u, 52u, 0u, 0u, 74u, 1u) != 0) {
        (void)fprintf(stderr, "adversarial: per_cpu_t ABI oracle differs\n");
        ctool_job_close(job);
        return 1;
    }

    (void)memset(e1000_types, 0, sizeof(e1000_types));
    (void)memset(e1000_members, 0, sizeof(e1000_members));
    e1000_types[D_U8] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/drivers/e1000.cc", 37u);
    e1000_types[D_U16] = type_node(CTOOL_C_TYPE_UNSIGNED_SHORT, "/drivers/e1000.cc", 35u);
    e1000_types[D_U64] = type_node(CTOOL_C_TYPE_UNSIGNED_LONG_LONG, "/drivers/e1000.cc", 34u);
    e1000_types[D_RECORD] = type_node(CTOOL_C_TYPE_RECORD, "/drivers/e1000.cc", 33u);
    e1000_types[D_RECORD].record_complete = CTOOL_TRUE;
    e1000_types[D_RECORD].record_packed = CTOOL_TRUE;
    e1000_types[D_RECORD].explicit_alignment = 16u;
    e1000_types[D_RECORD].member_count = ARRAY_COUNT(e1000_members);
    e1000_members[0] = record_member("addr", D_U64, "/drivers/e1000.cc", 34u, 0u);
    e1000_members[1] = record_member("len", D_U16, "/drivers/e1000.cc", 35u, 0u);
    e1000_members[2] = record_member("csum", D_U16, "/drivers/e1000.cc", 36u, 0u);
    e1000_members[3] = record_member("status", D_U8, "/drivers/e1000.cc", 37u, 0u);
    e1000_members[4] = record_member("err", D_U8, "/drivers/e1000.cc", 38u, 0u);
    e1000_members[5] = record_member("special", D_U16, "/drivers/e1000.cc", 39u, 0u);

    request = layout_request(e1000_types, D_COUNT, e1000_members, ARRAY_COUNT(e1000_members),
                             "/drivers/e1000.cc");
    status = ctool_c_layout_types(job, &request, &e1000_result);
    if (status != CTOOL_OK ||
        expect_type("adversarial", &e1000_result, D_RECORD,
                    (expected_type_layout_t){16u, 16u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0 ||
        expect_member("adversarial", &e1000_result, 0u, 0u, 0u, 0u, 8u, 1u) != 0 ||
        expect_member("adversarial", &e1000_result, 1u, 8u, 0u, 0u, 2u, 1u) != 0 ||
        expect_member("adversarial", &e1000_result, 2u, 10u, 0u, 0u, 2u, 1u) != 0 ||
        expect_member("adversarial", &e1000_result, 3u, 12u, 0u, 0u, 1u, 1u) != 0 ||
        expect_member("adversarial", &e1000_result, 4u, 13u, 0u, 0u, 1u, 1u) != 0 ||
        expect_member("adversarial", &e1000_result, 5u, 14u, 0u, 0u, 2u, 1u) != 0) {
        (void)fprintf(stderr, "adversarial: e1000_rx_desc_t ABI oracle differs\n");
        ctool_job_close(job);
        return 1;
    }
    if (expect_type("adversarial", &process_result, P_PROCESS,
                    (expected_type_layout_t){656u, 16u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0 ||
        expect_type("adversarial", &syscall_result, S_TABLE,
                    (expected_type_layout_t){412u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                                             CTOOL_FALSE}) != 0) {
        (void)fprintf(stderr, "adversarial: aligned operation invalidated prior result\n");
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    return 0;
}

static int check_repeated_success(void) {
    const ctool_u32 array_count = 128u;
    const ctool_u32 record_count = 128u;
    const ctool_u32 type_count = 1u + array_count + record_count;
    const ctool_u32 repeat_count = 24u;
    ctool_c_type_node_t *nodes = (ctool_c_type_node_t *)0;
    ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
    ctool_c_layout_result_t results[24];
    ctool_c_layout_request_t request;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_status_t status;
    ctool_u32 index;
    ctool_u32 prior;
    int failed = 0;

    nodes = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*nodes));
    members = (ctool_c_record_member_t *)calloc((size_t)record_count, sizeof(*members));
    if (nodes == NULL || members == NULL) {
        (void)fprintf(stderr, "adversarial: repeated fixture allocation failed\n");
        failed = 1;
        goto cleanup;
    }
    nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/repeated.c", 1u);
    for (index = 0u; index < array_count; index++) {
        ctool_u32 type = 1u + index;
        nodes[type] = type_node(CTOOL_C_TYPE_ARRAY, "/repeated.c", type + 1u);
        nodes[type].referenced_type = 0u;
        nodes[type].element_count = 1u;
    }
    for (index = 0u; index < record_count; index++) {
        ctool_u32 type = 1u + array_count + index;
        nodes[type] = type_node(CTOOL_C_TYPE_RECORD, "/repeated.c", type + 1u);
        nodes[type].record_complete = CTOOL_TRUE;
        nodes[type].first_member = index;
        nodes[type].member_count = 1u;
        members[index] = record_member("value", 1u + index, "/repeated.c", index + 2u, 0u);
    }
    if (open_limited_job("adversarial", 4096u, 256u * 1024u, &adapter, &job) != 0) {
        failed = 1;
        goto cleanup;
    }
    request = layout_request(nodes, type_count, members, record_count, "/repeated.c");
    for (index = 0u; index < repeat_count; index++) {
        status = ctool_c_layout_types(job, &request, &results[index]);
        if (status != CTOOL_OK || results[index].type_count != type_count ||
            results[index].member_count != record_count) {
            (void)fprintf(stderr, "adversarial: repeated success stopped at operation %u\n", index);
            failed = 1;
            break;
        }
        for (prior = 0u; prior <= index; prior++) {
            ctool_u32 last_type = type_count - 1u;
            if (results[prior].types[last_type].size != 1u ||
                results[prior].types[last_type].alignment != 1u ||
                results[prior].members[record_count - 1u].byte_offset != 0u) {
                (void)fprintf(stderr, "adversarial: operation %u invalidated preserved result %u\n",
                              index, prior);
                failed = 1;
                break;
            }
        }
        if (failed != 0) {
            break;
        }
    }
    if (failed == 0 && ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "adversarial: repeated success emitted diagnostics\n");
        failed = 1;
    }

cleanup:
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
    free(members);
    free(nodes);
    return failed;
}

static int run_adversarial(void) {
    if (check_active_shapes() != 0 || check_repeated_success() != 0) {
        return 1;
    }
    (void)printf("adversarial: ok\n");
    return 0;
}

static int check_bool_wrapper_scale(void) {
    const ctool_u32 wrapper_count = 4096u;
    const ctool_u32 field_count = 4096u;
    const ctool_u32 bool_type = wrapper_count;
    const ctool_u32 record_type = wrapper_count + 1u;
    const ctool_u32 type_count = wrapper_count + 2u;
    ctool_c_type_node_t *nodes = (ctool_c_type_node_t *)0;
    ctool_c_type_node_t *node_copy = (ctool_c_type_node_t *)0;
    ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
    ctool_c_record_member_t *member_copy = (ctool_c_record_member_t *)0;
    ctool_c_layout_request_t request;
    ctool_c_layout_result_t first;
    ctool_c_layout_result_t second;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_status_t status;
    ctool_u32 index;
    int failed = 0;

    nodes = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*nodes));
    node_copy = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*node_copy));
    members = (ctool_c_record_member_t *)calloc((size_t)field_count, sizeof(*members));
    member_copy = (ctool_c_record_member_t *)calloc((size_t)field_count, sizeof(*member_copy));
    if (nodes == NULL || node_copy == NULL || members == NULL || member_copy == NULL) {
        (void)fprintf(stderr, "scale: bool-wrapper fixture allocation failed\n");
        failed = 1;
        goto cleanup;
    }
    for (index = 0u; index < wrapper_count; index++) {
        nodes[index] = type_node(CTOOL_C_TYPE_ALIGNED, "/bool-wrapper-scale.c", index + 1u);
        nodes[index].referenced_type = index + 1u;
        nodes[index].explicit_alignment = 1u;
    }
    nodes[bool_type] = type_node(CTOOL_C_TYPE_BOOL, "/bool-wrapper-scale.c", bool_type + 1u);
    nodes[record_type] = type_node(CTOOL_C_TYPE_RECORD, "/bool-wrapper-scale.c", record_type + 1u);
    nodes[record_type].record_complete = CTOOL_TRUE;
    nodes[record_type].member_count = field_count;
    for (index = 0u; index < field_count; index++) {
        members[index] =
            record_member("flag", 0u, "/bool-wrapper-scale.c", record_type + index + 2u, 0u);
        members[index].is_bit_field = CTOOL_TRUE;
        members[index].bit_width = 1u;
    }
    (void)memcpy(node_copy, nodes, (size_t)type_count * sizeof(*nodes));
    (void)memcpy(member_copy, members, (size_t)field_count * sizeof(*members));
    if (open_job("scale", &adapter, &job) != 0) {
        failed = 1;
        goto cleanup;
    }
    request = layout_request(nodes, type_count, members, field_count, "/bool-wrapper-scale.c");
    status = ctool_c_layout_types(job, &request, &first);
    if (status == CTOOL_OK) {
        status = ctool_c_layout_types(job, &request, &second);
    }
    if (status != CTOOL_OK || first.type_count != type_count || second.type_count != type_count ||
        first.member_count != field_count || second.member_count != field_count ||
        memcmp(nodes, node_copy, (size_t)type_count * sizeof(*nodes)) != 0 ||
        memcmp(members, member_copy, (size_t)field_count * sizeof(*members)) != 0 ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "scale: bool-wrapper operation shape or ownership differs\n");
        failed = 1;
    }
    if (failed == 0) {
        for (index = 0u; index < type_count; index++) {
            if (type_layouts_equal(&first.types[index], &second.types[index]) == 0) {
                (void)fprintf(stderr, "scale: bool-wrapper type %u is nondeterministic\n", index);
                failed = 1;
                break;
            }
        }
    }
    if (failed == 0 && (first.types[0].size != 1u || first.types[0].alignment != 1u ||
                        first.types[0].is_integer != CTOOL_TRUE ||
                        first.types[record_type].size != field_count / 8u ||
                        first.types[record_type].alignment != 1u)) {
        (void)fprintf(stderr, "scale: bool-wrapper type layout differs\n");
        failed = 1;
    }
    if (failed == 0) {
        for (index = 0u; index < field_count; index++) {
            const ctool_c_member_layout_t *layout = &first.members[index];
            if (member_layouts_equal(layout, &second.members[index]) == 0 ||
                layout->byte_offset != index / 8u || layout->bit_offset != index % 8u ||
                layout->bit_width != 1u || layout->size != 1u || layout->alignment != 1u) {
                (void)fprintf(stderr, "scale: bool-wrapper field %u differs\n", index);
                failed = 1;
                break;
            }
        }
    }

cleanup:
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
    free(member_copy);
    free(members);
    free(node_copy);
    free(nodes);
    return failed;
}

static int run_scale(void) {
    const ctool_u32 array_count = 4096u;
    const ctool_u32 record_count = 4096u;
    const ctool_u32 type_count = 1u + array_count + record_count;
    ctool_c_type_node_t *nodes = (ctool_c_type_node_t *)0;
    ctool_c_type_node_t *node_copy = (ctool_c_type_node_t *)0;
    ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
    ctool_c_record_member_t *member_copy = (ctool_c_record_member_t *)0;
    ctool_c_layout_request_t request;
    ctool_c_layout_result_t first;
    ctool_c_layout_result_t second;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_status_t status;
    ctool_u32 index;
    int failed = 0;

    nodes = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*nodes));
    node_copy = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*node_copy));
    members = (ctool_c_record_member_t *)calloc((size_t)record_count, sizeof(*members));
    member_copy = (ctool_c_record_member_t *)calloc((size_t)record_count, sizeof(*member_copy));
    if (nodes == NULL || node_copy == NULL || members == NULL || member_copy == NULL) {
        (void)fprintf(stderr, "scale: fixture allocation failed\n");
        failed = 1;
        goto cleanup;
    }
    nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/scale.c", 1u);
    for (index = 0u; index < array_count; index++) {
        ctool_u32 type_index = 1u + index;
        nodes[type_index] = type_node(CTOOL_C_TYPE_ARRAY, "/scale.c", type_index + 1u);
        nodes[type_index].referenced_type = index + 1u < array_count ? type_index + 1u : 0u;
        nodes[type_index].element_count = 1u;
    }
    for (index = 0u; index < record_count; index++) {
        ctool_u32 type_index = 1u + array_count + index;
        nodes[type_index] = type_node(CTOOL_C_TYPE_RECORD, "/scale.c", type_index + 1u);
        nodes[type_index].record_complete = CTOOL_TRUE;
        nodes[type_index].first_member = index;
        nodes[type_index].member_count = 1u;
        members[index] = record_member("value", 1u + index, "/scale.c", index + 2u, 0u);
    }
    (void)memcpy(node_copy, nodes, (size_t)type_count * sizeof(*nodes));
    (void)memcpy(member_copy, members, (size_t)record_count * sizeof(*members));
    if (open_job("scale", &adapter, &job) != 0) {
        failed = 1;
        goto cleanup;
    }
    request = layout_request(nodes, type_count, members, record_count, "/scale.c");
    status = ctool_c_layout_types(job, &request, &first);
    if (status == CTOOL_OK) {
        status = ctool_c_layout_types(job, &request, &second);
    }
    if (status != CTOOL_OK || first.type_count != type_count || second.type_count != type_count ||
        first.member_count != record_count || second.member_count != record_count ||
        memcmp(nodes, node_copy, (size_t)type_count * sizeof(*nodes)) != 0 ||
        memcmp(members, member_copy, (size_t)record_count * sizeof(*members)) != 0) {
        (void)fprintf(stderr, "scale: operation shape or input ownership differs\n");
        failed = 1;
    }
    if (failed == 0) {
        for (index = 0u; index < type_count; index++) {
            if (type_layouts_equal(&first.types[index], &second.types[index]) == 0) {
                (void)fprintf(stderr, "scale: type %u is nondeterministic\n", index);
                failed = 1;
                break;
            }
        }
    }
    if (failed == 0) {
        for (index = 0u; index < record_count; index++) {
            ctool_u32 type_index = 1u + array_count + index;
            if (member_layouts_equal(&first.members[index], &second.members[index]) == 0 ||
                first.members[index].byte_offset != 0u || first.types[type_index].size != 1u ||
                first.types[type_index].alignment != 1u) {
                (void)fprintf(stderr, "scale: record %u differs\n", index);
                failed = 1;
                break;
            }
        }
    }
    ctool_job_close(job);
    job = (ctool_job_t *)0;

cleanup:
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
    free(member_copy);
    free(members);
    free(node_copy);
    free(nodes);
    if (failed != 0) {
        return 1;
    }
    if (check_bool_wrapper_scale() != 0) {
        return 1;
    }
    (void)printf("scale: ok\n");
    return 0;
}
