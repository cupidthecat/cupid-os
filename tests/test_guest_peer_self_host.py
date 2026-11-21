## @file
#  @brief Check the read-only plans used to rebuild guest toolchain workers.

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import DEFAULT, patch


GUEST_TOOLS = Path(__file__).resolve().parents[1] / "bootstrap" / "guest"
with patch.object(sys, "path", [str(GUEST_TOOLS), *sys.path]):
    import check_guest_peer_self_host as peer_replay


class GuestPeerSelfHostTests(unittest.TestCase):
    def setUp(self):
        scratch = self.enterContext(tempfile.TemporaryDirectory())
        self.scratch = Path(scratch)
        self.root = Path("checked-production")
        self.calls = self.enterContext(patch.multiple(
            peer_replay, publish_plan_iso=DEFAULT, create_disk=DEFAULT, run=DEFAULT,
            read_homefs=DEFAULT, compare_homefs_files=DEFAULT,
        ))
        self.calls["publish_plan_iso"].return_value = 0
        self.enterContext(patch.object(peer_replay, "print", create=True))

    def test_workers_compile_in_link_order_from_read_only_plans(self):
        for tool, sources in peer_replay.PEERS.items():
            with self.subTest(tool=tool):
                peer_replay.check(self.scratch, self.root, tool, "qemu", 60)
                _, _, plan, steps = self.calls["publish_plan_iso"].call_args.args
                self.assertEqual(plan.name, "p.iso")
                aliases = tuple(f"/home/i{index}.o" for index in range(len(sources)))
                pairs = tuple(path for source, obj in zip(sources, aliases, strict=True)
                              for path in (f"/{source}", obj))
                self.assertEqual(steps, (
                    ("compile-list", b"sources.job", peer_replay.job_contents("sources", *pairs)),
                    ("link", b"link.job", peer_replay.job_contents(
                        "/home/worker.elf", "0x01e00000", f"{tool}_guest_run", *aliases)),
                ))
                _, *staged = self.calls["create_disk"].call_args.args
                self.assertIn(f"{peer_replay.BUILD / peer_replay.MEDIA_BUILD}:/b", staged)
                self.assertFalse(any(entry.lower().endswith(":/homefs.sys") for entry in staged))
                command = self.calls["run"].call_args.args
                self.assertLess(command.index("mount /disk/p.iso /plan"), command.index("exec /disk/b"))
                self.assertGreater(command.index("exec /home/worker.elf"), command.index("exec /disk/b"))
                if tool == "cupidbuild":
                    self.assertEqual((self.scratch / "r1").read_bytes(), b"assemble\n/home/asm.job\n")
                    self.assertIn("cp /disk/r1 /home/cupidbuild.job", command)

    def test_plan_publication_failure_stops_before_staging_and_boot(self):
        self.calls["publish_plan_iso"].return_value = 1
        with self.assertRaisesRegex(ValueError, "could not publish the peer build plan ISO"):
            peer_replay.check(self.scratch, self.root, "cupidc", "qemu", 60)
        self.calls["create_disk"].assert_not_called()
        self.calls["run"].assert_not_called()
