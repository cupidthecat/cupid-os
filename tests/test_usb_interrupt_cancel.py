import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbInterruptCancellationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hc = (ROOT / "kernel" / "usb" / "usb_hc.h").read_text(
            encoding="utf-8"
        )
        cls.ehci = (ROOT / "kernel" / "usb" / "ehci.cc").read_text(
            encoding="utf-8"
        )
        cls.uhci = (ROOT / "kernel" / "usb" / "uhci.cc").read_text(
            encoding="utf-8"
        )
        cls.hid = (ROOT / "kernel" / "usb" / "usb_hid.cc").read_text(
            encoding="utf-8"
        )
        cls.hub = (ROOT / "kernel" / "usb" / "usb_hub.cc").read_text(
            encoding="utf-8"
        )
        cls.usb = (ROOT / "kernel" / "usb" / "usb.cc").read_text(
            encoding="utf-8"
        )
        cls.blockdev = (ROOT / "kernel" / "fs" / "blockdev.cc").read_text(
            encoding="utf-8"
        )
        cls.blockdev_header = (
            ROOT / "kernel" / "fs" / "blockdev.h"
        ).read_text(encoding="utf-8")
        cls.msc = (ROOT / "kernel" / "usb" / "usb_msc.cc").read_text(
            encoding="utf-8"
        )

    def test_both_controllers_cancel_the_matching_interrupt_slot(self):
        self.assertIn("(*cancel_interrupt)", self.hc)
        for source, prefix in (
            (self.ehci, "ehci"),
            (self.uhci, "uhci"),
        ):
            cancel = re.search(
                rf"static int {prefix}_cancel_interrupt"
                rf"\([^;]+?\) \{{.*?\n\}}",
                source,
                re.S,
            )
            self.assertIsNotNone(cancel)
            body = cancel.group(0)
            self.assertIn("candidate->t.device_addr == t->device_addr", body)
            self.assertIn("candidate->t.endpoint == t->endpoint", body)
            self.assertIn("candidate->t.buffer == t->buffer", body)
            self.assertIn(
                "usb_interrupt_ownership_request_cancel",
                body,
            )
            self.assertIn(
                "usb_interrupt_ownership_is_in_flight",
                body,
            )
            self.assertIn("usb_interrupt_ownership_retire", body)
            self.assertIn("slot->cb = NULL;", body)

    def test_class_drivers_cancel_before_freeing_report_storage(self):
        for source, function in (
            (self.hid, "hid_kbd_disconnect"),
            (self.hid, "hid_mouse_disconnect"),
            (self.hub, "hub_disconnect"),
        ):
            disconnect = re.search(
                rf"static int {function}\(.*?\n\}}",
                source,
                re.S,
            )
            self.assertIsNotNone(disconnect)
            body = disconnect.group(0)
            self.assertIn("cancel_interrupt", body)
            self.assertLess(
                body.index("cancel_interrupt"),
                body.index("kfree("),
            )
            self.assertIn("state retained", body)
            self.assertIn("return -1;", body)

    def test_root_port_disconnects_enter_the_removal_path(self):
        self.assertIn("remove_root_device(hc, port)", self.usb)
        self.assertIn(
            "usb_port_change(&c->hc, port)",
            self.ehci,
        )
        self.assertIn(
            "usb_port_change(&c->hc, p)",
            self.uhci,
        )

    def test_block_device_unregister_ends_the_index_lease(self):
        self.assertIn(
            "int blkdev_unregister(block_device_t* dev);",
            self.blockdev_header,
        )
        unregister = re.search(
            r"int blkdev_unregister\(block_device_t\* dev\) \{"
            r".*?\n\}",
            self.blockdev,
            re.S,
        )
        self.assertIsNotNone(unregister)
        body = unregister.group(0)
        self.assertIn("devices[i] == dev", body)
        self.assertIn("devices[index] = NULL;", body)
        self.assertNotIn("devices[i] = devices[i + 1];", body)
        self.assertIn("registered_device_count--;", body)
        self.assertIn("device_index_limit--;", body)
        self.assertIn("dev->registry_ref_count--;", body)
        self.assertIn("registry_lock();", body)
        self.assertIn("registry_unlock();", body)
        self.assertIsNotNone(
            re.search(r"if \(!dev\).*?return -1;", body, re.S)
        )
        self.assertIsNotNone(
            re.search(r"if \(index < 0\).*?return -1;", body, re.S)
        )

        register = re.search(
            r"int blkdev_register\(block_device_t\* dev\) \{"
            r".*?\n\}",
            self.blockdev,
            re.S,
        )
        self.assertIsNotNone(register)
        register_body = register.group(0)
        self.assertIn("devices[i] == NULL", register_body)
        self.assertIn("devices[vacant] = dev;", register_body)
        self.assertIn("vacant = device_index_limit;", register_body)
        self.assertIn("device_index_limit++;", register_body)
        self.assertIn("registered_device_count++;", register_body)
        self.assertIn("registry_lock();", register_body)
        self.assertIn("registry_unlock();", register_body)

    def test_mass_storage_disconnect_retires_cached_handles(self):
        disconnect = re.search(
            r"static int msc_disconnect\(.*?\n\}",
            self.msc,
            re.S,
        )
        self.assertIsNotNone(disconnect)
        body = disconnect.group(0)
        self.assertIn("blkdev_unregister(&st->blk)", body)
        self.assertNotIn("kfree(st)", body)
        self.assertIn("st->dev = NULL;", body)
        self.assertNotIn("st->blk.read =", body)
        self.assertNotIn("st->blk.write =", body)
        self.assertNotIn("blk_offline_read", self.msc)
        self.assertNotIn("blk_offline_write", self.msc)

        command = re.search(
            r"static int scsi_cmd\(.*?\n\}",
            self.msc,
            re.S,
        )
        self.assertIsNotNone(command)
        self.assertIn("!st->online || !st->dev", command.group(0))

        release = re.search(
            r"static void msc_block_release\(.*?\n\}",
            self.msc,
            re.S,
        )
        self.assertIsNotNone(release)
        self.assertIn("kfree(driver_data)", release.group(0))
        self.assertIn("st->blk.release = msc_block_release;", self.msc)

    def test_device_removal_is_transactional(self):
        header = (ROOT / "kernel" / "usb" / "usb.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("int  (*disconnect)(usb_device_t *dev);", header)
        self.assertIn("int usb_device_remove(usb_device_t *dev);", header)
        removal = re.search(
            r"int usb_device_remove\(usb_device_t \*dev\) \{.*?\n\}",
            self.usb,
            re.S,
        )
        self.assertIsNotNone(removal)
        body = removal.group(0)
        refusal = body.index("dev->driver->disconnect(dev) < 0")
        unpublish = body.index("release_device_slot(dev);")
        self.assertLess(refusal, unpublish)
        self.assertIn("return -1;", body[refusal:unpublish])

        release = re.search(
            r"static void release_device_slot\(.*?\n\}",
            self.usb,
            re.S,
        )
        self.assertIsNotNone(release)
        release_body = release.group(0)
        self.assertIn("release_address(dev->address);", release_body)
        self.assertIn("clear_device_slot(dev);", release_body)

        clear = re.search(
            r"static void clear_device_slot\(.*?\n\}",
            self.usb,
            re.S,
        )
        self.assertIsNotNone(clear)
        clear_body = clear.group(0)
        for cleanup in (
            "dev->address = 0;",
            "dev->driver_data = NULL;",
            "dev->driver = NULL;",
            "dev->in_use = false;",
        ):
            self.assertIn(cleanup, clear_body)

    def test_controller_failure_still_allows_logical_cancellation(self):
        for source, prefix in (
            (self.ehci, "ehci"),
            (self.uhci, "uhci"),
        ):
            cancel = re.search(
                rf"static int {prefix}_cancel_interrupt"
                rf"\([^;]+?\) \{{.*?\n\}}",
                source,
                re.S,
            )
            self.assertIsNotNone(cancel)
            body = cancel.group(0)
            self.assertNotIn("if (c->submit_failed)", body)
            self.assertIn("usb_interrupt_ownership_retire", body)

    def test_mass_storage_commands_and_disconnect_share_one_lock(self):
        command = re.search(
            r"static int scsi_cmd\(.*?\n\}",
            self.msc,
            re.S,
        )
        self.assertIsNotNone(command)
        command_body = command.group(0)
        self.assertIn("msc_command_lock(st);", command_body)
        self.assertIn("!st->online || !st->dev", command_body)
        self.assertIn("msc_command_unlock(st);", command_body)

        disconnect = re.search(
            r"static int msc_disconnect\(.*?\n\}",
            self.msc,
            re.S,
        )
        self.assertIsNotNone(disconnect)
        disconnect_body = disconnect.group(0)
        self.assertIn("msc_command_lock(st);", disconnect_body)
        self.assertIn("st->online = false;", disconnect_body)
        self.assertIn("st->dev = NULL;", disconnect_body)
        self.assertLess(
            disconnect_body.index("msc_command_lock(st);"),
            disconnect_body.index("st->online = false;"),
        )
        self.assertLess(
            disconnect_body.index("st->online = false;"),
            disconnect_body.index("msc_command_unlock(st);"),
        )

    def test_hub_removal_propagates_child_teardown_failure(self):
        disconnect = re.search(
            r"static int hub_disconnect\(.*?\n\}",
            self.hub,
            re.S,
        )
        self.assertIsNotNone(disconnect)
        body = disconnect.group(0)
        self.assertIn("usb_device_remove(child) < 0", body)
        self.assertIn("return -1;", body)
        self.assertLess(
            body.index("usb_device_remove(child) < 0"),
            body.index("cancel_interrupt"),
        )


if __name__ == "__main__":
    unittest.main()
