"""Synthetic release claims with real installed images for verifier boundary tests."""
import hashlib
import json


def released_behavior_fixture(root, stage, inputs, runner):
    """Authorize observed test-stage bytes without promoting installed seeds."""
    from dataclasses import replace
    from tools import bootstrap_toolchain as bootstrap
    from tools.bootstrap_stage_release import BehaviorRelease

    record = json.loads((root / "bootstrap/seeds/release.json").read_bytes())
    provenance = inputs.manifest["provenance"]
    tool_format = "pe32" if inputs.manifest["schema"].startswith("cupid.execution-") else "elf32"
    for key in ("source_revision", "source_snapshot_sha256", "source_input_count"):
        record[key] = provenance[key]
    linux = bootstrap._candidate_build_plan(json.loads(
        (root / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())["build_plan"])
    record["linux_plan_sha256"] = bootstrap._build_plan_sha256(linux)
    if tool_format == "pe32":
        record.update(
            windows_plan_sha256=provenance["native_build_plan_sha256"],
            parent_source_revision=provenance["parent_execution_seed_source_revision"],
            parent_linux_manifest_sha256=provenance["parent_plan_seed_manifest_sha256"],
            parent_windows_manifest_sha256=provenance["parent_execution_seed_manifest_sha256"])
    else:
        record.update(parent_source_revision=provenance["parent_seed_source_revision"],
                      parent_linux_manifest_sha256=provenance["parent_seed_manifest_sha256"])
    rows = []
    assert set(stage.tools) == set(bootstrap.CANDIDATE_TOOL_NAMES)
    for selected_format, host in (("elf32", "linux"), ("pe32", "windows")):
        seed_root = root / ("bootstrap/seeds/i386-" + host)
        manifest = json.loads((seed_root / "manifest.json").read_bytes())
        for row in manifest["artifacts"]:
            contents = (stage.tools[row["name"]] if selected_format == tool_format else
                        seed_root / row["file"]).read_bytes()
            rows.append({"name": row["name"], "format": selected_format,
                         "size": len(contents), "sha256": hashlib.sha256(contents).hexdigest()})
    record["artifacts"] = rows
    encode = lambda value: (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")
    authority = BehaviorRelease(encode(record), record, tool_format, encode(linux))
    return replace(inputs, behavior_release=authority), authority.runner(runner)


def replace_fixture_tool(manifest_path, release_path, name, tool_format, contents):
    """Bind a test-owned tool image in its manifest and explicit release."""
    manifest = json.loads(manifest_path.read_bytes())
    artifact = next(row for row in manifest["artifacts"] if row["name"] == name)
    record = json.loads(release_path.read_bytes())
    released = next(row for row in record["artifacts"]
                    if row["name"] == name and row["format"] == tool_format)
    facts = {"size": len(contents), "sha256": hashlib.sha256(contents).hexdigest()}
    (manifest_path.parent / artifact["file"]).write_bytes(contents)
    artifact.update(facts)
    released.update(facts)
    for path, value in ((manifest_path, manifest), (release_path, record)):
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def install_future_fixture(root, *, new_parent=False):
    from tests.test_seed_manifest import user_abi_manifest
    from tests.test_seed_release import release, encode
    linux = user_abi_manifest(1, long_paths=True, aliases=True)
    windows = user_abi_manifest(2, long_paths=True, aliases=True)
    record = release()
    record.update(
        source_input_count=82,
        source_revision=linux["provenance"]["source_revision"],
        source_snapshot_sha256=linux["provenance"]["source_snapshot_sha256"],
        linux_plan_sha256=linux["build_plan_sha256"],
        windows_plan_sha256=windows["provenance"]["native_build_plan_sha256"],
        parent_source_revision=linux["provenance"]["parent_seed_source_revision"],
        parent_linux_manifest_sha256=linux["provenance"]["parent_seed_manifest_sha256"],
        parent_windows_manifest_sha256=windows["provenance"]["parent_execution_seed_manifest_sha256"],
    )
    if new_parent:
        record.update(parent_source_revision="1234567890abcdef1234567890abcdef12345678",
                      parent_linux_manifest_sha256="0123456789abcdef" * 4,
                      parent_windows_manifest_sha256="fedcba9876543210" * 4)
    linux["provenance"].update(
        parent_seed_source_revision=record["parent_source_revision"],
        parent_seed_manifest_sha256=record["parent_linux_manifest_sha256"])
    windows["provenance"].update(
        parent_execution_seed_source_revision=record["parent_source_revision"],
        parent_execution_seed_manifest_sha256=record["parent_windows_manifest_sha256"],
        parent_plan_seed_source_revision=record["parent_source_revision"],
        parent_plan_seed_manifest_sha256=record["parent_linux_manifest_sha256"])
    record["artifacts"] = [
        {"name": row["name"], "format": fmt, "size": row["size"], "sha256": row["sha256"]}
        for fmt, document in (("elf32", linux), ("pe32", windows))
        for row in document["artifacts"]
    ]
    linux_bytes = encode(linux)
    windows["provenance"]["plan_seed_manifest_sha256"] = hashlib.sha256(linux_bytes).hexdigest()
    (root / "bootstrap/seeds/i386-linux/manifest.json").write_bytes(linux_bytes)
    (root / "bootstrap/seeds/i386-windows/manifest.json").write_bytes(encode(windows))
    (root / "bootstrap/seeds/release.json").write_bytes(encode(record))
    return record, linux, windows
