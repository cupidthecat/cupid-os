import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbDataToggleContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ehci = (ROOT / "kernel" / "usb" / "ehci.c").read_text(
            encoding="utf-8"
        )
        cls.uhci = (ROOT / "kernel" / "usb" / "uhci.c").read_text(
            encoding="utf-8"
        )

    def test_ehci_uses_the_completed_packet_toggle(self):
        self.assertNotIn("t->data_toggle ^ 1u", self.ehci)
        success = re.search(
            r"if \(status == 0\) \{.*?\n    \}",
            self.ehci,
            re.S,
        )
        self.assertIsNotNone(success)
        self.assertIn("t->data_toggle", success.group(0))
        self.assertIn("completion_token & EHCI_QTD_TOGGLE", success.group(0))
        self.assertIn(
            "*(volatile uint32_t *)&qh->overlay_token",
            self.ehci,
        )

    def test_ehci_starts_with_an_inactive_qh_overlay(self):
        self.assertRegex(
            self.ehci,
            re.compile(
                r"qh->current_qtd = 0;.*?"
                r"qh->overlay_next\s+= \(uint32_t\)q;.*?"
                r"qh->overlay_alt\s+= 1u;.*?"
                r"qh->overlay_token = 0;",
                re.S,
            ),
        )
        self.assertNotIn("qh->overlay_token = q->token", self.ehci)

    def test_ehci_proves_dma_is_quiescent_before_switching_or_freeing(self):
        helper = re.search(
            r"static bool ehci_quiesce_async\(.*?\n\}",
            self.ehci,
            re.S,
        )
        self.assertIsNotNone(helper)
        body = helper.group(0)
        self.assertIn("EHCI_STS_ASYNC", body)
        self.assertIn("requested && active", body)
        self.assertIn("ehci_halt_controller(c)", body)

        halt_helper = re.search(
            r"static bool ehci_halt_controller\(.*?\n\}",
            self.ehci,
            re.S,
        )
        self.assertIsNotNone(halt_helper)
        halt_body = halt_helper.group(0)
        self.assertIn("EHCI_STS_HALTED", halt_body)
        self.assertLess(
            halt_body.index("~EHCI_CMD_RUN"),
            halt_body.index("~EHCI_CMD_ASYNC_EN"),
        )

        teardowns = re.findall(
            r"if \(!ehci_quiesce_async\(c\)\) \{.*?\n    \}",
            self.ehci,
            re.S,
        )
        self.assertGreaterEqual(len(teardowns), 2)
        teardown = teardowns[-1]
        self.assertIn("kernel_panic(", teardown)
        self.assertIn("DMA ownership could not be revoked", teardown)
        self.assertNotIn("kfree(", teardown)

    def test_interrupt_pollers_persist_successful_toggles(self):
        self.assertIn(
            "slot->t.data_toggle = local.data_toggle;",
            self.ehci,
        )
        self.assertIn(
            "slot->t.data_toggle = local.data_toggle;",
            self.uhci,
        )

    def test_controller_schedule_mutation_is_serialized(self):
        for source, prefix, mutation in (
            (self.ehci, "ehci", "EHCI_OP_ASYNCLISTADDR"),
            (self.uhci, "uhci", "c->skel_qh->elem_link"),
        ):
            self.assertIn("volatile uint32_t submit_lock;", source)
            self.assertIn(f"{prefix}_submit_lock(c);", source)
            self.assertIn(
                f"__atomic_store_n(&c->submit_lock, 0u, "
                "__ATOMIC_RELEASE);",
                source,
            )
            submit_start = source.index(
                f"static int {prefix}_submit_sync(usb_hc_t *hc,"
            )
            submit_end = source.index("\n}\n", submit_start)
            body = source[submit_start:submit_end]
            self.assertLess(
                body.index(f"{prefix}_submit_lock(c);"),
                body.index(mutation),
            )
            self.assertLess(
                body.rindex(mutation),
                body.rindex(f"{prefix}_submit_unlock(c);"),
            )

    def test_uhci_halts_before_every_scheduled_descriptor_free(self):
        stop = re.search(
            r"static bool uhci_stop_schedule\(.*?\n\}",
            self.uhci,
            re.S,
        )
        self.assertIsNotNone(stop)
        body = stop.group(0)
        self.assertIn("saved_command & ~UHCI_CMD_RS", body)
        self.assertIn("UHCI_STS_HALTED", body)
        self.assertIn("timer_delay_us(UHCI_POLL_STEP_US);", body)

        teardown_start = self.uhci.rindex(
            "c->skel_qh->elem_link = UHCI_TD_TERMINATE;"
        )
        teardown_end = self.uhci.index("return status;", teardown_start)
        teardown = self.uhci[teardown_start:teardown_end]
        self.assertIn("uhci_stop_schedule(c, &saved_command)", teardown)
        self.assertIn("kernel_panic(", teardown)
        self.assertIn("kfree(td_raw[i])", teardown)
        self.assertIn("uhci_restore_schedule(c, saved_command)", teardown)
        self.assertLess(
            teardown.index("uhci_stop_schedule(c, &saved_command)"),
            teardown.index("kfree(td_raw[i])"),
        )
        self.assertLess(
            teardown.index("kfree(td_raw[i])"),
            teardown.index("uhci_restore_schedule(c, saved_command)"),
        )

    def test_multi_packet_parity_is_not_assumed_to_flip_once(self):
        def next_toggle(toggle, length, max_packet):
            packets = max(1, (length + max_packet - 1) // max_packet)
            return toggle ^ (packets & 1)

        self.assertEqual(next_toggle(0, 13, 64), 1)
        self.assertEqual(next_toggle(1, 512, 64), 1)
        self.assertEqual(next_toggle(0, 576, 64), 1)


if __name__ == "__main__":
    unittest.main()
