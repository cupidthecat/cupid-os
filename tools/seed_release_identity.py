"""Author and check the reviewed seed identities during native-verifier migration.

The tracked record is release authority, not a signature. During migration its
semantic fields must agree with the independently retained Python release pins.
No identities are learned from an untrusted manifest. Filesystem capture and
final drift checks belong to the caller of the byte validator.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from tools import bootstrap_toolchain as seed


RELEASE_PATH = "bootstrap/seeds/release.json"
MAX_RELEASE_BYTES = 65536
ROLES = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")


class ReleaseIdentityError(ValueError):
    """A release record differs from the reviewed identities."""


def current_release_identity() -> dict[str, object]:
    """Return an owned record from existing reviewed pins, not manifest claims."""
    return {
        "schema": "cupid.seed-release.v1",
        "source_revision": seed.PROMOTED_SOURCE_REVISION,
        "source_snapshot_sha256": seed.PROMOTED_SOURCE_SNAPSHOT_SHA256,
        "source_input_count": seed.PROMOTED_SOURCE_INPUT_COUNT,
        "parent_source_revision": seed.PROMOTION_PARENT_SOURCE_REVISION,
        "parent_linux_manifest_sha256": seed.PROMOTION_PARENT_LINUX_MANIFEST_SHA256,
        "parent_windows_manifest_sha256": seed.PROMOTION_PARENT_WINDOWS_MANIFEST_SHA256,
        "linux_plan_sha256": seed.PROMOTED_LINUX_PLAN_SHA256,
        "windows_plan_sha256": seed.PROMOTED_WINDOWS_PLAN_SHA256,
        "artifacts": [
            {"name": role, "format": format_name, "size": identities[role][0],
             "sha256": identities[role][1]}
            for format_name, identities in (
                ("elf32", seed.PROMOTED_LINUX_ARTIFACT_IDENTITIES),
                ("pe32", seed.PROMOTED_WINDOWS_ARTIFACT_IDENTITIES))
            for role in ROLES
        ],
    }


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ReleaseIdentityError("release contains a duplicate decoded key")
        result[key] = value
    return result


def _invalid_number(_text):
    raise ReleaseIdentityError("release numbers must be integers")


def verify_release_identity_bytes(payload: bytes) -> dict[str, object]:
    """Check exact semantic pins; return an owned, role-ordered record."""
    if not isinstance(payload, bytes) or not 1 <= len(payload) <= MAX_RELEASE_BYTES:
        raise ReleaseIdentityError("release input must contain 1 to 65536 bytes")
    try:
        record = json.loads(payload.decode("utf-8"), object_pairs_hook=_object,
                            parse_float=_invalid_number, parse_constant=_invalid_number)
    except ReleaseIdentityError:
        raise
    except (UnicodeError, ValueError, RecursionError) as error:
        raise ReleaseIdentityError("release JSON is invalid") from error
    expected = current_release_identity()
    if type(record) is not dict or record.keys() != expected.keys():
        raise ReleaseIdentityError("release identity fields differ")
    for name, value in expected.items():
        if name != "artifacts" and (
            type(record[name]) is not type(value) or record[name] != value
        ):
            raise ReleaseIdentityError(f"release identity field differs: {name}")
    rows = record["artifacts"]
    if type(rows) is not list or len(rows) != 12:
        raise ReleaseIdentityError("release requires both complete six-tool cohorts")
    identities = {(row["format"], row["name"]): row for row in expected["artifacts"]}
    seen = set()
    for row in rows:
        if type(row) is not dict or set(row) != {"name", "format", "size", "sha256"}:
            raise ReleaseIdentityError("release artifact fields differ")
        if any(type(row[name]) is not str for name in ("name", "format", "sha256")):
            raise ReleaseIdentityError("release artifact text field differs")
        key = (row["format"], row["name"])
        if key not in identities or key in seen:
            raise ReleaseIdentityError("release artifact is unknown or duplicated")
        seen.add(key)
        if type(row["size"]) is not int or row != identities[key]:
            raise ReleaseIdentityError("release artifact identity differs")
    return expected


def encode_release_identity() -> bytes:
    return (json.dumps(current_release_identity(), indent=2, sort_keys=True) + "\n").encode("utf-8")


def checked_release_bytes(root: Path) -> bytes:
    """Require both installed, pinned cohorts and the exact Linux pair binding."""
    try:
        linux = seed.verify_seed_inputs(root / "bootstrap/seeds/i386-linux/manifest.json")
        windows = seed.verify_seed_inputs(root / "bootstrap/seeds/i386-windows/manifest.json")
        if (linux.manifest.get("schema") != seed.PROMOTED_SEED_SCHEMA or
                windows.manifest.get("schema") != seed.PROMOTED_WINDOWS_SEED_SCHEMA):
            raise ReleaseIdentityError("release author requires both promoted six-tool cohorts")
        manifest = json.loads(windows.manifest_bytes)
        if manifest["provenance"]["plan_seed_manifest_sha256"] != linux.manifest_sha256:
            raise ReleaseIdentityError("Windows seed is not bound to the selected Linux manifest")
        seed.require_live_seed_inputs(linux, windows)
    except seed.BootstrapError as error:
        raise ReleaseIdentityError(f"cannot author release identity: {error}") from error
    return encode_release_identity()


def write_release_candidate(root: Path, output: Path) -> None:
    """Write a new review candidate; never replace a release or promote seeds."""
    payload = checked_release_bytes(root)
    with output.open("xb") as stream:
        stream.write(payload)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        write_release_candidate(args.root, args.output)
    except (ReleaseIdentityError, OSError) as error:
        sys.stderr.write(f"seed release identity failed: {error}\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
