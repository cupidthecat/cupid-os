## @file
#  @brief Define the commands and serial markers for Cupid OS runtime tests.

from __future__ import annotations

import re
from dataclasses import dataclass


FRONTIER_STORAGE_REATTACHMENTS = 5
SMP_RUNTIME_TLS_SUCCESS_COUNT = 62
SMP_RUNTIME_REQUIRED_MARKERS = (
    "[csprng] seeded from RDRAND",
    "mp: discovered 1 CPUs, 1 IOAPIC(s)",
    "acpi: MADT: 4 CPUs, 1 IOAPIC(s)",
    "[fpu] SSE2 enabled",
    "cpu1: online",
    "cpu2: online",
    "cpu3: online",
    "smp: 4 CPUs online (of 4 discovered)",
    "Scheduler started",
    "[fpu] boot smoke ok",
    "FPU boot smoke passed",
    "Entering desktop environment",
    "Terminal launched",
    "[cupidc] JIT execution complete",
    "[tls-selftest] all 62 crypto, ASN.1, and X.509 checks passed",
)
SMP_RUNTIME_REJECTED_MARKERS = (
    "KERNEL PANIC",
    "[PANIC] CPU Exception",
    "Heap corruption",
    "CORRUPTION detected",
    "[tls-selftest] FAIL",
    "self-test failed",
    "illegal instruction",
    "failed to come online",
    "smp: stack oom",
    "ATA: Timeout/error",
    "ATA: Write error",
    "Block cache initialization failed",
    "Block cache: writeback failed",
    "Block cache: disk read failed",
    "Block cache: flush failed",
    "[homefs] flush failed",
    "FAT16: write incomplete",
)
NIC_RUNTIME_PATTERNS = {
    "e1000": (
        ("e1000 initialization", r"e1000: init OK"),
        (
            "e1000 packet traffic",
            r"dhcp: bound ip=0x(?!00000000\b)[0-9A-Fa-f]{8}"
            r".*net: if=e1000 ip=[1-9][0-9]*\.[0-9]+\.[0-9]+\.[0-9]+",
        ),
    ),
    "rtl8139": (
        ("RTL8139 initialization", r"rtl8139: init OK"),
        (
            "RTL8139 packet traffic",
            r"dhcp: bound ip=0x(?!00000000\b)[0-9A-Fa-f]{8}"
            r".*net: if=rtl8139 ip=[1-9][0-9]*\.[0-9]+\.[0-9]+\.[0-9]+",
        ),
    ),
}
NIC_RUNTIME_REJECTED_MARKERS = (
    "e1000: BAR0 not MMIO",
    "e1000: rx ring alloc failed",
    "e1000: rx buf alloc failed",
    "e1000: tx ring alloc failed",
    "e1000: tx buf alloc failed",
    "rtl8139: BAR0 not IO port",
    "rtl8139: rx alloc failed",
    "rtl8139: tx alloc failed",
)
CUPIDC_COMPLETION_PATTERN = r"\[cupidc\] JIT execution complete"
ASM_COMPLETION_PATTERN = r"\[asm\] JIT execution complete"
GODSONG_SETTINGS_READY_PATTERN = (
    r"^\[godsong\] settings ready\r?$"
    r".*?^\[gfx2d\] popup input ready\r?$"
)
UNARY_TYPE_DIAGNOSTIC_LITERAL = (
    "[cupidc] error (line 1): "
    "unary sign requires an arithmetic scalar operand"
)
UNARY_TYPE_DIAGNOSTIC_PATTERN = (
    r"^" + re.escape(UNARY_TYPE_DIAGNOSTIC_LITERAL) + r"\r?\n"
)
FEATURE13_COMPILE_LITERAL = (
    "[cupidc] JIT compile: /bin/feature13_double.cc"
)
FEATURE13_COMPILE_PATTERN = (
    r"^" + re.escape(FEATURE13_COMPILE_LITERAL) + r"\r?$"
)


@dataclass(frozen=True)
class TerminalCommand:
    """One terminal command and the serial evidence that completes it."""

    text: str
    expected_pattern: str
    followup_keys: tuple[str, ...] = ()
    interaction_pattern: str | None = None
    followup_settle_seconds: float = 0.0
    allowed_failure_pattern: str | None = None
    allowed_failure_literal: str | None = None
    allowed_failure_context_pattern: str | None = None
    timeout_seconds: float | None = None
    capture_name: str | None = None
    pid_from_capture: str | None = None


FRONTIER_RUNTIME_COMMANDS = (
    TerminalCommand(
        "ls",
        (
            r"\[cupidc\] JIT compile: /bin/ls\.cc"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "/bin/kbdsub_test.cc",
        (
            r"\[cupidc\] JIT compile: /bin/kbdsub_test\.cc"
            r".*?\[PASS\] kbdsub: subscribe/unsubscribe round-trip"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
        ("shift",),
        r"\[kbdsub\] waiting for USB Shift make/break",
    ),
    TerminalCommand(
        "/bin/date.cc +epoch",
        (
            r"\[cupidc\] JIT compile: /bin/date\.cc"
            r".*?\[print_int\] num=[1-9][0-9]{8,9} "
            r"\(0x0x[0-9A-Fa-f]+\) gui_mode=1"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "as /demos/syscall_vfs_extended_demo.asm",
        (
            r"\[asm\] JIT assemble: "
            r"/demos/syscall_vfs_extended_demo\.asm"
            r".*?\[asm\] Assembled: [1-9][0-9]* bytes code, "
            r"[0-9]+ bytes data"
            r".*?extended SYS VFS calls: OK"
            rf".*?{ASM_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "dis /bin/test_fpaug.cc",
        (
            r"0F 9B C2  setnp dl"
            r".*?20 D0  and al, dl"
            r".*?0F 9A C2  setp dl"
            r".*?08 D0  or al, dl"
            r".*?0F B6 C0  movzx eax, al"
        ),
    ),
    TerminalCommand(
        "/bin/test_fpaug.cc",
        (
            r"\[cupidc\] JIT compile: /bin/test_fpaug\.cc"
            r".*?\[test_fpaug-parity\] PASS equal=1 unequal=1 truth=1"
            rf".*?PASS test_fpaug.*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "/bin/feature7_new_del.cc",
        (
            r"\[cupidc\] JIT compile: /bin/feature7_new_del\.cc"
            r".*?feature7_new_del: PASS"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "/bin/feature13_double.cc",
        (
            FEATURE13_COMPILE_PATTERN
            + rf".*?{UNARY_TYPE_DIAGNOSTIC_PATTERN}"
            r".*?\[feature13-unary\] PASS float=-15 double=-9 "
            r"zero=0x80000000 plus=9 reject=1 recovery=1"
            r".*?\[feature13-compare\] PASS ordered=6 mixed=4 "
            r"zero=2 unordered=6"
            r".*?\[feature13-truth\] PASS zero=2 nonzero=3 "
            r"control=255 nan=1"
            r".*?\[feature13-update\] PASS local=48 global=40 "
            r"for=3 zero=0x80000000 nan=2"
            r".*?\[feature13-indirect-update\] PASS score=41 "
            r"once=3 zero=0x80000000"
            r".*?\[feature13-lvalue\] PASS array=42 pointer=13 "
            r"record=26 sizes=56 unevaluated=1"
            r".*?\[feature13-unsigned\] PASS conversions=4 "
            r"remainders=2 once=1"
            r".*?\[feature13-literal\] PASS double=2 float=2 edge=3"
            r".*?\[feature13-call\] PASS checks=10"
            rf".*?PASS feature13_double.*?{CUPIDC_COMPLETION_PATTERN}"
        ),
        allowed_failure_pattern=UNARY_TYPE_DIAGNOSTIC_PATTERN,
        allowed_failure_literal=UNARY_TYPE_DIAGNOSTIC_LITERAL,
        allowed_failure_context_pattern=FEATURE13_COMPILE_PATTERN,
    ),
    TerminalCommand(
        "ccc /bin/feature13_derived_aot.cc -o /feature13_derived_aot",
        (
            r"\[cupidc\] AOT compile: /bin/feature13_derived_aot\.cc -> "
            r"/feature13_derived_aot"
            r".*?\[cupidc\] Wrote ELF: /feature13_derived_aot "
            r"\([1-9][0-9]* bytes code, [0-9]+ bytes data, "
            r"entry=0x(?:0x)?[0-9A-Fa-f]+, "
            r"total=[1-9][0-9]* bytes\)"
        ),
        timeout_seconds=180.0,
    ),
    TerminalCommand(
        "exec /feature13_derived_aot",
        (
            r"\[elf\] Loaded /feature13_derived_aot as PID "
            r"(?P<pid>[1-9][0-9]*)"
            r".*?\[feature13-derived-aot\] PASS score=41 "
            r"once=2 zero=0x80000000"
            r".*?\[PROCESS\] PID (?P=pid) "
            r'"/feature13_derived_aot" exiting'
        ),
        timeout_seconds=180.0,
    ),
    TerminalCommand(
        "/bin/feature14_simd.cc",
        (
            r"\[cupidc\] JIT compile: /bin/feature14_simd\.cc"
            r".*?\[feature14-operator\] PASS float=4 double=4"
            r".*?\[feature14-array\] PASS global=2 local=2 "
            r"static=2 sizeof=16 index=1"
            r".*?\[feature14-matrix\] PASS global=2 local=2 static=2 "
            r"sizes=8 index=6 unevaluated=2 canary=4"
            r".*?\[feature14-update\] PASS direct=6 leaves=3 once=6 "
            r"payload=8"
            r".*?\[feature14-call\] PASS float4=4 double2=2 nested=2 "
            r"calls=6"
            r".*?\[feature14-callback\] PASS float4=4 double2=2 calls=2"
            r".*?\[feature14-callback-typedef\] PASS float4=4 calls=1"
            r".*?\[feature14-callback-global\] PASS float4=4 initialized=1 "
            r"assigned=1 cleared=1 calls=2"
            r".*?\[feature14-callback-raw\] PASS initialized=1 parameter=1 "
            r"cleared=1 reassigned=1 calls=3"
            r".*?\[feature14-callback-raw-array\] PASS modes=2 phases=3 "
            r"calls=12 stored=1 persistent=1"
            r".*?\[feature14-callback-raw-automatic-array\] PASS "
            r"zeroed=4 initialized=2 assigned=1 copied=2 later=1 calls=4"
            r".*?\[feature14-callback-nested\] PASS outer=1 inner=1 value=43"
            r".*?\[feature14-callback-binding\] PASS call=1 ignored=1 "
            r"callback=0"
            r".*?\[feature14-callback-automatic\] PASS local=4 method=4 "
            r"calls=2"
            r".*?\[feature14-callback-field\] PASS stored=1 copied=1 "
            r"cleared=1 float4=4 calls=1"
            r".*?\[feature14-callback-field-call\] PASS typedef=1 raw=1 "
            r"float4=4 once=1 calls=2"
            r".*?\[feature14-minmax\] PASS nan=4 signed_zero=4"
            r".*?\[feature14-nan\] PASS float_left=[0-4] "
            r"float_right=[0-4] double_left=[0-4] double_right=[0-4]"
            rf".*?PASS feature14_simd.*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "/bin/feature15_libm.cc",
        (
            r"\[cupidc\] JIT compile: /bin/feature15_libm\.cc"
            r".*?\[feature15-x87\] 7 range checks, 0 failed"
            r".*?\[feature15\] 29 checks total, 0 failed"
            rf".*?PASS feature15_libm.*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "/bin/feature17_iso.cc",
        (
            r"\[cupidc\] JIT compile: /bin/feature17_iso\.cc"
            r".*?PASS feature17_readdir names=6 "
            r"long=long_named_file\.txt"
            r".*?PASS jpeg_decode_mem baseline 8x8 gray128"
            r".*?PASS glyph_rasterize Liberation Mono Q size37 "
            r"width=(?P<glyph_width>[1-9][0-9]*) "
            r"cache=(?P=glyph_width)"
            r".*?PASS feature17_binding calls=9 bytes=8"
            rf".*?PASS feature17_iso.*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "/bin/feature18_swap.cc",
        (
            r"\[cupidc\] JIT compile: /bin/feature18_swap\.cc"
            r".*?PASS feature18_binding sizes=1 handles=5 bytes=8"
            rf".*?PASS feature18_swap.*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "/bin/feature23_full_access.cc",
        (
            r"\[cupidc\] JIT compile: /bin/feature23_full_access\.cc"
            r".*?PASS feature23_binding bindings=20 slots=4 unsigned=4 peer=1 halfwords=8 guards=2"
            r".*?PASS feature23_initializers arrays=5 calls=2"
            r".*?PASS feature23_halfword_types strides=2 rows=3 broadcasts=4 casts=2"
            r".*?\[feature23\] PASS"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "ccc /bin/feature23_full_access.cc -o /feature23_binding_aot",
        (
            r"\[cupidc\] AOT compile: /bin/feature23_full_access\.cc -> "
            r"/feature23_binding_aot"
            r".*?\[cupidc\] Wrote ELF: /feature23_binding_aot "
            r"\([1-9][0-9]* bytes code, [0-9]+ bytes data, "
            r"entry=0x(?:0x)?[0-9A-Fa-f]+, total=[1-9][0-9]* bytes\)"
        ),
        timeout_seconds=180.0,
    ),
    TerminalCommand(
        "exec /feature23_binding_aot",
        (
            r"\[elf\] Loaded /feature23_binding_aot as PID (?P<pid>[1-9][0-9]*)"
            r".*?PASS feature23_binding bindings=20 slots=4 unsigned=4 peer=1 halfwords=8 guards=2"
            r".*?PASS feature23_initializers arrays=5 calls=2"
            r".*?PASS feature23_halfword_types strides=2 rows=3 broadcasts=4 casts=2"
            r".*?\[feature23\] PASS"
            r'.*?\[PROCESS\] PID (?P=pid) "/feature23_binding_aot" exiting'
        ),
        timeout_seconds=180.0,
    ),
    TerminalCommand(
        "ccc /bin/gfxgui_test.cc -o /gfxgui_test",
        (
            r"\[cupidc\] AOT compile: /bin/gfxgui_test\.cc -> "
            r"/gfxgui_test"
            r".*?\[cupidc\] Wrote ELF: /gfxgui_test "
            r"\([1-9][0-9]* bytes code, [0-9]+ bytes data, "
            r"entry=0x(?:0x)?[0-9A-Fa-f]+, "
            r"total=[1-9][0-9]* bytes\)"
        ),
        timeout_seconds=180.0,
    ),
    TerminalCommand(
        "/bin/gfxgui_test.cc",
        (
            r"\[cupidc\] JIT compile: /bin/gfxgui_test\.cc"
            r".*?\[gfxgui_test\] init"
            r".*?\[gfxgui_test\] assets ready"
            r".*?\[gfxgui_test\] fullscreen"
            r".*?\[gfxgui_test\] font ready"
            r".*?\[gfxgui_test\] surface ready"
            r".*?\[gfxgui_test\] transform ready"
            r".*?\[gfxgui_test\] frame 0 done"
            r".*?\[gfxgui_test\] frame 240 done"
            r".*?\[gfxgui_test\] done"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
        timeout_seconds=300.0,
    ),
    TerminalCommand(
        "ccc /bin/gfxhandoff_exit.cc -o /gfxhandoff_exit",
        (
            r"\[cupidc\] AOT compile: /bin/gfxhandoff_exit\.cc -> "
            r"/gfxhandoff_exit"
            r".*?\[cupidc\] Wrote ELF: /gfxhandoff_exit "
            r"\([1-9][0-9]* bytes code, [0-9]+ bytes data, "
            r"entry=0x(?:0x)?[0-9A-Fa-f]+, "
            r"total=[1-9][0-9]* bytes\)"
        ),
        timeout_seconds=180.0,
    ),
    TerminalCommand(
        "ccc /bin/gfxhandoff_kill.cc -o /gfxhandoff_kill",
        (
            r"\[cupidc\] AOT compile: /bin/gfxhandoff_kill\.cc -> "
            r"/gfxhandoff_kill"
            r".*?\[cupidc\] Wrote ELF: /gfxhandoff_kill "
            r"\([1-9][0-9]* bytes code, [0-9]+ bytes data, "
            r"entry=0x(?:0x)?[0-9A-Fa-f]+, "
            r"total=[1-9][0-9]* bytes\)"
        ),
        timeout_seconds=180.0,
    ),
    TerminalCommand(
        "exec /gfxhandoff_exit",
        (
            r"\[elf\] Loaded /gfxhandoff_exit as PID (?P<pid>[1-9][0-9]*)"
            r".*?\[PROCESS\] Delayed killer PID [1-9][0-9]* "
            r"waiting for PID (?P=pid) reuse"
            r".*?\[gfxhandoff_exit\] nested owner exiting"
        ),
        capture_name="gfx_owner_pid",
    ),
    TerminalCommand(
        "exec /gfxhandoff_kill {pid}",
        (
            r"\[elf\] Loaded /gfxhandoff_kill as PID {pid}"
            r"(?=.*?\[gfxhandoff_kill\] nested owner waiting for remote kill"
            r".*?\[PROCESS\] Killing PID {pid} \"/gfxhandoff_kill\")"
            r"(?=.*?\[PROCESS\] Delayed kill skipped stale PID {pid}"
            r".*?\[PROCESS\] Killing PID {pid} \"/gfxhandoff_kill\")"
            r"(?=.*?"
            r"\[PROCESS\] Delayed killer PID [1-9][0-9]* "
            r"targeting PID {pid} after 7000 ms"
            r".*?\[PROCESS\] Killing PID {pid} \"/gfxhandoff_kill\")"
            r".*?\[PROCESS\] Killing PID {pid} \"/gfxhandoff_kill\""
        ),
        pid_from_capture="gfx_owner_pid",
    ),
    TerminalCommand(
        "exec /gfxgui_test",
        (
            r"\[elf\] Loaded /gfxgui_test as PID {pid}"
            r".*?\[gfxgui_test\] init"
            r".*?\[gfxgui_test\] assets ready"
            r".*?\[gfxgui_test\] fullscreen"
            r".*?\[gfxgui_test\] font ready"
            r".*?\[gfxgui_test\] surface ready"
            r".*?\[gfxgui_test\] transform ready"
            r".*?\[gfxgui_test\] frame 0 done"
            r".*?\[gfxgui_test\] frame 240 done"
            r".*?\[gfxgui_test\] done"
        ),
        timeout_seconds=300.0,
        pid_from_capture="gfx_owner_pid",
    ),
    TerminalCommand(
        "dglibc_test",
        (
            r"\[cupidc\] JIT compile: /bin/dglibc_test\.cc"
            r".*?\[PASS\] dglibc snprintf"
            r".*?\[PASS\] dglibc malloc/free"
            r".*?\[PASS\] dglibc setjmp/longjmp and exit envelope"
            r".*?\[PASS\] dglibc checked integer parsing"
            r".*?\[PASS\] dglibc Doom exit callback lifecycle"
            r".*?\[PASS\] dglibc Doom path resolution"
            r".*?\[PASS\] dglibc shared errno bridge"
            r".*?\[PASS\] dglibc Doom config round trip"
            r".*?\[PASS\] dglibc synthetic config filesystem bridge"
            r".*?\[PASS\] dglibc synthetic save filesystem bridge"
            r".*?\[PASS\] dglibc VFS rename boundaries"
            r".*?\[PASS\] dglibc VFS copy boundaries"
            r".*?\[PASS\] dglibc block cache failure boundary"
            r".*?\[PASS\] dglibc RamFS size boundary"
            r".*?\[PASS\] dglibc FAT directory collision"
            r".*?\[PASS\] dglibc FAT read boundary"
            r".*?\[PASS\] dglibc FAT handle exhaustion"
            r".*?\[PASS\] dglibc FAT busy replacement"
            r".*?\[PASS\] dglibc FAT 8.3 path boundary"
            r".*?\[PASS\] dglibc HomeFS mount boundary"
            r".*?\[PASS\] dglibc HomeFS depth boundary"
            r".*?\[PASS\] dglibc HomeFS batch boundary"
            r".*?\[PASS\] dglibc_test"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "doom",
        (
            r"doom: no WAD found in /disk/wads/ or /home/doom/\."
            r".*?try: doom -iwad /path/to/your\.wad"
        ),
    ),
    TerminalCommand(
        "doom -iwad /disk/missing.wad",
        (
            r"IWAD file '/disk/missing\.wad' not found!"
            r".*?\[doom\] returned to shell"
        ),
    ),
    TerminalCommand(
        "ls",
        (
            r"\[cupidc\] JIT compile: /bin/ls\.cc"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "browser --selftest",
        (
            r"\[cupidc\] JIT compile: /bin/browser\.cc"
            r".*?\[js\] parse error: js: expected exponent digits"
            r".*?\[js\] parse error: js: expected hexadecimal digits"
            r".*?\[js\] parse error: js: invalid binary digit"
            r".*?\[js\] parse error: js: invalid octal digit"
            r".*?\[js\] parse error: js: invalid numeric separator"
            r".*?\[js\] parse error: js: invalid numeric separator"
            r".*?\[js\] parse error: js: invalid numeric separator"
            r".*?\[js\] parse error: js: invalid numeric separator"
            r".*?\[js\] parse error: js: invalid numeric separator"
            r".*?\[js\] parse error: js: identifier follows numeric literal"
            r".*?\[browser-js-number\] PASS close=1 large=1 "
            r"negzero=1 nan=1 truth=1 nanformat=1 posinfformat=1 "
            r"neginfformat=1 "
            r"literal=1 signedexp=1 upperexp=1 order=1 divide=1 "
            r"divideassign=1 remainder=1 expcap=1 radix=1 separators=1 "
            r"tonumber=1 looseeq=1 stringrel=1 largefmod=1 modassign=1 "
            r"strplusassign=1 reject=1 recovery=1"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "audiotest all",
        (
            r"\[cupidc\] JIT compile: /bin/audiotest\.cc"
            r".*?\[ac97\] DMA refills during audiotest: [1-9][0-9]*"
            rf".*?\[PASS\] audiotest all.*?{CUPIDC_COMPLETION_PATTERN}"
        ),
    ),
    TerminalCommand(
        "godsong 1 200",
        (
            r"\[cupidc\] JIT compile: /bin/godsong\.cc"
            r".*?\[print_int\] num=1 \(0x0x[0-9A-Fa-f]+\) gui_mode=1"
            r".*?\[print_int\] num=200 \(0x0x[0-9A-Fa-f]+\) gui_mode=1"
            rf".*?{CUPIDC_COMPLETION_PATTERN}"
        ),
        ("esc",) * 8,
        GODSONG_SETTINGS_READY_PATTERN,
    ),
)

FRONTIER_RUNTIME_REQUIRED_PATTERNS = (
    ("PCI enumeration", r"pci: enumerated [1-9][0-9]* devices"),
    (
        "RTC",
        (
            r"RTC: (?:19[7-9][0-9]|20[0-9]{2})-"
            r"(?:0[1-9]|1[0-2])-(?:0[1-9]|[12][0-9]|3[01]) "
            r"(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]"
        ),
    ),
    ("FAT storage", r"FAT16 mounted at /disk"),
    (
        "AC97 initialization",
        r"\[ac97\] present: NAM=0x[0-9A-Fa-f]+ NABM=0x[0-9A-Fa-f]+",
    ),
    (
        "AC97 refill exercise",
        r"\[ac97\] DMA refills during audiotest: [1-9][0-9]*",
    ),
    ("EHCI initialization", r"ehci: init OK"),
    ("UHCI initialization", r"uhci: init OK"),
    ("USB keyboard", r"usb_hid: keyboard attached addr=[1-9][0-9]*"),
    ("USB mouse", r"usb_hid: mouse attached addr=[1-9][0-9]*"),
    (
        "USB storage",
        r"usb_msc: usb[0-9]+ [1-9][0-9]*x[1-9][0-9]*",
    ),
    (
        "USB FAT storage",
        r"usb_msc: usb[0-9]+ has [1-9][0-9]* FAT16 partition\(s\)",
    ),
    (
        "USB mouse input",
        r"usb_hid: mouse activity report=[1-9][0-9]*",
    ),
    (
        "syscall initialization",
        r"\[SYSCALL\] Syscall table initialized \(v[0-9]+, [0-9]+ bytes\)",
    ),
    ("shell", r"Terminal launched"),
    (
        "graphics",
        r"VBE graphics initialized \(640x480, 32bpp\)",
    ),
)

FRONTIER_STORAGE_READY_PATTERN = (
    r"usb: dev addr=[1-9][0-9]* .*class=(?:8|0[xX]0*8)"
    r".*usb_msc: (usb[0-9]+) "
    r"[1-9][0-9]*x[1-9][0-9]*"
    r".*usb_msc: \1 has [1-9][0-9]* "
    r"FAT16 partition\(s\)"
)

FRONTIER_RUNTIME_REJECTED_MARKERS = (
    "FAIL feature23_binding",
    "FAIL feature23_initializers",
    "FAIL feature23_halfword_types",
    "KERNEL PANIC",
    "[PANIC] CPU Exception",
    "Heap corruption",
    "CORRUPTION detected",
    "illegal instruction",
    "ATA: Timeout",
    "ATA: Write error",
    "ATA: No drives detected",
    "Block cache initialization failed",
    "Block cache: writeback failed",
    "Block cache: disk read failed",
    "Block cache: flush failed",
    "[homefs] flush failed",
    "FAT16: write incomplete",
    "RTC: invalid data",
    "usb: work queue full",
    "usb: root port reset failed",
    "usb: no free device slot",
    "usb: first GET_DESC failed",
    "usb: address space exhausted",
    "usb: SET_ADDRESS failed",
    "usb: full GET_DESC failed",
    "usb: GET_CONFIG(short) failed",
    "usb: GET_CONFIG(full) failed",
    "usb: SET_CONFIGURATION failed",
    "usb: driver refused removal",
    "usb_hid: keyboard interrupt registration failed",
    "usb_hid: keyboard interrupt cancellation failed",
    "usb_hid: mouse interrupt registration failed",
    "usb_hid: mouse interrupt cancellation failed",
    "usb_hub: interrupt registration failed",
    "usb_hub: interrupt cancellation failed",
    "ehci: BAR0 not MMIO",
    "ehci: alloc failed",
    "ehci: could not quiesce async schedule",
    "ehci: timeout teardown could not quiesce controller",
    "uhci: BAR4 not IO port",
    "uhci: HC reset stuck",
    "uhci: alloc failed",
    "uhci: too many TDs",
    "uhci: timeout teardown could not halt controller",
    "usb_msc: not ready",
    "usb_msc: read capacity failed",
    "usb_msc: mbr read failed",
    "usb_msc: block device registry is full",
    "usb_msc: detached block device was not registered",
    "[ac97] no AC97 device found",
    "[ac97] OOM allocating BDL/DMA pool",
    "[SKIP] audiotest",
    "[FAIL] audiotest",
    "[FAIL] kbdsub",
    "[cupidc] Unresolved symbol:",
    "[cupidc] error",
    "[asm] error",
    "[test_fpaug-parity] FAIL",
    "FAIL test_fpaug",
    "[feature13-unary] FAIL",
    "[feature13-compare] FAIL",
    "[feature13-truth] FAIL",
    "[feature13-update] FAIL",
    "[feature13-indirect-update] FAIL",
    "[feature13-derived-aot] FAIL",
    "[feature13-lvalue] FAIL",
    "[feature13-unsigned-convert] FAIL",
    "[feature13-unsigned-remainder] FAIL",
    "[feature13-literal] FAIL",
    "[feature13-call] FAIL",
    "FAIL feature13_double",
    "[feature14-operator] FAIL",
    "[feature14-array] FAIL",
    "[feature14-matrix] FAIL",
    "[feature14-update] FAIL",
    "[feature14-call] FAIL",
    "[feature14-callback] FAIL",
    "[feature14-callback-typedef] FAIL",
    "[feature14-callback-global] FAIL",
    "[feature14-callback-raw] FAIL",
    "[feature14-callback-automatic] FAIL",
    "[feature14-callback-field] FAIL",
    "[feature14-callback-field-call] FAIL",
    "[feature14-callback-raw-array] FAIL",
    "[feature14-callback-raw-automatic-array] FAIL",
    "[feature14-callback-nested] FAIL",
    "[feature14-callback-binding] FAIL",
    "[feature14-minmax] FAIL",
    "[feature14-nan] FAIL",
    "FAIL feature14_simd",
    "FAIL jpeg_decode_mem",
    "FAIL glyph_rasterize",
    "FAIL feature15_libm",
    "FAIL feature17_iso",
    "FAIL feature18_swap",
    "[FAIL] dglibc",
    "[gfxgui_test] FAIL",
    "[gfxhandoff_exit] FAIL",
    "[gfxhandoff_kill] FAIL",
    "[browser-js-number] FAIL",
    "extended SYS VFS calls: FAIL",
) + NIC_RUNTIME_REJECTED_MARKERS
