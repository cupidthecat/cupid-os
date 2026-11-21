#!/usr/bin/env python3
## @file
#  @brief Run Cupid OS GUI terminal checks in QEMU.
"""Drive the GUI terminal in QEMU and fail on kernel panic.

This is intentionally dependency-free so it works from native Windows shells.
Build a normal GUI image first (`make`), then run:

    python tools/gui_terminal_smoke.py
"""
from __future__ import annotations

import argparse
import gc
import io
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import wave
from dataclasses import dataclass
from pathlib import Path

if __package__:
    from . import gui_terminal_contracts as contracts
else:
    import gui_terminal_contracts as contracts


REPO_ROOT = Path(__file__).resolve().parent.parent
PANIC_RE = re.compile(r"KERNEL PANIC|Heap corruption|CORRUPTION detected")
PANIC_REASON_RE = re.compile(r"(?m)^\[PANIC\][^\r\n]*")
PANIC_REASON_GRACE_SECONDS = 1.0
DEFAULT_SUCCESS_PATTERN = r"JIT execution complete"
KEY_HOLD_MILLISECONDS = 300
KEY_PAUSE_SECONDS = 0.35
COMMAND_SETTLE_SECONDS = 1.0
MIN_CHANGED_PIXELS = 16


class SmpRuntimeContractError(RuntimeError):
    """A serial log did not prove the checked four-vCPU runtime contract."""


class FrontierRuntimeContractError(RuntimeError):
    """The serial or artifact evidence did not prove the frontier runtime."""


@dataclass(frozen=True)
class FramebufferEvidence:
    """The dimensions and changed-pixel count from two QEMU screendumps."""

    width: int
    height: int
    changed_pixels: int


@dataclass(frozen=True)
class AudioEvidence:
    """The useful PCM facts retained from QEMU's WAV capture."""

    channels: int
    sample_rate: int
    frames: int
    peak: int


def _required_nic_patterns(
    nic: str,
    require_traffic: bool,
) -> tuple[tuple[str, str], ...]:
    try:
        patterns = contracts.NIC_RUNTIME_PATTERNS[nic]
    except KeyError as error:
        raise ValueError(f"unsupported runtime NIC: {nic}") from error
    return patterns if require_traffic else patterns[:1]


def validate_smp_runtime_log(data: str, nic: str = "e1000") -> None:
    """Require the SMP, crypto, network, desktop, and command boot evidence."""
    for marker in contracts.SMP_RUNTIME_REQUIRED_MARKERS:
        if marker not in data:
            raise SmpRuntimeContractError(
                f"missing required marker: {marker}"
            )

    success_count = data.count("[tls-selftest] ok:")
    if success_count != contracts.SMP_RUNTIME_TLS_SUCCESS_COUNT:
        raise SmpRuntimeContractError(
            f"found {success_count} TLS self-test successes; "
            f"expected {contracts.SMP_RUNTIME_TLS_SUCCESS_COUNT}"
        )

    folded = data.casefold()
    for marker in contracts.SMP_RUNTIME_REJECTED_MARKERS:
        if marker.casefold() in folded:
            raise SmpRuntimeContractError(
                f"found failure marker: {marker}"
            )
    for marker in contracts.NIC_RUNTIME_REJECTED_MARKERS:
        if marker.casefold() in folded:
            raise SmpRuntimeContractError(
                f"found failure marker: {marker}"
            )
    for subsystem, pattern in _required_nic_patterns(nic, False):
        if re.search(pattern, data, re.S | re.M) is None:
            raise SmpRuntimeContractError(
                f"missing {subsystem} marker: {pattern}"
            )


def validate_frontier_runtime_log(data: str, nic: str = "e1000") -> None:
    """Require the stable subsystem markers for the port-I/O cohort."""
    checked_data = mask_completed_frontier_command_failures(data)
    failure = frontier_failure_marker(
        checked_data,
    )
    if failure is not None:
        raise FrontierRuntimeContractError(
            f"found failure marker: {failure}"
        )

    for subsystem, pattern in contracts.FRONTIER_RUNTIME_REQUIRED_PATTERNS:
        if re.search(pattern, data, re.S | re.M) is None:
            raise FrontierRuntimeContractError(
                f"missing {subsystem} marker: {pattern}"
            )
    for subsystem, pattern in _required_nic_patterns(nic, True):
        if re.search(pattern, data, re.S | re.M) is None:
            raise FrontierRuntimeContractError(
                f"missing {subsystem} marker: {pattern}"
            )


def _ppm_token(data: bytes, offset: int) -> tuple[bytes, int]:
    while offset < len(data):
        if data[offset] in b" \t\r\n":
            offset += 1
            continue
        if data[offset] == ord("#"):
            newline = data.find(b"\n", offset)
            if newline < 0:
                raise ValueError("unterminated PPM comment")
            offset = newline + 1
            continue
        break
    start = offset
    while offset < len(data) and data[offset] not in b" \t\r\n#":
        offset += 1
    if start == offset:
        raise ValueError("missing PPM header token")
    return data[start:offset], offset


def _read_ppm(path: Path, label: str) -> tuple[int, int, bytes]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise FrontierRuntimeContractError(
            f"{label} framebuffer could not be read: {error}"
        ) from error

    try:
        magic, offset = _ppm_token(data, 0)
        width_token, offset = _ppm_token(data, offset)
        height_token, offset = _ppm_token(data, offset)
        maximum_token, offset = _ppm_token(data, offset)
        if magic != b"P6":
            raise ValueError("expected binary P6 PPM")
        width = int(width_token)
        height = int(height_token)
        maximum = int(maximum_token)
        if width < 1 or height < 1:
            raise ValueError("PPM dimensions must be positive")
        if maximum != 255:
            raise ValueError("PPM maximum channel value must be 255")
        if offset >= len(data) or data[offset] not in b" \t\r\n":
            raise ValueError("PPM header is not separated from pixels")
        if data[offset:offset + 2] == b"\r\n":
            offset += 2
        else:
            offset += 1
        pixels = data[offset:]
        expected = width * height * 3
        if len(pixels) != expected:
            raise ValueError(
                f"PPM has {len(pixels)} pixel bytes; expected {expected}"
            )
    except (ValueError, OverflowError) as error:
        raise FrontierRuntimeContractError(
            f"{label} framebuffer is not a valid P6 screendump: {error}"
        ) from error
    return width, height, pixels


def _validate_framebuffer_pixels(label: str, pixels: bytes) -> None:
    if not any(pixels):
        raise FrontierRuntimeContractError(
            f"{label} framebuffer is black"
        )
    colors = {
        pixels[offset:offset + 3]
        for offset in range(0, len(pixels), 3)
    }
    if len(colors) < 2:
        raise FrontierRuntimeContractError(
            f"{label} framebuffer is uniform"
        )


def validate_framebuffer_change(
    before_path: Path,
    after_path: Path,
) -> FramebufferEvidence:
    """Require two visible, nonuniform frames with meaningful pixel changes."""
    before_width, before_height, before = _read_ppm(
        before_path,
        "before",
    )
    after_width, after_height, after = _read_ppm(after_path, "after")
    if (before_width, before_height) != (after_width, after_height):
        raise FrontierRuntimeContractError(
            "framebuffer dimensions changed from "
            f"{before_width}x{before_height} to "
            f"{after_width}x{after_height}"
        )
    _validate_framebuffer_pixels("before", before)
    _validate_framebuffer_pixels("after", after)
    changed_pixels = sum(
        before[offset:offset + 3] != after[offset:offset + 3]
        for offset in range(0, len(before), 3)
    )
    if changed_pixels < MIN_CHANGED_PIXELS:
        raise FrontierRuntimeContractError(
            "framebuffer changed by only "
            f"{changed_pixels} pixel(s) after mouse input; "
            f"expected at least {MIN_CHANGED_PIXELS}"
        )
    return FramebufferEvidence(
        width=before_width,
        height=before_height,
        changed_pixels=changed_pixels,
    )


def _hmp_path(path: Path) -> str:
    rendered = path.resolve().as_posix()
    return '"' + rendered.replace("\\", "\\\\").replace('"', '\\"') + '"'


def capture_screendump(
    monitor: socket.socket,
    output: Path,
    timeout: float = 5.0,
) -> None:
    """Ask QEMU for a PPM frame and wait until it has bytes."""
    try:
        output.unlink()
    except FileNotFoundError:
        pass
    hmp(monitor, f"screendump {_hmp_path(output)}")
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if output.stat().st_size > 0:
                return
        except FileNotFoundError:
            pass
        time.sleep(0.05)
    raise FrontierRuntimeContractError(
        f"QEMU did not write screendump: {output}"
    )


def _pcm_peak(samples: bytes, sample_width: int) -> int:
    if sample_width == 1:
        return max((abs(sample - 128) for sample in samples), default=0)
    if sample_width not in (2, 3, 4):
        raise ValueError(f"unsupported PCM sample width: {sample_width}")
    peak = 0
    for offset in range(0, len(samples), sample_width):
        sample = int.from_bytes(
            samples[offset:offset + sample_width],
            "little",
            signed=True,
        )
        peak = max(peak, abs(sample))
    return peak


def validate_wav_audio(path: Path) -> AudioEvidence:
    """Require a readable PCM WAV with at least one nonzero sample.

    QEMU's WAV backend can leave both length fields at their initial zero
    values after a clean monitor quit. Accept that exact canonical header and
    derive its closed-file lengths in memory before asking ``wave`` to parse
    it. Other malformed headers remain errors.
    """
    try:
        contents = path.read_bytes()
        if (
            len(contents) >= 44
            and contents[0:4] == b"RIFF"
            and contents[4:8] == b"\x00\x00\x00\x00"
            and contents[8:12] == b"WAVE"
            and contents[12:16] == b"fmt "
            and contents[16:20] == b"\x10\x00\x00\x00"
            and contents[20:22] == b"\x01\x00"
            and contents[36:40] == b"data"
            and contents[40:44] == b"\x00\x00\x00\x00"
        ):
            block_align = int.from_bytes(contents[32:34], "little")
            data_length = len(contents) - 44
            if block_align < 1 or data_length % block_align != 0:
                raise ValueError(
                    "QEMU placeholder capture has misaligned PCM data"
                )
            if len(contents) - 8 > 0xFFFFFFFF:
                raise ValueError("QEMU placeholder capture exceeds RIFF limits")
            normalized = bytearray(contents)
            normalized[4:8] = (len(contents) - 8).to_bytes(4, "little")
            normalized[40:44] = data_length.to_bytes(4, "little")
            contents = bytes(normalized)

        with wave.open(io.BytesIO(contents), "rb") as capture:
            if capture.getcomptype() != "NONE":
                raise ValueError(
                    f"unsupported compression: {capture.getcomptype()}"
                )
            channels = capture.getnchannels()
            sample_rate = capture.getframerate()
            frames = capture.getnframes()
            sample_width = capture.getsampwidth()
            samples = capture.readframes(frames)
        if channels < 1:
            raise ValueError("channel count must be positive")
        if sample_rate < 1:
            raise ValueError("sample rate must be positive")
        if frames < 1:
            raise ValueError("capture has no frames")
        expected = frames * channels * sample_width
        if len(samples) != expected:
            raise ValueError(
                f"capture has {len(samples)} PCM bytes; expected {expected}"
            )
        peak = _pcm_peak(samples, sample_width)
    except (EOFError, OSError, ValueError, wave.Error) as error:
        raise FrontierRuntimeContractError(
            f"audio capture is not a readable PCM WAV: {error}"
        ) from error
    if peak == 0:
        raise FrontierRuntimeContractError("audio capture is silent")
    return AudioEvidence(
        channels=channels,
        sample_rate=sample_rate,
        frames=frames,
        peak=peak,
    )


def qemu_supports_wav_audio(qemu: str) -> bool:
    """Ask QEMU whether its build includes the WAV audio backend."""
    try:
        result = subprocess.run(
            [qemu, "-audiodev", "help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=5.0,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return re.search(r"(?m)^wav\r?$", result.stdout or "") is not None


def completion_pattern(success_pattern: str) -> re.Pattern[str]:
    """Return the first-event pattern for either success or a kernel panic."""
    re.compile(success_pattern, re.S | re.M)
    return re.compile(
        rf"(?:{PANIC_RE.pattern})|(?:{success_pattern})",
        re.S | re.M,
    )


def success_count(data: str, success_pattern: str) -> int:
    """Count completed command markers without depending on capture groups."""
    return sum(1 for _ in re.finditer(success_pattern, data, re.S | re.M))


def positive_count(text: str) -> int:
    """Parse a positive repeat count for argparse and direct callers."""
    value = int(text)
    if value < 1:
        raise ValueError("repeat count must be positive")
    return value


def positive_delay(text: str) -> float:
    """Parse a finite positive delay in seconds."""
    value = float(text)
    if not 0.0 < value < float("inf"):
        raise ValueError("delay must be finite and positive")
    return value


def free_tcp_port() -> int:
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])
    finally:
        s.close()


def read_log(path: Path) -> str:
    for attempt in range(2):
        try:
            return path.read_bytes().decode(errors="replace")
        except FileNotFoundError:
            return ""
        except MemoryError:
            if attempt == 0:
                gc.collect()
                continue
            try:
                size = path.stat().st_size
            except OSError:
                size = -1
            raise FrontierRuntimeContractError(
                f"serial log allocation failed for {path} ({size} bytes)"
            ) from None
    raise AssertionError("unreachable serial-log retry state")


def qemu_exit_diagnostic(proc: subprocess.Popen, capture) -> str:
    """Return bounded QEMU output when the child has already exited."""
    status = proc.poll()
    if status is None:
        return ""
    try:
        capture.flush()
        capture.seek(0, os.SEEK_END)
        size = capture.tell()
        capture.seek(max(0, size - 4000))
        raw = capture.read()
    except (OSError, ValueError):
        raw = b""
    if isinstance(raw, bytes):
        message = raw.decode("utf-8", errors="replace").strip()
    else:
        message = str(raw).strip()
    detail = f"QEMU exited with status {status}"
    if message:
        detail += f": {message}"
    return detail


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


def wait_log_after(
    proc: subprocess.Popen,
    log: Path,
    pattern: str,
    start_offset: int,
    timeout: float,
) -> tuple[bool, str]:
    """Wait for a marker written after a known log position."""
    deadline = time.time() + timeout
    compiled = re.compile(pattern, re.S)
    while time.time() < deadline:
        data = read_log(log)
        recent = data[start_offset:]
        if frontier_failure_marker(recent) is not None:
            return False, data
        if compiled.search(recent):
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


def hmp(
    sock: socket.socket,
    command: str,
    pause: float = 0.25,
    *,
    expect_prompt: bool = True,
) -> str:
    sock.sendall((command + "\n").encode())
    time.sleep(pause)
    response_bytes = bytearray()
    prompt_seen = False
    while len(response_bytes) < 1024 * 1024:
        try:
            chunk = sock.recv(4096)
        except OSError as error:
            if expect_prompt:
                raise FrontierRuntimeContractError(
                    f"QEMU monitor did not finish {command!r}: {error}"
                ) from error
            break
        if not chunk:
            if expect_prompt:
                raise FrontierRuntimeContractError(
                    f"QEMU monitor closed while running {command!r}"
                )
            break
        response_bytes.extend(chunk)
        if b"(qemu)" in response_bytes:
            prompt_seen = True
            break
    if expect_prompt and not prompt_seen:
        raise FrontierRuntimeContractError(
            f"QEMU monitor response exceeded 1048576 bytes for {command!r}"
        )
    response = response_bytes.decode("utf-8", errors="replace")
    failure = re.search(
        r"(?i)(?:error:|unknown command\b)[^\r\n]*",
        response,
    )
    if failure is not None:
        raise FrontierRuntimeContractError(
            f"QEMU monitor rejected {command!r}: {failure.group(0)}"
        )
    return response


def wait_hmp_device_deleted(
    sock: socket.socket,
    device_id: str,
    timeout: float,
) -> None:
    """Wait until QEMU's object tree no longer owns a deleted device."""
    device_pattern = re.compile(
        rf'dev: [^,\r\n]+, id "{re.escape(device_id)}"'
    )
    deadline = time.time() + timeout
    while True:
        response = hmp(sock, "info qtree", pause=0.05)
        if device_pattern.search(response) is None:
            return
        if time.time() >= deadline:
            raise FrontierRuntimeContractError(
                f"QEMU did not delete device {device_id!r} before timeout"
            )
        time.sleep(0.05)


def inject_mouse_activity(sock: socket.socket) -> None:
    """Exercise relative motion, the primary button, and both wheel lanes."""
    hmp(sock, "mouse_move 32 24")
    hmp(sock, "mouse_button 1")
    hmp(sock, "mouse_button 0")
    hmp(sock, "mouse_move -16 8 1")
    hmp(sock, "mouse_move 0 0 -1")


def stop_qemu(proc: subprocess.Popen, mon: socket.socket | None) -> None:
    """Request a disk-flushing QEMU exit before using hard-stop fallbacks."""
    graceful = mon is not None
    if mon is not None:
        try:
            hmp(mon, "quit", expect_prompt=False)
        except (OSError, FrontierRuntimeContractError):
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
    if "A" <= ch <= "Z":
        return "shift-" + ch.lower()
    source_keys = {
        "(": "shift-9", ")": "shift-0",
        "[": "bracket_left", "]": "bracket_right",
        "{": "shift-bracket_left", "}": "shift-bracket_right",
        ";": "semicolon", ":": "shift-semicolon",
        ",": "comma", "=": "equal", "%": "shift-5",
        "&": "shift-7", "*": "shift-8",
        "\"": "shift-apostrophe", "'": "apostrophe", "\\": "backslash",
    }
    if ch in source_keys:
        return source_keys[ch]
    if ch == " ":
        return "spc"
    if ch == "/":
        return "slash"
    if ch == ".":
        return "dot"
    if ch == "-":
        return "minus"
    if ch == "_":
        return "shift-minus"
    if ch == "+":
        return "shift-equal"
    if ch == "~":
        return "shift-grave_accent"
    raise ValueError(f"unsupported smoke-test character: {ch!r}")


def run_terminal_command(
    proc: subprocess.Popen,
    mon: socket.socket,
    log: Path,
    command: str,
    success_pattern: str,
    timeout: float,
    key_pause: float,
    *,
    require_completion: bool = False,
) -> tuple[bool, str]:
    """Type one command and require one new matching serial event."""
    re.compile(success_pattern, re.S | re.M)
    keys = [key_name(ch) for ch in command]
    before = read_log(log)
    completed = success_count(before, success_pattern)
    ready_pattern = r"^\[terminal\] command complete\r?$"
    ready_count = success_count(before, ready_pattern)
    for key in keys:
        send_key(mon, key, key_pause)
    send_key(mon, "ret", key_pause)

    deadline = time.monotonic() + timeout
    ok, data = wait_log_success_count(
        proc,
        log,
        success_pattern,
        completed + 1,
        timeout,
    )
    if PANIC_RE.search(data):
        return False, data
    if ok and require_completion and success_count(data, ready_pattern) <= ready_count:
        ok, data = wait_log_success_count(
            proc, log, ready_pattern, ready_count + 1,
            max(0.0, deadline - time.monotonic()),
        )
    if PANIC_RE.search(data):
        return False, data
    return ok and success_count(data, success_pattern) >= completed + 1, data


def frontier_failure_marker(data: str) -> str | None:
    """Return the first known frontier failure found in serial output."""
    folded = data.casefold()
    for marker in contracts.FRONTIER_RUNTIME_REJECTED_MARKERS:
        if marker.casefold() in folded:
            return marker
    return None


def settle_panic_serial(
    proc: subprocess.Popen,
    log: Path,
    data: str,
    timeout: float = PANIC_REASON_GRACE_SECONDS,
) -> str:
    """Give the panic formatter time to publish its serial reason."""
    if "KERNEL PANIC" not in data or PANIC_REASON_RE.search(data):
        return data

    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            break
        time.sleep(0.05)
        data = read_log(log)
        if PANIC_REASON_RE.search(data):
            break
    return read_log(log)


def _blank_frontier_spans(
    data: str,
    spans: list[tuple[int, int]],
) -> str:
    """Hide checked failures without joining neighboring serial lines."""
    characters = list(data)
    for start, end in spans:
        for index in range(start, end):
            if characters[index] not in "\r\n":
                characters[index] = " "
    return "".join(characters)


def _allowed_failure_matches(
    data: str,
    command: contracts.TerminalCommand,
) -> list[re.Match[str]]:
    pattern = command.allowed_failure_pattern
    if pattern is None:
        return []
    matches = list(re.finditer(pattern, data, re.I | re.M))
    if len(matches) > 1:
        raise FrontierRuntimeContractError(
            "allowed failure must appear exactly once for frontier "
            f"command {command.text!r}; found {len(matches)} copies"
        )
    return matches


def _trailing_allowed_failure_prefix(
    data: str,
    command: contracts.TerminalCommand,
) -> tuple[int, int] | None:
    literal = command.allowed_failure_literal
    if literal is None:
        return None
    best_length = 0
    for complete_text in (literal + "\n", literal + "\r\n"):
        for length in range(1, len(complete_text)):
            start = len(data) - length
            line_boundary = start == 0 or data[start - 1] in "\r\n"
            if (
                length > best_length
                and line_boundary
                and data.endswith(complete_text[:length])
            ):
                best_length = length
    if best_length == 0:
        return None
    return len(data) - best_length, len(data)


def _require_allowed_failure_context(
    data: str,
    command: contracts.TerminalCommand,
    failure_start: int,
) -> None:
    context_pattern = command.allowed_failure_context_pattern
    if context_pattern is None:
        raise FrontierRuntimeContractError(
            f"frontier command {command.text!r} allows a failure without "
            "declaring its serial context"
        )
    contexts = list(re.finditer(context_pattern, data, re.I | re.M))
    if len(contexts) != 1 or contexts[0].end() > failure_start:
        raise FrontierRuntimeContractError(
            "allowed failure appeared outside frontier command "
            f"{command.text!r}; its compile context must appear exactly "
            "once and before the diagnostic"
        )


def mask_frontier_command_failures(
    data: str,
    command: contracts.TerminalCommand,
) -> str:
    """Scope an in-flight command's one expected failure to its context."""
    matches = _allowed_failure_matches(data, command)
    if matches:
        _require_allowed_failure_context(
            data,
            command,
            matches[0].start(),
        )
        spans = [(matches[0].start(), matches[0].end())]
    else:
        partial_span = _trailing_allowed_failure_prefix(data, command)
        if partial_span is None:
            return data
        _require_allowed_failure_context(
            data,
            command,
            partial_span[0],
        )
        spans = [partial_span]

    return _blank_frontier_spans(data, spans)


def mask_completed_frontier_command_failures(data: str) -> str:
    """Hide only failures inside one fully proved frontier command."""
    spans: list[tuple[int, int]] = []
    for command in contracts.FRONTIER_RUNTIME_COMMANDS:
        if command.allowed_failure_pattern is None:
            continue
        matches = _allowed_failure_matches(data, command)
        if not matches:
            continue

        context_pattern = command.allowed_failure_context_pattern
        contexts = (
            []
            if context_pattern is None
            else list(re.finditer(context_pattern, data, re.I | re.M))
        )
        completed = list(
            re.finditer(command.expected_pattern, data, re.S | re.M)
        )
        if len(contexts) != 1 or len(completed) != 1:
            raise FrontierRuntimeContractError(
                "allowed failure appeared outside the completed frontier "
                f"command {command.text!r}"
            )

        command_match = completed[0]
        for match in matches:
            if not (
                command_match.start() <= match.start()
                and match.end() <= command_match.end()
            ):
                raise FrontierRuntimeContractError(
                    "allowed failure appeared outside the completed "
                    f"frontier command {command.text!r}"
                )
            spans.append((match.start(), match.end()))

    return _blank_frontier_spans(data, spans)


def wait_frontier_command(
    proc: subprocess.Popen,
    log: Path,
    command: contracts.TerminalCommand,
    start_offset: int,
    timeout: float,
) -> tuple[int, str]:
    """Wait for one command's marker after the preceding command."""
    deadline = time.time() + timeout
    compiled = re.compile(command.expected_pattern, re.S | re.M)
    while time.time() < deadline:
        data = read_log(log)
        suffix = data[start_offset:]
        checked_suffix = mask_frontier_command_failures(
            suffix,
            command,
        )
        failure = frontier_failure_marker(checked_suffix)
        if failure is not None:
            detail = ""
            if failure == "KERNEL PANIC":
                data = settle_panic_serial(proc, log, data)
                reason = PANIC_REASON_RE.search(data[start_offset:])
                if reason is not None:
                    detail = f"; {reason.group(0)}"
            raise FrontierRuntimeContractError(
                f"frontier command {command.text!r} saw failure marker: "
                f"{failure}{detail}"
            )

        matched = compiled.search(suffix)
        if matched is not None:
            return start_offset + matched.end(), data

        status = proc.poll()
        if status is not None:
            raise FrontierRuntimeContractError(
                f"frontier command {command.text!r} did not reach "
                f"{command.expected_pattern!r}; QEMU exited with status "
                f"{status}"
            )
        time.sleep(0.1)

    raise FrontierRuntimeContractError(
        f"frontier command {command.text!r} timed out waiting for "
        f"{command.expected_pattern!r}"
    )


def resolve_frontier_command(
    command: contracts.TerminalCommand,
    captures: dict[str, str],
) -> contracts.TerminalCommand:
    """Resolve one captured process ID into a later command and marker."""
    if command.pid_from_capture is None:
        return command
    pid = captures.get(command.pid_from_capture)
    if pid is None:
        raise FrontierRuntimeContractError(
            f"frontier command {command.text!r} needs missing capture "
            f"{command.pid_from_capture!r}"
        )
    return contracts.TerminalCommand(
        text=command.text.replace("{pid}", pid),
        expected_pattern=command.expected_pattern.replace(
            "{pid}", re.escape(pid)
        ),
        followup_keys=command.followup_keys,
        interaction_pattern=command.interaction_pattern,
        followup_settle_seconds=command.followup_settle_seconds,
        allowed_failure_pattern=command.allowed_failure_pattern,
        allowed_failure_literal=command.allowed_failure_literal,
        allowed_failure_context_pattern=(
            command.allowed_failure_context_pattern
        ),
        timeout_seconds=command.timeout_seconds,
        capture_name=command.capture_name,
        pid_from_capture=None,
    )


def run_frontier_commands(
    proc: subprocess.Popen,
    monitor: socket.socket,
    log: Path,
    start_offset: int,
    timeout: float,
    key_pause: float,
) -> str:
    """Run the fixed acceptance sequence, waiting after every command."""
    cursor = start_offset
    data = read_log(log)
    captures: dict[str, str] = {}
    for command_index, unresolved_command in enumerate(contracts.FRONTIER_RUNTIME_COMMANDS):
        command = resolve_frontier_command(unresolved_command, captures)
        command_start = cursor
        command_timeout = (
            command.timeout_seconds
            if command.timeout_seconds is not None
            else timeout
        )
        for ch in command.text:
            send_key(monitor, key_name(ch), key_pause)
        send_key(monitor, "ret", key_pause)
        if command.followup_keys:
            if command.interaction_pattern is None:
                raise FrontierRuntimeContractError(
                    f"frontier command {command.text!r} has follow-up "
                    "keys without an interaction marker"
                )
            wait_frontier_command(
                proc,
                log,
                contracts.TerminalCommand(
                    command.text,
                    command.interaction_pattern,
                ),
                cursor,
                command_timeout,
            )
            if command.followup_settle_seconds > 0.0:
                time.sleep(command.followup_settle_seconds)
            for key in command.followup_keys:
                send_key(
                    monitor,
                    key,
                    max(key_pause, KEY_PAUSE_SECONDS),
                )
                time.sleep(0.75)
        cursor, data = wait_frontier_command(
            proc,
            log,
            command,
            cursor,
            command_timeout,
        )
        if command.capture_name is not None:
            matched = re.compile(command.expected_pattern, re.S | re.M).search(
                data[command_start:cursor]
            )
            if matched is None or "pid" not in matched.groupdict():
                raise FrontierRuntimeContractError(
                    f"frontier command {command.text!r} did not publish a "
                    "named pid capture"
                )
            captures[command.capture_name] = matched.group("pid")
        if command_index + 1 < len(contracts.FRONTIER_RUNTIME_COMMANDS):
            time.sleep(max(COMMAND_SETTLE_SECONDS, key_pause * 2.0))
    return data


def require_frontier_log_after(
    proc: subprocess.Popen,
    log: Path,
    pattern: str,
    start_offset: int,
    timeout: float,
    step: str,
) -> str:
    """Require fresh guest evidence for one frontier runtime step."""
    found, data = wait_log_after(
        proc,
        log,
        pattern,
        start_offset,
        timeout,
    )
    recent = data[start_offset:]
    failure = frontier_failure_marker(recent)
    if failure is not None:
        raise FrontierRuntimeContractError(
            f"{step} saw failure marker: {failure}"
        )
    if found:
        return data

    status = proc.poll()
    if status is not None:
        raise FrontierRuntimeContractError(
            f"{step} did not reach its guest marker; "
            f"QEMU exited with status {status}"
        )
    raise FrontierRuntimeContractError(
        f"{step} timed out waiting for fresh guest evidence"
    )


def run_frontier_usb_replug_contract(
    proc: subprocess.Popen,
    monitor: socket.socket,
    log: Path,
    timeout: float,
    key_pause: float,
) -> str:
    """Prove HID recovery and six EHCI storage lifetimes in one boot."""
    initial_data = read_log(log)
    checked_initial_data = mask_completed_frontier_command_failures(
        initial_data
    )
    initial_failure = frontier_failure_marker(
        checked_initial_data,
    )
    if initial_failure is not None:
        raise FrontierRuntimeContractError(
            "initial USB storage check saw failure marker: "
            f"{initial_failure}"
        )
    if re.search(contracts.FRONTIER_STORAGE_READY_PATTERN, initial_data, re.S) is None:
        raise FrontierRuntimeContractError(
            "initial USB storage did not reach its FAT16 marker"
        )

    keyboard_detach_offset = len(read_log(log))
    hmp(monitor, "device_del frontier_keyboard")
    require_frontier_log_after(
        proc,
        log,
        (
            r"usb_hid: keyboard detached"
            r".*usb: removed device addr=[1-9][0-9]*"
        ),
        keyboard_detach_offset,
        timeout,
        "USB keyboard detach",
    )
    wait_hmp_device_deleted(
        monitor,
        "frontier_keyboard",
        min(timeout, 10.0),
    )

    keyboard_attach_offset = len(read_log(log))
    hmp(
        monitor,
        (
            "device_add usb-kbd,id=frontier_keyboard_replug,"
            "bus=frontier_uhci.0,port=1"
        ),
    )
    require_frontier_log_after(
        proc,
        log,
        (
            r"usb: dev addr=[1-9][0-9]* "
            r".*class=(?:3|0[xX]0*3)"
            r".*usb_hid: keyboard attached addr=[1-9][0-9]*"
        ),
        keyboard_attach_offset,
        timeout,
        "USB keyboard reattach",
    )

    command = contracts.FRONTIER_RUNTIME_COMMANDS[0]
    command_offset = len(read_log(log))
    for ch in command.text:
        send_key(monitor, key_name(ch), key_pause)
    send_key(monitor, "ret", key_pause)
    _, data = wait_frontier_command(
        proc,
        log,
        command,
        command_offset,
        timeout,
    )

    mouse_detach_offset = len(read_log(log))
    hmp(monitor, "device_del frontier_mouse")
    require_frontier_log_after(
        proc,
        log,
        (
            r"usb_hid: mouse detached"
            r".*usb: removed device addr=[1-9][0-9]*"
        ),
        mouse_detach_offset,
        timeout,
        "USB mouse detach",
    )
    wait_hmp_device_deleted(
        monitor,
        "frontier_mouse",
        min(timeout, 10.0),
    )

    mouse_attach_offset = len(read_log(log))
    hmp(
        monitor,
        (
            "device_add usb-mouse,id=frontier_mouse_replug,"
            "bus=frontier_uhci.0,port=2"
        ),
    )
    require_frontier_log_after(
        proc,
        log,
        (
            r"usb: dev addr=[1-9][0-9]* "
            r".*class=(?:3|0[xX]0*3)"
            r".*usb_hid: mouse attached addr=[1-9][0-9]*"
        ),
        mouse_attach_offset,
        timeout,
        "USB mouse reattach",
    )

    mouse_activity_offset = len(read_log(log))
    inject_mouse_activity(monitor)
    data = require_frontier_log_after(
        proc,
        log,
        r"usb_hid: mouse activity report=[1-9][0-9]*",
        mouse_activity_offset,
        timeout,
        "USB mouse input after reattach",
    )

    storage_id = "frontier_mass_storage"
    for cycle in range(1, contracts.FRONTIER_STORAGE_REATTACHMENTS + 1):
        detach_offset = len(read_log(log))
        hmp(monitor, f"device_del {storage_id}")
        require_frontier_log_after(
            proc,
            log,
            (
                r"usb_msc: detached"
                r".*usb: removed device addr=[1-9][0-9]*"
            ),
            detach_offset,
            timeout,
            f"USB storage detach {cycle}",
        )
        wait_hmp_device_deleted(
            monitor,
            storage_id,
            min(timeout, 10.0),
        )

        storage_id = f"frontier_mass_storage_replug_{cycle}"
        attach_offset = len(read_log(log))
        hmp(
            monitor,
            (
                f"device_add usb-storage,id={storage_id},"
                "bus=frontier_ehci.0,port=1,"
                "drive=frontier_usb_storage"
            ),
        )
        data = require_frontier_log_after(
            proc,
            log,
            contracts.FRONTIER_STORAGE_READY_PATTERN,
            attach_offset,
            timeout,
            f"USB storage reattach {cycle}",
        )
    return data


def qemu_args(
    args: argparse.Namespace,
    monitor_port: int,
    ac97_audio_path: Path | None = None,
    pcspk_audio_path: Path | None = None,
) -> list[str]:
    netdev = "user,id=n0"
    frontier_runtime = (
        getattr(args, "verify_frontier_runtime", False) is True
    )
    command = [args.qemu]
    if args.cpu is not None:
        command.extend(("-cpu", args.cpu))
    if (ac97_audio_path is None) != (pcspk_audio_path is None):
        raise ValueError(
            "AC97 and PC-speaker capture paths must be provided together"
        )
    if ac97_audio_path is None:
        command.extend(("-audiodev", "none,id=shared_audio"))
        ac97_audio_id = "shared_audio"
        pcspk_audio_id = "shared_audio"
    else:
        command.extend(
            (
                "-audiodev",
                f"wav,id=ac97_capture,path={ac97_audio_path}",
                "-audiodev",
                f"wav,id=pcspk_capture,path={pcspk_audio_path}",
            )
        )
        ac97_audio_id = "ac97_capture"
        pcspk_audio_id = "pcspk_capture"

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
        "-machine",
        (
            f"pcspk-audiodev={pcspk_audio_id},i8042=off"
            if frontier_runtime
            else f"pcspk-audiodev={pcspk_audio_id}"
        ),
        "-device",
        f"AC97,audiodev={ac97_audio_id}",
    ])
    if frontier_runtime:
        command.extend([
            "-device",
            "piix3-usb-uhci,id=frontier_uhci",
            "-device",
            "usb-ehci,id=frontier_ehci",
            "-device",
            "usb-kbd,id=frontier_keyboard,bus=frontier_uhci.0,port=1",
            "-device",
            "usb-mouse,id=frontier_mouse,bus=frontier_uhci.0,port=2",
            "-blockdev",
            (
                f"driver=file,filename={args.usb_image},"
                "node-name=frontier_usb_file"
            ),
            "-blockdev",
            (
                "driver=raw,file=frontier_usb_file,"
                "node-name=frontier_usb_storage"
            ),
            "-device",
            (
                "usb-storage,id=frontier_mass_storage,"
                "bus=frontier_ehci.0,port=1,"
                "drive=frontier_usb_storage"
            ),
        ])
    else:
        command.extend([
            "-device",
            "piix3-usb-uhci",
            "-device",
            "usb-ehci",
            "-device",
            "usb-kbd",
            "-device",
            "usb-mouse",
        ])
    command.extend([
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


def validate_frontier_usb_image(path: Path) -> None:
    """Require a partitioned FAT16 image before QEMU can attach it."""
    try:
        with path.open("rb") as image:
            mbr = image.read(512)
            image_size = path.stat().st_size
    except OSError as error:
        raise FrontierRuntimeContractError(
            f"USB image could not be read: {path}: {error}"
        ) from error
    if len(mbr) != 512:
        raise FrontierRuntimeContractError(
            f"USB image is shorter than one sector: {path}"
        )
    if mbr[510:512] != b"\x55\xaa":
        raise FrontierRuntimeContractError(
            f"USB image has no MBR signature: {path}"
        )
    fat16_partition = False
    invalid_partition = ""
    for partition in range(4):
        entry = 0x1BE + partition * 16
        partition_type = mbr[entry + 4]
        first_lba = int.from_bytes(mbr[entry + 8:entry + 12], "little")
        sectors = int.from_bytes(mbr[entry + 12:entry + 16], "little")
        if (
            partition_type in (0x04, 0x06, 0x0E)
            and first_lba > 0
            and sectors > 0
        ):
            partition_end = (first_lba + sectors) * 512
            if partition_end > image_size:
                invalid_partition = (
                    "FAT16 partition exceeds the USB image"
                )
                continue
            try:
                with path.open("rb") as image:
                    image.seek(first_lba * 512)
                    boot = image.read(512)
            except OSError as error:
                raise FrontierRuntimeContractError(
                    f"USB FAT16 boot sector could not be read: {error}"
                ) from error

            bytes_per_sector = int.from_bytes(boot[11:13], "little")
            sectors_per_cluster = boot[13] if len(boot) > 13 else 0
            reserved_sectors = int.from_bytes(boot[14:16], "little")
            fat_count = boot[16] if len(boot) > 16 else 0
            root_entries = int.from_bytes(boot[17:19], "little")
            total_sectors = int.from_bytes(boot[19:21], "little")
            if total_sectors == 0:
                total_sectors = int.from_bytes(boot[32:36], "little")
            sectors_per_fat = int.from_bytes(boot[22:24], "little")
            valid_cluster_size = (
                sectors_per_cluster > 0
                and sectors_per_cluster & (sectors_per_cluster - 1) == 0
            )
            root_dir_sectors = 0
            if bytes_per_sector > 0:
                root_dir_sectors = (
                    root_entries * 32 + bytes_per_sector - 1
                ) // bytes_per_sector
            overhead = (
                reserved_sectors
                + fat_count * sectors_per_fat
                + root_dir_sectors
            )
            data_clusters = 0
            if valid_cluster_size and total_sectors > overhead:
                data_clusters = (
                    total_sectors - overhead
                ) // sectors_per_cluster
            valid_boot = (
                len(boot) == 512
                and boot[510:512] == b"\x55\xaa"
                and bytes_per_sector == 512
                and valid_cluster_size
                and reserved_sectors > 0
                and fat_count > 0
                and root_entries > 0
                and sectors_per_fat > 0
                and 0 < total_sectors <= sectors
                and 4085 <= data_clusters < 65525
            )
            if valid_boot:
                fat16_partition = True
                break
            invalid_partition = (
                "FAT16 partition has no valid FAT16 boot sector"
            )
    if not fat16_partition:
        if invalid_partition:
            raise FrontierRuntimeContractError(invalid_partition)
        raise FrontierRuntimeContractError(
            f"USB image has no nonempty FAT16 partition: {path}"
        )


def _frontier_session(
    args: argparse.Namespace,
    before_frame: Path,
    after_frame: Path,
    ac97_audio_path: Path,
    pcspk_audio_path: Path,
) -> int:
    monitor_port = free_tcp_port()
    qemu_output = tempfile.TemporaryFile(mode="w+b")
    try:
        proc = subprocess.Popen(
            qemu_args(
                args,
                monitor_port,
                ac97_audio_path,
                pcspk_audio_path,
            ),
            cwd=REPO_ROOT,
            stdout=qemu_output,
            stderr=subprocess.STDOUT,
        )
    except BaseException:
        qemu_output.close()
        raise
    mon: socket.socket | None = None
    last_data = ""
    try:
        ok, last_data = wait_log(
            proc,
            args.log,
            r"Entering desktop environment",
            args.timeout,
        )
        if not ok:
            detail = qemu_exit_diagnostic(proc, qemu_output)
            raise FrontierRuntimeContractError(
                "GUI desktop did not boot before timeout"
                + (f"; {detail}" if detail else "")
            )

        mon = connect_monitor(monitor_port, 10.0)
        time.sleep(1.0)
        send_key(mon, "ctrl-alt-t", 0.8)
        ok, last_data = wait_log(
            proc,
            args.log,
            r"Terminal launched",
            10.0,
        )
        if not ok:
            raise FrontierRuntimeContractError(
                "Terminal did not launch from Ctrl+Alt+T"
            )

        time.sleep(0.5)
        start_offset = len(read_log(args.log))
        last_data = run_frontier_commands(
            proc,
            mon,
            args.log,
            start_offset=start_offset,
            timeout=args.timeout,
            key_pause=args.key_pause,
        )
        capture_screendump(mon, before_frame)
        mouse_activity_pattern = (
            r"usb_hid: mouse activity report=[1-9][0-9]*"
        )
        previous_mouse_reports = success_count(
            read_log(args.log),
            mouse_activity_pattern,
        )
        inject_mouse_activity(mon)
        mouse_ok, last_data = wait_log_success_count(
            proc,
            args.log,
            mouse_activity_pattern,
            previous_mouse_reports + 1,
            5.0,
        )
        if not mouse_ok:
            raise FrontierRuntimeContractError(
                "USB mouse did not deliver an activity report"
            )
        time.sleep(0.5)
        capture_screendump(mon, after_frame)

        last_data = run_frontier_usb_replug_contract(
            proc,
            mon,
            args.log,
            timeout=args.timeout,
            key_pause=args.key_pause,
        )

        panic, last_data = wait_log(
            proc,
            args.log,
            PANIC_RE.pattern,
            5.0,
        )
        if panic:
            raise FrontierRuntimeContractError("panic detected")
        if proc.poll() is not None:
            raise FrontierRuntimeContractError(
                "QEMU exited during the post-replug survival window "
                f"with status {proc.poll()}"
            )
        validate_frontier_runtime_log(last_data, args.nic)
        framebuffer = validate_framebuffer_change(
            before_frame,
            after_frame,
        )
        if args.verify_smp_runtime:
            try:
                validate_smp_runtime_log(last_data, args.nic)
            except SmpRuntimeContractError as error:
                raise FrontierRuntimeContractError(str(error)) from error

        stop_qemu(proc, mon)
        mon = None
        ac97_audio = validate_wav_audio(ac97_audio_path)
        pcspk_audio = validate_wav_audio(pcspk_audio_path)

        result = (
            "GUI terminal frontier runtime passed "
            f"(framebuffer={framebuffer.width}x{framebuffer.height}, "
            f"changed_pixels={framebuffer.changed_pixels}"
        )
        result += (
            f", ac97_audio={ac97_audio.channels}ch@"
            f"{ac97_audio.sample_rate}Hz, frames={ac97_audio.frames}, "
            f"peak={ac97_audio.peak}, "
            f"pcspk_audio={pcspk_audio.channels}ch@"
            f"{pcspk_audio.sample_rate}Hz, frames={pcspk_audio.frames}, "
            f"peak={pcspk_audio.peak}"
        )
        print(result + ")")
        return 0
    except FrontierRuntimeContractError as error:
        print(f"GUI terminal frontier runtime failed: {error}", file=sys.stderr)
        if last_data:
            print(last_data[-5000:], file=sys.stderr)
        return 1
    finally:
        stop_qemu(proc, mon)
        qemu_output.close()


def run_frontier_runtime(args: argparse.Namespace) -> int:
    """Run the destructive frontier commands against private disk copies."""
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupid-frontier-runtime-"
        ) as temporary:
            artifacts = Path(temporary)
            system_image = artifacts / "system.img"
            usb_image = artifacts / "usb.img"
            shutil.copy2(args.image, system_image)
            shutil.copy2(args.usb_image, usb_image)

            private_args = argparse.Namespace(**vars(args))
            private_args.image = system_image
            private_args.usb_image = usb_image
            before_frame = artifacts / "before.ppm"
            after_frame = artifacts / "after.ppm"
            ac97_audio_path = artifacts / "ac97.wav"
            pcspk_audio_path = artifacts / "pcspk.wav"
            return _frontier_session(
                private_args,
                before_frame,
                after_frame,
                ac97_audio_path,
                pcspk_audio_path,
            )
    except OSError as error:
        print(
            f"could not prepare private frontier images: {error}",
            file=sys.stderr,
        )
        return 2


def copy_terminal_image(
    args: argparse.Namespace,
    directory: Path,
) -> argparse.Namespace:
    """Copy a system image and return arguments that select the copy."""
    private_args = argparse.Namespace(**vars(args))
    private_args.image = directory / args.image.name
    shutil.copy2(args.image, private_args.image)
    return private_args


def run(args: argparse.Namespace) -> int:
    if not args.image.exists():
        print(f"image not found: {args.image}", file=sys.stderr)
        return 2
    if args.verify_frontier_runtime:
        try:
            validate_frontier_usb_image(args.usb_image)
        except FrontierRuntimeContractError as error:
            print(
                f"frontier runtime preflight failed: {error}",
                file=sys.stderr,
            )
            return 2
        if not qemu_supports_wav_audio(args.qemu):
            print(
                "frontier runtime preflight failed: QEMU does not "
                "advertise the WAV audio backend",
                file=sys.stderr,
            )
            return 2

    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        args.log.unlink()
    except FileNotFoundError:
        pass

    if args.verify_frontier_runtime:
        return run_frontier_runtime(args)

    for pattern in (
        args.success_pattern,
        *args.setup_success_pattern,
    ):
        re.compile(pattern, re.S)

    runtime_args = args
    private_directory: tempfile.TemporaryDirectory[str] | None = None
    if args.private_image:
        try:
            private_directory = tempfile.TemporaryDirectory(
                prefix="cupid-terminal-smoke-"
            )
            runtime_args = copy_terminal_image(
                args,
                Path(private_directory.name),
            )
        except OSError as error:
            if private_directory is not None:
                private_directory.cleanup()
            print(
                f"could not prepare private terminal image: {error}",
                file=sys.stderr,
            )
            return 2

    qemu_output = None
    proc = None
    mon: socket.socket | None = None
    try:
        qemu_output = tempfile.TemporaryFile(mode="w+b")
        monitor_port = free_tcp_port()
        proc = subprocess.Popen(
            qemu_args(runtime_args, monitor_port),
            cwd=REPO_ROOT,
            stdout=qemu_output,
            stderr=subprocess.STDOUT,
        )
        ok, data = wait_log(proc, args.log, r"Entering desktop environment", args.timeout)
        if not ok:
            reason = (
                "GUI desktop did not boot"
                if proc.poll() is not None
                else "GUI desktop did not boot before timeout"
            )
            print(reason, file=sys.stderr)
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
        for setup_index, (setup_command, setup_pattern) in enumerate(
            zip(args.setup_command, args.setup_success_pattern),
            start=1,
        ):
            ok, data = run_terminal_command(
                proc,
                mon,
                args.log,
                setup_command,
                setup_pattern,
                args.setup_timeout or args.timeout,
                args.key_pause,
                require_completion=args.require_command_completion,
            )
            if not ok:
                print(
                    "GUI terminal smoke failed: setup command did not "
                    f"complete ({setup_index}/{len(args.setup_command)})",
                    file=sys.stderr,
                )
                print(data[-5000:], file=sys.stderr)
                return 1

        for iteration in range(args.repeat):
            ok, data = run_terminal_command(
                proc,
                mon,
                args.log,
                args.command,
                args.success_pattern,
                args.timeout,
                args.key_pause,
                require_completion=args.require_command_completion,
            )
            if not ok:
                print(
                    "GUI terminal smoke failed: command did not complete "
                    f"({iteration + 1}/{args.repeat})",
                    file=sys.stderr,
                )
                print(data[-5000:], file=sys.stderr)
                return 1

        ok_after, data_after = wait_log(proc, args.log, PANIC_RE.pattern, 5.0)
        if ok_after:
            print("GUI terminal smoke failed: panic detected", file=sys.stderr)
            print(data_after[-5000:], file=sys.stderr)
            return 1
        if proc.poll() is not None:
            print(
                "GUI terminal smoke failed: QEMU exited during the "
                "post-command survival window",
                file=sys.stderr,
            )
            print(data_after[-5000:], file=sys.stderr)
            return 1
        if args.verify_smp_runtime:
            try:
                validate_smp_runtime_log(data_after, args.nic)
            except SmpRuntimeContractError as error:
                print(
                    f"GUI terminal smoke failed: {error}",
                    file=sys.stderr,
                )
                print(data_after[-5000:], file=sys.stderr)
                return 1

        print("GUI terminal smoke passed")
        return 0
    finally:
        if proc is not None:
            detail = qemu_exit_diagnostic(proc, qemu_output)
            if detail:
                print(detail, file=sys.stderr)
            stop_qemu(proc, mon)
        if qemu_output is not None:
            qemu_output.close()
        if private_directory is not None:
            private_directory.cleanup()


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
        "--require-command-completion",
        action="store_true",
        help="require a new terminal completion marker as well as command output",
    )
    parser.add_argument(
        "--setup-command",
        action="append",
        default=[],
        help="terminal command to run before the measured command",
    )
    parser.add_argument(
        "--setup-success-pattern",
        action="append",
        default=[],
        help="serial expression proving the matching setup command completed",
    )
    parser.add_argument(
        "--setup-timeout",
        type=positive_delay,
        help="seconds to wait for each setup command before failing",
    )
    parser.add_argument(
        "--private-image",
        action="store_true",
        help="run against a temporary copy of the system image",
    )
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
    parser.add_argument(
        "--verify-smp-runtime",
        action="store_true",
        help=(
            "require the four-vCPU SMP, RDRAND, TLS, e1000, desktop, "
            "and command runtime contract"
        ),
    )
    parser.add_argument(
        "--verify-frontier-runtime",
        action="store_true",
        help=(
            "run the fixed port-I/O cohort, USB replug, graphics, and "
            "audio acceptance contract"
        ),
    )
    parser.add_argument(
        "--usb-image",
        type=Path,
        default=REPO_ROOT / "test_usb_partitioned.img",
        help="FAT16 USB image attached to EHCI by the frontier contract",
    )
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args(argv)
    if len(args.setup_command) != len(args.setup_success_pattern):
        parser.error(
            "each --setup-command needs one --setup-success-pattern"
        )
    return args


def main(argv: list[str]) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
