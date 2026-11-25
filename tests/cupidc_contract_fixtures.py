## @file cupidc_contract_fixtures.py
#  @brief Provide ELF images and execution fixtures for native contract checks.

from __future__ import annotations

import struct
import subprocess
import threading
from pathlib import Path

from tools import cupidc_toolchain_contracts


EXPECTED_CONTRACTS = {
    "core",
    "cupidasm",
    "cupidasm-demos",
    "cupidasm-kernel-elf",
    "cupidc-frontend",
    "cupidc-ir",
    "cupidc-object",
    "cupidc-pp",
    "cupidc-type",
    "cupiddis",
    "cupidld",
    "cupidobj",
    "elf32",
    "x86",
    "user-syscall-abi",
}


def _test_relocatable_elf32() -> bytes:
    image = bytearray(52)
    image[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", image, 16, 1, 3, 1)
    return bytes(image)


def _test_static_elf32() -> bytes:
    image = bytearray(85)
    identity = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<16sHHIIIIIHHHHHH",
        image,
        0,
        identity,
        2,
        3,
        1,
        cupidc_toolchain_contracts.TARGET_ENTRY,
        52,
        0,
        0,
        52,
        32,
        1,
        0,
        0,
        0,
    )
    struct.pack_into(
        "<IIIIIIII",
        image,
        52,
        1,
        84,
        cupidc_toolchain_contracts.TARGET_ENTRY,
        cupidc_toolchain_contracts.TARGET_ENTRY,
        1,
        1,
        5,
        1,
    )
    image[84] = 0xC3
    return bytes(image)


class _ContractStageRunner:
    def __init__(
        self,
        source_root: Path,
        *,
        hold_normal_compiles: bool = False,
        timeout_source: str | None = None,
    ) -> None:
        self.source_root = source_root
        self.hold_normal_compiles = hold_normal_compiles
        self.timeout_source = timeout_source
        self.contract_compile_timeouts: dict[str, int] = {}
        self.normal_cohort_ready = threading.Event()
        self.release_normal_cohort = threading.Event()
        self.heavy_compile_overlapped = False
        self.heavy_started_after_normal_cohort = False
        self.completed_normal_compiles: set[str] = set()
        self.max_active_normal_compiles = 0
        self.max_active_links = 0
        self._active_normal_compiles = 0
        self._active_links = 0
        self._link_pair_ready = threading.Event()
        self._lock = threading.Lock()

    def _output_path(self, argument: str | Path) -> Path:
        if isinstance(argument, Path):
            return argument
        return self.source_root / argument.removeprefix("/")

    def run(
        self,
        executable: Path,
        arguments: tuple[str | Path, ...],
        timeout: int,
    ) -> subprocess.CompletedProcess[str]:
        del executable
        arguments = tuple(arguments)
        output = self._output_path(arguments[arguments.index("-o") + 1])
        output.parent.mkdir(parents=True, exist_ok=True)
        if "-c" in arguments:
            logical_source = str(arguments[arguments.index("-c") + 1])
            logical_source = logical_source.removeprefix("/")
            contract_sources = {
                plan.source
                for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
            }
            is_contract = logical_source in contract_sources
            is_heavy = logical_source == (
                "toolchain/tests/cupidc_object_contract.cc"
            )
            if is_contract:
                with self._lock:
                    self.contract_compile_timeouts[logical_source] = timeout
                    if is_heavy:
                        self.heavy_compile_overlapped = (
                            self._active_normal_compiles > 0
                        )
                        self.heavy_started_after_normal_cohort = (
                            self.completed_normal_compiles
                            == contract_sources - {logical_source}
                        )
                    elif self.hold_normal_compiles:
                        self._active_normal_compiles += 1
                        self.max_active_normal_compiles = max(
                            self.max_active_normal_compiles,
                            self._active_normal_compiles,
                        )
                        if self._active_normal_compiles >= 8:
                            self.normal_cohort_ready.set()
                if (
                    self.hold_normal_compiles
                    and not is_heavy
                    and not self.release_normal_cohort.wait(5)
                ):
                    raise AssertionError(
                        "normal contract compiles were not released"
                    )
                if self.hold_normal_compiles and not is_heavy:
                    with self._lock:
                        self._active_normal_compiles -= 1
            if logical_source == self.timeout_source:
                raise subprocess.TimeoutExpired(logical_source, timeout)
            output.write_bytes(_test_relocatable_elf32())
            if is_contract and not is_heavy:
                with self._lock:
                    self.completed_normal_compiles.add(logical_source)
        else:
            with self._lock:
                self._active_links += 1
                self.max_active_links = max(
                    self.max_active_links, self._active_links
                )
                if self._active_links >= 2:
                    self._link_pair_ready.set()
            if not self._link_pair_ready.wait(5):
                raise AssertionError("contract links did not overlap")
            output.write_bytes(_test_static_elf32())
            with self._lock:
                self._active_links -= 1
        return subprocess.CompletedProcess([], 0, "", "")
