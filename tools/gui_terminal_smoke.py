#!/usr/bin/env python3
"""Drive the GUI terminal in QEMU and fail on kernel panic.

This is intentionally dependency-free so it works from native Windows shells.
Build a normal GUI image first (`make`), then run:

    python tools/gui_terminal_smoke.py
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


REPO_ROOT = Path(__file__).resolve().parent.parent
PANIC_RE = re.compile(r"KERNEL PANIC|Heap corruption|CORRUPTION detected")
DEFAULT_SUCCESS_PATTERN = r"JIT execution complete"
KEY_HOLD_MILLISECONDS = 300
KEY_PAUSE_SECONDS = 0.35


def completion_pattern(success_pattern: str) -> re.Pattern[str]:
    """Return the first-event pattern for either success or a kernel panic."""
    re.compile(success_pattern)
    return re.compile(
        rf"(?:{PANIC_RE.pattern})|(?:{success_pattern})",
        re.S,
    )


def success_count(data: str, success_pattern: str) -> int:
    """Count completed command markers without depending on capture groups."""
    return sum(1 for _ in re.finditer(success_pattern, data, re.S))


def positive_count(text: str) -> int:
    """Parse a positive repeat count for argparse and direct callers."""
    value = int(text)
    if value < 1:
        raise ValueError("repeat count must be positive")
    return value


def positive_delay(text: str) -> float:
    """Parse a positive key delay for argparse and direct callers."""
    value = float(text)
    if value <= 0.0:
        raise ValueError("key pause must be positive")
    return value


def free_tcp_port() -> int:
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])
    finally:
        s.close()


def read_log(path: Path) -> str:
    try:
        return path.read_bytes().decode(errors="replace")
    except FileNotFoundError:
        return ""


def wait_log(proc: subprocess.Popen, log: Path, pattern: str, timeout: float) -> tuple[bool, str]:
    deadline = time.time() + timeout
    compiled = re.compile(pattern, re.S)
    while time.time() < deadline:
        data = read_log(log)
        if compiled.search(data):
            return True, data
        if proc.poll() is not None:
            return False, data
        time.sleep(0.1)
    return False, read_log(log)


def wait_log_success_count(
    proc: subprocess.Popen,
    log: Path,
    success_pattern: str,
    minimum_count: int,
    timeout: float,
) -> tuple[bool, str]:
    """Wait for a new repeated-command success or an immediate panic."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = read_log(log)
        if PANIC_RE.search(data):
            return True, data
        if success_count(data, success_pattern) >= minimum_count:
            return True, data
        if proc.poll() is not None:
            return False, data
        time.sleep(0.1)
    return False, read_log(log)


def connect_monitor(port: int, timeout: float) -> socket.socket:
    deadline = time.time() + timeout
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1.0)
            sock.settimeout(1.0)
            try:
                sock.recv(4096)
            except OSError:
                pass
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(0.1)
    raise RuntimeError(f"could not connect to QEMU monitor: {last_error}")


def hmp(sock: socket.socket, command: str, pause: float = 0.25) -> None:
    sock.sendall((command + "\n").encode())
    time.sleep(pause)
    try:
        sock.recv(4096)
    except OSError:
        pass


def stop_qemu(proc: subprocess.Popen, mon: socket.socket | None) -> None:
    """Request a disk-flushing QEMU exit before using hard-stop fallbacks."""
    graceful = mon is not None
    if mon is not None:
        try:
            hmp(mon, "quit")
        except OSError:
            pass
        finally:
            mon.close()
    if proc.poll() is not None:
        return
    if graceful:
        try:
            proc.wait(timeout=3.0)
            return
        except subprocess.TimeoutExpired:
            pass
    proc.terminate()
    try:
        proc.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3.0)


def send_key(
    sock: socket.socket, key: str, pause: float = KEY_PAUSE_SECONDS
) -> None:
    """Send a key report long enough for Cupid OS's USB HID poll to observe it."""
    hmp(sock, f"sendkey {key} {KEY_HOLD_MILLISECONDS}", pause)


def key_name(ch: str) -> str:
    if "a" <= ch <= "z" or "0" <= ch <= "9":
        return ch
    if ch == " ":
        return "spc"
    if ch == "/":
        return "slash"
    if ch == ".":
        return "dot"
    if ch == "-":
        return "minus"
    raise ValueError(f"unsupported smoke-test character: {ch!r}")


def qemu_args(args: argparse.Namespace, monitor_port: int) -> list[str]:
    netdev = "user,id=n0"
    command = [args.qemu]
    if args.cpu is not None:
        command.extend(("-cpu", args.cpu))
    command.extend([
        "-m",
        "512M",
        "-smp",
        f"cpus={args.smp}",
        "-boot",
        "c",
        "-drive",
        f"file={args.image},format=raw,if=ide,index=0,media=disk",
        "-rtc",
        "base=localtime",
        "-audiodev",
        "none,id=speaker",
        "-machine",
        "pcspk-audiodev=speaker",
        "-device",
        "AC97,audiodev=speaker",
        "-device",
        "piix3-usb-uhci",
        "-device",
        "usb-ehci",
        "-device",
        "usb-kbd",
        "-device",
        "usb-mouse",
        "-netdev",
        netdev,
        "-device",
        f"{args.nic},netdev=n0",
        "-display",
        "none",
        "-serial",
        f"file:{args.log}",
        "-monitor",
        f"tcp:127.0.0.1:{monitor_port},server,nowait",
        "-no-reboot",
        "-no-shutdown",
    ])
    return command


def run(args: argparse.Namespace) -> int:
    if not args.image.exists():
        print(f"image not found: {args.image}", file=sys.stderr)
        return 2

    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        args.log.unlink()
    except FileNotFoundError:
        pass

    monitor_port = free_tcp_port()
    re.compile(args.success_pattern, re.S)
    proc = subprocess.Popen(
        qemu_args(args, monitor_port),
        cwd=REPO_ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )
    mon: socket.socket | None = None
    try:
        ok, data = wait_log(proc, args.log, r"Entering desktop environment", args.timeout)
        if not ok:
            print("GUI desktop did not boot before timeout", file=sys.stderr)
            print(data[-4000:], file=sys.stderr)
            return 1

        mon = connect_monitor(monitor_port, 10.0)
        time.sleep(1.0)
        send_key(mon, "ctrl-alt-t", 0.8)
        ok, data = wait_log(proc, args.log, r"Terminal launched", 10.0)
        if not ok:
            print("Terminal did not launch from Ctrl+Alt+T", file=sys.stderr)
            print(data[-4000:], file=sys.stderr)
            return 1

        time.sleep(0.5)
        completed = success_count(read_log(args.log), args.success_pattern)
        for iteration in range(args.repeat):
            for ch in args.command:
                send_key(mon, key_name(ch), args.key_pause)
            send_key(mon, "ret", args.key_pause)

            ok, data = wait_log_success_count(
                proc,
                args.log,
                args.success_pattern,
                completed + 1,
                args.timeout,
            )
            if PANIC_RE.search(data):
                print("GUI terminal smoke failed: panic detected", file=sys.stderr)
                print(data[-5000:], file=sys.stderr)
                return 1
            if not ok or success_count(data, args.success_pattern) < completed + 1:
                print(
                    "GUI terminal smoke failed: command did not complete "
                    f"({iteration + 1}/{args.repeat})",
                    file=sys.stderr,
                )
                print(data[-5000:], file=sys.stderr)
                return 1
            completed += 1

        ok_after, data_after = wait_log(proc, args.log, PANIC_RE.pattern, 5.0)
        if ok_after:
            print("GUI terminal smoke failed: panic detected", file=sys.stderr)
            print(data_after[-5000:], file=sys.stderr)
            return 1

        print("GUI terminal smoke passed")
        return 0
    finally:
        stop_qemu(proc, mon)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    parser.add_argument("--image", type=Path, default=REPO_ROOT / "cupidos.img")
    parser.add_argument("--log", type=Path, default=REPO_ROOT / "tests" / "gui-terminal-smoke.log")
    parser.add_argument("--nic", default="e1000", choices=["e1000", "rtl8139"])
    parser.add_argument(
        "--smp",
        type=positive_count,
        default=1,
        help="number of virtual CPUs",
    )
    parser.add_argument(
        "--cpu",
        help="QEMU CPU model, such as max for optional instruction coverage",
    )
    parser.add_argument("--command", default="ls")
    parser.add_argument(
        "--repeat",
        type=positive_count,
        default=1,
        help="number of sequential command completions required",
    )
    parser.add_argument(
        "--key-pause",
        type=positive_delay,
        default=KEY_PAUSE_SECONDS,
        help="seconds to wait after each emulated key report",
    )
    parser.add_argument(
        "--success-pattern",
        default=DEFAULT_SUCCESS_PATTERN,
        help="regular expression required in serial output after the command",
    )
    parser.add_argument("--timeout", type=float, default=45.0)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
