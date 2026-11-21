## @file
#  @brief Share CupidBuild CLI inputs and transaction fixtures.
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path

from tests.artifact_release_fixtures import replace_fixture_tool
from tests.test_seed_manifest import historical_manifest
from tools.cupidc_kernel_compile import _profile_input_manifest


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"
BASELINE_JPEG = (
    b"\xff\xd8"
    b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
    b"\xff\xd9"
)


class CupidBuildCliFixtures:
    def _production_manifest(self):
        selected = os.environ.get("CUPIDBUILD_TEST_SEED_MANIFEST")
        if selected:
            return Path(selected).resolve()
        platform = "i386-windows" if os.name == "nt" else "i386-linux"
        return REPO_ROOT / "bootstrap" / "seeds" / platform / "manifest.json"

    def _production_release(self):
        selected = os.environ.get("CUPIDBUILD_TEST_SEED_RELEASE")
        return (Path(selected).resolve() if selected else
                self._production_manifest().parent.parent / "release.json")

    def _select_fixture_release(self, manifest, release):
        if not hasattr(self, "_fixture_releases"):
            self._fixture_releases = {}
        self._fixture_releases[Path(manifest)] = release

    def _seed_release_arguments(self, manifest=None):
        selected = Path(manifest or self._production_manifest())
        default = (selected.parent.parent / "release.json" if manifest else
                   self._production_release())
        release = getattr(self, "_fixture_releases", {}).get(selected, default)
        return [] if release is None else ["--seed-release", str(release)]

    def _production_tool(self, name):
        manifest = self._production_manifest()
        document = json.loads(manifest.read_text(encoding="utf-8"))
        artifact = next(
            artifact
            for artifact in document["artifacts"]
            if artifact["name"] == name
        )
        return manifest.parent / artifact["file"]

    def _run_checked_tool(
        self,
        tool,
        arguments,
        *,
        root=REPO_ROOT,
        timeout=None,
        manifest=None,
        cli=None,
        env=None,
    ):
        command = self._checked_tool_command(
            tool, arguments, root=root, timeout=timeout, manifest=manifest
        )
        if cli is not None:
            command[0] = str(cli)
        return subprocess.run(
            command,
            cwd=root,
            env=env,
            text=True,
            capture_output=True,
            timeout=90,
        )

    def _checked_tool_command(
        self,
        tool,
        arguments,
        *,
        root=REPO_ROOT,
        timeout=None,
        manifest=None,
    ):
        command = [
            str(self.cli_path),
            "run",
            "--seed-manifest",
            str(manifest or self._production_manifest()),
            *self._seed_release_arguments(manifest),
            "--root",
            str(root),
            "--tool",
            tool,
        ]
        if timeout is not None:
            command.extend(["--timeout", str(timeout)])
        command.extend(["--", *arguments])
        return command

    def _run_private_roots(self):
        return set(REPO_ROOT.glob(".cupidbuild-run-*"))

    def _assembly_command(
        self,
        operation,
        source,
        output,
        *,
        manifest=None,
        root=REPO_ROOT,
    ):
        return [
            str(self.cli_path),
            operation,
            "--seed-manifest",
            str(manifest or self._production_manifest()),
            *self._seed_release_arguments(manifest),
            "--root",
            str(root),
            "--source",
            source.relative_to(root).as_posix(),
            "--output",
            output.relative_to(root).as_posix(),
        ]

    def _run_assembly(
        self,
        operation,
        source,
        output,
        manifest=None,
        *,
        root=REPO_ROOT,
        preexec_fn=None,
    ):
        return subprocess.run(
            self._assembly_command(
                operation,
                source,
                output,
                manifest=manifest,
                root=root,
            ),
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
            preexec_fn=preexec_fn,
        )

    def _run_object(
        self,
        source,
        output,
        manifest=None,
        *,
        root=REPO_ROOT,
        preexec_fn=None,
    ):
        return self._run_assembly(
            "assemble-cupidasm-object",
            source,
            output,
            manifest,
            root=root,
            preexec_fn=preexec_fn,
        )

    def _run_embed_jpeg(self, source, output, manifest=None):
        return self._run_assembly("embed-jpeg", source, output, manifest)

    def _run_generate_ksyms(self, source, output, manifest=None):
        return self._run_assembly("generate-ksyms", source, output, manifest)

    def _run_flatten_kernel(self, root, input_manifest, output, seed_manifest):
        return subprocess.run(
            [
                str(self.cli_path),
                "flatten-kernel",
                "--seed-manifest",
                str(seed_manifest),
                *self._seed_release_arguments(seed_manifest),
                "--root",
                str(root),
                "--input-manifest",
                input_manifest.relative_to(root).as_posix(),
                "--output",
                output.relative_to(root).as_posix(),
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )

    def _run_profile_manifest(
        self,
        output,
        manifest=None,
        root=REPO_ROOT,
        *,
        cli=None,
        env=None,
        cwd=None,
    ):
        return subprocess.run(
            [
                str(cli or self.cli_path),
                "generate-profile-manifest",
                "--seed-manifest",
                str(manifest or self._production_manifest()),
                *self._seed_release_arguments(manifest),
                "--root",
                str(root),
                "--output",
                output.relative_to(root).as_posix(),
            ],
            cwd=cwd or root,
            text=True,
            capture_output=True,
            timeout=90,
            env=env,
        )

    def _copy_profile_repository(self, destination):
        document = _profile_input_manifest(REPO_ROOT)
        relative_paths = {item["path"] for item in document["inputs"]}
        for paths in document["sources"].values():
            relative_paths.update(paths)
        for relative in sorted(relative_paths):
            source = REPO_ROOT / relative
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        return self._copy_checked_assembly_seed(destination / "seed")

    @staticmethod
    def _add_directories(parent, count):
        for index in range(count):
            (parent / f"d{index}").mkdir()

    def _build_ksyms_elf(self, root, source_text):
        source = root / "entry.asm"
        object_path = root / "entry.o"
        elf = root / "kernel.elf.pass1"
        source.write_text(source_text, encoding="ascii")
        assembled = subprocess.run(
            [
                str(self._production_tool("cupidasm")),
                "-f",
                "elf32",
                source.name,
                "-o",
                object_path.name,
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )
        self.assertEqual(assembled.returncode, 0, assembled.stderr)
        linked = subprocess.run(
            [
                str(self._production_tool("cupidld")),
                "-m",
                "elf_i386",
                "--text-address",
                "0x01C00000",
                "--entry",
                "_start",
                "-o",
                elf.name,
                object_path.name,
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )
        self.assertEqual(linked.returncode, 0, linked.stderr)
        return elf

    def _ksyms_blob_from_source(self, source):
        words = [
            int(match, 16)
            for match in re.findall(rb"0x([0-9a-f]{8})u,", source)
        ]
        size_match = re.search(rb"ksym_blob_size = ([0-9]+)u;", source)
        self.assertIsNotNone(size_match)
        self.assertTrue(words)
        packed = b"".join(struct.pack("<I", word) for word in words)
        size = int(size_match.group(1))
        self.assertGreaterEqual(len(packed), size)
        self.assertLess(len(packed) - size, 4)
        return packed[:size]

    @staticmethod
    def _large_baseline_jpeg(entropy_size=8 * 1024 * 1024):
        return BASELINE_JPEG[:-2] + b"\x12" * entropy_size + BASELINE_JPEG[-2:]

    def _private_roots(self, root=REPO_ROOT):
        tokens = set()
        for pattern in (".cupidbuild-object-*", "..cupidbuild-object-*"):
            for path in root.glob(pattern):
                match = re.fullmatch(
                    r"(?P<cleanup>\.)?"
                    r"(?P<token>\.cupidbuild-object-[0-9a-f]{8})"
                    r"(?P<entry>\..*)?",
                    path.name,
                )
                if match is None:
                    continue
                if match["cleanup"] is None and match["entry"] is None:
                    tokens.add(path)
                else:
                    tokens.add(root / f"{match['token']}.reserve")
        return tokens

    @staticmethod
    def _private_entry(token, logical_name):
        if token.name.endswith(".reserve"):
            prefix = token.name[: -len(".reserve")]
            return token.with_name(f"{prefix}.{logical_name}")
        return token / logical_name

    def _private_candidate_has_bytes(self, token):
        try:
            return self._private_entry(token, "candidate.o").stat().st_size > 0
        except FileNotFoundError:
            return False

    def _copy_checked_assembly_seed(self, destination):
        destination.mkdir()
        production_seed = self._production_manifest().parent
        shutil.copy2(production_seed / "manifest.json", destination / "manifest.json")
        suffix = ".exe" if os.name == "nt" else ".elf"
        for tool in (
            "cupidasm",
            "cupidc",
            "cupiddis",
            "cupidld",
            "cupidobj",
            "cupidbuild",
        ):
            shutil.copy2(
                production_seed / f"{tool}{suffix}",
                destination / f"{tool}{suffix}",
            )
        manifest = destination / "manifest.json"
        release = destination.parent / "release.json"
        shutil.copy2(self._production_release(), release)
        self._select_fixture_release(manifest, release)
        return manifest

    def _replace_seed_tool(self, manifest, name, executable):
        self._replace_seed_tool_bytes(manifest, name, Path(executable).read_bytes())

    def _replace_seed_tool_bytes(self, manifest, name, payload):
        release = self._seed_release_arguments(manifest)
        if release:
            replace_fixture_tool(manifest, Path(release[1]), name,
                                 "pe32" if os.name == "nt" else "elf32", payload)
            return
        document = json.loads(manifest.read_text(encoding="utf-8"))
        artifact = next(row for row in document["artifacts"] if row["name"] == name)
        (manifest.parent / artifact["file"]).write_bytes(payload)
        artifact.update(size=len(payload), sha256=hashlib.sha256(payload).hexdigest())
        manifest.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                            encoding="utf-8")

    def _rewrite_manifest(self, manifest, change, *, compact=False):
        document = json.loads(manifest.read_text(encoding="utf-8"))
        changed = change(document)
        if changed is not None:
            document = changed
        if compact:
            encoded = json.dumps(document, separators=(",", ":")) + "\n"
        else:
            encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
        manifest.write_text(encoded, encoding="utf-8")

    def _promote_seed_contract(self, manifest):
        artifacts = json.loads(manifest.read_bytes())["artifacts"]
        document = historical_manifest(2 if os.name == "nt" else 1)
        document["artifacts"] = artifacts
        document["provenance"].update(
            source_revision="abcdef0123456789abcdef0123456789abcdef01",
            source_snapshot_sha256="2" * 64)
        # Historical parser controls retain their frozen plan without a release.
        self._select_fixture_release(manifest, None)
        manifest.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                            encoding="utf-8")
        return document

    @staticmethod
    def _legacy_seed_contract(document):
        legacy = json.loads(json.dumps(document))
        legacy["artifacts"] = [
            artifact
            for artifact in legacy["artifacts"]
            if artifact["name"] != "cupidbuild"
        ]
        revision = "a17c9465911da41d59b7ada71733d36c39faa5ea"
        snapshot = (
            "46c5335c80d822dd5085ee22077486ea"
            "647e5396482d42454847c87e4222aa67"
        )
        lineage = legacy["provenance"]["producer_lineage"]
        if os.name == "nt":
            legacy["schema"] = "cupid.execution-seed.v1"
            legacy["provenance"] = {
                "artifact_generation": "paired-stage-four-native-windows",
                "fixed_point_command": "make bootstrap-windows-from-seed",
                "fixed_point_result": "pass",
                "parent_seed_manifest_sha256": (
                    "b6e34a2e18dd18aba91c6358116eafde"
                    "39953566efeadb224575ac8c13ab2c1b"
                ),
                "parent_seed_source_revision": revision,
                "producer_lineage": lineage,
                "source_input_count": 50,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        else:
            legacy["schema"] = "cupid.bootstrap-seed.v1"
            plan = historical_manifest(1)["build_plan"]
            legacy["build_plan"] = plan
            candidate_sources = {
                "cupidbuild",
                "cupidbuild_host",
                "cupidbuild_main",
            }
            plan["sources"] = [
                source
                for source in plan["sources"]
                if source["name"] not in candidate_sources
            ]
            del plan["links"]["cupidbuild"]
            encoded_plan = json.dumps(
                plan,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=True,
            ).encode("ascii")
            legacy["build_plan_sha256"] = hashlib.sha256(
                encoded_plan
            ).hexdigest()
            legacy["provenance"] = {
                "fixed_point_command": "make bootstrap-from-seed",
                "fixed_point_result": "pass",
                "producer_lineage": lineage,
                "seed_generation": "stage-four",
                "source_input_count": 50,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        return legacy

    @classmethod
    def _reverse_object_fields(cls, value):
        if isinstance(value, dict):
            return {
                key: cls._reverse_object_fields(value[key])
                for key in reversed(value)
            }
        if isinstance(value, list):
            return [
                cls._reverse_object_fields(item)
                for item in value
            ]
        return value
