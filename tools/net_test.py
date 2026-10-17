#!/usr/bin/env python3
"""Run the Cupid OS network integration gate.

The harness boots headless QEMU, drives the shell through a local TCP serial
channel, and checks DHCP, ARP, ICMP, the feature21 TCP client, and the
feature22 TCP server on one selected NIC.

Usage:
    python tools/net_test.py [--nic rtl8139|e1000] [--image cupidos.img]
                             [--keep] [--boot-only]

The command returns zero after a complete pass and writes the packet capture
to tests/<nic>.pcap.
"""
from __future__ import annotations
import argparse
import os
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Callable

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_IMAGE = REPO_ROOT / "cupidos.img"
PCAP_DIR = REPO_ROOT / "tests"
PCAP_DIR.mkdir(exist_ok=True)

PROMPT = re.compile(rb"/[^\r\n]*>\s*$")
BOOT_FAILURE_RE = re.compile(
    rb"KERNEL PANIC|Heap corruption detected|\[PANIC\] CPU Exception"
)
BOOT_EVENT_RE = re.compile(
    rb"(?:" + PROMPT.pattern + rb")|(?:" + BOOT_FAILURE_RE.pattern + rb")"
)
HOST_FWD_PORT = 18080  # host port forwarded to guest tcp/80
HOST_HTTP_MAX_BYTES = 65536
HOST_HTTP_SECONDS = 20.0

# Kernel writes per-call debug lines whenever shell_print_int runs (kernel.c:366).
# They interleave inside actual shell output and break naive regex matching.
NOISE_RE = re.compile(r"\[print_int\][^\n]*\n")


class PipeExpectTimeout(TimeoutError):
    """A subprocess did not publish the requested serial marker in time."""


class PipeExpectEOF(EOFError):
    """A subprocess closed its serial stream before the requested marker."""


class PipeSession:
    """Match byte patterns on a subprocess pipe without a pseudo-terminal."""

    def __init__(self, process: subprocess.Popen):
        if process.stdin is None or process.stdout is None:
            raise ValueError("pipe session requires subprocess stdin and stdout")
        self.process = process
        self._input = process.stdin
        self._output = process.stdout
        self._socket: socket.socket | None = None
        self._read_chunk = getattr(self._output, "read1", self._output.read)
        self._write_data = self._write_pipe
        self._start_reader()

    @classmethod
    def from_socket(cls, connection: socket.socket) -> "PipeSession":
        session = cls.__new__(cls)
        session.process = None
        session._socket = connection
        session._input = None
        session._output = None
        session._read_chunk = connection.recv
        session._write_data = connection.sendall
        session._start_reader()
        return session

    def _start_reader(self) -> None:
        self._condition = threading.Condition()
        self._buffer = bytearray()
        self._eof = False
        self._reader_error: OSError | None = None
        self._reader = threading.Thread(
            target=self._read_output,
            name="qemu-serial-reader",
            daemon=True,
        )
        self._reader.start()

    def _read_output(self) -> None:
        try:
            while True:
                chunk = self._read_chunk(4096)
                if not chunk:
                    break
                with self._condition:
                    self._buffer.extend(chunk)
                    self._condition.notify_all()
        except (OSError, ValueError) as error:
            with self._condition:
                self._reader_error = error
        finally:
            with self._condition:
                self._eof = True
                self._condition.notify_all()

    @staticmethod
    def _pattern(value: bytes | re.Pattern[bytes]) -> re.Pattern[bytes]:
        if isinstance(value, bytes):
            return re.compile(re.escape(value))
        return value

    def expect(
        self,
        pattern: bytes | re.Pattern[bytes],
        timeout: float,
    ) -> tuple[bytes, bytes]:
        compiled = self._pattern(pattern)
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                match = compiled.search(self._buffer)
                if match is not None:
                    before = bytes(self._buffer[: match.start()])
                    matched = bytes(self._buffer[match.start() : match.end()])
                    del self._buffer[: match.end()]
                    return before, matched
                if self._reader_error is not None:
                    raise PipeExpectEOF(
                        f"serial reader failed: {self._reader_error}"
                    )
                if self._eof:
                    unmatched = bytes(self._buffer).decode(errors="replace")
                    raise PipeExpectEOF(
                        "serial stream ended before the requested marker; "
                        f"unmatched output: {unmatched!r}"
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    unmatched = bytes(self._buffer[-1000:]).decode(
                        errors="replace"
                    )
                    raise PipeExpectTimeout(
                        "timed out waiting for serial output; "
                        f"recent output: {unmatched!r}"
                    )
                self._condition.wait(remaining)

    def discard(self) -> None:
        with self._condition:
            self._buffer.clear()

    def send(self, data: bytes) -> None:
        try:
            self._write_data(data)
        except (BrokenPipeError, OSError) as error:
            raise PipeExpectEOF(f"serial input failed: {error}") from error

    def _write_pipe(self, data: bytes) -> None:
        self._input.write(data)
        self._input.flush()

    def close(self) -> None:
        if self._socket is not None:
            try:
                self._socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self._socket.close()
            except OSError:
                pass
            self._reader.join(timeout=1)
        for stream in (self._input, self._output):
            if stream is None:
                continue
            try:
                stream.close()
            except OSError:
                pass
        if self._socket is None:
            self._reader.join(timeout=1)


class BoundedPipeCapture:
    """Drain a process pipe while retaining a bounded diagnostic tail."""

    def __init__(self, stream, limit: int = 16384):
        self._stream = stream
        self._read_chunk = getattr(stream, "read1", stream.read)
        self._limit = limit
        self._lock = threading.Lock()
        self._buffer = bytearray()
        self._thread = threading.Thread(
            target=self._read_output,
            name="qemu-stderr-reader",
            daemon=True,
        )
        self._thread.start()

    def _read_output(self) -> None:
        try:
            while True:
                chunk = self._read_chunk(4096)
                if not chunk:
                    break
                with self._lock:
                    self._buffer.extend(chunk)
                    if len(self._buffer) > self._limit:
                        del self._buffer[: -self._limit]
        except (OSError, ValueError):
            pass

    def text(self) -> str:
        with self._lock:
            return bytes(self._buffer).decode(errors="replace").strip()

    def close(self) -> None:
        try:
            self._stream.close()
        except OSError:
            pass
        self._thread.join(timeout=1)


def _scrub(s: str) -> str:
    """Strip kernel debug noise that interleaves with shell output."""
    return NOISE_RE.sub("", s)


def _qemu_argv(
    nic: str,
    image: Path,
    pcap: Path,
    hostfwd: bool,
    *,
    serial_port: int | None = None,
) -> list[str]:
    netdev = f"user,id=n0,hostfwd=tcp::{HOST_FWD_PORT}-:80" if hostfwd else "user,id=n0"
    serial = (
        "stdio"
        if serial_port is None
        else f"tcp:127.0.0.1:{serial_port},server=on,wait=off"
    )
    return [
        "qemu-system-i386",
        "-m", "512M",
        "-cpu", "max",
        "-boot", "c",
        "-drive", f"file={image},format=raw",
        "-display", "none",
        "-serial", serial,
        "-no-reboot",
        "-no-shutdown",
        "-netdev", netdev,
        "-device", f"{nic},netdev=n0",
        "-object", f"filter-dump,id=f0,netdev=n0,file={pcap}",
    ]


def _free_tcp_port() -> int:
    listener = socket.socket()
    try:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])
    finally:
        listener.close()


def _connect_qemu_serial(
    port: int,
    process: subprocess.Popen,
    timeout: float,
    diagnostics: Callable[[], str] | None = None,
) -> socket.socket:
    def diagnostic_suffix() -> str:
        if diagnostics is None:
            return ""
        detail = diagnostics()
        if not detail:
            time.sleep(0.05)
            detail = diagnostics()
        return f"; QEMU stderr: {detail!r}" if detail else ""

    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"QEMU exited before serial connection with status "
                f"{process.returncode}{diagnostic_suffix()}"
            )
        try:
            connection = socket.create_connection(
                ("127.0.0.1", port),
                timeout=min(1.0, max(0.05, deadline - time.monotonic())),
            )
            connection.settimeout(None)
            return connection
        except OSError as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(
        f"could not connect to QEMU serial socket: {last_error}"
        f"{diagnostic_suffix()}"
    )


class QemuNet:
    def __init__(self, nic: str = "rtl8139", image: Path = DEFAULT_IMAGE,
                 hostfwd: bool = True):
        self.nic = nic
        self.image = image
        self.hostfwd = hostfwd
        self.pcap = PCAP_DIR / f"{nic}.pcap"
        self.process: subprocess.Popen | None = None
        self.session: PipeSession | None = None
        self.stderr_capture: BoundedPipeCapture | None = None

    def _expect_guest(
        self,
        pattern: bytes | re.Pattern[bytes],
        timeout: float,
        activity: str,
    ) -> tuple[bytes, bytes]:
        assert self.session is not None
        target = PipeSession._pattern(pattern)
        event = re.compile(
            rb"(?:" + BOOT_FAILURE_RE.pattern + rb")|(?:" + target.pattern + rb")",
            target.flags,
        )
        before, match = self.session.expect(event, timeout=timeout)
        if BOOT_FAILURE_RE.search(match):
            recent = (before + match)[-1000:].decode(errors="replace")
            raise RuntimeError(
                f"guest failed while {activity}; recent output: {recent!r}"
            )
        return before, match

    def boot(self, timeout: int = 60) -> str:
        serial_port = _free_tcp_port()
        argv = _qemu_argv(
            self.nic,
            self.image,
            self.pcap,
            self.hostfwd,
            serial_port=serial_port,
        )
        cmd = " ".join(argv)
        print(f"[qemu] {cmd}", flush=True)
        self.process = subprocess.Popen(
            argv,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        assert self.process.stderr is not None
        self.stderr_capture = BoundedPipeCapture(self.process.stderr)
        connection = _connect_qemu_serial(
            serial_port,
            self.process,
            min(10.0, float(timeout)),
            diagnostics=self.stderr_capture.text,
        )
        self.session = PipeSession.from_socket(connection)
        try:
            before, _match = self._expect_guest(
                PROMPT,
                timeout,
                "waiting for the shell prompt",
            )
        except (PipeExpectEOF, PipeExpectTimeout) as error:
            raise RuntimeError(f"shell prompt never appeared: {error}") from error
        return before.decode(errors="replace")

    def shell(self, line: str, timeout: int = 30) -> str:
        assert self.session is not None
        self.session.discard()
        self.session.send((line + "\r").encode())
        before, _match = self._expect_guest(
            PROMPT,
            timeout,
            f"running {line!r}",
        )
        return before.decode(errors="replace")

    def send_no_wait(self, line: str) -> None:
        assert self.session is not None
        self.session.send((line + "\r").encode())

    def collect_until(
        self,
        pattern: bytes | re.Pattern[bytes],
        timeout: int = 30,
    ) -> str:
        """Collect output until a substring or regex appears (no prompt expected)."""
        before, match = self._expect_guest(
            pattern,
            timeout,
            "waiting for guest output",
        )
        return (before + match).decode(errors="replace")

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            try:
                if self.session is not None:
                    self.session.send(b"reboot\r")
                self.process.wait(timeout=2)
            except (PipeExpectEOF, subprocess.TimeoutExpired):
                pass
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=3)
        if self.session is not None:
            self.session.close()
            self.session = None
        if self.stderr_capture is not None:
            self.stderr_capture.close()
            self.stderr_capture = None


# ---------------- tests ----------------

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
    if ip != "10.0.2.15":
        return TestResult("dhcp", False, f"unexpected ip {ip}")
    return TestResult("dhcp", True, ip)


def test_ping_gw(q: QemuNet) -> TestResult:
    raw = q.shell("ping 10.0.2.2 2", timeout=20)
    out = _scrub(raw)
    # ping reports per-seq "rtt_ms=N" and a final "recv=N" tally.
    m = re.search(r"recv=(\d+)", out)
    if m and int(m.group(1)) == 2:
        return TestResult("ping_gw", True, "recv=2")
    return TestResult("ping_gw", False, out[:300])


def test_arp(q: QemuNet) -> TestResult:
    out = _scrub(q.shell("arp"))
    # After a successful ping, gateway should be in the cache:
    # "10.0.2.2 -> 82:85:0:18:52:86"
    if re.search(
        r"10\.0\.2\.2\s*->\s*(?:[0-9a-fA-F]{1,2}:){5}"
        r"[0-9a-fA-F]{1,2}",
        out,
    ):
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
    # then connect from the host, then wait for the server's PASS line.
    q.send_no_wait("/bin/feature22_net_server.cc")
    # Wait for "[feature22] listening on port 80"
    try:
        q.collect_until(b"[feature22] listening", timeout=15)
    except PipeExpectTimeout:
        return TestResult("tcp_server", False, "server never reported listening")

    # Connect from the host through the forwarded port.
    body = ""
    connection = None
    try:
        # Manual HTTP keeps this check independent of system curl.
        connection = socket.create_connection(
            ("127.0.0.1", HOST_FWD_PORT),
            timeout=15,
        )
        connection.sendall(b"GET / HTTP/1.0\r\nHost: localhost\r\n\r\n")
        chunks = []
        received = 0
        deadline = time.monotonic() + HOST_HTTP_SECONDS
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("response deadline exceeded")
            connection.settimeout(min(5.0, remaining))
            try:
                d = connection.recv(2048)
            except socket.timeout:
                break
            if not d:
                break
            received += len(d)
            if received > HOST_HTTP_MAX_BYTES:
                raise RuntimeError(
                    "response exceeded "
                    f"{HOST_HTTP_MAX_BYTES} bytes"
                )
            chunks.append(d)
        body = b"".join(chunks).decode(errors="replace")
    except Exception as e:
        return TestResult("tcp_server", False, f"host connect: {e}")
    finally:
        if connection is not None:
            connection.close()

    # Wait for the server PASS line.
    try:
        q.collect_until(b"[feature22] PASS", timeout=15)
    except PipeExpectTimeout:
        return TestResult("tcp_server", False, f"server no PASS; got body={body[:200]!r}")

    if "Hello CupidOS" not in body:
        return TestResult("tcp_server", False, f"unexpected body: {body[:200]!r}")

    # A PASS line is not enough: the guest must return to a live shell.
    try:
        q._expect_guest(
            PROMPT,
            timeout=5,
            activity="waiting for the shell after feature22",
        )
    except (PipeExpectEOF, PipeExpectTimeout) as error:
        return TestResult(
            "tcp_server",
            False,
            f"server did not return to the shell: {error}",
        )
    return TestResult("tcp_server", True, "host got Hello CupidOS")


def run(
    nic: str,
    image: Path,
    keep: bool,
    *,
    boot_only: bool = False,
) -> bool:
    q = QemuNet(nic=nic, image=image, hostfwd=not boot_only)
    results: list[TestResult] = []
    ok = False
    try:
        boot_log = q.boot(timeout=120)
        # Tests: order matters — ping populates ARP, then we check arp.
        if boot_only:
            results.append(TestResult("headless_boot", True, "shell prompt"))
        else:
            results.append(test_dhcp(q, boot_log))
            results.append(test_ping_gw(q))
            results.append(test_arp(q))
            results.append(test_tcp_client(q))
            results.append(test_tcp_server(q))
        ok = all(result.ok for result in results)
    except (
        OSError,
        PipeExpectEOF,
        PipeExpectTimeout,
        RuntimeError,
    ) as error:
        results.append(TestResult("integration", False, str(error)))
    finally:
        running = q.process is not None and q.process.poll() is None
        if ok or not keep or not running:
            q.stop()
        else:
            print(
                f"[qemu] keeping failed {nic} guest running "
                f"(pid={q.process.pid})",
                flush=True,
            )

    print(f"\n=== {nic} results (pcap={q.pcap}) ===")
    for r in results:
        print(r)
    print(f"=== {nic}: {'OK' if ok else 'FAIL'} ===\n")
    return ok


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--nic", default="rtl8139", choices=["rtl8139", "e1000"])
    ap.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    ap.add_argument("--keep", action="store_true", help="keep QEMU running on failure")
    ap.add_argument(
        "--boot-only",
        action="store_true",
        help="stop after the headless shell prompt",
    )
    args = ap.parse_args(argv)
    if not args.image.exists():
        print(f"image not found: {args.image} (run `make headless-net-image` first)",
              file=sys.stderr)
        return 2
    ok = run(
        args.nic,
        args.image,
        args.keep,
        boot_only=args.boot_only,
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
