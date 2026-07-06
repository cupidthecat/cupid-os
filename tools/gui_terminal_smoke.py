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
    return [
        args.qemu,
        "-m",
        "512M",
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
    ]


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
        hmp(mon, "sendkey ctrl-alt-t", 0.8)
        ok, data = wait_log(proc, args.log, r"Terminal launched", 10.0)
        if not ok:
            print("Terminal did not launch from Ctrl+Alt+T", file=sys.stderr)
            print(data[-4000:], file=sys.stderr)
            return 1

        time.sleep(0.5)
        for ch in args.command:
            hmp(mon, f"sendkey {key_name(ch)}")
        hmp(mon, "sendkey ret")

        ok, data = wait_log(
            proc,
            args.log,
            r"KERNEL PANIC|Heap corruption|CORRUPTION detected|JIT execution complete",
            args.timeout,
        )
        if ok and "JIT execution complete" in data and not PANIC_RE.search(data):
            ok_after, data_after = wait_log(proc, args.log, PANIC_RE.pattern, 5.0)
            if ok_after:
                data = data_after

        if PANIC_RE.search(data):
            print("GUI terminal smoke failed: panic detected", file=sys.stderr)
            print(data[-5000:], file=sys.stderr)
            return 1
        if "JIT execution complete" not in data:
            print("GUI terminal smoke failed: command did not complete", file=sys.stderr)
            print(data[-5000:], file=sys.stderr)
            return 1

        print("GUI terminal smoke passed")
        return 0
    finally:
        if mon is not None:
            mon.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3.0)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    parser.add_argument("--image", type=Path, default=REPO_ROOT / "cupidos.img")
    parser.add_argument("--log", type=Path, default=REPO_ROOT / "tests" / "gui-terminal-smoke.log")
    parser.add_argument("--nic", default="e1000", choices=["e1000", "rtl8139"])
    parser.add_argument("--command", default="ls")
    parser.add_argument("--timeout", type=float, default=45.0)
    return run(parser.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
