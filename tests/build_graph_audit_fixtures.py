## @file
#  @brief Provide shared build graph fixtures and load the audit tool.
import importlib.util
import json
import re
import sys
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
AUDIT_TOOL = REPO_ROOT / "tools" / "build_graph_audit.py"
ACTIVE_BUILD_MANIFEST = (
    REPO_ROOT / "docs" / "bootstrap" / "audits" / "active-build.json"
)
CONDITIONAL_MANIFEST = (
    REPO_ROOT / "toolchain" / "tests" / "cupidc_pp_conditional_cases.inc"
)
ACTIVE_CASE_MANIFEST = (
    REPO_ROOT / "toolchain" / "tests" / "cupidc_pp_active_cases.inc"
)
CUPIDC_PP_CONTRACT = (
    REPO_ROOT / "toolchain" / "tests" / "cupidc_pp_contract.cc"
)
TOOLCHAIN_MAKEFILE = REPO_ROOT / "toolchain" / "Makefile"
LINUX_BOOTSTRAP_SEED_INPUTS = (
    "bootstrap/seeds/i386-linux/manifest.json",
    "bootstrap/seeds/i386-linux/cupidasm.elf",
    "bootstrap/seeds/i386-linux/cupidc.elf",
    "bootstrap/seeds/i386-linux/cupiddis.elf",
    "bootstrap/seeds/i386-linux/cupidld.elf",
    "bootstrap/seeds/i386-linux/cupidobj.elf",
    "bootstrap/seeds/i386-linux/cupidbuild.elf",
)
WINDOWS_EXECUTION_SEED_INPUTS = (
    "bootstrap/seeds/i386-windows/manifest.json",
    "bootstrap/seeds/i386-windows/cupidasm.exe",
    "bootstrap/seeds/i386-windows/cupidc.exe",
    "bootstrap/seeds/i386-windows/cupiddis.exe",
    "bootstrap/seeds/i386-windows/cupidld.exe",
    "bootstrap/seeds/i386-windows/cupidobj.exe",
    "bootstrap/seeds/i386-windows/cupidbuild.exe",
)
WINDOWS_PRODUCTION_SEED_INPUTS = (
    WINDOWS_EXECUTION_SEED_INPUTS[0],
    "bootstrap/seeds/release.json",
    *WINDOWS_EXECUTION_SEED_INPUTS[1:],
)
LINUX_PRODUCTION_SEED_INPUTS = (
    LINUX_BOOTSTRAP_SEED_INPUTS[0],
    "bootstrap/seeds/release.json",
    *LINUX_BOOTSTRAP_SEED_INPUTS[1:],
)


def _write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")


def _write_source_suffix_policy(
    root,
    *,
    residual_c_sources=None,
    runtime_delivery_sources=None,
    unreachable_cupid_c_sources=None,
):
    payload = {
        "schema": "cupid.c-source-suffix-ownership.v1",
        "residual_c_sources": residual_c_sources or {},
        "runtime_delivery_sources": runtime_delivery_sources or [],
        "unreachable_cupid_c_sources": unreachable_cupid_c_sources or {},
    }
    _write(
        root / "docs" / "bootstrap" / "c-source-suffix-ownership.json",
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
    )


def _json_list_recipe(values):
    python = Path(sys.executable).resolve().as_posix()
    return (
        f'\t@"{python}" -c "import json; print(json.dumps({list(values)!r}))"'
    )


def _conditional_manifest_records():
    records = {}
    pattern = re.compile(
        r'^CUPIDC_PP_CONDITIONAL_CASE\("([^"]+)", '
        r"([0-9]+)u, ([0-9]+)u, ([01])\)$"
    )
    for line in CONDITIONAL_MANIFEST.read_text(encoding="utf-8").splitlines():
        match = pattern.fullmatch(line)
        if match is None:
            continue
        expression, if_count, elif_count, expected = match.groups()
        if expression in records:
            raise AssertionError(
                f"duplicate conditional manifest: {expression}"
            )
        records[expression] = (
            int(if_count),
            int(elif_count),
            int(expected),
        )
    return records


def _load_audit_module():
    spec = importlib.util.spec_from_file_location(
        "_cupid_build_graph_audit_manifest_test", AUDIT_TOOL
    )
    if spec is None or spec.loader is None:
        raise AssertionError("could not load build graph audit module")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    finally:
        del sys.modules[spec.name]
    return module
