"""Synthetic release claims with real installed images for verifier boundary tests."""
import hashlib


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
