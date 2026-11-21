## @file
#  @brief Provide serial logs, monitor sockets, and disk fixtures for GUI smoke tests.
import wave
from pathlib import Path
from unittest import mock

from tools import gui_terminal_contracts


REPO_ROOT = Path(__file__).resolve().parents[1]
JPEG_FIXTURE = REPO_ROOT / "test_iso" / "fixtures" / "jpeg_baseline_8x8.jpg"
GFXGUI_SOURCE = REPO_ROOT / "bin" / "gfxgui_test.cc"
FEATURE7_SOURCE = REPO_ROOT / "bin" / "feature7_new_del.cc"


def _smp_runtime_log():
    required = [
        "[csprng] seeded from RDRAND",
        "mp: discovered 1 CPUs, 1 IOAPIC(s)",
        "acpi: MADT: 4 CPUs, 1 IOAPIC(s)",
        "[fpu] SSE2 enabled",
        "cpu1: online apic=1",
        "cpu2: online apic=2",
        "cpu3: online apic=3",
        "smp: 4 CPUs online (of 4 discovered)",
        "e1000: init OK",
        "Scheduler started",
        "[fpu] boot smoke ok",
        "FPU boot smoke passed",
        "Entering desktop environment",
        "Terminal launched",
        "[cupidc] JIT execution complete",
        (
            "[tls-selftest] all 62 crypto, ASN.1, and X.509 "
            "checks passed"
        ),
    ]
    checks = [
        f"[tls-selftest] ok: fixture {index}"
        for index in range(62)
    ]
    return "\n".join([*required, *checks]) + "\n"


def _frontier_runtime_log():
    return "\n".join(
        [
            "[0.010] [INFO]  pci: enumerated 10 devices",
            "[0.020] [INFO]  e1000: init OK mmio=0xfebc0000 irq=10",
            (
                "[0.990] [INFO]  dhcp: bound ip=0x0f02000a "
                "mask=0x00ffffff gw=0x0202000a dns=0x0302000a"
            ),
            "[0.990] [INFO]  net: if=e1000 ip=10.0.2.15",
            "[ac97] present: NAM=0xC000 NABM=0xC400",
            "[0.040] [INFO]  ehci: init OK mmio=0xfebf1000 ports=6 irq=11",
            "[0.070] [INFO]  uhci: init OK io=0x0000c540 irq=11",
            "[0.070] [INFO]  RTC: 2026-07-24 22:14:05",
            "[0.100] [INFO]  FAT16 mounted at /disk",
            "[1.600] [INFO]  VBE graphics initialized (640x480, 32bpp)",
            "[SYSCALL] Syscall table initialized (v5, 412 bytes)",
            "[5.600] [INFO]  Entering desktop environment",
            "[5.650] [INFO]  usb_hid: keyboard attached addr=1",
            "[5.700] [INFO]  usb_hid: mouse attached addr=2",
            "[5.710] [INFO]  usb_hid: mouse activity report=1",
            (
                "[5.730] [INFO]  usb: dev addr=3 speed=2 "
                "vid=0x000046f4 pid=0x00000001 class=0x00000008"
            ),
            "[5.750] [INFO]  usb_msc: usb0 65536x512 inq='QEMU    '",
            (
                "[5.760] [INFO]  usb_msc: usb0 has 1 FAT16 partition(s); "
                "auto-mount NYI, use raw /dev/usb0"
            ),
            "[12.010] [INFO]  Terminal launched (wid=1, pid=3)",
            "[ac97] DMA refills during audiotest: 441",
            "[PASS] audiotest all",
            "godsong: done",
        ]
    ) + "\n"


def _frontier_command_outputs():
    return [
        (
            "[cupidc] JIT compile: /bin/ls.cc\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/kbdsub_test.cc\n"
            "[kbdsub] waiting for USB Shift make/break\n"
            "[PASS] kbdsub: subscribe/unsubscribe round-trip\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/date.cc\n"
            "[print_int] num=1784931245 (0x0x6a67566d) gui_mode=1\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[asm] JIT assemble: /demos/syscall_vfs_extended_demo.asm\n"
            "[asm] Assembled: 96 bytes code, 112 bytes data\n"
            "extended SYS VFS calls: OK\n"
            "[asm] JIT execution complete\n"
        ),
        (
            "01100120:  0F 9B C2  setnp dl\n"
            "01100123:  20 D0  and al, dl\n"
            "01100125:  0F B6 C0  movzx eax, al\n"
            "01100210:  0F 9A C2  setp dl\n"
            "01100213:  08 D0  or al, dl\n"
            "01100215:  0F B6 C0  movzx eax, al\n"
        ),
        (
            "[cupidc] JIT compile: /bin/test_fpaug.cc\n"
            "[test_fpaug-parity] PASS equal=1 unequal=1 truth=1\n"
            "PASS test_fpaug\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/feature7_new_del.cc\n"
            "feature7_new_del: PASS\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/feature13_double.cc\n"
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
            "[feature13-unary] PASS float=-15 double=-9 "
            "zero=0x80000000 plus=9 reject=1 recovery=1\n"
            "[feature13-compare] PASS ordered=6 mixed=4 "
            "zero=2 unordered=6\n"
            "[feature13-truth] PASS zero=2 nonzero=3 "
            "control=255 nan=1\n"
            "[feature13-update] PASS local=48 global=40 "
            "for=3 zero=0x80000000 nan=2\n"
            "[feature13-indirect-update] PASS score=41 "
            "once=3 zero=0x80000000\n"
            "[feature13-lvalue] PASS array=42 pointer=13 "
            "record=26 sizes=56 unevaluated=1\n"
            "[feature13-unsigned] PASS conversions=4 "
            "remainders=2 once=1\n"
            "[feature13-literal] PASS double=2 float=2 edge=3\n"
            "[feature13-call] PASS checks=10\n"
            "PASS feature13_double\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] AOT compile: /bin/feature13_derived_aot.cc -> "
            "/feature13_derived_aot\n"
            "Compiled: 8192 bytes code, 256 bytes data\n"
            "[cupidc] Wrote ELF: /feature13_derived_aot "
            "(8192 bytes code, 256 bytes data, entry=0x01100000, "
            "total=12288 bytes)\n"
            "Written to /feature13_derived_aot\n"
        ),
        (
            "[elf] Loaded /feature13_derived_aot as PID 11 "
            "(ELF32, 12288 bytes at 0x01100000)\n"
            "[feature13-derived-aot] PASS score=41 "
            "once=2 zero=0x80000000\n"
            '[PROCESS] PID 11 "/feature13_derived_aot" exiting\n'
        ),
        (
            "[cupidc] JIT compile: /bin/feature14_simd.cc\n"
            "[feature14-operator] PASS float=4 double=4\n"
            "[feature14-array] PASS global=2 local=2 static=2 "
            "sizeof=16 index=1\n"
            "[feature14-matrix] PASS global=2 local=2 static=2 "
            "sizes=8 index=6 unevaluated=2 canary=4\n"
            "[feature14-update] PASS direct=6 leaves=3 once=6 "
            "payload=8\n"
            "[feature14-call] PASS float4=4 double2=2 nested=2 calls=6\n"
            "[feature14-callback] PASS float4=4 double2=2 calls=2\n"
            "[feature14-callback-typedef] PASS float4=4 calls=1\n"
            "[feature14-callback-global] PASS float4=4 initialized=1 "
            "assigned=1 cleared=1 calls=2\n"
            "[feature14-callback-raw] PASS initialized=1 parameter=1 "
            "cleared=1 reassigned=1 calls=3\n"
            "[feature14-callback-raw-array] PASS modes=2 phases=3 "
            "calls=12 stored=1 persistent=1\n"
            "[feature14-callback-raw-automatic-array] PASS zeroed=4 "
            "initialized=2 assigned=1 copied=2 later=1 calls=4\n"
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43\n"
            "[feature14-callback-binding] PASS call=1 ignored=1 callback=0\n"
            "[feature14-callback-automatic] PASS local=4 method=4 calls=2\n"
            "[feature14-callback-field] PASS stored=1 copied=1 cleared=1 "
            "float4=4 calls=1\n"
            "[feature14-callback-field-call] PASS typedef=1 raw=1 "
            "float4=4 once=1 calls=2\n"
            "[feature14-minmax] PASS nan=4 signed_zero=4\n"
            "[feature14-nan] PASS float_left=4 float_right=0 "
            "double_left=4 double_right=0\n"
            "PASS feature14_simd\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/feature15_libm.cc\n"
            "[feature15-x87] 7 range checks, 0 failed\n"
            "[feature15] 29 checks total, 0 failed\n"
            "PASS feature15_libm\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/feature17_iso.cc\n"
            "PASS feature17_readdir names=6 "
            "long=long_named_file.txt\n"
            "PASS jpeg_decode_mem baseline 8x8 gray128\n"
            "PASS glyph_rasterize Liberation Mono Q size37 "
            "width=22 cache=22\n"
            "PASS feature17_binding calls=9 bytes=8\n"
            "PASS feature17_iso\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/feature18_swap.cc\n"
            "PASS feature18_binding sizes=1 handles=5 bytes=8\n"
            "PASS feature18_swap\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/feature23_full_access.cc\n"
            "PASS feature23_binding bindings=20 slots=4 unsigned=4 peer=1 halfwords=8 guards=2\n"
            "PASS feature23_initializers arrays=5 calls=2\n"
            "PASS feature23_halfword_types strides=2 rows=3 broadcasts=4 casts=2\n"
            "[feature23] PASS\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] AOT compile: /bin/feature23_full_access.cc -> /feature23_binding_aot\n"
            "[cupidc] Wrote ELF: /feature23_binding_aot "
            "(4096 bytes code, 512 bytes data, entry=0x01100000, total=12288 bytes)\n"
        ),
        (
            "[elf] Loaded /feature23_binding_aot as PID 17\n"
            "PASS feature23_binding bindings=20 slots=4 unsigned=4 peer=1 halfwords=8 guards=2\n"
            "PASS feature23_initializers arrays=5 calls=2\n"
            "PASS feature23_halfword_types strides=2 rows=3 broadcasts=4 casts=2\n"
            "[feature23] PASS\n"
            '[PROCESS] PID 17 "/feature23_binding_aot" exiting\n'
        ),
        (
            "[cupidc] AOT compile: /bin/gfxgui_test.cc -> "
            "/gfxgui_test\n"
            "Compiled: 4096 bytes code, 512 bytes data\n"
            "[cupidc] Wrote ELF: /gfxgui_test "
            "(4096 bytes code, 512 bytes data, entry=0x01100000, "
            "total=8704 bytes)\n"
            "Written to /gfxgui_test\n"
        ),
        (
            "[cupidc] JIT compile: /bin/gfxgui_test.cc\n"
            "[gfxgui_test] init\n"
            "[gfxgui_test] assets ready\n"
            "[gfxgui_test] fullscreen\n"
            "[gfxgui_test] font ready\n"
            "[gfxgui_test] surface ready\n"
            "[gfxgui_test] transform ready\n"
            "[gfxgui_test] frame 0 done\n"
            "[gfx2d] flip frame=2\n"
            "[gfxgui_test] frame 240 done\n"
            "[gfxgui_test] done\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] AOT compile: /bin/gfxhandoff_exit.cc -> "
            "/gfxhandoff_exit\n"
            "Compiled: 1024 bytes code, 64 bytes data\n"
            "[cupidc] Wrote ELF: /gfxhandoff_exit "
            "(1024 bytes code, 64 bytes data, entry=0x01100000, "
            "total=2048 bytes)\n"
            "Written to /gfxhandoff_exit\n"
        ),
        (
            "[cupidc] AOT compile: /bin/gfxhandoff_kill.cc -> "
            "/gfxhandoff_kill\n"
            "Compiled: 1024 bytes code, 64 bytes data\n"
            "[cupidc] Wrote ELF: /gfxhandoff_kill "
            "(1024 bytes code, 64 bytes data, entry=0x01100000, "
            "total=2048 bytes)\n"
            "Written to /gfxhandoff_kill\n"
        ),
        (
            "[elf] Loaded /gfxhandoff_exit as PID 17 "
            "(ELF32, 2048 bytes at 0x01100000)\n"
            "[PROCESS] Delayed killer PID 18 waiting for PID 17 reuse\n"
            "[gfxhandoff_exit] nested owner exiting\n"
        ),
        (
            "[elf] Loaded /gfxhandoff_kill as PID 17 "
            "(ELF32, 2048 bytes at 0x01100000)\n"
            "[gfxhandoff_kill] nested owner waiting for remote kill\n"
            "[PROCESS] Delayed killer PID 19 targeting PID 17 after 7000 ms\n"
            "[PROCESS] Delayed kill skipped stale PID 17\n"
            "[PROCESS] Killing PID 17 \"/gfxhandoff_kill\"\n"
        ),
        (
            "[elf] Loaded /gfxgui_test as PID 17 "
            "(ELF32, 8704 bytes at 0x01100000)\n"
            "[gfxgui_test] init\n"
            "[gfxgui_test] assets ready\n"
            "[gfxgui_test] fullscreen\n"
            "[gfxgui_test] font ready\n"
            "[gfxgui_test] surface ready\n"
            "[gfxgui_test] transform ready\n"
            "[gfxgui_test] frame 0 done\n"
            "[gfxgui_test] frame 240 done\n"
            "[gfxgui_test] done\n"
        ),
        (
            "[cupidc] JIT compile: /bin/dglibc_test.cc\n"
            "[PASS] dglibc snprintf\n"
            "[PASS] dglibc malloc/free\n"
            "[PASS] dglibc setjmp/longjmp and exit envelope\n"
            "[PASS] dglibc checked integer parsing\n"
            "[PASS] dglibc Doom exit callback lifecycle\n"
            "[PASS] dglibc Doom path resolution\n"
            "[PASS] dglibc shared errno bridge\n"
            "[PASS] dglibc Doom config round trip\n"
            "[PASS] dglibc synthetic config filesystem bridge\n"
            "[PASS] dglibc synthetic save filesystem bridge\n"
            "[PASS] dglibc VFS rename boundaries\n"
            "[PASS] dglibc VFS copy boundaries\n"
            "[PASS] dglibc block cache failure boundary\n"
            "[PASS] dglibc RamFS size boundary\n"
            "[PASS] dglibc FAT directory collision\n"
            "[PASS] dglibc FAT read boundary\n"
            "[PASS] dglibc FAT handle exhaustion\n"
            "[PASS] dglibc FAT busy replacement\n"
            "[PASS] dglibc FAT 8.3 path boundary\n"
            "[PASS] dglibc HomeFS mount boundary\n"
            "[PASS] dglibc HomeFS depth boundary\n"
            "[PASS] dglibc HomeFS batch boundary\n"
            "[PASS] dglibc_test\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "doom: no WAD found in /disk/wads/ or /home/doom/.\n"
            "       try: doom -iwad /path/to/your.wad\n"
        ),
        (
            "IWAD file '/disk/missing.wad' not found!\n"
            "[doom] returned to shell\n"
        ),
        (
            "[cupidc] JIT compile: /bin/ls.cc\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/browser.cc\n"
            "[js] parse error: js: expected exponent digits\n"
            "[js] parse error: js: expected hexadecimal digits\n"
            "[js] parse error: js: invalid binary digit\n"
            "[js] parse error: js: invalid octal digit\n"
            "[js] parse error: js: invalid numeric separator\n"
            "[js] parse error: js: invalid numeric separator\n"
            "[js] parse error: js: invalid numeric separator\n"
            "[js] parse error: js: invalid numeric separator\n"
            "[js] parse error: js: invalid numeric separator\n"
            "[js] parse error: js: identifier follows numeric literal\n"
            "[browser-js-number] PASS close=1 large=1 negzero=1 nan=1 "
            "truth=1 nanformat=1 posinfformat=1 neginfformat=1 literal=1 "
            "signedexp=1 upperexp=1 order=1 divide=1 divideassign=1 "
            "remainder=1 expcap=1 radix=1 separators=1 tonumber=1 "
            "looseeq=1 stringrel=1 largefmod=1 modassign=1 "
            "strplusassign=1 reject=1 recovery=1\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/audiotest.cc\n"
            "[ac97] DMA refills during audiotest: 441\n"
            "[PASS] audiotest all\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/godsong.cc\n"
            "[cupidc] Executing at 0x0x01100000\n"
            "[godsong] settings ready\n"
            "[gfx2d] popup input ready\n"
            "[print_int] num=1 (0x0x00000001) gui_mode=1\n"
            "[print_int] num=200 (0x0x000000c8) gui_mode=1\n"
            "[cupidc] JIT execution complete\n"
        ),
    ]


def _frontier_command(text, occurrence=0):
    commands = [
        command
        for command in gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS
        if command.text == text
    ]
    return commands[occurrence]


def _frontier_command_output(text, occurrence=0):
    command_indexes = [
        index
        for index, command in enumerate(
            gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS
        )
        if command.text == text
    ]
    return _frontier_command_outputs()[command_indexes[occurrence]]


def _write_ppm(path, width, height, pixels):
    path.write_bytes(
        f"P6\n# unit fixture\n{width} {height}\n255\n".encode("ascii")
        + b"".join(pixels)
    )


def _write_wav(path, samples):
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(22050)
        output.writeframes(
            b"".join(
                int(sample).to_bytes(2, "little", signed=True)
                for sample in samples
            )
        )


def _write_qemu_placeholder_wav(path, samples):
    _write_wav(path, samples)
    contents = bytearray(path.read_bytes())
    contents[4:8] = b"\0\0\0\0"
    contents[40:44] = b"\0\0\0\0"
    path.write_bytes(contents)


def _write_frontier_usb_image(path):
    partition_sectors = 5000
    image = bytearray((partition_sectors + 1) * 512)
    partition = 0x1BE
    image[partition + 4] = 0x06
    image[partition + 8:partition + 12] = (1).to_bytes(4, "little")
    image[partition + 12:partition + 16] = partition_sectors.to_bytes(
        4,
        "little",
    )
    image[510:512] = b"\x55\xaa"

    boot = 512
    image[boot:boot + 3] = b"\xeb\x3c\x90"
    image[boot + 3:boot + 11] = b"CUPIDOS "
    image[boot + 11:boot + 13] = (512).to_bytes(2, "little")
    image[boot + 13] = 1
    image[boot + 14:boot + 16] = (1).to_bytes(2, "little")
    image[boot + 16] = 2
    image[boot + 17:boot + 19] = (512).to_bytes(2, "little")
    image[boot + 19:boot + 21] = partition_sectors.to_bytes(2, "little")
    image[boot + 21] = 0xF8
    image[boot + 22:boot + 24] = (20).to_bytes(2, "little")
    image[boot + 54:boot + 62] = b"FAT16   "
    image[boot + 510:boot + 512] = b"\x55\xaa"
    path.write_bytes(image)


class FakeMonitorSocket:
    def __init__(self):
        self.sent = []
        self.closed = False

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, _size):
        return b"(qemu)"

    def close(self):
        self.closed = True


class SequencedMonitorSocket(FakeMonitorSocket):
    def __init__(self, log, command_outputs, delayed_marker=None):
        super().__init__()
        self.log = log
        self.command_outputs = list(command_outputs)
        self.delayed_marker = delayed_marker
        self.pending_marker = None
        self.pending_tail = None
        self.marker_wait_sent_count = None
        self.completed = 0

    def append_log(self, text):
        with self.log.open("a", encoding="utf-8") as output:
            output.write(text)

    def release_delayed_marker(self):
        if self.pending_marker is None:
            raise AssertionError("no delayed interaction marker is pending")
        self.append_log(self.pending_marker)
        self.pending_marker = None

    def sendall(self, data):
        super().sendall(data)
        if data.startswith(b"sendkey ret ") and self.command_outputs:
            command_output = self.command_outputs.pop(0)
            if not self.command_outputs and self.delayed_marker is not None:
                prefix, marker, tail = command_output.partition(
                    self.delayed_marker
                )
                if marker == "":
                    raise AssertionError(
                        "delayed interaction marker is absent"
                    )
                self.append_log(prefix)
                self.pending_marker = marker
                self.pending_tail = tail
                self.marker_wait_sent_count = len(self.sent)
            else:
                self.append_log(command_output)
            self.completed += 1
        elif (
            data.startswith(b"sendkey ")
            and self.pending_marker is None
            and self.pending_tail is not None
        ):
            self.append_log(self.pending_tail)
            self.pending_tail = None


class ReplugMonitorSocket(FakeMonitorSocket):
    def __init__(
        self,
        log,
        *,
        omit_prefix=None,
        failure_storage_cycle=None,
    ):
        super().__init__()
        self.log = log
        self.omit_prefix = omit_prefix
        self.failure_storage_cycle = failure_storage_cycle
        self.keyboard_online = False
        self.mouse_online = False
        self.mouse_reported = False
        self.storage_cycle = 0

    def append_log(self, text):
        with self.log.open("a", encoding="utf-8") as output:
            output.write(text)

    def sendall(self, data):
        super().sendall(data)
        command = data.decode("ascii").strip()
        if (
            self.omit_prefix is not None
            and command.startswith(self.omit_prefix)
        ):
            return

        if command == "device_del frontier_keyboard":
            self.append_log(
                "usb_hid: keyboard detached\n"
                "usb: removed device addr=1\n"
            )
        elif command.startswith(
            "device_add usb-kbd,id=frontier_keyboard_replug,"
        ):
            self.keyboard_online = True
            self.append_log(
                "usb: dev addr=1 speed=1 vid=0x00000627 "
                "pid=0x00000001 class=0x00000003\n"
                "usb_hid: keyboard attached addr=1\n"
            )
        elif (
            command == "sendkey ret 300"
            and self.keyboard_online
        ):
            self.append_log(_frontier_command_output("ls"))
        elif command == "device_del frontier_mouse":
            self.append_log(
                "usb_hid: mouse detached\n"
                "usb: removed device addr=2\n"
            )
        elif command.startswith(
            "device_add usb-mouse,id=frontier_mouse_replug,"
        ):
            self.mouse_online = True
            self.append_log(
                "usb: dev addr=2 speed=1 vid=0x00000627 "
                "pid=0x00000001 class=0x00000003\n"
                "usb_hid: mouse attached addr=2\n"
            )
        elif (
            command.startswith("mouse_move ")
            and self.mouse_online
            and not self.mouse_reported
        ):
            self.mouse_reported = True
            self.append_log("usb_hid: mouse activity report=2\n")
        elif command.startswith("device_del frontier_mass_storage"):
            self.append_log(
                "usb_msc: detached\n"
                "usb: removed device addr=3\n"
            )
        elif command.startswith("device_add usb-storage,"):
            self.storage_cycle += 1
            if self.storage_cycle == self.failure_storage_cycle:
                self.append_log(
                    "usb_msc: block device registry is full\n"
                )
            self.append_log(
                "usb: dev addr=3 speed=2 vid=0x000046f4 "
                "pid=0x00000001 class=0x00000008\n"
                f"usb_msc: usb{self.storage_cycle} "
                "65536x512 inq='QEMU    '\n"
                f"usb_msc: usb{self.storage_cycle} has 1 FAT16 "
                f"partition(s); auto-mount NYI, use raw "
                f"/dev/usb{self.storage_cycle}\n"
            )
