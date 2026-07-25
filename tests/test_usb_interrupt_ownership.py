import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbInterruptOwnershipTests(unittest.TestCase):
    def test_shared_ownership_state_machine(self):
        compiler = shlex.split(
            os.environ.get("CC", "clang" if os.name == "nt" else "cc")
        )
        with tempfile.TemporaryDirectory(
            prefix="cupid-usb-interrupt-ownership-"
        ) as build_dir:
            executable = Path(build_dir) / (
                "ownership.exe" if os.name == "nt" else "ownership"
            )
            command = compiler + [
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wconversion",
                "-Wsign-conversion",
                f"-I{ROOT / 'kernel' / 'core'}",
                f"-I{ROOT / 'kernel' / 'usb'}",
                str(ROOT / "tests" / "usb_interrupt_ownership_contract.c"),
                "-o",
                str(executable),
            ]
            if os.name == "nt":
                command[len(compiler):len(compiler)] = [
                    "-fno-ms-compatibility",
                    "-Wno-gnu-zero-variadic-macro-arguments",
                ]
            built = subprocess.run(
                command,
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(
                built.returncode,
                0,
                "strict ownership contract build failed\n"
                + built.stdout
                + built.stderr,
            )
            run = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(
                run.returncode,
                0,
                "ownership contract failed\n" + run.stdout + run.stderr,
            )

    def test_controllers_use_local_slots_and_finish_after_callbacks(self):
        for source_name, prefix in (("ehci.c", "ehci"), ("uhci.c", "uhci")):
            source = (ROOT / "kernel" / "usb" / source_name).read_text(
                encoding="utf-8"
            )
            self.assertIn(
                f"{prefix}_int_slot_t interrupt_slots["
                f"{prefix.upper()}_INT_SLOTS];",
                source,
            )
            self.assertNotIn(
                f"static {prefix}_int_slot_t {prefix}_int[",
                source,
            )

            poll_start = source.index(f"void {prefix}_poll_interrupts(void) {{")
            poll_end = source.index("\n}\n", poll_start)
            poll = source[poll_start:poll_end]
            claim = poll.index("usb_interrupt_ownership_claim")
            submit = poll.index(f"{prefix}_submit_sync")
            callback = poll.index("if (deliver) cb(0, &local);")
            finish = poll.index("usb_interrupt_ownership_finish")
            self.assertLess(claim, submit)
            self.assertLess(submit, callback)
            self.assertLess(callback, finish)
            self.assertIn(f"{prefix}_submit_unlock(c);", poll[submit:callback])

    def test_cancellation_waits_for_the_claimed_generation(self):
        for source_name, prefix in (("ehci.c", "ehci"), ("uhci.c", "uhci")):
            source = (ROOT / "kernel" / "usb" / source_name).read_text(
                encoding="utf-8"
            )
            cancel_start = source.index(
                f"static int {prefix}_cancel_interrupt(usb_hc_t *hc,"
            )
            cancel_end = source.index("\n}\n", cancel_start)
            cancel = source[cancel_start:cancel_end]
            request = cancel.index(
                "usb_interrupt_ownership_request_cancel"
            )
            wait = cancel.index(
                "while (usb_interrupt_ownership_is_in_flight"
            )
            generation_check = cancel.index(
                "slot->owner.generation != generation"
            )
            retire = cancel.index("usb_interrupt_ownership_retire")
            self.assertLess(request, wait)
            self.assertLess(wait, generation_check)
            self.assertLess(generation_check, retire)


if __name__ == "__main__":
    unittest.main()
