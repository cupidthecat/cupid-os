import hashlib
import re
import tempfile
import unittest
import wave
from pathlib import Path
from unittest import mock

from tools import gui_terminal_smoke


REPO_ROOT = Path(__file__).resolve().parents[1]
JPEG_FIXTURE = REPO_ROOT / "test_iso" / "fixtures" / "jpeg_baseline_8x8.jpg"


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
            "[feature13-call] PASS checks=10\n"
            "PASS feature13_double\n"
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
            "[cupidc] JIT compile: /bin/browser.cc\n"
            "[js] parse error: js: expected exponent digits\n"
            "[browser-js-number] PASS close=1 large=1 negzero=1 nan=1 "
            "truth=1 nanformat=1 posinfformat=1 neginfformat=1 literal=1 "
            "signedexp=1 upperexp=1 order=1 divide=1 divideassign=1 "
            "remainder=1 expcap=1 "
            "reject=1\n"
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
            "[gfx2d] flip frame=2\n"
            "[print_int] num=1 (0x0x00000001) gui_mode=1\n"
            "[print_int] num=200 (0x0x000000c8) gui_mode=1\n"
            "[cupidc] JIT execution complete\n"
        ),
    ]


def _frontier_command(text):
    return next(
        command
        for command in gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS
        if command.text == text
    )


def _frontier_command_output(text):
    command_texts = [
        command.text
        for command in gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS
    ]
    return _frontier_command_outputs()[command_texts.index(text)]


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
                "/bin/feature13_double.cc",
                "/bin/feature15_libm.cc",
                "/bin/feature17_iso.cc",
                "/bin/feature18_swap.cc",
                "dglibc_test",
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
        self.assertIn("flip frame=2", commands[-1].interaction_pattern)
        self.assertEqual(commands[-1].followup_settle_seconds, 2.0)

        for command, sample in zip(commands, _frontier_command_outputs()):
            with self.subTest(command=command.text):
                self.assertIsNotNone(
                    re.search(command.expected_pattern, sample, re.S | re.M)
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
            "reject=1",
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
            "update_float_old = update_float++",
            "update_float_new = ++update_float",
            "update_float--;",
            "update_double_old = update_double--",
            "update_double_new = --update_double",
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
        rule = re.search(
            r"^\$\(OS_IMAGE\): ([^\n]+)$",
            makefile,
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
                delayed_marker="[gfx2d] flip frame=2\n",
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
        self.assertEqual(
            sleep.call_args_list.count(mock.call(1.0)),
            len(gui_terminal_smoke.FRONTIER_RUNTIME_COMMANDS) - 1,
        )
        self.assertEqual(
            sleep.call_args_list.count(mock.call(2.0)),
            1,
        )
        sent = b"".join(monitor.sent)
        self.assertIn(b"sendkey shift-minus 300\n", sent)
        self.assertIn(b"sendkey dot 300\n", sent)
        self.assertEqual(
            monitor.sent[-8:],
            [b"sendkey esc 300\n"] * 8,
        )

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
