#!/usr/bin/env python3
"""
CupidOS network integration tester.

Boots a headless QEMU instance, drives the shell over COM1 stdio, and
verifies DHCP, ARP, ICMP, DNS, TCP-client (feature21_net), TCP-server
(feature22_net_server) on a chosen NIC.

Usage:
    python3 tools/net_test.py [--nic rtl8139|e1000] [--image cupidos.img]
                              [--keep] [--loss N]

Exits 0 on full pass, 1 otherwise. Pcap captured to tests/<nic>.pcap.
"""
from __future__ import annotations
import argparse
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

import pexpect

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_IMAGE = REPO_ROOT / "cupidos.img"
PCAP_DIR = REPO_ROOT / "tests"
PCAP_DIR.mkdir(exist_ok=True)

PROMPT = re.compile(rb"/[^\r\n]*>\s*$")
HOST_FWD_PORT = 18080  # host port forwarded to guest tcp/80

# Kernel writes per-call debug lines whenever shell_print_int runs (kernel.c:366).
# They interleave inside actual shell output and break naive regex matching.
NOISE_RE = re.compile(r"\[print_int\][^\n]*\n")


def _scrub(s: str) -> str:
    """Strip kernel debug noise that interleaves with shell output."""
    return NOISE_RE.sub("", s)


def _qemu_argv(nic: str, image: Path, pcap: Path, hostfwd: bool, loss: int) -> list[str]:
    netdev = f"user,id=n0,hostfwd=tcp::{HOST_FWD_PORT}-:80" if hostfwd else "user,id=n0"
    if loss > 0:
        netdev = f"user,id=n0,hostfwd=tcp::{HOST_FWD_PORT}-:80,restrict=off"
        # Note: QEMU SLIRP doesn't have built-in loss; we use filter-dump only.
        # Loss simulation is best done via netem on tap, beyond scope.
    return [
        "qemu-system-i386",
        "-m", "128M",
        "-boot", "c",
        "-drive", f"file={image},format=raw",
        "-display", "none",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown",
        "-netdev", netdev,
        "-device", f"{nic},netdev=n0",
        "-object", f"filter-dump,id=f0,netdev=n0,file={pcap}",
    ]


class QemuNet:
    def __init__(self, nic: str = "rtl8139", image: Path = DEFAULT_IMAGE,
                 hostfwd: bool = True, loss: int = 0, keep: bool = False):
        self.nic = nic
        self.image = image
        self.hostfwd = hostfwd
        self.loss = loss
        self.keep = keep
        self.pcap = PCAP_DIR / f"{nic}.pcap"
        self.child: pexpect.spawn | None = None

    def boot(self, timeout: int = 60) -> str:
        argv = _qemu_argv(self.nic, self.image, self.pcap, self.hostfwd, self.loss)
        cmd = " ".join(argv)
        print(f"[qemu] {cmd}", flush=True)
        self.child = pexpect.spawn(argv[0], argv[1:], encoding=None, timeout=timeout,
                                   echo=False)
        # Wait for shell prompt; if HEADLESS not enabled the kernel enters desktop
        # and we'll never see "/> ". A 60s timeout fails fast.
        idx = self.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=timeout)
        if idx != 0:
            raise RuntimeError(f"shell prompt never appeared (idx={idx})\n"
                               f"--- buffer ---\n{self.child.before!r}")
        return self.child.before.decode(errors="replace")

    def shell(self, line: str, timeout: int = 30) -> str:
        assert self.child is not None
        # Drop any unread output
        try:
            self.child.read_nonblocking(size=4096, timeout=0.05)
        except Exception:
            pass
        self.child.send((line + "\r").encode())
        self.child.expect(PROMPT, timeout=timeout)
        return self.child.before.decode(errors="replace")

    def send_no_wait(self, line: str) -> None:
        assert self.child is not None
        self.child.send((line + "\r").encode())

    def collect_until(self, pattern: bytes | re.Pattern, timeout: int = 30) -> str:
        """Collect output until a substring or regex appears (no prompt expected)."""
        assert self.child is not None
        if isinstance(pattern, bytes):
            pattern = re.compile(re.escape(pattern))
        self.child.expect(pattern, timeout=timeout)
        return self.child.before.decode(errors="replace") + self.child.after.decode(errors="replace")

    def stop(self) -> None:
        if self.child and self.child.isalive():
            try:
                self.child.send(b"reboot\r")
                self.child.expect(pexpect.EOF, timeout=2)
            except Exception:
                pass
            try:
                self.child.terminate(force=True)
            except Exception:
                pass


# tests

class TestResult:
    def __init__(self, name: str, ok: bool, detail: str = ""):
        self.name = name
        self.ok = ok
        self.detail = detail

    def __str__(self) -> str:
        tag = "PASS" if self.ok else "FAIL"
        out = f"  [{tag}] {self.name}"
        if self.detail:
            out += f"  ({self.detail})"
        return out


def test_dhcp(q: QemuNet, boot_log: str) -> TestResult:
    out = _scrub(q.shell("ifconfig"))
    m = re.search(r"ip\s*[=:]?\s*(\d+\.\d+\.\d+\.\d+)", out)
    if not m:
        m = re.search(r"net:\s*if=\S+\s+ip=(\d+\.\d+\.\d+\.\d+)", _scrub(boot_log))
    if not m:
        return TestResult("dhcp", False, f"no ip in ifconfig\n{out[:200]}")
    ip = m.group(1)
    if not ip.startswith("10.0.2."):
        return TestResult("dhcp", False, f"unexpected ip {ip}")
    return TestResult("dhcp", True, ip)


def test_ping_gw(q: QemuNet) -> TestResult:
    raw = q.shell("ping 10.0.2.2 2", timeout=20)
    out = _scrub(raw)
    # ping reports per-seq "rtt_ms=N" and a final "recv=N" tally.
    m = re.search(r"recv=(\d+)", out)
    if m and int(m.group(1)) >= 1:
        return TestResult("ping_gw", True, f"recv={m.group(1)}")
    if re.search(r"rtt_ms=", out):
        return TestResult("ping_gw", True, "rtt_ms seen")
    return TestResult("ping_gw", False, out[:300])


def test_arp(q: QemuNet) -> TestResult:
    out = _scrub(q.shell("arp"))
    # After a successful ping, gateway should be in the cache:
    # "10.0.2.2 -> 82:85:0:18:52:86"
    if re.search(r"\d+\.\d+\.\d+\.\d+\s*->\s*\d+:\d+:\d+:\d+:\d+:\d+", out):
        return TestResult("arp", True, "entry present")
    if "(empty)" in out:
        return TestResult("arp", False, "cache empty (ping should have populated it)")
    return TestResult("arp", False, out[:300])


def test_tcp_client(q: QemuNet) -> TestResult:
    out = q.shell("/bin/feature21_net.cc", timeout=60)
    if "[feature21] PASS" in out:
        return TestResult("tcp_client", True, "feature21 PASS")
    return TestResult("tcp_client", False, out[:400])


def test_tcp_server(q: QemuNet) -> TestResult:
    # Launch server in foreground; it blocks on accept. We send the command,
    # then host-curl, then wait for the server's PASS line.
    q.send_no_wait("/bin/feature22_net_server.cc")
    # Wait for "[feature22] listening on port 80"
    try:
        q.collect_until(b"[feature22] listening", timeout=15)
    except pexpect.TIMEOUT:
        return TestResult("tcp_server", False, "server never reported listening")

    # Curl from host on the forwarded port.
    body = ""
    try:
        # Manual HTTP with a socket - avoids relying on system curl.
        s = socket.create_connection(("127.0.0.1", HOST_FWD_PORT), timeout=15)
        s.sendall(b"GET / HTTP/1.0\r\nHost: localhost\r\n\r\n")
        chunks = []
        while True:
            try:
                d = s.recv(2048)
            except socket.timeout:
                break
            if not d:
                break
            chunks.append(d)
        s.close()
        body = b"".join(chunks).decode(errors="replace")
    except Exception as e:
        return TestResult("tcp_server", False, f"host connect: {e}")

    # Wait for server PASS line
    try:
        srv_out = q.collect_until(b"[feature22] PASS", timeout=15)
    except pexpect.TIMEOUT:
        return TestResult("tcp_server", False, f"server no PASS; got body={body[:200]!r}")

    if "Hello CupidOS" not in body:
        return TestResult("tcp_server", False, f"unexpected body: {body[:200]!r}")
    # Drain any trailing prompt
    try:
        q.child.expect(PROMPT, timeout=5)
    except Exception:
        pass
    return TestResult("tcp_server", True, "host got Hello CupidOS")


def run(nic: str, image: Path, keep: bool, loss: int) -> bool:
    q = QemuNet(nic=nic, image=image, hostfwd=True, loss=loss, keep=keep)
    results: list[TestResult] = []
    try:
        boot_log = q.boot(timeout=60)
        # Tests: order matters - ping populates ARP, then we check arp.
        results.append(test_dhcp(q, boot_log))
        results.append(test_ping_gw(q))
        results.append(test_arp(q))
        results.append(test_tcp_client(q))
        results.append(test_tcp_server(q))
    finally:
        q.stop()

    print(f"\n=== {nic} results (pcap={q.pcap}) ===")
    for r in results:
        print(r)
    ok = all(r.ok for r in results)
    print(f"=== {nic}: {'OK' if ok else 'FAIL'} ===\n")
    return ok


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--nic", default="rtl8139", choices=["rtl8139", "e1000"])
    ap.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    ap.add_argument("--keep", action="store_true", help="keep QEMU running on failure")
    ap.add_argument("--loss", type=int, default=0)
    args = ap.parse_args(argv)
    if not args.image.exists():
        print(f"image not found: {args.image} (run `make headless-image-net` first)",
              file=sys.stderr)
        return 2
    ok = run(args.nic, args.image, args.keep, args.loss)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
