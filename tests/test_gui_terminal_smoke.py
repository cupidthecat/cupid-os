import hashlib
import io
import re
import tempfile
import unittest
import wave
from pathlib import Path
from unittest import mock

from tools import gui_terminal_smoke


REPO_ROOT = Path(__file__).resolve().parents[1]
JPEG_FIXTURE = REPO_ROOT / "test_iso" / "fixtures" / "jpeg_baseline_8x8.jpg"
GFXGUI_SOURCE = REPO_ROOT / "bin" / "gfxgui_test.cc"


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
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43\n"
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
            "PASS feature17_iso\n"
            "[cupidc] JIT execution complete\n"
        ),
        (
            "[cupidc] JIT compile: /bin/feature18_swap.cc\n"
            "PASS feature18_swap\n"
            "[cupidc] JIT execution complete\n"
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
        for command in gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS
        if command.text == text
    ]
    return commands[occurrence]


def _frontier_command_output(text, occurrence=0):
    command_indexes = [
        index
        for index, command in enumerate(
            gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS
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


class GuiTerminalInputTests(unittest.TestCase):
    def test_read_log_retries_one_host_allocation_failure(self):
        log = Path("serial.log")
        with (
            mock.patch.object(
                Path,
                "read_bytes",
                side_effect=[MemoryError(), b"marker\n"],
            ) as read_bytes,
            mock.patch("tools.gui_terminal_smoke.gc.collect") as collect,
        ):
            data = gui_terminal_smoke.read_log(log)

        self.assertEqual(data, "marker\n")
        self.assertEqual(read_bytes.call_count, 2)
        collect.assert_called_once_with()

    def test_read_log_reports_a_persistent_host_allocation_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_bytes(b"marker")
            with (
                mock.patch.object(
                    Path,
                    "read_bytes",
                    side_effect=MemoryError(),
                ),
                mock.patch("tools.gui_terminal_smoke.gc.collect"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    r"serial log allocation failed.*serial\.log.*6 bytes",
                ),
            ):
                gui_terminal_smoke.read_log(log)

    def test_qemu_launch_failure_reports_captured_output(self):
        process = mock.Mock()
        process.poll.return_value = 1
        output = io.BytesIO(
            b"qemu-system-i386: monitor socket address is already in use\n"
        )

        detail = gui_terminal_smoke.qemu_exit_diagnostic(process, output)

        self.assertIn("QEMU exited with status 1", detail)
        self.assertIn("monitor socket address is already in use", detail)

    def test_hmp_rejects_a_monitor_command_error(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            return_value=(
                b"Error: Drive 'frontier_usb_storage' is already in use\r\n"
                b"(qemu)"
            )
        )

        with (
            mock.patch("tools.gui_terminal_smoke.time.sleep"),
            self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "QEMU monitor rejected.*frontier_usb_storage",
            ),
        ):
            gui_terminal_smoke.hmp(
                monitor,
                (
                    "device_add usb-storage,id=replug,"
                    "drive=frontier_usb_storage"
                ),
            )

    def test_hmp_drains_readline_echo_before_a_monitor_error(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            side_effect=[
                b"x" * 4096,
                (
                    b"Error: Drive 'frontier_usb_storage' "
                    b"is already in use\r\n(qemu)"
                ),
            ]
        )

        with (
            mock.patch("tools.gui_terminal_smoke.time.sleep"),
            self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "QEMU monitor rejected.*frontier_usb_storage",
            ),
        ):
            gui_terminal_smoke.hmp(
                monitor,
                (
                    "device_add usb-storage,id=replug,"
                    "drive=frontier_usb_storage"
                ),
            )

        self.assertEqual(monitor.recv.call_count, 2)

    def test_wait_hmp_device_deleted_polls_until_qtree_forgets_id(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            side_effect=[
                (
                    b'dev: usb-storage, id "frontier_mass_storage"\r\n'
                    b"(qemu)"
                ),
                b"(qemu)",
            ]
        )

        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            gui_terminal_smoke.wait_hmp_device_deleted(
                monitor,
                "frontier_mass_storage",
                timeout=1.0,
            )

        self.assertEqual(
            monitor.sent,
            [b"info qtree\n", b"info qtree\n"],
        )

    def test_wait_hmp_device_deleted_rejects_a_stuck_device(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            return_value=(
                b'dev: usb-storage, id "frontier_mass_storage"\r\n'
                b"(qemu)"
            )
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "QEMU did not delete device 'frontier_mass_storage'",
        ):
            gui_terminal_smoke.wait_hmp_device_deleted(
                monitor,
                "frontier_mass_storage",
                timeout=0.0,
            )

    def test_send_key_holds_usb_report_long_enough_for_guest_polling(self):
        monitor = FakeMonitorSocket()

        with mock.patch("tools.gui_terminal_smoke.time.sleep") as sleep:
            gui_terminal_smoke.send_key(monitor, "slash")

        self.assertEqual(monitor.sent, [b"sendkey slash 300\n"])
        sleep.assert_called_once_with(0.35)

    def test_key_names_cover_frontier_runtime_commands(self):
        self.assertEqual(gui_terminal_smoke.key_name("_"), "shift-minus")
        self.assertEqual(gui_terminal_smoke.key_name("+"), "shift-equal")
        self.assertEqual(gui_terminal_smoke.key_name("/"), "slash")
        self.assertEqual(gui_terminal_smoke.key_name("."), "dot")
        self.assertEqual(gui_terminal_smoke.key_name(" "), "spc")

    def test_completion_pattern_accepts_a_caller_success_regex_and_panics(self):
        pattern = gui_terminal_smoke.completion_pattern(
            r"\[elf\] Loaded /home/hello as PID [0-9]+"
        )

        self.assertIsNotNone(pattern.search("[elf] Loaded /home/hello as PID 7"))
        self.assertIsNotNone(pattern.search("KERNEL PANIC: bad"))
        self.assertIsNone(pattern.search("unrelated serial output"))

    def test_success_count_supports_repeated_command_gates(self):
        pattern = r'\[PROCESS\] PID [0-9]+ "hello" exiting'
        data = (
            '[PROCESS] PID 4 "hello" exiting\n'
            '[PROCESS] PID 4 released external image lease 1\n'
            '[PROCESS] PID 5 "hello" exiting\n'
        )

        self.assertEqual(gui_terminal_smoke.success_count(data, pattern), 2)

    def test_command_matchers_support_anchored_multiline_patterns(self):
        pattern = r"^compile\r?$.*?^complete\r?$"
        data = "boot noise\ncompile\r\nprogram output\ncomplete\r\n"

        self.assertIsNotNone(
            gui_terminal_smoke.completion_pattern(pattern).search(data)
        )
        self.assertEqual(gui_terminal_smoke.success_count(data, pattern), 1)

    def test_positive_count_rejects_zero(self):
        self.assertEqual(gui_terminal_smoke.positive_count("2"), 2)
        with self.assertRaisesRegex(ValueError, "positive"):
            gui_terminal_smoke.positive_count("0")

    def test_qemu_args_request_the_configured_cpu_count(self):
        args = mock.Mock(
            qemu="qemu-system-i386",
            image="fresh.img",
            log="serial.log",
            nic="e1000",
            smp=4,
            cpu=None,
        )

        command = gui_terminal_smoke.qemu_args(args, 12345)

        smp_index = command.index("-smp")
        self.assertEqual(command[smp_index + 1], "cpus=4")

    def test_qemu_args_request_the_configured_cpu_model(self):
        args = mock.Mock(
            qemu="qemu-system-i386",
            image="fresh.img",
            log="serial.log",
            nic="e1000",
            smp=1,
            cpu="max",
        )

        command = gui_terminal_smoke.qemu_args(args, 12345)

        cpu_index = command.index("-cpu")
        self.assertEqual(command[cpu_index + 1], "max")

    def test_cli_accepts_a_cpu_model(self):
        args = gui_terminal_smoke.parse_args(["--cpu", "max"])

        self.assertEqual(args.cpu, "max")

    def test_cli_accepts_the_strong_smp_runtime_contract(self):
        args = gui_terminal_smoke.parse_args(["--verify-smp-runtime"])

        self.assertTrue(args.verify_smp_runtime)

    def test_cli_accepts_the_frontier_runtime_contract_and_usb_image(self):
        args = gui_terminal_smoke.parse_args(
            [
                "--verify-frontier-runtime",
                "--usb-image",
                "frontier-usb.img",
            ]
        )

        self.assertTrue(args.verify_frontier_runtime)
        self.assertEqual(args.usb_image, Path("frontier-usb.img"))

    def test_frontier_qemu_topology_pins_usb_devices_and_wav_capture(self):
        args = gui_terminal_smoke.parse_args(
            [
                "--verify-frontier-runtime",
                "--image",
                "fresh.img",
                "--log",
                "serial.log",
                "--usb-image",
                "frontier-usb.img",
            ]
        )

        command = gui_terminal_smoke.qemu_args(
            args,
            12345,
            Path("frontier-ac97.wav"),
            Path("frontier-pcspk.wav"),
        )

        self.assertIn(
            "wav,id=ac97_capture,path=frontier-ac97.wav",
            command,
        )
        self.assertIn(
            "wav,id=pcspk_capture,path=frontier-pcspk.wav",
            command,
        )
        self.assertIn(
            "pcspk-audiodev=pcspk_capture,i8042=off",
            command,
        )
        self.assertIn("AC97,audiodev=ac97_capture", command)
        self.assertIn("piix3-usb-uhci,id=frontier_uhci", command)
        self.assertIn("usb-ehci,id=frontier_ehci", command)
        self.assertIn(
            "usb-kbd,id=frontier_keyboard,bus=frontier_uhci.0,port=1",
            command,
        )
        self.assertIn(
            "usb-mouse,id=frontier_mouse,bus=frontier_uhci.0,port=2",
            command,
        )
        self.assertIn(
            (
                "driver=file,filename=frontier-usb.img,"
                "node-name=frontier_usb_file"
            ),
            command,
        )
        self.assertIn(
            (
                "driver=raw,file=frontier_usb_file,"
                "node-name=frontier_usb_storage"
            ),
            command,
        )
        self.assertNotIn(
            (
                "file=frontier-usb.img,format=raw,if=none,"
                "id=frontier_usb_storage"
            ),
            command,
        )
        self.assertIn(
            (
                "usb-storage,id=frontier_mass_storage,"
                "bus=frontier_ehci.0,port=1,"
                "drive=frontier_usb_storage"
            ),
            command,
        )

    def test_wait_log_after_ignores_matching_history(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            stale = "usb_hid: mouse detached\n"
            log.write_text(stale, encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = 9

            found, data = gui_terminal_smoke.wait_log_after(
                process,
                log,
                r"usb_hid: mouse detached",
                len(stale),
                0.1,
            )

        self.assertFalse(found)
        self.assertEqual(data.splitlines(), stale.splitlines())

    def test_cli_accepts_a_slower_inter_key_pause(self):
        args = gui_terminal_smoke.parse_args(["--key-pause", "0.60"])

        self.assertEqual(args.key_pause, 0.60)

        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            gui_terminal_smoke.parse_args(["--key-pause", "0"])

    def test_cli_pairs_setup_commands_with_completion_patterns(self):
        args = gui_terminal_smoke.parse_args(
            [
                "--setup-command",
                "cp /disk/input /home/output",
                "--setup-success-pattern",
                "copy complete",
            ]
        )

        self.assertEqual(
            args.setup_command,
            ["cp /disk/input /home/output"],
        )
        self.assertEqual(args.setup_success_pattern, ["copy complete"])

    def test_cli_accepts_a_private_system_image(self):
        args = gui_terminal_smoke.parse_args(["--private-image"])

        self.assertTrue(args.private_image)

    def test_private_system_image_preserves_the_selected_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "cupidos.img"
            source.write_bytes(b"original image")
            private = root / "private"
            private.mkdir()
            args = gui_terminal_smoke.parse_args(
                ["--image", str(source)]
            )

            copied_args = gui_terminal_smoke.copy_terminal_image(
                args,
                private,
            )
            copied_args.image.write_bytes(b"guest mutation")

            self.assertEqual(source.read_bytes(), b"original image")
            self.assertEqual(
                copied_args.image,
                private / "cupidos.img",
            )

    def test_cli_rejects_an_unpaired_setup_command(self):
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            gui_terminal_smoke.parse_args(
                ["--setup-command", "cp /disk/input /home/output"]
            )

    def test_terminal_command_requires_a_new_completion_event(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("copy complete\n", encoding="utf-8")
            process = mock.Mock()
            monitor = FakeMonitorSocket()

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.send_key"
                ) as send_key,
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log_success_count",
                    return_value=(
                        True,
                        "copy complete\ncopy complete\n",
                    ),
                ) as wait,
            ):
                ok, _ = gui_terminal_smoke.run_terminal_command(
                    process,
                    monitor,
                    log,
                    "cp",
                    "copy complete",
                    12.0,
                    0.6,
                )

        self.assertTrue(ok)
        self.assertEqual(
            [call.args[1] for call in send_key.call_args_list],
            ["c", "p", "ret"],
        )
        wait.assert_called_once_with(
            process,
            log,
            "copy complete",
            2,
            12.0,
        )

    def test_shutdown_requests_qemu_quit_before_process_termination_fallback(self):
        monitor = FakeMonitorSocket()
        process = mock.Mock()
        process.poll.return_value = None
        process.wait.return_value = 0

        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            gui_terminal_smoke.stop_qemu(process, monitor)

        self.assertEqual(monitor.sent, [b"quit\n"])
        self.assertTrue(monitor.closed)
        process.wait.assert_called_once_with(timeout=3.0)
        process.terminate.assert_not_called()
        process.kill.assert_not_called()

    def test_mouse_activity_covers_motion_button_and_wheel_reports(self):
        monitor = FakeMonitorSocket()

        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            gui_terminal_smoke.inject_mouse_activity(monitor)

        self.assertEqual(
            monitor.sent,
            [
                b"mouse_move 32 24\n",
                b"mouse_button 1\n",
                b"mouse_button 0\n",
                b"mouse_move -16 8 1\n",
                b"mouse_move 0 0 -1\n",
            ],
        )


class SmpRuntimeContractTests(unittest.TestCase):
    def test_complete_runtime_log_passes(self):
        gui_terminal_smoke.validate_smp_runtime_log(_smp_runtime_log())

    def test_smp_runtime_accepts_the_selected_rtl8139_driver(self):
        data = _smp_runtime_log().replace(
            "e1000: init OK",
            "rtl8139: init OK",
        )
        gui_terminal_smoke.validate_smp_runtime_log(data, "rtl8139")

    def test_missing_required_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "cpu3: online apic=3\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "missing required marker.*cpu3: online",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_missing_fpu_boot_smoke_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "[fpu] boot smoke ok\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            r"missing required marker: \[fpu\] boot smoke ok",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_missing_fpu_enable_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "[fpu] SSE2 enabled\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            r"missing required marker: \[fpu\] SSE2 enabled",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_missing_fpu_boot_summary_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "FPU boot smoke passed\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "missing required marker: FPU boot smoke passed",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_wrong_tls_success_count_is_rejected(self):
        data = _smp_runtime_log().replace(
            "[tls-selftest] ok: fixture 61\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "61 TLS self-test successes; expected 62",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_failure_marker_is_rejected_case_insensitively(self):
        data = _smp_runtime_log() + "block cache: FLUSH FAILED\n"

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "failure marker.*Block cache: flush failed",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_run_applies_the_strong_contract_after_command_completion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            image.write_bytes(b"fixture")
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--verify-smp-runtime",
                ]
            )
            process = mock.Mock()
            process.poll.return_value = None
            monitor = FakeMonitorSocket()
            completed = "[cupidc] JIT execution complete\n"
            final_log = "captured strong runtime log"

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen",
                    return_value=process,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.free_tcp_port",
                    return_value=43210,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log",
                    side_effect=[
                        (True, "Entering desktop environment"),
                        (True, "Terminal launched"),
                        (False, final_log),
                    ],
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log_success_count",
                    return_value=(True, completed),
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.connect_monitor",
                    return_value=monitor,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.read_log",
                    return_value="",
                ),
                mock.patch("tools.gui_terminal_smoke.send_key"),
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                mock.patch("tools.gui_terminal_smoke.stop_qemu"),
                mock.patch(
                    "tools.gui_terminal_smoke.validate_smp_runtime_log"
                ) as validate,
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 0)
        validate.assert_called_once_with(final_log, "e1000")


class FrontierRuntimeContractTests(unittest.TestCase):
    def run_replug_contract(self, log, monitor, process=None):
        if process is None:
            process = mock.Mock()
            process.poll.return_value = None
        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            return gui_terminal_smoke.run_frontier_usb_replug_contract(
                process,
                monitor,
                log,
                timeout=0.1,
                key_pause=0.01,
            )

    def test_replug_contract_orders_recovery_proofs_after_each_device_add(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(_frontier_runtime_log(), encoding="utf-8")
            monitor = ReplugMonitorSocket(log)

            self.run_replug_contract(log, monitor)

        commands = [
            command.decode("ascii").strip()
            for command in monitor.sent
        ]
        self.assertEqual(
            commands[:14],
            [
                "device_del frontier_keyboard",
                "info qtree",
                (
                    "device_add usb-kbd,id=frontier_keyboard_replug,"
                    "bus=frontier_uhci.0,port=1"
                ),
                "sendkey l 300",
                "sendkey s 300",
                "sendkey ret 300",
                "device_del frontier_mouse",
                "info qtree",
                (
                    "device_add usb-mouse,id=frontier_mouse_replug,"
                    "bus=frontier_uhci.0,port=2"
                ),
                "mouse_move 32 24",
                "mouse_button 1",
                "mouse_button 0",
                "mouse_move -16 8 1",
                "mouse_move 0 0 -1",
            ],
        )
        first_storage_delete = commands.index(
            "device_del frontier_mass_storage"
        )
        self.assertEqual(first_storage_delete, 14)
        expected_storage_commands = []
        storage_id = "frontier_mass_storage"
        for cycle in range(1, 6):
            expected_storage_commands.extend(
                [
                    f"device_del {storage_id}",
                    "info qtree",
                    (
                        "device_add usb-storage,"
                        f"id=frontier_mass_storage_replug_{cycle},"
                        "bus=frontier_ehci.0,port=1,"
                        "drive=frontier_usb_storage"
                    ),
                ]
            )
            storage_id = f"frontier_mass_storage_replug_{cycle}"
        self.assertEqual(commands[14:], expected_storage_commands)

    def test_replug_contract_accepts_the_expected_unary_diagnostic(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_runtime_log()
                + _frontier_command_output("/bin/feature13_double.cc"),
                encoding="utf-8",
            )
            monitor = ReplugMonitorSocket(log)

            self.run_replug_contract(log, monitor)

        self.assertGreater(len(monitor.sent), 0)

    def test_replug_contract_exercises_six_storage_lifetimes(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(_frontier_runtime_log(), encoding="utf-8")
            monitor = ReplugMonitorSocket(log)

            self.run_replug_contract(log, monitor)

        commands = [
            command.decode("ascii").strip()
            for command in monitor.sent
        ]
        storage_adds = [
            command
            for command in commands
            if command.startswith("device_add usb-storage,")
        ]
        storage_deletes = [
            command
            for command in commands
            if command.startswith("device_del frontier_mass_storage")
        ]
        self.assertGreaterEqual(
            gui_terminal_smoke.FRONTIER_STORAGE_REATTACHMENTS,
            5,
        )
        self.assertEqual(len(storage_adds), 5)
        self.assertEqual(
            storage_adds,
            [
                (
                    f"device_add usb-storage,"
                    f"id=frontier_mass_storage_replug_{cycle},"
                    "bus=frontier_ehci.0,port=1,"
                    "drive=frontier_usb_storage"
                )
                for cycle in range(1, 6)
            ],
        )
        self.assertEqual(
            storage_deletes,
            [
                "device_del frontier_mass_storage",
                *[
                    f"device_del frontier_mass_storage_replug_{cycle}"
                    for cycle in range(1, 5)
                ],
            ],
        )

    def test_replug_contract_requires_the_initial_storage_lifetime(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_runtime_log().replace(
                    (
                        "usb_msc: usb0 has 1 FAT16 partition(s); "
                        "auto-mount NYI, use raw /dev/usb0\n"
                    ),
                    "",
                ),
                encoding="utf-8",
            )
            monitor = ReplugMonitorSocket(log)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "initial USB storage did not reach its FAT16 marker",
            ):
                self.run_replug_contract(log, monitor)

        self.assertEqual(monitor.sent, [])

    def test_replug_contract_rejects_stale_keyboard_attach_history(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_runtime_log()
                + "usb_hid: keyboard attached addr=77\n",
                encoding="utf-8",
            )
            monitor = ReplugMonitorSocket(
                log,
                omit_prefix="device_add usb-kbd,",
            )
            process = mock.Mock()
            process.poll.return_value = 7

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                (
                    "USB keyboard reattach did not reach its guest marker"
                    ".*QEMU exited with status 7"
                ),
            ):
                self.run_replug_contract(log, monitor, process)

    def test_replug_contract_rejects_a_storage_failure_even_with_fat16_log(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(_frontier_runtime_log(), encoding="utf-8")
            monitor = ReplugMonitorSocket(
                log,
                failure_storage_cycle=5,
            )

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                (
                    "USB storage reattach 5 saw failure marker: "
                    "usb_msc: block device registry is full"
                ),
            ):
                self.run_replug_contract(log, monitor)

    def test_command_sequence_uses_source_backed_success_markers(self):
        commands = gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS

        self.assertEqual(
            [command.text for command in commands],
            [
                "ls",
                "/bin/kbdsub_test.cc",
                "/bin/date.cc +epoch",
                "as /demos/syscall_vfs_extended_demo.asm",
                "dis /bin/test_fpaug.cc",
                "/bin/test_fpaug.cc",
                "/bin/feature13_double.cc",
                "ccc /bin/feature13_derived_aot.cc -o /feature13_derived_aot",
                "exec /feature13_derived_aot",
                "/bin/feature14_simd.cc",
                "/bin/feature15_libm.cc",
                "/bin/feature17_iso.cc",
                "/bin/feature18_swap.cc",
                "ccc /bin/gfxgui_test.cc -o /gfxgui_test",
                "/bin/gfxgui_test.cc",
                "ccc /bin/gfxhandoff_exit.cc -o /gfxhandoff_exit",
                "ccc /bin/gfxhandoff_kill.cc -o /gfxhandoff_kill",
                "exec /gfxhandoff_exit",
                "exec /gfxhandoff_kill {pid}",
                "exec /gfxgui_test",
                "dglibc_test",
                "doom",
                "doom -iwad /disk/missing.wad",
                "ls",
                "browser --selftest",
                "audiotest all",
                "godsong 1 200",
            ],
        )
        self.assertEqual(
            commands[-1].followup_keys,
            ("esc",) * 8,
        )
        self.assertGreater(len(commands[-1].followup_keys), 5)
        self.assertEqual(commands[1].followup_keys, ("shift",))
        self.assertEqual(commands[1].followup_settle_seconds, 0.0)
        self.assertIn(
            "waiting for USB Shift",
            commands[1].interaction_pattern,
        )
        self.assertEqual(
            commands[-1].interaction_pattern,
            gui_terminal_smoke.GODSONG_SETTINGS_READY_PATTERN,
        )
        self.assertNotIn("flip frame=2", commands[-1].interaction_pattern)
        self.assertEqual(commands[-1].followup_settle_seconds, 0.0)
        exit_command = _frontier_command("exec /gfxhandoff_exit")
        self.assertEqual(exit_command.capture_name, "gfx_owner_pid")
        for text in (
            "exec /gfxhandoff_kill {pid}",
            "exec /gfxgui_test",
        ):
            self.assertEqual(
                _frontier_command(text).pid_from_capture,
                "gfx_owner_pid",
            )

        resolved = gui_terminal_smoke.resolve_frontier_command(
            _frontier_command("exec /gfxhandoff_kill {pid}"),
            {"gfx_owner_pid": "17"},
        )
        self.assertEqual(resolved.text, "exec /gfxhandoff_kill 17")
        self.assertIn("targeting PID 17", resolved.expected_pattern)
        stale_first_output = (
            '[elf] Loaded /gfxhandoff_kill as PID 17 '
            '(ELF32, 2048 bytes at 0x01100000)\n'
            '[gfxhandoff_kill] nested owner waiting for remote kill\n'
            '[PROCESS] Delayed kill skipped stale PID 17\n'
            '[PROCESS] Delayed killer PID 19 targeting PID 17 after 7000 ms\n'
            '[PROCESS] Killing PID 17 "/gfxhandoff_kill"\n'
        )
        self.assertIsNotNone(
            re.search(
                resolved.expected_pattern,
                stale_first_output,
                re.S | re.M,
            )
        )
        stale_before_owner_output = (
            '[elf] Loaded /gfxhandoff_kill as PID 17 '
            '(ELF32, 2048 bytes at 0x01100000)\n'
            '[PROCESS] Delayed kill skipped stale PID 17\n'
            '[gfxhandoff_kill] nested owner waiting for remote kill\n'
            '[PROCESS] Delayed killer PID 19 targeting PID 17 after 7000 ms\n'
            '[PROCESS] Killing PID 17 "/gfxhandoff_kill"\n'
        )
        self.assertIsNotNone(
            re.search(
                resolved.expected_pattern,
                stale_before_owner_output,
                re.S | re.M,
            )
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "needs missing capture",
        ):
            gui_terminal_smoke.resolve_frontier_command(
                _frontier_command("exec /gfxhandoff_kill {pid}"),
                {},
            )

        captures = {"gfx_owner_pid": "17"}
        for command, sample in zip(commands, _frontier_command_outputs()):
            with self.subTest(command=command.text):
                resolved_command = gui_terminal_smoke.resolve_frontier_command(
                    command,
                    captures,
                )
                self.assertIsNotNone(
                    re.search(
                        resolved_command.expected_pattern,
                        sample,
                        re.S | re.M,
                    )
                )

    def test_gui_disassembly_mirrors_its_listing_to_serial(self):
        source = (
            REPO_ROOT / "kernel" / "lang" / "shell.cc"
        ).read_text(encoding="utf-8")

        router = re.search(
            r"static void shell_route_print\(const char \*s,\s*"
            r"int mirror_gui_to_serial\) \{(?P<body>.*?)\n\}",
            source,
            re.S,
        )
        self.assertIsNotNone(router)
        body = router.group("body")
        self.assertRegex(
            body,
            re.compile(
                r"if \(shell_output_write_current\(s, \(uint32_t\)strlen\(s\)\)\)"
                r"\s+return;\s+if \(redir_active && redir_buf\) \{.*?"
                r"\s+return;\s+\}\s+if \(output_mode == SHELL_OUTPUT_GUI\)"
                r" \{\s+shell_gui_print\(s\);\s+"
                r"if \(mirror_gui_to_serial\)\s+serial_write_string\(s\);"
                r"\s+\} else \{\s+print\(s\);\s+\}",
                re.S,
            ),
        )
        self.assertIn(
            "static void shell_print(const char *s) {\n"
            "  shell_route_print(s, 0);\n"
            "}",
            source,
        )
        self.assertIn(
            "static void shell_dis_print(const char *s) {\n"
            "  shell_route_print(s, 1);\n"
            "}",
            source,
        )
        self.assertEqual(
            source.count("cupidc_dis(rpath, shell_dis_print);"),
            3,
        )
        self.assertEqual(
            source.count("dis_elf(rpath, shell_dis_print);"),
            2,
        )
        self.assertNotIn("cupidc_dis(rpath, shell_print);", source)
        self.assertNotIn("dis_elf(rpath, shell_print);", source)

    def test_fpaug_disassembly_requires_canonical_parity_setcc(self):
        command = _frontier_command("dis /bin/test_fpaug.cc")
        sample = _frontier_command_output("dis /bin/test_fpaug.cc")

        self.assertIsNotNone(
            re.search(command.expected_pattern, sample, re.S | re.M)
        )
        for spelling in ("setnp dl", "and al, dl", "setp dl", "or al, dl"):
            with self.subTest(spelling=spelling):
                self.assertIsNone(
                    re.search(
                        command.expected_pattern,
                        sample.replace(spelling, "db 0x0F"),
                        re.S | re.M,
                    )
                )

        source = (REPO_ROOT / "bin" / "test_fpaug.cc").read_text(
            encoding="utf-8"
        )
        self.assertLess(len(source.encode("utf-8")), 4096)
        for spelling in (
            "int fpaug_equal(double left, double right)",
            "int fpaug_not_equal(double left, double right)",
            "int fpaug_truth(double value)",
        ):
            with self.subTest(spelling=spelling):
                self.assertIn(spelling, source)

    def test_fpaug_runtime_requires_parity_semantics(self):
        command = _frontier_command("/bin/test_fpaug.cc")
        sample = _frontier_command_output("/bin/test_fpaug.cc")

        self.assertIsNotNone(
            re.search(command.expected_pattern, sample, re.S | re.M)
        )
        self.assertIn(
            "[test_fpaug-parity] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        self.assertIn(
            "FAIL test_fpaug",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_godsong_publishes_readiness_at_the_first_popup_boundary(self):
        source = (REPO_ROOT / "bin" / "godsong.cc").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'serial_printf("[godsong] settings ready\\n");\n'
            "  c = popup_menu(220, 110, (void*)items, 3);",
            source,
        )
        popup = (REPO_ROOT / "kernel" / "gfx" / "gfx2d.cc").read_text(
            encoding="utf-8"
        ).split("int gfx2d_popup_menu(", 1)[1]
        self.assertLess(
            popup.index("gfx2d_shared_writer_begin()"),
            popup.index('[gfx2d] popup input ready'),
        )

    def test_gfxgui_frontier_requires_aot_and_runtime_evidence(self):
        aot = _frontier_command(
            "ccc /bin/gfxgui_test.cc -o /gfxgui_test"
        )
        aot_output = _frontier_command_output(aot.text)
        self.assertIsNotNone(
            re.search(aot.expected_pattern, aot_output, re.S | re.M)
        )
        self.assertIsNotNone(
            re.search(
                aot.expected_pattern,
                aot_output.replace("entry=0x", "entry=0x0x"),
                re.S | re.M,
            )
        )
        for marker in (
            "[cupidc] AOT compile: /bin/gfxgui_test.cc",
            "[cupidc] Wrote ELF: /gfxgui_test",
        ):
            with self.subTest(command="AOT", marker=marker):
                self.assertIsNone(
                    re.search(
                        aot.expected_pattern,
                        aot_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        runtime = _frontier_command("/bin/gfxgui_test.cc")
        runtime_output = _frontier_command_output(runtime.text)
        self.assertIsNotNone(
            re.search(
                runtime.expected_pattern,
                runtime_output,
                re.S | re.M,
            )
        )
        for marker in (
            "[gfxgui_test] init",
            "[gfxgui_test] assets ready",
            "[gfxgui_test] fullscreen",
            "[gfxgui_test] font ready",
            "[gfxgui_test] surface ready",
            "[gfxgui_test] transform ready",
            "[gfxgui_test] frame 0 done",
            "[gfxgui_test] frame 240 done",
            "[gfxgui_test] done",
            "[cupidc] JIT execution complete",
        ):
            with self.subTest(command="runtime", marker=marker):
                self.assertIsNone(
                    re.search(
                        runtime.expected_pattern,
                        runtime_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        external = gui_terminal_smoke.resolve_frontier_command(
            _frontier_command("exec /gfxgui_test"),
            {"gfx_owner_pid": "17"},
        )
        external_output = _frontier_command_output(external.text)
        self.assertIsNotNone(
            re.search(
                external.expected_pattern,
                external_output,
                re.S | re.M,
            )
        )
        self.assertIn("[elf] Loaded /gfxgui_test as PID", external_output)

    def test_gfxgui_frontier_uses_a_workload_specific_timeout(self):
        aot = _frontier_command(
            "ccc /bin/gfxgui_test.cc -o /gfxgui_test"
        )
        runtime = _frontier_command("/bin/gfxgui_test.cc")
        external = _frontier_command("exec /gfxgui_test")
        self.assertEqual(aot.timeout_seconds, 180.0)
        self.assertEqual(runtime.timeout_seconds, 300.0)
        self.assertEqual(external.timeout_seconds, 300.0)

    def test_gfxgui_program_publishes_runtime_markers_to_serial(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        for marker in ("init", "done"):
            with self.subTest(marker=marker):
                self.assertIn(
                    f'serial_printf("[gfxgui_test] {marker}\\n");',
                    source,
                )
        for marker in (
            "assets ready",
            "fullscreen",
            "font ready",
            "surface ready",
            "transform ready",
            "frame %d begin",
            "frame %d done",
        ):
            with self.subTest(marker=marker):
                self.assertIn(f"[gfxgui_test] {marker}", source)
        for path in (
            "/gfxgui_test.theme",
            "/gfxgui_test.bmp",
            "/gfxgui_test.fnt",
        ):
            with self.subTest(path=path):
                self.assertIn(path, source)
        self.assertNotIn("/home/gfxgui_test", source)

    def test_gfxgui_program_requires_each_runtime_asset(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        for operation in (
            'ui_theme_save("/gfxgui_test.theme")',
            'ui_theme_load("/gfxgui_test.theme")',
            'make_test_bmp("/gfxgui_test.bmp")',
            'gfx2d_image_load("/gfxgui_test.bmp")',
            'make_test_font("/gfxgui_test.fnt")',
            'gfx2d_font_load("/gfxgui_test.fnt")',
            "gfx2d_surface_alloc(96, 96)",
        ):
            with self.subTest(operation=operation):
                self.assertIn(operation, source)
        self.assertIn("[gfxgui_test] FAIL", source)
        self.assertIn("if (surf < 0)", source)

    def test_gfxgui_program_checks_font_and_filtered_surface_pixels(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        self.assertIn("gfx2d_blend_mode(0);", source)
        self.assertIn('gfx2d_text_ex(16, 16, "A",', source)
        self.assertIn("glyph[0] = 0xFF;", source)
        self.assertIn("gfx2d_getpixel(16, 16)", source)
        self.assertIn("[gfxgui_test] FAIL font pixel", source)
        self.assertIn("gfx2d_rect_fill(3, 3, 3, 3, 0x000000)", source)
        self.assertIn("gfx2d_pixel(4, 4, 0xFFFFFF)", source)
        self.assertIn("gfx2d_getpixel(4, 4)", source)
        self.assertIn("0x001C1C1C", source)
        self.assertIn("[gfxgui_test] FAIL surface blur pixel", source)
        self.assertIn("gfx2d_pixel(4, 4, 0x123456)", source)
        self.assertIn("0x00123456", source)
        self.assertIn("[gfxgui_test] FAIL surface isolation", source)

    def test_gfxgui_program_checks_linear_transform_and_stack_restore(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        self.assertIn("gfx2d_rotate(90);", source)
        self.assertIn("gfx2d_transform_point(2, 3, &ox, &oy);", source)
        self.assertIn("ox != 91 || oy != 104", source)
        self.assertIn("[gfxgui_test] FAIL transform linear", source)
        self.assertIn("[gfxgui_test] FAIL transform restore", source)
        self.assertIn("gfx2d_getpixel(484, 150)", source)
        self.assertIn("0x00BC809E", source)
        self.assertIn("[gfxgui_test] FAIL transformed scale pixel", source)

    def test_gfxgui_non_gui_mode_is_an_immediate_serial_failure(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        gui_guard = source.index("if (!is_gui_mode())")
        next_block = source.index("\n  }", gui_guard)
        self.assertIn(
            "[gfxgui_test] FAIL requires GUI mode",
            source[gui_guard:next_block],
        )

    def test_gfxgui_failure_marker_stops_the_command_gate_immediately(self):
        marker = "[gfxgui_test] FAIL image load"
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(marker + "\n", encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*gfxgui_test",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("/bin/gfxgui_test.cc"),
                    start_offset=0,
                    timeout=0.1,
                )

    def test_kernel_panic_gate_waits_for_the_serial_reason(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("KERNEL PANIC\n", encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            def publish_reason(_delay):
                log.write_text(
                    "KERNEL PANIC\n"
                    "[PANIC] uhci: DMA revocation failed\n",
                    encoding="utf-8",
                )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.time.sleep",
                    side_effect=publish_reason,
                ),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "KERNEL PANIC.*uhci: DMA revocation failed",
                ),
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("/bin/gfxgui_test.cc"),
                    start_offset=0,
                    timeout=0.1,
                )

    def test_command_sequence_applies_only_explicit_timeout_overrides(self):
        commands = (
            gui_terminal_smoke.TerminalCommand("a", "A"),
            gui_terminal_smoke.TerminalCommand(
                "b",
                "B",
                timeout_seconds=7.5,
            ),
        )
        process = mock.Mock()
        monitor = FakeMonitorSocket()

        with (
            tempfile.TemporaryDirectory() as temporary,
            mock.patch.object(
                gui_terminal_smoke,
                "FRONTIER_RUNTIME_COMMANDS",
                commands,
            ),
            mock.patch(
                "tools.gui_terminal_smoke.wait_frontier_command",
                side_effect=[(1, "A"), (2, "B")],
            ) as wait_command,
            mock.patch("tools.gui_terminal_smoke.send_key"),
            mock.patch("tools.gui_terminal_smoke.time.sleep"),
        ):
            gui_terminal_smoke.run_frontier_commands(
                process,
                monitor,
                Path(temporary) / "serial.log",
                start_offset=0,
                timeout=1.25,
                key_pause=0.01,
            )

        self.assertEqual(
            [call.args[4] for call in wait_command.call_args_list],
            [1.25, 7.5],
        )

    def test_doom_recovery_requires_both_failures_before_a_fresh_ls(self):
        commands = gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS
        command_texts = [command.text for command in commands]
        no_wad_index = command_texts.index("doom")
        missing_iwad_index = command_texts.index(
            "doom -iwad /disk/missing.wad"
        )
        ls_indexes = [
            index
            for index, text in enumerate(command_texts)
            if text == "ls"
        ]

        self.assertEqual(len(ls_indexes), 2)
        self.assertEqual(
            [no_wad_index, missing_iwad_index, ls_indexes[1]],
            [ls_indexes[1] - 2, ls_indexes[1] - 1, ls_indexes[1]],
        )

        no_wad = _frontier_command("doom")
        no_wad_output = _frontier_command_output("doom")
        for marker in (
            "doom: no WAD found in /disk/wads/ or /home/doom/.",
            "try: doom -iwad /path/to/your.wad",
        ):
            with self.subTest(command="doom", marker=marker):
                self.assertIsNone(
                    re.search(
                        no_wad.expected_pattern,
                        no_wad_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        missing_iwad = _frontier_command(
            "doom -iwad /disk/missing.wad"
        )
        missing_iwad_output = _frontier_command_output(
            "doom -iwad /disk/missing.wad"
        )
        for marker in (
            "IWAD file '/disk/missing.wad' not found!",
            "[doom] returned to shell",
        ):
            with self.subTest(command="missing IWAD", marker=marker):
                self.assertIsNone(
                    re.search(
                        missing_iwad.expected_pattern,
                        missing_iwad_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        post_doom_ls = _frontier_command("ls", occurrence=1)
        post_doom_ls_output = _frontier_command_output(
            "ls",
            occurrence=1,
        )
        self.assertIsNotNone(
            re.search(
                post_doom_ls.expected_pattern,
                post_doom_ls_output,
                re.S | re.M,
            )
        )

    def test_dglibc_command_requires_every_filesystem_boundary(self):
        command = _frontier_command("dglibc_test")
        expected = command.expected_pattern
        sample = _frontier_command_output("dglibc_test")

        self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
        for fragment in (
            "[PASS] dglibc snprintf",
            "[PASS] dglibc malloc/free",
            "[PASS] dglibc setjmp/longjmp and exit envelope",
            "[PASS] dglibc checked integer parsing",
            "[PASS] dglibc Doom exit callback lifecycle",
            "[PASS] dglibc Doom path resolution",
            "[PASS] dglibc shared errno bridge",
            "[PASS] dglibc Doom config round trip",
            "[PASS] dglibc synthetic config filesystem bridge",
            "[PASS] dglibc synthetic save filesystem bridge",
            "[PASS] dglibc VFS rename boundaries",
            "[PASS] dglibc VFS copy boundaries",
            "[PASS] dglibc block cache failure boundary",
            "[PASS] dglibc RamFS size boundary",
            "[PASS] dglibc FAT directory collision",
            "[PASS] dglibc FAT read boundary",
            "[PASS] dglibc FAT handle exhaustion",
            "[PASS] dglibc FAT busy replacement",
            "[PASS] dglibc FAT 8.3 path boundary",
            "[PASS] dglibc HomeFS mount boundary",
            "[PASS] dglibc HomeFS depth boundary",
            "[PASS] dglibc HomeFS batch boundary",
            "[PASS] dglibc_test",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )

        self.assertIn(
            "[FAIL] dglibc",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_browser_number_selftest_requires_every_boundary(self):
        command = _frontier_command("browser --selftest")
        expected = command.expected_pattern
        sample = _frontier_command_output("browser --selftest")

        self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
        for fragment in (
            "[js] parse error: js: expected exponent digits",
            "[js] parse error: js: expected hexadecimal digits",
            "[js] parse error: js: invalid binary digit",
            "[js] parse error: js: invalid octal digit",
            "[js] parse error: js: invalid numeric separator",
            "[js] parse error: js: identifier follows numeric literal",
            "close=1",
            "large=1",
            "negzero=1",
            "nan=1",
            "truth=1",
            "nanformat=1",
            "posinfformat=1",
            "neginfformat=1",
            "literal=1",
            "signedexp=1",
            "upperexp=1",
            "order=1",
            "divide=1",
            "divideassign=1",
            "remainder=1",
            "expcap=1",
            "radix=1",
            "separators=1",
            "tonumber=1",
            "looseeq=1",
            "stringrel=1",
            "largefmod=1",
            "modassign=1",
            "strplusassign=1",
            "reject=1",
            "recovery=1",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )

        self.assertIn(
            "[browser-js-number] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_unary_command_requires_value_type_error_and_recovery_evidence(
        self,
    ):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        for fragment in (
            (
                "[cupidc] error (line 1): "
                "unary sign requires an arithmetic scalar operand\n"
            ),
            "float=-15",
            "double=-9",
            "zero=0x80000000",
            "plus=9",
            "reject=1",
            "recovery=1",
            "PASS feature13_double\n",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )
        self.assertIn(
            "[feature13-unary] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        self.assertEqual(
            command.allowed_failure_pattern,
            r"^" + re.escape(command.allowed_failure_literal) + r"\r?\n",
        )
        self.assertEqual(
            command.allowed_failure_literal,
            (
                "[cupidc] error (line 1): "
                "unary sign requires an arithmetic scalar operand"
            ),
        )
        self.assertEqual(
            command.allowed_failure_context_pattern,
            (
                r"^"
                + re.escape(
                    "[cupidc] JIT compile: /bin/feature13_double.cc"
                )
                + r"\r?$"
            ),
        )

    def test_feature14_requires_operator_array_matrix_minmax_and_nan_evidence(self):
        command = _frontier_command("/bin/feature14_simd.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature14_simd.cc")

        for fragment in (
            "[feature14-operator] PASS float=4 double=4\n",
            (
                "[feature14-array] PASS global=2 local=2 static=2 "
                "sizeof=16 index=1\n"
            ),
            (
                "[feature14-matrix] PASS global=2 local=2 static=2 "
                "sizes=8 index=6 unevaluated=2 canary=4\n"
            ),
            (
                "[feature14-update] PASS direct=6 leaves=3 once=6 "
                "payload=8\n"
            ),
            "[feature14-call] PASS float4=4 double2=2 nested=2 calls=6\n",
            "[feature14-callback] PASS float4=4 double2=2 calls=2\n",
            "[feature14-callback-typedef] PASS float4=4 calls=1\n",
            (
                "[feature14-callback-global] PASS float4=4 initialized=1 "
                "assigned=1 cleared=1 calls=2\n"
            ),
            (
                "[feature14-callback-raw] PASS initialized=1 parameter=1 "
                "cleared=1 reassigned=1 calls=3\n"
            ),
            (
                "[feature14-callback-raw-array] PASS modes=2 phases=3 "
                "calls=12 stored=1 persistent=1\n"
            ),
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43\n",
            (
                "[feature14-callback-automatic] PASS local=4 method=4 "
                "calls=2\n"
            ),
            (
                "[feature14-callback-field] PASS stored=1 copied=1 "
                "cleared=1 float4=4 calls=1\n"
            ),
            (
                "[feature14-callback-field-call] PASS typedef=1 raw=1 "
                "float4=4 once=1 calls=2\n"
            ),
            "[feature14-minmax] PASS nan=4 signed_zero=4\n",
            (
                "[feature14-nan] PASS float_left=4 float_right=0 "
                "double_left=4 double_right=0\n"
            ),
            "PASS feature14_simd\n",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )

        for failure in (
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
            "[feature14-callback-nested] FAIL",
            "[feature14-minmax] FAIL",
            "[feature14-nan] FAIL",
            "FAIL feature14_simd",
        ):
            self.assertIn(
                failure,
                gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
            )

        source = (
            REPO_ROOT / "bin" / "feature14_simd.cc"
        ).read_text(encoding="utf-8")
        for spelling in (
            "float4 feature14_global_floats[3]",
            "double2 feature14_global_doubles[2]",
            "float4 feature14_global_matrix[2][2]",
            "double2 feature14_global_cube[2][2][2]",
            "direct_float = a + b",
            "direct_double = dv / dpos",
            "feature14_global_floats[1] += a",
            "saved_doubles[1].y",
            "local_matrix[1][0] += b",
            "saved_cube[1][1][0] /= local_step",
            "feature14_global_cube[feature14_next_outer()]",
            "feature14_global_cube[feature14_next_outer()][feature14_next_middle()]",
            "old_float = feature14_update_float++",
            "new_matrix = --feature14_global_matrix[matrix_outer++]",
            "old_cube = feature14_global_cube[cube_outer++][cube_middle++]",
            "return feature14_merge_float4(",
            "feature14_merge_float4(first, 7, second)",
            "return feature14_merge_double2(",
            "feature14_merge_double2(first, 13, second)",
            "float4 (*float_callback)(float4 left, int marker, float4 right)",
            "double2 (*double_callback)(double2 left, int marker, double2 right)",
            "typedef float4 (*feature14_float_callback_t)(float4 left, int marker,",
            "feature14_float_callback_t feature14_global_callback = "
            "feature14_merge_float4",
            "int (*feature14_raw_callback)(int) = feature14_raw_target;",
            "int feature14_invoke_raw_callback(int (*callback)(int), int value)",
            "initialized_result = feature14_raw_callback(1);",
            "parameter_result = feature14_invoke_raw_callback(",
            "feature14_raw_target, 2);",
            "feature14_raw_callback = 0;",
            "if (feature14_raw_callback != 0) return 2;",
            "feature14_raw_callback = feature14_raw_target;",
            "reassigned_result = feature14_raw_callback(3);",
            "float4 feature14_invoke_float_callback(",
            "feature14_float_callback_t callback, float4 left, int marker,",
            "result = feature14_invoke_float_callback(",
            "feature14_merge_float4, first, 7, second);",
            "feature14_set_global_callback(feature14_merge_float4);",
            "initialized_result = feature14_global_callback(first, 7, second);",
            "assigned_result = feature14_global_callback(first, 7, second);",
            "feature14_set_global_callback(0);",
            "class Feature14CallbackInvoker",
            "feature14_float_callback_t callback = feature14_merge_float4;",
            "method_result = invoker.Invoke(",
            "sizeof(feature14_global_matrix[feature14_sizeof_index()])",
            "_mm_min_ps(edge_float_first, edge_float_second)",
            "_mm_max_pd(edge_double_first, edge_double_second)",
            "float_result = float_left + float_right",
            "float_result = _mm_mul_ps(float_left, float_right)",
            "double_result = double_left + double_right",
            "double_result = _mm_mul_pd(double_left, double_right)",
            "[feature14-nan] PASS float_left=%d float_right=%d "
            "double_left=%d double_right=%d",
            "[feature14-matrix] PASS global=2 local=2 static=2 "
            "sizes=8 index=6 unevaluated=2 canary=4",
            "[feature14-update] PASS direct=6 leaves=3 once=6 payload=8",
            "[feature14-call] PASS float4=4 double2=2 nested=2 calls=6",
            "[feature14-callback] PASS float4=4 double2=2 calls=2",
            "[feature14-callback-typedef] PASS float4=4 calls=1",
            "[feature14-callback-global] PASS float4=4 initialized=1 "
            "assigned=1 cleared=1 calls=2",
            "[feature14-callback-raw] PASS initialized=1 parameter=1 "
            "cleared=1 reassigned=1 calls=3",
            "[feature14-callback-raw-array] PASS modes=2 phases=3 "
            "calls=12 stored=1 persistent=1",
            "void (*set_drawer)(int, void (*)(int, int)) =",
            "void feature14_nested_install(int handle, "
            "void (*drawer)(int, int))",
            "set_drawer(4, feature14_nested_draw);",
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43",
            "static int (*wipes[])(int, int, int)",
            "(*wipes[wipeno*3])(4, 2, 1)",
            "(*wipes[wipeno*3+1])(4, 2, 1)",
            "(*wipes[wipeno*3+2])(4, 2, 1)",
            "[feature14-callback-automatic] PASS local=4 method=4 calls=2",
            "[feature14-callback-field] PASS stored=1 copied=1 cleared=1 "
            "float4=4 calls=1",
            "[feature14-callback-field-call] PASS typedef=1 raw=1 "
            "float4=4 once=1 calls=2",
        ):
            self.assertIn(spelling, source)

    def test_feature14_raw_callback_failure_cannot_hide_behind_pass_evidence(self):
        sample = _frontier_command_output("/bin/feature14_simd.cc")
        poisoned = sample.replace(
            "[feature14-callback-raw] PASS",
            "[feature14-callback-raw] FAIL\n[feature14-callback-raw] PASS",
            1,
        )

        self.assertEqual(
            gui_terminal_smoke.frontier_failure_marker(poisoned),
            "[feature14-callback-raw] FAIL",
        )

    def test_feature14_nested_callback_failure_cannot_hide_behind_pass_evidence(self):
        sample = _frontier_command_output("/bin/feature14_simd.cc")
        poisoned = sample.replace(
            "[feature14-callback-nested] PASS",
            "[feature14-callback-nested] FAIL\n"
            "[feature14-callback-nested] PASS",
            1,
        )

        self.assertEqual(
            gui_terminal_smoke.frontier_failure_marker(poisoned),
            "[feature14-callback-nested] FAIL",
        )

    def test_feature14_nested_callback_evidence_keeps_callback_order(self):
        command = _frontier_command("/bin/feature14_simd.cc")
        sample = _frontier_command_output("/bin/feature14_simd.cc")
        raw_array = (
            "[feature14-callback-raw-array] PASS modes=2 phases=3 "
            "calls=12 stored=1 persistent=1\n"
        )
        nested = (
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43\n"
        )
        reordered = sample.replace(
            raw_array + nested,
            nested + raw_array,
            1,
        )

        self.assertIsNone(
            re.search(command.expected_pattern, reordered, re.S | re.M)
        )

    def test_feature13_requires_all_scalar_comparison_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-compare] PASS ordered=6 mixed=4 "
            "zero=2 unordered=6\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-compare] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "1.0 == 1.0",
            "1.0 != 2.0",
            "1.0 < 2.0",
            "2.0 > 1.0",
            "1.0 <= 1.0",
            "2.0 >= 2.0",
            "1.0f < 2.0",
            "negative_zero == 0.0",
            "compare_nan != compare_nan",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_all_scalar_truth_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-truth] PASS zero=2 nonzero=3 "
            "control=255 nan=1\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-truth] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "!truth_zero",
            "!truth_negative_zero",
            "!!truth_nan",
            "if (truth_nonzero)",
            "truth_nonzero ? 4 : 1000",
            "while (truth_while)",
            "for (; truth_for; truth_for = 0.0)",
            "} while (truth_do);",
            "if (truth_nan)",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_all_scalar_update_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-update] PASS local=48 global=40 "
            "for=3 zero=0x80000000 nan=2\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-update] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "update_float_old = (update_float)++",
            "update_float_new = ++(update_float)",
            "update_float--;",
            "update_double_old = ((update_double))--",
            "update_double_new = --((update_double))",
            "update_double++;",
            "feature13_update_global_float++;",
            "--feature13_update_global_double;",
            "update_global_old = feature13_update_global_double--",
            "update_global_new = ++feature13_update_global_float",
            "for (; update_iterations < 3; update_for++)",
            "update_zero_old = update_negative_zero++",
            "update_nan_old = update_nan++",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_typed_floating_lvalue_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-lvalue] PASS array=42 pointer=13 "
            "record=26 sizes=56 unevaluated=1\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-lvalue] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "float feature13_lvalue_global[2][3]",
            "float *row = &matrix[1][0]",
            "double *cube_cell = &cube[1][0][0]",
            "float *returned_row = feature13_lvalue_row()",
            "returned_row[2] *= 2.0f",
            "record->gain += 0.5f",
            "record->bias *= 2.0",
            "sizeof(matrix[index++])",
            "sizeof(cube[0])",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_indirect_floating_update_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")

        self.assertIn(r"\[feature13-indirect-update\] PASS", expected)
        self.assertIn(
            "[feature13-indirect-update] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        for expression in (
            "(*feature13_indirect_float_pointer(&pointer_value))++",
            "++indirect_values[feature13_indirect_index()]",
            "feature13_indirect_record(&indirect_record)->bias--",
            "*(int*)&pointer_old",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_aot_and_external_update_evidence(self):
        aot = _frontier_command(
            "ccc /bin/feature13_derived_aot.cc -o /feature13_derived_aot"
        )
        aot_output = _frontier_command_output(aot.text)
        external = _frontier_command("exec /feature13_derived_aot")
        external_output = _frontier_command_output(external.text)
        self.assertIn(
            "[feature13-derived-aot] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        self.assertIsNotNone(
            re.search(aot.expected_pattern, aot_output, re.S | re.M)
        )
        self.assertIsNotNone(
            re.search(
                external.expected_pattern,
                external_output,
                re.S | re.M,
            )
        )
        for marker in (
            "[elf] Loaded /feature13_derived_aot as PID",
            "[feature13-derived-aot] PASS",
            '[PROCESS] PID 11 "/feature13_derived_aot" exiting',
        ):
            with self.subTest(marker=marker):
                self.assertIsNone(
                    re.search(
                        external.expected_pattern,
                        external_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )
        self.assertIsNone(
            re.search(
                external.expected_pattern,
                external_output.replace(
                    '[PROCESS] PID 11 "/feature13_derived_aot" exiting',
                    '[PROCESS] PID 12 "/feature13_derived_aot" exiting',
                ),
                re.S | re.M,
            )
        )

    def test_feature13_requires_unsigned_word_runtime_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-unsigned] PASS conversions=4 "
            "remainders=2 once=1\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        for failure in (
            "[feature13-unsigned-convert] FAIL",
            "[feature13-unsigned-remainder] FAIL",
        ):
            self.assertIn(
                failure,
                gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
            )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "float unsigned_float_below = 2147483520.0f",
            "float unsigned_float_above = 2147483904.0f",
            "double unsigned_double_below = 2147483647.75",
            "double unsigned_double_above = 4294967295.75",
            "(uint32_t)unsigned_float_below",
            "(uint32_t)unsigned_double_below",
            "signed_remainder %= 6",
            (
                "feature13_unsigned_values["
                "feature13_unsigned_next_index()] %= 7"
            ),
            "feature13_unsigned_index_calls == 1",
            "[feature13-unsigned] PASS conversions=4 remainders=2 once=1",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_mixed_width_user_call_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = "[feature13-call] PASS checks=10\n"

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-call] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r"int feature13_within\(\s*double actual,\s*double expected,\s*"
            r"double scale,\s*int max_scaled_error\)",
        )
        self.assertEqual(source.count("feature13_within("), 11)
        self.assertIn("double ex = exp(1.0);", source)
        self.assertIn(
            "return scaled >= 0 && scaled <= max_scaled_error;",
            source,
        )
        self.assertNotIn("known bug", source)
        self.assertNotIn("calling-convention edge cases", source)

    def test_feature13_requires_exact_decimal_literal_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = "[feature13-literal] PASS double=2 float=2 edge=3\n"

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-literal] FAIL",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for literal in (
            "double literal_double = 0.75",
            "float literal_float = 0.75f",
            "1.00000000000000011102230246251565404236316680908203125",
            "1.000000059604644775390625f",
            "double literal_subnormal = 5e-324",
            "double literal_overflow = 1e400",
            "double literal_negative_zero = -0e-9999",
        ):
            with self.subTest(literal=literal):
                self.assertIn(literal, source)

    def test_unary_command_allows_only_its_expected_compiler_error(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                output,
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            cursor, _data = gui_terminal_smoke.wait_frontier_command(
                process,
                log,
                command,
                start_offset=0,
                timeout=1.0,
            )
            self.assertGreater(cursor, 0)

            log.write_text(
                output.replace(
                    "unary sign requires an arithmetic scalar operand",
                    "undefined variable",
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*\\[cupidc\\] error",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=1.0,
                )

    def test_unary_command_rejects_its_diagnostic_before_compile_context(
        self,
    ):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                diagnostic + output.replace(diagnostic, ""),
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "outside.*feature13_double",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=0.1,
                )

    def test_unary_command_rejects_a_second_copy_of_its_diagnostic(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(output + diagnostic, encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "exactly once.*feature13_double",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=0.1,
                )

    def test_unary_command_accepts_each_trailing_diagnostic_prefix(self):
        command = _frontier_command("/bin/feature13_double.cc")
        context = (
            "[cupidc] JIT compile: /bin/feature13_double.cc\n"
        )
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )

        for prefix_length in range(1, len(diagnostic)):
            with self.subTest(prefix_length=prefix_length):
                checked = gui_terminal_smoke.mask_frontier_command_failures(
                    context + diagnostic[:prefix_length],
                    command,
                )
                self.assertIsNone(
                    gui_terminal_smoke.frontier_failure_marker(checked)
                )

        checked = gui_terminal_smoke.mask_frontier_command_failures(
            context + diagnostic[:-1] + "\r",
            command,
        )
        self.assertIsNone(
            gui_terminal_smoke.frontier_failure_marker(checked)
        )

    def test_unary_command_does_not_mask_an_embedded_partial_diagnostic(
        self,
    ):
        command = _frontier_command("/bin/feature13_double.cc")
        data = (
            "[cupidc] JIT compile: /bin/feature13_double.cc\n"
            "junk[cupidc] error (line 1): unary sign requires"
        )

        checked = gui_terminal_smoke.mask_frontier_command_failures(
            data,
            command,
        )
        self.assertEqual(
            gui_terminal_smoke.frontier_failure_marker(checked),
            "[cupidc] error",
        )

    def test_unary_command_rejects_an_embedded_complete_diagnostic(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                output.replace(diagnostic, "junk" + diagnostic),
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*\\[cupidc\\] error",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=0.1,
                )

    def test_unary_command_rejects_an_embedded_compile_context(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        context = "[cupidc] JIT compile: /bin/feature13_double.cc"
        malformed_contexts = (
            ("leading text", "junk" + context),
            ("trailing text", context + "junk"),
        )
        for name, malformed in malformed_contexts:
            with (
                self.subTest(name=name),
                tempfile.TemporaryDirectory() as temporary,
            ):
                log = Path(temporary) / "serial.log"
                log.write_text(
                    output.replace(context, malformed),
                    encoding="utf-8",
                )
                process = mock.Mock()
                process.poll.return_value = None

                with self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "outside.*feature13_double",
                ):
                    gui_terminal_smoke.wait_frontier_command(
                        process,
                        log,
                        command,
                        start_offset=0,
                        timeout=0.1,
                    )

    def test_libm_command_requires_total_and_pass_markers(self):
        expected = _frontier_command(
            "/bin/feature15_libm.cc"
        ).expected_pattern
        for marker in (
            "[feature15-x87] 7 range checks, 0 failed\n",
            "[feature15] 29 checks total, 0 failed\n",
            "PASS feature15_libm\n",
        ):
            with self.subTest(marker=marker):
                output = _frontier_command_output(
                    "/bin/feature15_libm.cc"
                ).replace(marker, "")
                self.assertIsNone(
                    re.search(expected, output, re.S | re.M)
                )
        self.assertIn(
            "FAIL feature15_libm",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_libm_guest_source_calls_the_production_math_path(self):
        source = (
            REPO_ROOT / "bin" / "feature15_libm.cc"
        ).read_text(encoding="utf-8")

        for function in (
            "sin(",
            "cos(",
            "tan(",
            "atan(",
            "sqrt(",
            "exp(",
            "expf(",
            "exp2(",
            "exp2f(",
            "log(",
            "pow(",
            "powf(",
            "sinh(",
            "fabs(",
        ):
            with self.subTest(function=function):
                self.assertIn(function, source)
        self.assertIn(
            '"[feature15] %d checks total, %d failed\\n"',
            source,
        )
        self.assertIn(
            '"[feature15-x87] 7 range checks, %d failed\\n"',
            source,
        )
        self.assertIn('"PASS feature15_libm\\n"', source)
        self.assertEqual(source.count("scaled != 0"), 29)
        self.assertNotIn("scaled > 0", source)
        self.assertIn("double neg_x = -1.5;", source)
        self.assertNotIn("0.0 - 1.5", source)

        libm_source = (
            REPO_ROOT / "kernel" / "cpu" / "libm.cc"
        ).read_text(encoding="utf-8")
        self.assertEqual(libm_source.count("fsubr  %st, %st(1)"), 4)
        self.assertEqual(libm_source.count("fsubr  %%st, %%st(1)"), 3)
        self.assertNotIn("fsub   %st, %st(1)", libm_source)
        self.assertNotIn("fsub   %%st, %%st(1)", libm_source)

    def test_iso_jpeg_fixture_is_a_byte_fixed_baseline_image(self):
        data = JPEG_FIXTURE.read_bytes()
        self.assertEqual(len(data), 331)
        self.assertEqual(
            hashlib.sha256(data).hexdigest(),
            "76aac1d6ee61f230d47cd6fef3ba1ea5"
            "0fe55f1a32634c109489cb3b8d931957",
        )
        sof0 = data.index(b"\xff\xc0")
        self.assertEqual(
            data[sof0:sof0 + 13],
            b"\xff\xc0\x00\x0b\x08\x00\x08\x00\x08\x01\x01\x11\x00",
        )
        self.assertNotIn(b"\xff\xc2", data)

    def test_system_image_tracks_the_iso_runtime_fixture(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        logical_makefile = re.sub(r"\\\r?\n[ \t]*", " ", makefile)
        rule = re.search(
            r"^\$\(OS_IMAGE\): ([^\n]+)$",
            logical_makefile,
            re.MULTILINE,
        )
        self.assertIsNotNone(rule)
        self.assertIn("test_iso/hello.iso", rule.group(1).split())

    def test_iso_command_requires_the_jpeg_and_glyph_markers(self):
        expected = _frontier_command(
            "/bin/feature17_iso.cc"
        ).expected_pattern
        for marker in (
            "PASS jpeg_decode_mem baseline 8x8 gray128\n",
            (
                "PASS glyph_rasterize Liberation Mono Q size37 "
                "width=22 cache=22\n"
            ),
        ):
            with self.subTest(marker=marker):
                output = _frontier_command_output(
                    "/bin/feature17_iso.cc"
                ).replace(marker, "")
                self.assertIsNone(
                    re.search(expected, output, re.S | re.M)
                )
        mismatched_cache = _frontier_command_output(
            "/bin/feature17_iso.cc"
        ).replace(
            "width=22 cache=22",
            "width=22 cache=23",
        )
        self.assertIsNone(
            re.search(expected, mismatched_cache, re.S | re.M)
        )
        self.assertIn(
            "FAIL jpeg_decode_mem",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        self.assertIn(
            "FAIL glyph_rasterize",
            gui_terminal_smoke.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_iso_guest_source_calls_the_production_glyph_path(self):
        source = (
            REPO_ROOT / "bin" / "feature17_iso.cc"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'fontsys_match("Liberation Mono", 3, 400, 0)',
            source,
        )
        self.assertEqual(
            source.count('fontsys_run_width(face, 37, "Q", 1)'),
            2,
        )
        self.assertIn(
            "PASS glyph_rasterize Liberation Mono Q size37",
            source,
        )

    def test_iso_guest_source_checks_readdir_rock_ridge_names(self):
        source = (
            REPO_ROOT / "bin" / "feature17_iso.cc"
        ).read_text(encoding="utf-8")

        self.assertIn("int check_directory_names()", source)
        self.assertIn('vfs_open("/iso", 0)', source)
        self.assertIn("vfs_readdir(fd, ent)", source)
        self.assertIn(
            'strcmp(ent, "long_named_file.txt")',
            source,
        )
        self.assertIn(
            "PASS feature17_readdir names=6 "
            "long=long_named_file.txt",
            source,
        )
        command = next(
            entry
            for entry in gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS
            if entry.text == "/bin/feature17_iso.cc"
        )
        self.assertIn(
            "PASS feature17_readdir names=6",
            command.expected_pattern,
        )

    def test_command_sequence_waits_for_each_marker_before_continuing(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("", encoding="utf-8")
            monitor = SequencedMonitorSocket(
                log,
                _frontier_command_outputs(),
                delayed_marker=(
                    "[godsong] settings ready\n"
                    "[gfx2d] popup input ready\n"
                ),
            )
            process = mock.Mock()
            process.poll.return_value = None
            marker_release_sent_counts = []

            def release_interaction_marker(_seconds):
                if monitor.pending_marker is None:
                    return
                self.assertEqual(
                    len(monitor.sent),
                    monitor.marker_wait_sent_count,
                )
                marker_release_sent_counts.append(len(monitor.sent))
                monitor.release_delayed_marker()

            with mock.patch(
                "tools.gui_terminal_smoke.time.sleep",
                side_effect=release_interaction_marker,
            ) as sleep:
                data = gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=0,
                    timeout=1.0,
                    key_pause=0.01,
                )

        self.assertEqual(
            monitor.completed,
            len(gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS),
        )
        self.assertEqual(
            marker_release_sent_counts,
            [monitor.marker_wait_sent_count],
        )
        self.assertIsNone(monitor.pending_tail)
        self.assertIn("[cupidc] JIT compile: /bin/godsong.cc", data)
        self.assertIn("[gfx2d] flip frame=2", data)
        self.assertEqual(
            sleep.call_args_list.count(mock.call(1.0)),
            len(gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS) - 1,
        )
        self.assertEqual(sleep.call_args_list.count(mock.call(2.0)), 0)
        sent = b"".join(monitor.sent)
        self.assertIn(b"sendkey shift-minus 300\n", sent)
        self.assertIn(b"sendkey dot 300\n", sent)
        self.assertEqual(
            monitor.sent[-8:],
            [b"sendkey esc 300\n"] * 8,
        )

    def test_godsong_interaction_rejects_an_earlier_graphics_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                "[gfx2d] flip frame=2\n"
                "[cupidc] JIT compile: /bin/godsong.cc\n",
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None
            command = _frontier_command("godsong 1 200")
            interaction = gui_terminal_smoke.TerminalCommand(
                command.text,
                command.interaction_pattern,
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.time.time",
                    side_effect=(0.0, 0.0, 1.0),
                ),
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "timed out waiting",
                ),
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    interaction,
                    start_offset=0,
                    timeout=0.5,
                )

    def test_doom_recovery_does_not_reuse_the_first_ls_completion(self):
        command_texts = [
            command.text
            for command in gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS
        ]
        post_doom_ls_index = [
            index
            for index, text in enumerate(command_texts)
            if text == "ls"
        ][1]
        command_outputs = _frontier_command_outputs()
        command_outputs[post_doom_ls_index] = ""

        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, command_outputs)
            process = mock.Mock()
            process.poll.return_value = 9

            with (
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "frontier command 'ls'.*QEMU exited with status 9",
                ),
            ):
                gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=0,
                    timeout=1.0,
                    key_pause=0.01,
                )

        self.assertEqual(monitor.completed, post_doom_ls_index + 1)

    def test_syscall_demo_failure_cannot_pass_on_jit_completion(self):
        failed = _frontier_command_output(
            "as /demos/syscall_vfs_extended_demo.asm"
        ).replace(
            "extended SYS VFS calls: OK",
            "extended SYS VFS calls: FAIL",
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(failed, encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker: extended SYS VFS calls: FAIL",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command(
                        "as /demos/syscall_vfs_extended_demo.asm"
                    ),
                    0,
                    0.1,
                )

    def test_command_sequence_reports_qemu_exit_and_pending_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("", encoding="utf-8")
            monitor = FakeMonitorSocket()
            process = mock.Mock()
            process.poll.return_value = 7

            with (
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "frontier command 'ls'.*QEMU exited with status 7",
                ),
            ):
                gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=0,
                    timeout=1.0,
                    key_pause=0.01,
                )

    def test_command_sequence_ignores_a_stale_completion_before_its_cursor(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_command_output("ls"),
                encoding="utf-8",
            )
            start_offset = len(log.read_text(encoding="utf-8"))
            monitor = FakeMonitorSocket()
            process = mock.Mock()
            process.poll.return_value = 9

            with (
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "frontier command 'ls'.*QEMU exited with status 9",
                ),
            ):
                gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=start_offset,
                    timeout=1.0,
                    key_pause=0.01,
                )

    def test_complete_frontier_runtime_log_passes(self):
        gui_terminal_smoke.validate_frontier_runtime_log(
            _frontier_runtime_log()
        )

    def test_complete_frontier_allows_only_the_expected_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        data = _frontier_runtime_log() + output
        gui_terminal_smoke.validate_frontier_runtime_log(data)

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*\\[cupidc\\] error",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                data.replace(
                    "unary sign requires an arithmetic scalar operand",
                    "undefined variable",
                )
            )

    def test_complete_frontier_rejects_a_stale_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        data = (
            _frontier_runtime_log()
            + diagnostic
            + output.replace(diagnostic, "")
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "outside.*feature13_double",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_complete_frontier_rejects_a_duplicate_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        data = _frontier_runtime_log() + output + diagnostic

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "exactly once.*feature13_double",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_complete_frontier_rejects_an_embedded_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        data = _frontier_runtime_log() + output.replace(
            diagnostic,
            "junk" + diagnostic,
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*\\[cupidc\\] error",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_complete_frontier_rejects_an_embedded_compile_context(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        context = "[cupidc] JIT compile: /bin/feature13_double.cc"
        malformed_contexts = (
            ("leading text", "junk" + context),
            ("trailing text", context + "junk"),
        )
        for name, malformed in malformed_contexts:
            data = _frontier_runtime_log() + output.replace(
                context,
                malformed,
            )
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "outside.*feature13_double",
                ),
            ):
                gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_frontier_runtime_requires_traffic_from_the_selected_nic(self):
        rtl8139 = (
            _frontier_runtime_log()
            .replace("e1000: init OK", "rtl8139: init OK")
            .replace("net: if=e1000", "net: if=rtl8139")
        )
        gui_terminal_smoke.validate_frontier_runtime_log(
            rtl8139,
            "rtl8139",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTL8139 initialization marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                _frontier_runtime_log(),
                "rtl8139",
            )

        no_traffic = rtl8139.replace(
            "[0.990] [INFO]  net: if=rtl8139 ip=10.0.2.15\n",
            "",
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTL8139 packet traffic marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                no_traffic,
                "rtl8139",
            )

    def test_frontier_runtime_rejects_every_rtl8139_probe_failure(self):
        for marker in (
            "rtl8139: BAR0 not IO port",
            "rtl8139: rx alloc failed",
            "rtl8139: tx alloc failed",
        ):
            with (
                self.subTest(marker=marker),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    f"found failure marker: {re.escape(marker)}",
                ),
            ):
                gui_terminal_smoke.validate_frontier_runtime_log(
                    _frontier_runtime_log() + marker + "\n",
                )

    def test_missing_frontier_marker_names_the_unproved_subsystem(self):
        data = _frontier_runtime_log().replace(
            "[5.650] [INFO]  usb_hid: keyboard attached addr=1\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing USB keyboard marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_pass_without_dma_refills_does_not_prove_ac97_runtime(self):
        data = _frontier_runtime_log().replace(
            "[ac97] DMA refills during audiotest: 441\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing AC97 refill exercise marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_rtc_requires_real_fields_and_rejects_invalid_data(self):
        gui_terminal_smoke.validate_frontier_runtime_log(
            _frontier_runtime_log()
        )

        placeholder = _frontier_runtime_log().replace(
            "RTC: 2026-07-24 22:14:05",
            "RTC: 2026-%02u-%02u %02u:%02u:%02u",
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTC marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(placeholder)

        unpadded = _frontier_runtime_log().replace(
            "RTC: 2026-07-24 22:14:05",
            "RTC: 2026-7-24 22:14:5",
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTC marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(unpadded)

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*RTC: invalid data",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                _frontier_runtime_log()
                + "[WARN] RTC: invalid data (time or date out of range)\n"
            )

    def test_frontier_failures_are_rejected_case_insensitively(self):
        data = _frontier_runtime_log() + "USB_MSC: READ CAPACITY FAILED\n"

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*usb_msc: read capacity failed",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_guest_compiler_errors_stop_the_command_gate_immediately(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                "[cupidc] error (line 1): undefined variable\n",
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*\\[cupidc\\] error",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("ls"),
                    start_offset=0,
                    timeout=1.0,
                )

    def test_unresolved_guest_symbol_stops_the_command_gate_immediately(self):
        marker = "[cupidc] Unresolved symbol: gfx2d_blur_box"
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(marker + "\n", encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*Unresolved symbol",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("ls"),
                    start_offset=0,
                    timeout=0.1,
                )

    def test_missing_usb_image_stops_before_qemu_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            image.write_bytes(b"system image")
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(root / "missing-usb.img"),
                    "--verify-frontier-runtime",
                ]
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen"
                ) as launch,
                mock.patch("sys.stderr"),
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 2)
        launch.assert_not_called()

    def test_frontier_mode_runs_on_private_disk_copies_and_all_validators(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            usb_image = root / "frontier-usb.img"
            image_bytes = b"system image bytes"
            image.write_bytes(image_bytes)
            _write_frontier_usb_image(usb_image)
            usb_bytes = usb_image.read_bytes()
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(usb_image),
                    "--verify-frontier-runtime",
                ]
            )
            process = mock.Mock()
            process.poll.return_value = None
            monitor = FakeMonitorSocket()
            copied_paths = []
            stopped = {"value": False}
            event_order = []

            def launch_private_images(command, **_kwargs):
                drives = [
                    command[index + 1]
                    for index, item in enumerate(command)
                    if item == "-drive"
                ]
                blockdevs = [
                    command[index + 1]
                    for index, item in enumerate(command)
                    if item == "-blockdev"
                ]
                self.assertEqual(len(drives), 1)
                self.assertEqual(len(blockdevs), 2)
                system_path = Path(
                    drives[0].split(",", 1)[0].split("=", 1)[1]
                )
                usb_file = next(
                    option
                    for option in blockdevs
                    if option.startswith("driver=file,")
                )
                usb_path = Path(
                    next(
                        field.split("=", 1)[1]
                        for field in usb_file.split(",")
                        if field.startswith("filename=")
                    )
                )
                copied_paths.extend((system_path, usb_path))
                for path in copied_paths:
                    self.assertTrue(path.is_file())
                self.assertNotIn(image, copied_paths)
                self.assertNotIn(usb_image, copied_paths)
                self.assertEqual(copied_paths[0].read_bytes(), image_bytes)
                self.assertEqual(copied_paths[1].read_bytes(), usb_bytes)
                copied_paths[0].write_bytes(b"guest changed system disk")
                copied_paths[1].write_bytes(b"guest changed USB disk")
                return process

            def mark_stopped(_process, _monitor):
                stopped["value"] = True

            def check_audio_after_stop(_path):
                self.assertTrue(stopped["value"])
                return gui_terminal_smoke.AudioEvidence(
                    channels=2,
                    sample_rate=22050,
                    frames=44100,
                    peak=8000,
                )

            def run_commands(*_args, **_kwargs):
                event_order.append("commands")
                return _frontier_runtime_log()

            def capture_frame(_monitor, path, **_kwargs):
                event_order.append(f"capture:{path.name}")

            def inject_mouse(_monitor):
                event_order.append("mouse")

            def run_replug(*_args, **_kwargs):
                event_order.append("replug")
                return _frontier_runtime_log()

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen",
                    side_effect=launch_private_images,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.free_tcp_port",
                    return_value=43210,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log",
                    side_effect=[
                        (True, "Entering desktop environment"),
                        (True, "Terminal launched"),
                        (False, _frontier_runtime_log()),
                    ],
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log_success_count",
                    return_value=(True, _frontier_runtime_log()),
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.connect_monitor",
                    return_value=monitor,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.qemu_supports_wav_audio",
                    return_value=True,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.run_frontier_commands",
                    side_effect=run_commands,
                ) as commands,
                mock.patch(
                    "tools.gui_terminal_smoke.capture_screendump",
                    side_effect=capture_frame,
                ) as screendump,
                mock.patch(
                    "tools.gui_terminal_smoke.inject_mouse_activity",
                    side_effect=inject_mouse,
                ) as mouse,
                mock.patch(
                    "tools.gui_terminal_smoke.run_frontier_usb_replug_contract",
                    side_effect=run_replug,
                ) as replug,
                mock.patch(
                    "tools.gui_terminal_smoke.validate_framebuffer_change",
                    return_value=gui_terminal_smoke.FramebufferEvidence(
                        width=640,
                        height=480,
                        changed_pixels=32,
                    ),
                ) as framebuffer,
                mock.patch(
                    "tools.gui_terminal_smoke.validate_wav_audio",
                    side_effect=check_audio_after_stop,
                ) as audio,
                mock.patch(
                    "tools.gui_terminal_smoke.stop_qemu",
                    side_effect=mark_stopped,
                ),
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
            ):
                status = gui_terminal_smoke.run(args)

            self.assertEqual(status, 0)
            self.assertEqual(image.read_bytes(), image_bytes)
            self.assertEqual(usb_image.read_bytes(), usb_bytes)
            commands.assert_called_once()
            self.assertEqual(screendump.call_count, 2)
            mouse.assert_called_once_with(monitor)
            self.assertEqual(
                monitor.sent,
                [
                    b"sendkey ctrl-alt-t 300\n",
                ],
            )
            self.assertEqual(
                event_order,
                [
                    "commands",
                    "capture:before.ppm",
                    "mouse",
                    "capture:after.ppm",
                    "replug",
                ],
            )
            replug.assert_called_once_with(
                process,
                monitor,
                mock.ANY,
                timeout=args.timeout,
                key_pause=args.key_pause,
            )
            framebuffer.assert_called_once()
            self.assertEqual(audio.call_count, 2)
            self.assertEqual(
                [call.args[0].name for call in audio.call_args_list],
                ["ac97.wav", "pcspk.wav"],
            )

    def test_frontier_mode_requires_qemu_wav_capture_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            usb_image = root / "frontier-usb.img"
            image.write_bytes(b"system image")
            _write_frontier_usb_image(usb_image)
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(usb_image),
                    "--verify-frontier-runtime",
                ]
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.qemu_supports_wav_audio",
                    return_value=False,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen"
                ) as launch,
                mock.patch("sys.stderr"),
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 2)
        launch.assert_not_called()

    def test_frontier_mode_rejects_a_usb_image_without_an_mbr(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            usb_image = root / "frontier-usb.img"
            image.write_bytes(b"system image")
            usb_image.write_bytes(b"\0" * 1024)
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(usb_image),
                    "--verify-frontier-runtime",
                ]
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.qemu_supports_wav_audio"
                ) as audio_probe,
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen"
                ) as launch,
                mock.patch("sys.stderr"),
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 2)
        audio_probe.assert_not_called()
        launch.assert_not_called()

    def test_frontier_usb_partition_must_fit_inside_the_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            usb_image = Path(temporary) / "frontier-usb.img"
            _write_frontier_usb_image(usb_image)
            contents = bytearray(usb_image.read_bytes())
            partition = 0x1BE
            contents[partition + 12:partition + 16] = (6000).to_bytes(
                4,
                "little",
            )
            usb_image.write_bytes(contents)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "partition exceeds",
            ):
                gui_terminal_smoke.validate_frontier_usb_image(usb_image)

    def test_frontier_usb_partition_requires_a_fat16_boot_sector(self):
        with tempfile.TemporaryDirectory() as temporary:
            usb_image = Path(temporary) / "frontier-usb.img"
            _write_frontier_usb_image(usb_image)
            contents = bytearray(usb_image.read_bytes())
            contents[512:1024] = b"\0" * 512
            usb_image.write_bytes(contents)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "valid FAT16 boot sector",
            ):
                gui_terminal_smoke.validate_frontier_usb_image(usb_image)


class FrontierFramebufferTests(unittest.TestCase):
    def test_changed_nonuniform_screendumps_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            _write_ppm(
                before,
                4,
                4,
                [b"\0\0\0", b"\xff\0\0"] * 8,
            )
            _write_ppm(
                after,
                4,
                4,
                [b"\0\xff\0", b"\0\0\xff"] * 8,
            )

            evidence = gui_terminal_smoke.validate_framebuffer_change(
                before,
                after,
            )

        self.assertEqual(evidence.width, 4)
        self.assertEqual(evidence.height, 4)
        self.assertEqual(evidence.changed_pixels, 16)

    def test_single_pixel_change_is_not_meaningful_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            pixels = [b"\0\0\0", b"\xff\0\0"] * 8
            changed = list(pixels)
            changed[0] = b"\0\xff\0"
            _write_ppm(before, 4, 4, pixels)
            _write_ppm(after, 4, 4, changed)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "changed by only 1 pixel",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_uniform_screendump_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            _write_ppm(before, 2, 1, [b"\x01\x01\x01", b"\x01\x01\x01"])
            _write_ppm(after, 2, 1, [b"\xff\0\0", b"\0\xff\0"])

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "before framebuffer is uniform",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_black_screendump_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            _write_ppm(before, 2, 1, [b"\0\0\0", b"\0\0\0"])
            _write_ppm(after, 2, 1, [b"\xff\0\0", b"\0\xff\0"])

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "before framebuffer is black",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_unchanged_screendump_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            pixels = [b"\0\0\0", b"\xff\0\0"]
            _write_ppm(before, 2, 1, pixels)
            _write_ppm(after, 2, 1, pixels)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "changed by only 0 pixel",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_malformed_screendump_has_a_useful_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            before.write_bytes(b"P3\n1 1\n255\n0 0 0\n")
            _write_ppm(after, 1, 1, [b"\xff\0\0"])

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "before framebuffer.*P6",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_screendump_capture_uses_a_quoted_temporary_path(self):
        monitor = FakeMonitorSocket()
        with tempfile.TemporaryDirectory(prefix="frontier artifacts ") as temporary:
            output = Path(temporary) / "before frame.ppm"

            def create_dump(_monitor, command, pause=0.25):
                self.assertEqual(
                    command,
                    f'screendump "{output.as_posix()}"',
                )
                output.write_bytes(b"P6\n1 1\n255\n\x01\x02\x03")

            with mock.patch(
                "tools.gui_terminal_smoke.hmp",
                side_effect=create_dump,
            ):
                gui_terminal_smoke.capture_screendump(
                    monitor,
                    output,
                    timeout=1.0,
                )

        self.assertTrue(output.name.endswith(".ppm"))


class FrontierAudioTests(unittest.TestCase):
    def test_non_silent_pcm_capture_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_wav(capture, [0, 0, -8000, 8000, 0])

            evidence = gui_terminal_smoke.validate_wav_audio(capture)

        self.assertEqual(evidence.channels, 1)
        self.assertEqual(evidence.sample_rate, 22050)
        self.assertEqual(evidence.frames, 5)
        self.assertEqual(evidence.peak, 8000)

    def test_qemu_placeholder_lengths_use_the_closed_file_size(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_qemu_placeholder_wav(capture, [0, -3200, 8000, 0])

            evidence = gui_terminal_smoke.validate_wav_audio(capture)

            self.assertEqual(evidence.frames, 4)
            self.assertEqual(evidence.peak, 8000)

    def test_qemu_placeholder_rejects_partial_pcm_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_qemu_placeholder_wav(capture, [0, 8000])
            with capture.open("ab") as output:
                output.write(b"\x7f")

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "misaligned PCM data",
            ):
                gui_terminal_smoke.validate_wav_audio(capture)

    def test_silent_pcm_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_wav(capture, [0] * 32)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "audio capture is silent",
            ):
                gui_terminal_smoke.validate_wav_audio(capture)

    def test_malformed_audio_capture_has_a_useful_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            capture.write_bytes(b"not a wave")

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "audio capture is not a readable PCM WAV",
            ):
                gui_terminal_smoke.validate_wav_audio(capture)

    def test_qemu_wav_backend_detection_reads_driver_inventory(self):
        available = mock.Mock(
            stdout="Available audio drivers:\nnone\ndsound\nwav\n"
        )
        unavailable = mock.Mock(
            stdout="Available audio drivers:\nnone\ndsound\n"
        )

        with mock.patch(
            "tools.gui_terminal_smoke.subprocess.run",
            side_effect=[available, unavailable],
        ):
            self.assertTrue(
                gui_terminal_smoke.qemu_supports_wav_audio("qemu")
            )
            self.assertFalse(
                gui_terminal_smoke.qemu_supports_wav_audio("qemu")
            )


if __name__ == "__main__":
    unittest.main()
