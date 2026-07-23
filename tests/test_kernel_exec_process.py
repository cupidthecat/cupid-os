import os
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
KERNEL_INCLUDES = [
    "kernel",
    "kernel/audio",
    "kernel/core",
    "kernel/cpu",
    "kernel/crypto",
    "kernel/doom",
    "kernel/fs",
    "kernel/gfx",
    "kernel/gui",
    "kernel/lang",
    "kernel/mm",
    "kernel/network",
    "kernel/smp",
    "kernel/tls",
    "kernel/usb",
    "kernel/util",
    "drivers",
    "toolchain",
]
STRICT_FLAGS = [
    "-std=gnu11",
    "-m32",
    "-ffreestanding",
    "-fno-pie",
    "-fno-stack-protector",
    "-fno-builtin",
    "-msse",
    "-msse2",
    "-pedantic",
    "-Wno-variadic-macros",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wshadow",
    "-Wpointer-arith",
    "-Wcast-qual",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Wconversion",
    "-Wsign-conversion",
]


class KernelContractCase(unittest.TestCase):
    production_source = None
    contract_source = None
    extra_includes = []

    @classmethod
    def setUpClass(cls):
        if cls is KernelContractCase:
            return
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix="cupid-kernel-contract-"
        )
        executable_name = cls.contract_source.stem + (".exe" if os.name == "nt" else "")
        cls.contract = Path(cls._build_directory.name) / executable_name

        compiler = shlex.split(os.environ.get("CC", "clang" if os.name == "nt" else "cc"))
        command = compiler + STRICT_FLAGS
        if os.name == "nt":
            # The kernel intentionally defines freestanding size_t as unsigned
            # long; disable Clang's hosted MS typedef before including types.h.
            command += [
                "-fno-ms-compatibility",
                "-Wno-gnu-zero-variadic-macro-arguments",
            ]
        else:
            command += ["-nostdlib", "-no-pie", "-Wl,-e,_start", "-Wl,--build-id=none"]
        command += [f"-I{REPO_ROOT / include}" for include in cls.extra_includes]
        command += [f"-I{REPO_ROOT / include}" for include in KERNEL_INCLUDES]
        command += [
            str(REPO_ROOT / cls.production_source),
            str(REPO_ROOT / cls.contract_source),
            "-o",
            str(cls.contract),
        ]
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "strict kernel contract build failed\n" + result.stdout + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        if cls is not KernelContractCase:
            cls._build_directory.cleanup()

    def run_contract(self, mode):
        result = subprocess.run(
            [str(self.contract), mode],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"{mode} contract exited {result.returncode}\n"
            + result.stdout
            + result.stderr,
        )


class KernelElfLoaderContractTests(KernelContractCase):
    production_source = Path("kernel/lang/exec.c")
    contract_source = Path("tests/kernel_exec_contract.c")

    def test_valid_external_image_is_staged_loaded_and_published(self):
        self.run_contract("valid-external")

    def test_cupidc_and_cupidasm_legacy_arenas_remain_loadable(self):
        self.run_contract("valid-legacy")

    def test_busy_external_lease_leaves_fixed_memory_unchanged(self):
        self.run_contract("busy-external")

    def test_truncated_program_header_table_is_rejected_before_publication(self):
        self.run_contract("truncated-table")

    def test_truncated_segment_is_rejected_before_fixed_memory_copy(self):
        self.run_contract("truncated-segment")

    def test_segment_crossing_an_executable_arena_is_rejected(self):
        self.run_contract("crossing-arena")

    def test_former_external_base_inside_the_stack_is_rejected(self):
        self.run_contract("former-external-base")

    def test_overlapping_load_segments_are_rejected(self):
        self.run_contract("overlapping-segments")

    def test_non_power_of_two_segment_alignment_is_rejected(self):
        self.run_contract("bad-alignment")

    def test_incongruent_file_and_virtual_alignment_is_rejected(self):
        self.run_contract("bad-congruence")

    def test_entry_in_zero_fill_tail_is_rejected(self):
        self.run_contract("entry-in-bss")

    def test_entry_in_non_executable_file_bytes_is_rejected(self):
        self.run_contract("entry-non-executable")

    def test_dynamic_program_header_type_is_rejected(self):
        self.run_contract("unsupported-type")

    def test_unknown_segment_permission_bits_are_rejected(self):
        self.run_contract("unknown-flags")

    def test_invalid_ident_version_is_rejected(self):
        self.run_contract("bad-ident-version")

    def test_invalid_header_version_is_rejected(self):
        self.run_contract("bad-header-version")

    def test_process_creation_failure_discards_claimed_lease_once(self):
        self.run_contract("process-create-failure")


class KernelProcessImageContractTests(KernelContractCase):
    production_source = Path("kernel/core/process.c")
    contract_source = Path("tests/kernel_process_contract.c")
    extra_includes = ["tests/kernel_contract_support"]

    def test_external_image_claim_is_exclusive_until_discarded(self):
        self.run_contract("claim-busy-discard")

    def test_stale_descriptor_cannot_cancel_a_newer_lease(self):
        self.run_contract("stale-discard")

    def test_process_creation_consumes_and_kill_releases_the_lease(self):
        self.run_contract("consume-kill")

    def test_failed_process_creation_leaves_the_lease_with_the_caller(self):
        self.run_contract("failed-consume")

    def test_current_process_resources_are_released_by_deferred_reaping(self):
        self.run_contract("deferred-self-reap")

    def test_remote_running_process_is_quiescent_before_deferred_reaping(self):
        self.run_contract("deferred-remote-reap")

    def test_context_switch_handoff_releases_once_and_preserves_caller_flags(self):
        self.run_contract("schedule-handoff")

    def test_context_switch_handoff_preserves_an_interrupts_disabled_caller(self):
        self.run_contract("if-clear-handoff")

    def test_scheduler_no_switch_path_uses_the_normal_bkl_unlock(self):
        self.run_contract("no-switch-unlock")

    def test_scheduler_does_not_dispatch_an_idle_stack_owned_by_another_cpu(self):
        self.run_contract("remote-idle-owned")

    def test_terminated_ap_task_hands_off_to_its_cpu_local_idle_context(self):
        self.run_contract("terminated-ap-idle-context")

    def test_generic_interrupt_context_defers_pending_reschedule_until_exit(self):
        self.run_contract("interrupt-defers-pending")

    def test_direct_schedule_call_in_interrupt_context_is_deferred(self):
        self.run_contract("interrupt-direct-schedule-defers")

    def test_scheduler_defers_switch_inside_an_existing_critical_section(self):
        self.run_contract("nested-schedule-defers")

    def test_pending_remote_reschedule_runs_on_outer_bkl_release(self):
        self.run_contract("pending-on-outer-unlock")

    def test_stack_canary_termination_detaches_reaps_and_releases_lease(self):
        self.run_contract("stack-canary-reap")

    def test_valid_cupid_permanent_images_are_consumed_in_external_domain(self):
        self.run_contract("permanent-consume")

    def test_permanent_image_is_rejected_outside_external_domain(self):
        self.run_contract("permanent-wrong-domain")

    def test_permanent_image_rejects_reserved_unaligned_and_crossing_ranges(self):
        self.run_contract("invalid-permanent-ranges")

    def test_permanent_image_discard_never_releases_physical_pages(self):
        self.run_contract("descriptor-cleanup")


class KernelSmpRescheduleSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (REPO_ROOT / "kernel/smp/smp.c").read_text()

    def function_body(self, name):
        match = re.search(
            rf"void\s+{re.escape(name)}\s*\([^)]*\)\s*\{{(?P<body>.*?)\n\}}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(match, f"missing {name} definition")
        return match.group("body")

    def test_sender_publishes_pending_request_before_raising_ipi(self):
        body = self.function_body("smp_reschedule")
        publish = body.find("percpu_request_reschedule(&cpus[cpu_id])")
        interrupt = body.find("lapic_send_ipi(")
        self.assertGreaterEqual(publish, 0)
        self.assertGreater(interrupt, publish)

    def test_real_ipi_handler_consumes_request_through_process_safe_point(self):
        body = self.function_body("ipi_reschedule_c")
        acknowledge = body.find("lapic_eoi()")
        consume = body.find("process_reschedule_if_pending()")
        self.assertGreaterEqual(acknowledge, 0)
        self.assertGreater(consume, acknowledge)
        self.assertNotIn("percpu_request_reschedule", body)

    def test_ap_enables_fpu_sse_before_any_other_c_operation(self):
        body = self.function_body("ap_main_c")
        enable = body.find("fpu_init_cpu()")
        identify = body.find("lapic_get_id()")
        percpu = body.find("percpu_load_kernel_gdt")
        log = body.find("KINFO(")
        self.assertGreaterEqual(enable, 0)
        self.assertGreater(identify, enable)
        self.assertGreater(percpu, identify)
        self.assertGreater(log, percpu)


class KernelFpuInitializationSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (REPO_ROOT / "kernel/cpu/fpu.c").read_text()

    def function_body(self, name):
        match = re.search(
            rf"void\s+{re.escape(name)}\s*\([^)]*\)\s*\{{(?P<body>.*?)\n\}}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(match, f"missing {name} definition")
        return match.group("body")

    def test_cpu_local_enable_path_has_no_logging_before_sse_is_usable(self):
        body = self.function_body("fpu_init_cpu")
        self.assertIn("mov %%cr0", body)
        self.assertIn("mov %%cr4", body)
        self.assertIn("fninit", body)
        self.assertIn("ldmxcsr", body)
        self.assertNotIn("serial_printf", body)

    def test_cpu_local_enable_path_restricts_compiler_to_general_registers(self):
        self.assertRegex(
            self.source,
            r'__attribute__\(\(target\("general-regs-only"\)\)\)\s*'
            r"void\s+fpu_init_cpu",
        )

    def test_bsp_initializer_uses_the_same_cpu_local_enable_path(self):
        body = self.function_body("fpu_init")
        self.assertIn("fpu_init_cpu()", body)


class KernelInterruptGsSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (REPO_ROOT / "kernel/cpu/isr.asm").read_text()
        cls.isr_common = source.split("isr_common_stub:", 1)[1].split(
            "irq_common_stub:", 1
        )[0]
        cls.irq_common = source.split("irq_common_stub:", 1)[1].split(
            "; Add all ISR handlers", 1
        )[0]

    def assert_common_stub_preserves_percpu_gs(self, body, handler):
        self.assertIn("push gs", body)
        self.assertNotIn("pop gs", body)
        self.assertIn("add esp, 4", body)
        self.assertIn("mov ds, ax", body)
        self.assertIn("mov es, ax", body)
        self.assertIn("mov fs, ax", body)
        self.assertNotRegex(body, r"\bmov\s+gs\s*,")
        enter = body.find("call percpu_interrupt_enter")
        dispatch = body.find(f"call {handler}")
        leave = body.find("call percpu_interrupt_leave")
        consume = body.find("call process_reschedule_if_pending")
        self.assertGreaterEqual(enter, 0)
        self.assertGreater(dispatch, enter)
        self.assertGreater(leave, dispatch)
        self.assertGreater(consume, leave)

    def test_exception_common_stub_preserves_percpu_gs_for_c_handlers(self):
        self.assert_common_stub_preserves_percpu_gs(self.isr_common, "isr_handler")

    def test_irq_common_stub_preserves_percpu_gs_for_c_handlers(self):
        self.assert_common_stub_preserves_percpu_gs(self.irq_common, "irq_handler")


class KernelBlockCacheSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (REPO_ROOT / "kernel/fs/blockcache.c").read_text()

    def function_body(self, name):
        match = re.search(
            rf"(?:int|void)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{(?P<body>.*?)\n\}}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(match, f"missing {name} definition")
        return match.group("body")

    def assert_guarded_wrapper(self, name, operation):
        body = self.function_body(name)
        enter = body.find("blockcache_guard_enter()")
        execute = body.find(operation)
        leave = body.find("blockcache_guard_leave(locked)")
        self.assertGreaterEqual(enter, 0)
        self.assertGreater(execute, enter)
        self.assertGreater(leave, execute)

    def test_sector_reads_and_writes_serialize_cache_and_ata_state(self):
        self.assert_guarded_wrapper("blockcache_read", "blockcache_read_unlocked")
        self.assert_guarded_wrapper("blockcache_write", "blockcache_write_unlocked")

    def test_manual_and_periodic_flushes_share_the_same_serialized_path(self):
        self.assert_guarded_wrapper(
            "blockcache_flush_all", "blockcache_flush_all_unlocked"
        )
        periodic = self.function_body("blockcache_periodic_flush")
        self.assertIn("blockcache_flush_all()", periodic)

    def test_sync_holds_the_guard_across_homefs_and_cache_flush(self):
        body = self.function_body("blockcache_sync")
        enter = body.find("blockcache_guard_enter()")
        homefs = body.find("homefs_sync()")
        flush = body.find("blockcache_flush_all_unlocked()")
        leave = body.find("blockcache_guard_leave(locked)")
        self.assertGreaterEqual(enter, 0)
        self.assertGreater(homefs, enter)
        self.assertGreater(flush, homefs)
        self.assertGreater(leave, flush)


class KernelContextSwitchSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (REPO_ROOT / "kernel/core/context_switch.asm").read_text()
        cls.handoff = source.split("context_switch:", 1)[1].split(
            "context_switch_resume:", 1
        )[0]
        cls.resume = source.split("context_switch_resume:", 1)[1]

    def test_bkl_release_occurs_on_target_stack_after_fp_restore_before_entry(self):
        stack = self.handoff.find("mov esp, [edx + PCB_ESP_OFFSET]")
        fp = self.handoff.find("fxrstor [edx + PCB_FP_STATE_OFFSET]")
        release = self.handoff.find("call bkl_context_switch_release")
        interrupt = self.handoff.find("\n    sti")
        entry = self.handoff.find("jmp dword [edx + PCB_EIP_OFFSET]")
        self.assertGreater(stack, -1)
        self.assertGreater(fp, stack)
        self.assertGreater(release, fp)
        self.assertGreater(interrupt, release)
        self.assertGreater(entry, interrupt)

    def test_suspended_frame_carries_pre_bkl_flags_and_restores_them(self):
        self.assertIn("mov ecx, [esp + 12]", self.handoff)
        self.assertIn("push ecx", self.handoff)
        self.assertNotIn("pushfd", self.handoff)
        self.assertIn("popfd", self.resume)


if __name__ == "__main__":
    unittest.main()
