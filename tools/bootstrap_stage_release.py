"""Prepare paired stages and qualify behavior with caller-reviewed release bytes.

A preparation is not a bootstrap report or a promoted seed. The release author
requires an independently selected source digest and parent pair. Qualification
rebuilds the stages and matches their actual tools before running behavior.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import tempfile

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

try:
    from tools import bootstrap_toolchain as seed
except ModuleNotFoundError:
    import bootstrap_toolchain as seed


SCHEMA = "cupid.bootstrap-stage-preparation.v1"
STAGES = ("stage-two", "stage-three", "stage-four")
ROLES = tuple(seed.CANDIDATE_TOOL_NAMES)
MAX_RELEASE_BYTES = 65536
SEEDED_COMMANDS = frozenset(("run", "assemble-cupidasm-object", "assemble-bootloader",
    "assemble-smp-trampoline", "assemble-iso-pattern", "embed-jpeg", "generate-ksyms",
    "flatten-kernel", "generate-profile-manifest", "compile-kernel", "compile-doom",
    "compile-production", "compile-user", "link-user"))


def _encode(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n").encode("ascii")


def _identity(payload):
    return {"size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise seed.BootstrapError("duplicate decoded JSON key")
        result[key] = value
    return result


def _invalid_number(_value):
    raise seed.BootstrapError("JSON numbers must be integers")


def _json(payload):
    try:
        return json.loads(payload.decode("utf-8"), object_pairs_hook=_pairs,
                          parse_float=_invalid_number, parse_constant=_invalid_number)
    except (UnicodeError, ValueError, RecursionError) as error:
        raise seed.BootstrapError("invalid release or preparation JSON") from error


def _regular_bytes(path, limit=64 * 1024 * 1024):
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise seed.BootstrapError(f"input must be a regular, unlinked file: {path}")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOINHERIT", 0)
    flags |= getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_CLOEXEC", 0)
    descriptor = os.open(path, flags)
    try:
        opened = os.fstat(descriptor)
        if (not stat.S_ISREG(opened.st_mode) or
                (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino)):
            raise seed.BootstrapError(f"input changed kind or identity: {path}")
        if not 1 <= opened.st_size <= limit:
            raise seed.BootstrapError(f"input must contain 1 to {limit} bytes")
        chunks = []
        count = 0
        while count <= limit:
            part = os.read(descriptor, min(65536, limit + 1 - count))
            if not part:
                break
            chunks.append(part)
            count += len(part)
        after = os.fstat(descriptor)
        live = path.lstat()
        def observed(value):
            return (value.st_dev, value.st_ino, value.st_mode, value.st_size,
                    value.st_mtime_ns, value.st_ctime_ns)
        # Windows lstat infers executable suffix bits and exposes a different
        # ctime meaning from fstat. Keep complete comparisons within each family.
        cross_identity = lambda value: (value.st_dev, value.st_ino, stat.S_IFMT(value.st_mode),
                                        value.st_size, value.st_mtime_ns)
        if (count > limit or count != opened.st_size or observed(opened) != observed(after)
                or observed(before) != observed(live) or cross_identity(after) != cross_identity(live)):
            raise seed.BootstrapError(f"input changed during capture: {path}")
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def _parent_record(inputs):
    return {"manifest_sha256": inputs.manifest_sha256,
            "source_revision": inputs.manifest["provenance"]["source_revision"]}


def _inventory(paths, stage_root):
    result = {}
    for role, path in sorted(paths.items()):
        logical = path.relative_to(stage_root).as_posix()
        if len(Path(logical).parts) != 1 or path.is_symlink():
            raise seed.BootstrapError("prepared stage artifact must be an unlinked leaf")
        result[role] = {"file": logical, **_identity(_regular_bytes(path))}
    return result


def publish_preparation(source_inputs, source_root, linux_plan, windows_plan,
                        linux_seed, windows_seed, stages, workspace, output, format_name):
    """Publish only source, stage and parent evidence, with no success report."""
    parents = {"elf32": _parent_record(linux_seed)}
    if windows_seed is not None:
        parents["pe32"] = _parent_record(windows_seed)
    record = {"schema": SCHEMA, "status": "unqualified", "format": format_name,
              "source_inputs": source_inputs.inventory,
              "source_snapshot_sha256": seed._source_snapshot_sha256(source_inputs.inventory),
              "linux_plan": linux_plan, "windows_plan": windows_plan,
              "parents": parents, "stages": {}}
    publication = workspace / "preparation"
    publication.mkdir()
    source_copy = publication / "source"
    source_copy.mkdir()
    for name, identity in source_inputs.inventory.items():
        contents = _regular_bytes(source_inputs.root / name)
        if _identity(contents) != identity:
            raise seed.BootstrapError(f"prepared source differs: {name}")
        destination = source_copy / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(contents)
    for name, stage in zip(STAGES, stages, strict=True):
        stage_root = source_inputs.root / name
        record["stages"][name] = {"objects": _inventory(stage.objects, stage_root),
                                  "tools": _inventory(stage.tools, stage_root)}
        stage_root.replace(publication / name)
    (publication / "stage-preparation.json").write_bytes(_encode(record))
    _read_preparation(publication, format_name, source_inputs.inventory, linux_plan, windows_plan, parents)
    seed.require_source_closures(source_inputs, source_root, linux_plan)
    seed.require_live_seed_inputs(*(item for item in (linux_seed, windows_seed) if item is not None))
    # This bundle deliberately does not use publish_bootstrap_outputs: it has
    # neither behavior evidence nor a bootstrap-report.json.
    seed._require_bootstrap_output_available(output)
    existed = output.exists()
    if existed:
        output.rmdir()
    try:
        publication.replace(output)
    except OSError as error:
        if existed and not output.exists():
            output.mkdir()
        raise seed.BootstrapError(f"cannot publish stage preparation: {error}") from error
    return record


def _expected_identity(source_revision, snapshot, linux_plan, windows_plan,
                       linux_seed, windows_seed, cohorts):
    seed._require_lower_hex(source_revision, 40, "caller source revision")
    seed._require_seed_pair_identity(windows_seed, linux_seed)
    parent_revision = linux_seed.manifest["provenance"]["source_revision"]
    if windows_seed.manifest["provenance"]["source_revision"] != parent_revision:
        raise seed.BootstrapError("paired parent source revisions differ")
    return {"schema": "cupid.seed-release.v1", "source_revision": source_revision,
            "source_snapshot_sha256": seed._source_snapshot_sha256(snapshot),
            "source_input_count": len(snapshot), "parent_source_revision": parent_revision,
            "parent_linux_manifest_sha256": linux_seed.manifest_sha256,
            "parent_windows_manifest_sha256": windows_seed.manifest_sha256,
            "linux_plan_sha256": seed._build_plan_sha256(linux_plan),
            "windows_plan_sha256": seed._build_plan_sha256(windows_plan),
            "artifacts": [{"name": role, "format": format_name, **cohorts[format_name]["tools"][role]}
                          for format_name in ("elf32", "pe32") for role in ROLES]}


def _check_record(payload, expected):
    record = _json(payload)
    if type(record) is not dict or record.keys() != expected.keys():
        raise seed.BootstrapError("release fields differ from caller-owned context")
    for key in expected.keys() - {"artifacts"}:
        if type(record[key]) is not type(expected[key]) or record[key] != expected[key]:
            raise seed.BootstrapError(f"release differs from caller-owned context: {key}")
    rows = record["artifacts"]
    if type(rows) is not list or len(rows) != 12:
        raise seed.BootstrapError("release requires both complete six-tool cohorts")
    ordered = {}
    for row in rows:
        if (type(row) is not dict or set(row) != {"name", "format", "size", "sha256"}
                or any(type(row[key]) is not str for key in ("name", "format", "sha256"))
                or type(row["size"]) is not int):
            raise seed.BootstrapError("release artifact fields differ")
        key = (row["format"], row["name"])
        if key in ordered:
            raise seed.BootstrapError("release artifact is duplicated")
        ordered[key] = row
    wanted = {(row["format"], row["name"]): row for row in expected["artifacts"]}
    if ordered != wanted:
        raise seed.BootstrapError("release artifacts differ from prepared cohorts")


def _read_preparation(root, format_name, snapshot, linux_plan, windows_plan, parents):
    if root.is_symlink() or not root.is_dir():
        raise seed.BootstrapError("preparation must be an unlinked directory")
    if {path.name for path in root.iterdir()} != {*STAGES, "source", "stage-preparation.json"}:
        raise seed.BootstrapError("preparation directory inventory differs")
    record = _json(_regular_bytes(root / "stage-preparation.json"))
    fields = {"schema", "status", "format", "source_inputs", "source_snapshot_sha256",
              "linux_plan", "windows_plan", "parents", "stages"}
    if type(record) is not dict or set(record) != fields:
        raise seed.BootstrapError("preparation fields differ")
    expected = {"schema": SCHEMA, "status": "unqualified", "format": format_name,
                "source_inputs": snapshot, "source_snapshot_sha256": seed._source_snapshot_sha256(snapshot),
                "linux_plan": linux_plan, "windows_plan": windows_plan, "parents": parents}
    if any(type(record[key]) is not type(value) or _encode(record[key]) != _encode(value)
           for key, value in expected.items()):
        raise seed.BootstrapError("preparation differs from caller-owned source, plan or parents")
    utf8, long_paths, aliases = seed._windows_plan_profile(windows_plan)
    seed.require_source_snapshot(root / "source", linux_plan, snapshot, windows_utf8=utf8,
                                 windows_long_paths=long_paths, windows_user_link_aliases=aliases)
    stage_maps = record["stages"]
    if type(stage_maps) is not dict or set(stage_maps) != set(STAGES):
        raise seed.BootstrapError("preparation stage inventory differs")
    inventories = {}
    object_roles = {source["name"] for source in
                    (linux_plan if format_name == "elf32" else windows_plan)["sources"]}
    if format_name == "elf32":
        object_roles.add("start")
    else:
        object_roles.update(source["name"] for source in windows_plan["assembly_sources"])
    for stage_name in STAGES:
        if (root / stage_name).is_symlink() or not (root / stage_name).is_dir():
            raise seed.BootstrapError("prepared stage must be an unlinked directory")
        stage = stage_maps[stage_name]
        if type(stage) is not dict or set(stage) != {"objects", "tools"}:
            raise seed.BootstrapError("preparation stage fields differ")
        actual = {}
        files = set()
        for kind in ("objects", "tools"):
            rows = stage[kind]
            expected_roles = set(ROLES) if kind == "tools" else object_roles
            if type(rows) is not dict or set(rows) != expected_roles:
                raise seed.BootstrapError("preparation artifact role inventory differs")
            actual[kind] = {}
            for role, row in rows.items():
                if (type(row) is not dict or set(row) != {"file", "size", "sha256"}
                        or type(row["file"]) is not str or Path(row["file"]).name != row["file"]
                        or row["file"] in files or type(row["size"]) is not int):
                    raise seed.BootstrapError("preparation artifact fields differ")
                files.add(row["file"])
                contents = _regular_bytes(root / stage_name / row["file"])
                identity = _identity(contents)
                if identity != {"size": row["size"], "sha256": row["sha256"]}:
                    raise seed.BootstrapError("prepared artifact bytes differ")
                actual[kind][role] = identity
        if {item.name for item in (root / stage_name).iterdir()} != files:
            raise seed.BootstrapError("prepared artifact directory inventory differs")
        inventories[stage_name] = actual
    if inventories["stage-three"] != inventories["stage-four"]:
        raise seed.BootstrapError("prepared stage three differs from stage four")
    return inventories["stage-four"]


@dataclass(frozen=True)
class BehaviorRelease:
    payload: bytes
    identity: dict
    format_name: str
    linux_plan_bytes: bytes

    def manifest_bytes(self, payload, artifacts):
        document = _json(payload)
        identity = self.identity
        expected = {row["name"]: {"size": row["size"], "sha256": row["sha256"]}
                    for row in identity["artifacts"] if row["format"] == self.format_name}
        if {name: _identity(data) for name, data in artifacts} != expected:
            raise seed.BootstrapError("behavior tools differ from authorized staged cohort")
        provenance = document["provenance"]
        for key in ("source_revision", "source_snapshot_sha256", "source_input_count"):
            provenance[key] = identity[key]
        if self.format_name == "elf32":
            linux_plan = _json(self.linux_plan_bytes)
            if seed._build_plan_sha256(linux_plan) != identity["linux_plan_sha256"]:
                raise seed.BootstrapError("behavior Linux plan differs from authorized release")
            document["build_plan"] = linux_plan
            document["build_plan_sha256"] = identity["linux_plan_sha256"]
            provenance["parent_seed_manifest_sha256"] = identity["parent_linux_manifest_sha256"]
            provenance["parent_seed_source_revision"] = identity["parent_source_revision"]
        else:
            provenance["parent_execution_seed_manifest_sha256"] = identity["parent_windows_manifest_sha256"]
            provenance["parent_execution_seed_source_revision"] = identity["parent_source_revision"]
            provenance["parent_plan_seed_manifest_sha256"] = identity["parent_linux_manifest_sha256"]
            provenance["parent_plan_seed_source_revision"] = identity["parent_source_revision"]
            provenance["native_build_plan_sha256"] = identity["windows_plan_sha256"]
            provenance["linux_candidate_build_plan_sha256"] = identity["linux_plan_sha256"]
        return _encode(document)

    def runner(self, runner):
        return ReleasedBehaviorRunner(runner, self.payload)


class ReleasedBehaviorRunner:
    def __init__(self, runner, payload):
        self._runner = runner
        self._payload = payload

    def __getattr__(self, name):
        return getattr(self._runner, name)

    def run(self, executable, arguments, timeout=60):
        arguments = list(arguments)
        release = None
        parent_arguments = arguments[:arguments.index("--")] if "--" in arguments else arguments
        if arguments and arguments[0] in SEEDED_COMMANDS and "--seed-manifest" in parent_arguments:
            index = parent_arguments.index("--seed-manifest")
            release = Path(arguments[index + 1]).parent / "seed-release.json"
            if _regular_bytes(release, MAX_RELEASE_BYTES) != self._payload:
                raise seed.BootstrapError("behavior release changed before execution")
            arguments[index:index] = ["--seed-release", release]
        result = self._runner.run(executable, arguments, timeout)
        if release is not None and _regular_bytes(release, MAX_RELEASE_BYTES) != self._payload:
            raise seed.BootstrapError("behavior release changed during execution")
        return result


@dataclass(frozen=True)
class ReleaseRequest:
    path: Path
    payload: bytes
    source_revision: str
    snapshot_sha256: str
    linux_seed: seed.SeedInputs
    windows_seed: seed.SeedInputs
    cohorts: dict

    def require_live(self):
        if _regular_bytes(self.path, MAX_RELEASE_BYTES) != self.payload:
            raise seed.BootstrapError("caller release changed during qualification")
        seed.require_live_seed_inputs(self.linux_seed, self.windows_seed)

    def authorize(self, source_inputs, source_root, linux_plan, windows_plan,
                  linux_seed, windows_seed, stage_three, stage_four, format_name):
        self.require_live()
        if (linux_seed.manifest_sha256 != self.linux_seed.manifest_sha256 or
                (windows_seed is not None and windows_seed.manifest_sha256 != self.windows_seed.manifest_sha256)):
            raise seed.BootstrapError("qualification parent selection differs")
        if seed._source_snapshot_sha256(source_inputs.inventory) != self.snapshot_sha256:
            raise seed.BootstrapError("qualification source differs from caller-selected digest")
        seed.require_source_closures(source_inputs, source_root, linux_plan)
        for stage in (stage_three, stage_four):
            for kind, paths in (("objects", stage.objects), ("tools", stage.tools)):
                if {role: _identity(_regular_bytes(path)) for role, path in paths.items()} != self.cohorts[format_name][kind]:
                    raise seed.BootstrapError(f"qualification {kind} differ from prepared cohort")
        identity = _expected_identity(self.source_revision, source_inputs.inventory, linux_plan,
                                      windows_plan, self.linux_seed, self.windows_seed, self.cohorts)
        _check_record(self.payload, identity)
        return BehaviorRelease(self.payload, identity, format_name, _encode(linux_plan))


@dataclass(frozen=True)
class SeedBehaviorRequest:
    """Reuse a pinned seed cohort for behavior, without claiming a new producer proof.

    The publication author still owns comparisons of the rebuilt objects. Its
    source inventory may differ from the reviewed seed's producer inventory.
    Only byte-identical tools from both rebuilt stages can reuse this authority.
    """

    path: Path
    payload: bytes
    linux_seed: seed.SeedInputs
    linux_plan_bytes: bytes
    windows_seed: seed.SeedInputs | None = None

    def _identity(self):
        try:
            from tools.seed_release_identity import (
                ReleaseIdentityError, verify_release_identity_bytes,
            )
        except ModuleNotFoundError:
            from seed_release_identity import (
                ReleaseIdentityError, verify_release_identity_bytes,
            )
        try:
            return verify_release_identity_bytes(self.payload)
        except ReleaseIdentityError as error:
            raise seed.BootstrapError(f"behavior seed release is invalid: {error}") from error

    @property
    def source_revision(self):
        return self._identity()["source_revision"]

    @property
    def snapshot_sha256(self):
        return self._identity()["source_snapshot_sha256"]

    def require_live(self):
        if _behavior_release_bytes(self.path) != self.payload:
            raise seed.BootstrapError("caller seed release changed during behavior")
        seed.require_live_seed_inputs(self.linux_seed, *(
            (self.windows_seed,) if self.windows_seed is not None else ()))

    def authorize(self, source_inputs, source_root, linux_plan, windows_plan,
                  linux_seed, windows_seed, stage_three, stage_four, format_name):
        # These remain the publication author's actual source and object facts;
        # they are not replaced by the reused seed's producer claims.
        del source_inputs, source_root
        self.require_live()
        identity = self._identity()
        if (format_name not in ("elf32", "pe32") or
                linux_seed.manifest_bytes != self.linux_seed.manifest_bytes or
                linux_seed.artifact_bytes != self.linux_seed.artifact_bytes):
            raise seed.BootstrapError("behavior seed selection differs from reviewed cohort")
        if format_name == "elf32":
            if windows_seed is not None:
                raise seed.BootstrapError("behavior seed selection differs from reviewed cohort")
        elif (self.windows_seed is None or windows_seed is None or
                windows_seed.manifest_bytes != self.windows_seed.manifest_bytes or
                windows_seed.artifact_bytes != self.windows_seed.artifact_bytes):
            raise seed.BootstrapError("behavior seed selection differs from reviewed cohort")
        if (seed._build_plan_sha256(linux_plan) != identity["linux_plan_sha256"] or
                seed._build_plan_sha256(_json(self.linux_plan_bytes)) != identity["linux_plan_sha256"]):
            raise seed.BootstrapError("behavior seed plan differs from reviewed cohort")
        if format_name == "pe32" and seed._build_plan_sha256(windows_plan) != identity["windows_plan_sha256"]:
            raise seed.BootstrapError("behavior seed plan differs from reviewed cohort")
        expected = {row["name"]: {"size": row["size"], "sha256": row["sha256"]}
                    for row in identity["artifacts"] if row["format"] == format_name}
        for stage in (stage_three, stage_four):
            actual = {role: _identity(_regular_bytes(path)) for role, path in stage.tools.items()}
            if actual != expected:
                raise seed.BootstrapError("behavior stage tools differ from reviewed seed cohort")
        return BehaviorRelease(self.payload, identity, format_name, self.linux_plan_bytes)



def _behavior_release_bytes(path):
    try:
        return _regular_bytes(path, MAX_RELEASE_BYTES)
    except OSError as error:
        raise seed.BootstrapError("behavior seed release could not be read") from error


def capture_seed_behavior_release(path, linux_seed, windows_seed=None):
    """Capture explicitly selected release bytes against independently reviewed pins."""
    payload = _behavior_release_bytes(path)
    checked = seed.verify_seed_inputs(linux_seed.live_manifest_path)
    if (checked.manifest.get("schema") != seed.PROMOTED_SEED_SCHEMA or
            checked.manifest_bytes != linux_seed.manifest_bytes or
            checked.artifact_bytes != linux_seed.artifact_bytes):
        raise seed.BootstrapError("behavior seed selection differs from reviewed cohort")
    if windows_seed is not None:
        windows_checked = seed.verify_seed_inputs(windows_seed.live_manifest_path)
        if (windows_checked.manifest.get("schema") != seed.PROMOTED_WINDOWS_SEED_SCHEMA or
                windows_checked.manifest_bytes != windows_seed.manifest_bytes or
                windows_checked.artifact_bytes != windows_seed.artifact_bytes):
            raise seed.BootstrapError("behavior seed selection differs from reviewed cohort")
        seed._require_seed_pair_identity(windows_checked, checked)
    request = SeedBehaviorRequest(path.absolute(), payload, linux_seed,
                                  _encode(checked.manifest["build_plan"]), windows_seed)
    request._identity()
    request.require_live()
    return request


def _capture_pair(root, linux_seed, windows_seed, long_paths, aliases):
    if (linux_seed.manifest.get("schema") != seed.PROMOTED_SEED_SCHEMA or
            windows_seed.manifest.get("schema") != seed.PROMOTED_WINDOWS_SEED_SCHEMA):
        raise seed.BootstrapError("staged release requires both promoted six-tool parents")
    seed._require_seed_pair_identity(windows_seed, linux_seed)
    linux_plan = seed._candidate_build_plan(linux_seed.manifest["build_plan"])
    windows_plan = seed._windows_build_plan(linux_plan, utf8=True, long_paths=long_paths,
                                          user_link_aliases=aliases)
    snapshot = seed.capture_source_snapshot(root, linux_plan, windows_utf8=True,
                                            windows_long_paths=long_paths, windows_user_link_aliases=aliases)
    return snapshot, linux_plan, windows_plan


def paired_context(root, linux_preparation, windows_preparation, linux_seed, windows_seed,
                   source_revision, snapshot_sha256, long_paths=False, aliases=True):
    snapshot, linux_plan, windows_plan = _capture_pair(root, linux_seed, windows_seed, long_paths, aliases)
    seed._require_lower_hex(snapshot_sha256, 64, "caller source snapshot")
    if seed._source_snapshot_sha256(snapshot) != snapshot_sha256:
        raise seed.BootstrapError("live source differs from caller-selected digest")
    linux_parents = {"elf32": _parent_record(linux_seed)}
    windows_parents = {**linux_parents, "pe32": _parent_record(windows_seed)}
    cohorts = {"elf32": _read_preparation(linux_preparation, "elf32", snapshot, linux_plan, windows_plan, linux_parents),
               "pe32": _read_preparation(windows_preparation, "pe32", snapshot, linux_plan, windows_plan, windows_parents)}
    seed.require_live_seed_inputs(linux_seed, windows_seed)
    seed.require_source_snapshot(root, linux_plan, snapshot, windows_utf8=True,
                                 windows_long_paths=long_paths, windows_user_link_aliases=aliases)
    identity = _expected_identity(source_revision, snapshot, linux_plan, windows_plan,
                                  linux_seed, windows_seed, cohorts)
    return identity, cohorts


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare-linux", "prepare-windows", "author", "qualify-linux", "qualify-windows"))
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--linux-manifest", type=Path, required=True)
    parser.add_argument("--windows-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--linux-preparation", type=Path)
    parser.add_argument("--windows-preparation", type=Path)
    parser.add_argument("--release", type=Path)
    parser.add_argument("--source-revision")
    parser.add_argument("--source-snapshot-sha256")
    parser.add_argument("--windows-long-paths", action="store_true")
    parser.add_argument("--windows-user-link-aliases", action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args(argv)
    paired = args.command in ("author", "qualify-linux", "qualify-windows")
    if paired and any(getattr(args, name) is None for name in (
            "linux_preparation", "windows_preparation", "source_revision", "source_snapshot_sha256")):
        parser.error("paired commands require both preparations and caller source identities")
    if args.command.startswith("qualify") and args.release is None:
        parser.error("qualification requires --release")
    if args.command.endswith("windows") and os.name != "nt":
        parser.error("Windows stages require a Windows host")
    try:
        with tempfile.TemporaryDirectory(prefix="cupid-stage-release-seeds-") as temporary:
            private = Path(temporary)
            linux_seed = seed.freeze_seed_inputs(args.linux_manifest, private / "linux")
            windows_seed = seed.freeze_seed_inputs(args.windows_manifest, private / "windows")
            seed._require_seed_pair_identity(windows_seed, linux_seed)
            if (linux_seed.manifest.get("schema") != seed.PROMOTED_SEED_SCHEMA or
                    windows_seed.manifest.get("schema") != seed.PROMOTED_WINDOWS_SEED_SCHEMA):
                raise seed.BootstrapError("staged release requires both promoted six-tool parents")
            request = None
            if paired:
                identity, cohorts = paired_context(args.root, args.linux_preparation, args.windows_preparation,
                    linux_seed, windows_seed, args.source_revision, args.source_snapshot_sha256,
                    args.windows_long_paths, args.windows_user_link_aliases)
                if args.command == "author":
                    with args.output.open("xb") as stream:
                        stream.write(_encode(identity))
                    print("paired behavior release candidate: written (stages remain unqualified)")
                    return 0
                payload = _regular_bytes(args.release, MAX_RELEASE_BYTES)
                _check_record(payload, identity)
                request = ReleaseRequest(args.release, payload, args.source_revision,
                                         args.source_snapshot_sha256, linux_seed, windows_seed, cohorts)
            options = {"windows_long_paths": args.windows_long_paths,
                       "windows_user_link_aliases": args.windows_user_link_aliases,
                       "prepare_stages": not paired, "release_request": request}
            if args.command.endswith("linux"):
                seed._bootstrap_from_frozen_seed(linux_seed, args.root, args.output,
                                                compare_fixed_point=True, **options)
            else:
                seed._bootstrap_windows_from_frozen_seed(windows_seed, linux_seed, args.root, args.output, **options)
            print("stage preparation: written (behavior pending)" if not paired else "release-authorized bootstrap: passed")
            return 0
    except (seed.BootstrapError, OSError) as error:
        parser.exit(1, f"staged release failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
