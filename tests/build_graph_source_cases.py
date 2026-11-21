## @file
#  @brief Check source classification, includes, and artifact inputs.
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from tests.build_graph_audit_fixtures import (
    ACTIVE_BUILD_MANIFEST,
    AUDIT_TOOL,
    REPO_ROOT,
    _json_list_recipe,
    _write,
    _write_source_suffix_policy,
)


class SourceInventoryCases:
    def test_inventory_accounts_for_unreachable_and_duplicate_sources(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ALL_CC := $(filter-out filtered.cc, $(wildcard *.cc))

                .PHONY: all
                all: active.o

                active.o: active.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "active.c", "int answer(void) { return 42; }\n")
            _write(root / "copy.c", "int answer(void) { return 42; }\n")
            _write(root / "unused.c", "int unused(void) { return 0; }\n")
            _write(root / "filtered.cc", "U0 Legacy() {}\n")
            _write(root / "bin" / "build.cup", "echo bootstrap\n")
            _write(
                root / "TempleOS" / "reference.c",
                '#line 900 "temple-reference.c"\nint reference;\n',
            )

            first = root / "first.json"
            second = root / "second.json"
            for output in (first, second):
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            audit = json.loads(first.read_text(encoding="utf-8"))
            unreachable = {
                entry["path"]: entry for entry in audit["unreachable_sources"]
            }
            self.assertEqual(
                set(unreachable),
                {"bin/build.cup", "copy.c", "filtered.cc", "unused.c"},
            )
            self.assertEqual(
                audit["contracts"]["c_preprocessor_line_directives"],
                {
                    "status": "pass",
                    "source_files": 1,
                    "named_line_occurrences": 0,
                    "direct_line_occurrences": 0,
                    "pp_token_line_occurrences": 0,
                    "filename_occurrences": 0,
                    "ordinary_marker_occurrences": 0,
                    "digraph_marker_occurrences": 0,
                    "numeric_marker_occurrences": 0,
                    "max_conditional_depth": 0,
                    "forms": [],
                },
            )
            self.assertEqual(
                unreachable["bin/build.cup"]["language"], "cupid_script"
            )
            self.assertEqual(
                unreachable["copy.c"]["classification"], "exact_duplicate"
            )
            self.assertEqual(
                unreachable["copy.c"]["duplicate_of"], ["active.c"]
            )
            self.assertEqual(
                unreachable["filtered.cc"]["classification"],
                "explicitly_excluded",
            )
            self.assertEqual(
                unreachable["unused.c"]["classification"], "not_reached"
            )
            self.assertEqual(
                audit["contracts"]["c_source_ownership"],
                {
                    "status": "pass",
                    "policy": {
                        "path": None,
                        "sha256": None,
                        "residual_c_sources": 0,
                        "runtime_delivery_sources": 0,
                        "unreachable_cupid_c_sources": 0,
                    },
                    "tracked_c_sources": 3,
                    "active_tracked_c_sources": 1,
                    "cupidc_owned_tracked_c_sources": 0,
                    "unreachable_tracked_c_sources": 2,
                    "tracked_cupid_c_sources": 1,
                    "active_tracked_cupid_c_sources": 0,
                    "proven_cupidc_owned_tracked_cupid_c_sources": 0,
                    "unreachable_tracked_cupid_c_sources": 1,
                    "cupid_c_ownership_evidence": {},
                    "active": [
                        {
                            "path": "active.c",
                            "build_owners": ["host_c_compiler"],
                            "runtime_owner": None,
                        }
                    ],
                    "unreachable": [
                        {
                            "path": "copy.c",
                            "classification": "exact_duplicate",
                        },
                        {
                            "path": "unused.c",
                            "classification": "not_reached",
                        },
                    ],
                    "unreachable_cupid_c": [
                        {
                            "path": "filtered.cc",
                            "classification": "explicitly_excluded",
                            "policy": "make_filter_out",
                        }
                    ],
                },
            )
            self.assertEqual(audit["summary"]["active_sources"], 1)
            self.assertEqual(audit["summary"]["unreachable_sources"], 4)

    def test_inventory_records_known_historical_source_relationships(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                PYTHON := python3
                PRODUCTION_SEED_MANIFEST := seed.json
                CUPIDC_KERNEL_COMPILE := $(PYTHON) \
                    tools/cupidc_kernel_compile.py --root . \
                    --manifest $(PRODUCTION_SEED_MANIFEST)

                .PHONY: all
                all: kernel/lang/cupidc.o

                kernel/lang/cupidc.o: kernel/lang/cupidc.cc
                \t$(CUPIDC_KERNEL_COMPILE) --profile kernel $< $@
                """,
            )
            _write(
                root / "kernel" / "lang" / "cupidc.cc",
                "int current_compiler(void) { return 2; }\n",
            )
            _write(
                root / "bin" / "cupidc.c",
                "int historical_compiler(void) { return 1; }\n",
            )
            for relative in (
                "tools/cupidc_kernel_compile.py",
                "tools/cupidc_kernel_inputs.py",
                "tools/cupidc_i386_object.py",
                "tools/kernel_cupidc_frontier.py",
                "tools/bootstrap_toolchain.py",
                "bootstrap/seeds/i386-windows/manifest.json",
            ):
                _write(root / relative, "fixture\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            historical = {
                entry["path"]: entry for entry in audit["unreachable_sources"]
            }["bin/cupidc.c"]
            self.assertEqual(historical["classification"], "historical_copy")
            self.assertEqual(
                historical["relations"],
                [
                    {
                        "kind": "historical_copy_of",
                        "path": "kernel/lang/cupidc.cc",
                        "evidence": "audited project source relationship",
                    }
                ],
            )

    def test_cupidc_owned_tracked_c_source_requires_cupid_c_suffix(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                PYTHON := python3
                PRODUCTION_SEED_MANIFEST := seed.json
                CUPIDC_KERNEL_COMPILE := $(PYTHON) \
                    tools/cupidc_kernel_compile.py --root . \
                    --manifest $(PRODUCTION_SEED_MANIFEST)

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CUPIDC_KERNEL_COMPILE) --profile kernel $< $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 2, result.stdout + result.stderr
            )
            self.assertIn(
                "CupidC-owned tracked .c source must use .cc: main.c",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_host_owned_tracked_cc_cannot_claim_cupidc_from_suffix(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.cc
                \t$(CC) -x c -c $< -o $@
                """,
            )
            _write(root / "main.cc", "int main(void) { return 0; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 2, result.stdout + result.stderr
            )
            self.assertIn(
                "tracked .cc source lacks independent CupidC ownership "
                "evidence: main.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_unreachable_tracked_cc_requires_explicit_policy(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(root / "orphan.cc", "int orphan(void) { return 0; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 2, result.stdout + result.stderr
            )
            self.assertIn(
                "unreachable tracked .cc source lacks explicit ownership "
                "policy: orphan.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_residual_c_policy_cannot_follow_a_suffix_only_rename(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(root / "retired.cc", "int retired(void) { return 0; }\n")
            _write_source_suffix_policy(
                root,
                residual_c_sources={
                    "main.c": "active_host",
                    "retired.c": "not_reached",
                },
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 2, result.stdout + result.stderr
            )
            self.assertIn(
                "source suffix ownership policy path is missing: retired.c",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_inventory_classifies_residual_host_and_dormant_c_sources(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: active.o

                active.o: active.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "active.c", "int active(void) { return 0; }\n")
            _write(
                root / "kernel" / "lang" / "cupidc_runtime.c",
                "int dormant(void) { return 0; }\n",
            )
            _write(
                root / "tests" / "usb_reconciliation_runtime.c",
                "int host_fixture(void) { return 0; }\n",
            )
            _write(
                root / "toolchain" / "tests" / "elf32_oracle.c",
                "int host_oracle(void) { return 0; }\n",
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            audit = json.loads(output.read_text(encoding="utf-8"))
            unreachable = {
                source["path"]: source["classification"]
                for source in audit["unreachable_sources"]
            }
            self.assertEqual(
                unreachable,
                {
                    "kernel/lang/cupidc_runtime.c": "dormant",
                    "tests/usb_reconciliation_runtime.c": "host_fixture",
                    "toolchain/tests/elf32_oracle.c": "host_oracle",
                },
            )

    def test_cupidc_owned_tracked_cc_source_passes_suffix_contract(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                PYTHON := python3
                PRODUCTION_SEED_MANIFEST := seed.json
                CUPIDC_KERNEL_COMPILE := $(PYTHON) \
                    tools/cupidc_kernel_compile.py --root . \
                    --manifest $(PRODUCTION_SEED_MANIFEST)

                .PHONY: all
                all: main.o

                main.o: main.cc
                \t$(CUPIDC_KERNEL_COMPILE) --profile kernel $< $@
                """,
            )
            _write(root / "main.cc", "int main(void) { return 0; }\n")
            for relative in (
                "tools/cupidc_kernel_compile.py",
                "tools/cupidc_kernel_inputs.py",
                "tools/cupidc_i386_object.py",
                "tools/kernel_cupidc_frontier.py",
                "tools/bootstrap_toolchain.py",
                "bootstrap/seeds/i386-windows/manifest.json",
            ):
                _write(root / relative, "fixture\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            audit = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                audit["contracts"]["c_source_ownership"],
                {
                    "status": "pass",
                    "policy": {
                        "path": None,
                        "sha256": None,
                        "residual_c_sources": 0,
                        "runtime_delivery_sources": 0,
                        "unreachable_cupid_c_sources": 0,
                    },
                    "tracked_c_sources": 0,
                    "active_tracked_c_sources": 0,
                    "cupidc_owned_tracked_c_sources": 0,
                    "unreachable_tracked_c_sources": 0,
                    "tracked_cupid_c_sources": 1,
                    "active_tracked_cupid_c_sources": 1,
                    "proven_cupidc_owned_tracked_cupid_c_sources": 1,
                    "unreachable_tracked_cupid_c_sources": 0,
                    "cupid_c_ownership_evidence": {
                        "checked_cupidc_compile": 1,
                    },
                    "active": [],
                    "unreachable": [],
                    "unreachable_cupid_c": [],
                },
            )
            source = {item["path"]: item for item in audit["sources"]}[
                "main.cc"
            ]
            self.assertEqual(source["runtime_owner"], "CupidC")

    def test_policy_can_authorize_a_cupidobj_delivered_runtime_source(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CUPIDOBJ = seed/cupidbuild.exe run \
                    --seed-manifest seed/manifest.json --root . \
                    --tool cupidobj --

                .PHONY: all
                all: program.o

                program.o: program.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@
                """,
            )
            _write(root / "program.cc", "U0 Main() {}\n")
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=["program.cc"],
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            audit = json.loads(output.read_text(encoding="utf-8"))
            source = {item["path"]: item for item in audit["sources"]}[
                "program.cc"
            ]
            self.assertEqual(source["runtime_owner"], "CupidC")
            self.assertEqual(
                audit["contracts"]["c_source_ownership"][
                    "cupid_c_ownership_evidence"
                ],
                {"explicit_runtime_delivery_policy": 1},
            )
            self.assertEqual(
                audit["contracts"]["c_source_ownership"]["policy"][
                    "runtime_delivery_sources"
                ],
                1,
            )

    def test_runtime_delivery_policy_cannot_authorize_a_host_compile(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.cc
                \t$(CC) -x c -c $< -o $@
                """,
            )
            _write(root / "main.cc", "int main(void) { return 0; }\n")
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=["main.cc"],
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 2, result.stdout + result.stderr
            )
            self.assertIn(
                "runtime delivery policy lacks the exact CupidBuild/CupidObj "
                "ownership edge: main.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_runtime_delivery_policy_rejects_an_extra_host_owner(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CUPIDOBJ = seed/cupidbuild.exe run \
                    --seed-manifest seed/manifest.json --root . \
                    --tool cupidobj --
                OBJCOPY = objcopy

                .PHONY: all
                all: program.o

                program.o: program.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@
                \t$(OBJCOPY) --version
                """,
            )
            _write(root / "program.cc", "U0 Main() {}\n")
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=["program.cc"],
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 2, result.stdout + result.stderr
            )
            self.assertIn(
                "runtime delivery policy lacks the exact CupidBuild/CupidObj "
                "ownership edge: program.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_source_suffix_policy_rejects_invalid_document_shapes(self):
        valid = {
            "schema": "cupid.c-source-suffix-ownership.v1",
            "residual_c_sources": {},
            "runtime_delivery_sources": [],
            "unreachable_cupid_c_sources": {},
        }
        duplicate_key = """
            {
              "schema": "cupid.c-source-suffix-ownership.v1",
              "residual_c_sources": {
                "main.c": "active_host",
                "main.c": "dormant"
              },
              "runtime_delivery_sources": [],
              "unreachable_cupid_c_sources": {}
            }
        """
        cases = (
            (
                "duplicate key",
                duplicate_key,
                "source suffix ownership policy repeats key: main.c",
            ),
            (
                "unknown field",
                {**valid, "comment": "not part of the schema"},
                "source suffix ownership policy fields differ",
            ),
            (
                "wrong schema",
                {**valid, "schema": "cupid.c-source-suffix-ownership.v2"},
                "source suffix ownership policy schema differs",
            ),
            (
                "non-object root",
                [],
                "source suffix ownership policy root must be an object",
            ),
            (
                "residual type",
                {**valid, "residual_c_sources": []},
                "residual_c_sources must be a string map",
            ),
            (
                "delivery type",
                {**valid, "runtime_delivery_sources": {}},
                "runtime_delivery_sources must be a string list",
            ),
            (
                "unreachable type",
                {**valid, "unreachable_cupid_c_sources": []},
                "unreachable_cupid_c_sources must be a string map",
            ),
        )
        for name, document, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(root / "Makefile", ".PHONY: all\nall:\n")
                policy = (
                    root
                    / "docs"
                    / "bootstrap"
                    / ("c-source-suffix-ownership.json")
                )
                serialized = (
                    document
                    if isinstance(document, str)
                    else json.dumps(document, indent=2) + "\n"
                )
                _write(policy, serialized)

                output = root / "audit.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)
                self.assertFalse(output.exists())

    def test_source_suffix_policy_rejects_invalid_path_inventories(self):
        schema = "cupid.c-source-suffix-ownership.v1"

        def policy(*, residual=None, deliveries=None, unreachable=None):
            return {
                "schema": schema,
                "residual_c_sources": residual or {},
                "runtime_delivery_sources": deliveries or [],
                "unreachable_cupid_c_sources": unreachable or {},
            }

        cases = (
            (
                "residual suffix",
                policy(residual={"main.cc": "active_host"}),
                "residual C path is invalid: 'main.cc'",
            ),
            (
                "delivery suffix",
                policy(deliveries=["program.c"]),
                "runtime delivery path is invalid: 'program.c'",
            ),
            (
                "unreachable suffix",
                policy(unreachable={"orphan.c": "not_reached"}),
                "unreachable Cupid C path is invalid: 'orphan.c'",
            ),
            (
                "parent traversal",
                policy(residual={"../escape.c": "dormant"}),
                "residual C path is invalid: '../escape.c'",
            ),
            (
                "drive-qualified path",
                policy(residual={"C:/escape.c": "dormant"}),
                "residual C path is invalid: 'C:/escape.c'",
            ),
            (
                "residual order",
                policy(
                    residual={
                        "z.c": "dormant",
                        "a.c": "dormant",
                    }
                ),
                "residual_c_sources must be path-sorted",
            ),
            (
                "delivery order",
                policy(deliveries=["z.cc", "a.cc"]),
                "runtime_delivery_sources must be a unique path-sorted list",
            ),
            (
                "duplicate delivery",
                policy(deliveries=["a.cc", "a.cc"]),
                "runtime_delivery_sources must be a unique path-sorted list",
            ),
            (
                "unreachable order",
                policy(
                    unreachable={
                        "z.cc": "not_reached",
                        "a.cc": "not_reached",
                    }
                ),
                "unreachable_cupid_c_sources must be path-sorted",
            ),
            (
                "residual classification",
                policy(residual={"main.c": "future"}),
                "residual C role is invalid: main.c: future",
            ),
            (
                "unreachable classification",
                policy(unreachable={"orphan.cc": "future"}),
                "unreachable Cupid C classification is invalid: orphan.cc: future",
            ),
        )
        for name, document, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(root / "Makefile", ".PHONY: all\nall:\n")
                _write(
                    root
                    / "docs"
                    / "bootstrap"
                    / "c-source-suffix-ownership.json",
                    json.dumps(document, indent=2) + "\n",
                )

                output = root / "audit.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)
                self.assertFalse(output.exists())

    def test_source_suffix_policy_rejects_classification_drift(self):
        cases = (
            (
                "residual C",
                "unused.c",
                {
                    "residual_c_sources": {"unused.c": "dormant"},
                },
                "residual C classification differs: unused.c: "
                "expected=dormant, actual=not_reached",
            ),
            (
                "unreachable Cupid C",
                "orphan.cc",
                {
                    "unreachable_cupid_c_sources": {
                        "orphan.cc": "dormant",
                    },
                },
                "unreachable Cupid C classification differs: orphan.cc: "
                "expected=dormant, actual=not_reached",
            ),
        )
        for name, source, overrides, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(root / "Makefile", ".PHONY: all\nall:\n")
                _write(root / source, "int unused(void) { return 0; }\n")
                _write_source_suffix_policy(root, **overrides)

                output = root / "audit.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)
                self.assertFalse(output.exists())

    def test_source_suffix_policy_rejects_active_unreachable_overlap(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(root / "Makefile", ".PHONY: all\nall:\n")
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=["program.cc"],
                unreachable_cupid_c_sources={"program.cc": "not_reached"},
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(
                result.returncode, 2, result.stdout + result.stderr
            )
            self.assertIn(
                "policy assigns active and unreachable roles to the same "
                "path: program.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_inventory_resolves_declared_and_assembly_include_edges(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ASM = nasm
                CFLAGS = -Iinclude -include forced.h

                .PHONY: all
                all: main.o entry.o

                main.o: main.c
                \t$(CC) $(CFLAGS) -c $< -o $@

                entry.o: entry.asm
                \t$(ASM) -f elf32 $< -o $@
                """,
            )
            _write(
                root / "main.c",
                """
                /*
                #include "ignored.h"
                */
                #include <api.h>
                int value;
                """,
            )
            _write(root / "include" / "api.h", '%:include "types.h"\n')
            _write(root / "include" / "types.h", "typedef int word;\n")
            _write(root / "ignored.h", "#define IGNORED 1\n")
            _write(root / "forced.h", "#define FORCED 1\n")
            _write(root / "entry.asm", '%include "helper.asm"\nret\n')
            _write(root / "helper.asm", "%define VALUE 1\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                set(sources),
                {
                    "entry.asm",
                    "forced.h",
                    "helper.asm",
                    "include/api.h",
                    "include/types.h",
                    "main.c",
                },
            )
            self.assertEqual(sources["main.c"]["includes"], ["include/api.h"])
            self.assertEqual(
                sources["include/api.h"]["includes"], ["include/types.h"]
            )
            self.assertEqual(sources["entry.asm"]["includes"], ["helper.asm"])
            self.assertEqual(
                sources["forced.h"]["reachability"], "forced_include"
            )
            self.assertEqual(
                audit["build"]["include_search_paths"], ["include"]
            )

    def test_inventory_contracts_direct_c_include_operand_forms(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                CFLAGS = -Iinclude

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) $(CFLAGS) -c $< -o $@
                """,
            )
            _write(
                root / "main.c",
                """
                # include "local.h" // trailing comment
                %:include <angle.h>
                #inc\\
                lude "spliced.h"
                #include /* operand comment */ "commented.h"
                int value;
                """,
            )
            _write(root / "local.h", "int local_value;\n")
            _write(root / "spliced.h", "int spliced_value;\n")
            _write(root / "commented.h", "int commented_value;\n")
            _write(root / "include" / "angle.h", "int angle_value;\n")

            output = root / "audit.json"
            summary = root / "audit.md"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                    "--summary",
                    str(summary),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                audit["contracts"]["c_preprocessor_include_operands"],
                {
                    "status": "pass",
                    "source_files": 5,
                    "include_occurrences": 4,
                    "direct_quoted_occurrences": 3,
                    "direct_angle_occurrences": 1,
                    "pp_token_operand_occurrences": 0,
                    "ordinary_marker_occurrences": 3,
                    "digraph_marker_occurrences": 1,
                    "max_conditional_depth": 0,
                },
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                sources["main.c"]["includes"],
                ["commented.h", "include/angle.h", "local.h", "spliced.h"],
            )
            self.assertIn(
                "4 C include operands (3 quoted, 1 angle, 0 pp-token); "
                "5 source files; max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_include_closure_fails_closed_on_pp_token_operands(self):
        cases = {
            "object": {
                "flags": "",
                "source": """
                    #define HEADER "x.h"
                    #if FEATURE
                    #include /* bridge */ HEADER
                    #endif
                """,
                "line": 3,
                "marker": "#",
                "raw": "#include /* bridge */ HEADER",
                "normalized": "HEADER",
                "conditional": "#if FEATURE at line 2",
            },
            "function": {
                "flags": "",
                "source": """
                    #define PICK(value) value
                    %:include PICK("x.h")
                """,
                "line": 2,
                "marker": "%:",
                "raw": '%:include PICK("x.h")',
                "normalized": 'PICK ( "x.h" )',
                "conditional": "<unconditional>",
            },
            "configured": {
                "flags": '-DCONFIG_HEADER=\\"x.h\\"',
                "source": "#include CONFIG_HEADER\n",
                "line": 1,
                "marker": "#",
                "raw": "#include CONFIG_HEADER",
                "normalized": "CONFIG_HEADER",
                "conditional": "<unconditional>",
            },
            "forced": {
                "flags": "-include forced.h",
                "source": "#include FORCED_HEADER\n",
                "line": 1,
                "marker": "#",
                "raw": "#include FORCED_HEADER",
                "normalized": "FORCED_HEADER",
                "conditional": "<unconditional>",
            },
        }
        for name, case in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    f"""
                    .SUFFIXES:
                    CC = host-cc
                    CFLAGS = {case["flags"]}

                    .PHONY: all
                    all: main.o

                    main.o: main.c
                    \t$(CC) $(CFLAGS) -c $< -o $@
                    """,
                )
                _write(root / "main.c", case["source"])
                if name == "forced":
                    _write(
                        root / "forced.h",
                        '#define FORCED_HEADER "x.h"\n',
                    )

                output = root / "audit.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
                self.assertIn(f"main.c:{case['line']}", result.stderr)
                self.assertIn("macro-expanded #include operand", result.stderr)
                self.assertIn(f"marker={case['marker']!r}", result.stderr)
                self.assertIn(f"raw={case['raw']!r}", result.stderr)
                self.assertIn(
                    f"normalized={case['normalized']!r}", result.stderr
                )
                self.assertIn(
                    f"conditional={case['conditional']!r}", result.stderr
                )

    def test_checked_include_operand_contract_matches_active_sources(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--supplemental-build",
                    "user:all",
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            generated = json.loads(output.read_text(encoding="utf-8"))
            checked = json.loads(
                ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8")
            )
            contract = generated["contracts"][
                "c_preprocessor_include_operands"
            ]
            self.assertEqual(
                checked["contracts"]["c_preprocessor_include_operands"],
                contract,
            )
            self.assertEqual(contract["source_files"], 773)
            self.assertEqual(contract["include_occurrences"], 2677)
            self.assertEqual(contract["direct_quoted_occurrences"], 2329)
            self.assertEqual(contract["direct_angle_occurrences"], 348)
            self.assertEqual(contract["pp_token_operand_occurrences"], 0)

    def test_inventory_detects_link_inputs_missing_from_artifact_manifest(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            artifact_recipe = _json_list_recipe(["main.o"])
            _write(
                root / "Makefile",
                f"""
                .SUFFIXES:
                CC = host-cc
                LD = host-ld
                OBJECTS = main.o
                ARTIFACTS := $(OBJECTS)
                OBJECTS += late.o

                .PHONY: all print-bootstrap-artifacts
                all: kernel.elf

                kernel.elf: $(OBJECTS)
                \t$(LD) -o $@ $^

                main.o: main.c
                \t$(CC) -c $< -o $@

                late.o: late.c
                \t$(CC) -c $< -o $@

                print-bootstrap-artifacts:
                {artifact_recipe}
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(root / "late.c", "int late(void) { return 1; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                audit["contracts"]["bootstrap_artifact_coverage"],
                {
                    "status": "fail",
                    "declared_artifacts": 1,
                    "linked_objects": 2,
                    "missing_link_inputs": ["late.o"],
                },
            )

    def test_checked_json_and_markdown_fail_when_sources_drift(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            output = root / "audit.json"
            summary = root / "AUDIT.md"
            command = [
                sys.executable,
                str(AUDIT_TOOL),
                "--root",
                str(root),
                "--output",
                str(output),
                "--summary",
                str(summary),
            ]

            generated = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertIn(
                "# Active build and source audit", summary.read_text()
            )

            checked = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)

            _write(root / "main.c", "int main(void) { return 1; }\n")
            stale = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(stale.returncode, 1)
            self.assertIn("out of date", stale.stderr)
            self.assertIn("audit.json", stale.stderr)

    def test_inventory_is_stable_when_generated_c_has_not_been_materialized(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                PYTHON = python

                .PHONY: all
                all: generated.o

                generated.c: input.txt
                \t$(PYTHON) generator.py $< $@

                generated.o: generated.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "input.txt", "payload\n")
            _write(root / "generator.py", "# fixture generator\n")
            _write(root / "ignored.h", "#define DIRTY_GENERATOR_EDGE 1\n")

            absent_output = root / "absent.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(absent_output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            _write(
                root / "generated.c",
                '#line 77 "generated-template.c"\n'
                '#include "ignored.h"\nint generated;\n',
            )
            materialized_output = root / "materialized.json"
            materialized = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(materialized_output),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(materialized.returncode, 0, materialized.stderr)
            self.assertEqual(
                absent_output.read_bytes(), materialized_output.read_bytes()
            )

            audit = json.loads(absent_output.read_text(encoding="utf-8"))
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(sources["generated.c"]["origin"], "generated")
            self.assertIsNone(sources["generated.c"]["sha256"])
            self.assertEqual(sources["generated.c"]["includes"], [])
            self.assertEqual(
                sources["generated.c"]["features"],
                ["c.output.elf32_relocatable"],
            )
            self.assertEqual(
                audit["contracts"]["c_preprocessor_line_directives"],
                {
                    "status": "pass",
                    "source_files": 0,
                    "named_line_occurrences": 0,
                    "direct_line_occurrences": 0,
                    "pp_token_line_occurrences": 0,
                    "filename_occurrences": 0,
                    "ordinary_marker_occurrences": 0,
                    "digraph_marker_occurrences": 0,
                    "numeric_marker_occurrences": 0,
                    "max_conditional_depth": 0,
                    "forms": [],
                },
            )

    def test_inventory_includes_an_explicit_supplemental_build(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: kernel.o
                kernel.o: kernel.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "kernel.c", "int kernel(void) { return 0; }\n")
            _write(
                root / "user" / "Makefile",
                """
                .SUFFIXES:
                CC = gcc
                LD = ld
                .PHONY: all
                all: build build/tool
                build:
                \tmkdir -p build
                build/tool: build/tool.o build/shared.o
                \t$(LD) -o $@ $^
                build/tool.o: examples/tool.c cupid.h
                \t$(CC) -I. -c $< -o $@
                build/shared.o: ../shared.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "user" / "examples" / "tool.c",
                '#include "cupid.h"\nint tool(void) { return CUPID; }\n',
            )
            _write(root / "user" / "cupid.h", "#define CUPID 1\n")
            _write(root / "shared.c", "int shared(void) { return 1; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--supplemental-build",
                    "user:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn("user/examples/tool.c", sources)
            self.assertIn("user/cupid.h", sources)
            self.assertIn("shared.c", sources)
            self.assertNotIn("user/../shared.c", sources)
            self.assertEqual(
                sources["user/examples/tool.c"]["cohort"], "user_program"
            )
            self.assertEqual(
                audit["supplemental_builds"][0]["directory"], "user"
            )
            transforms = {
                entry["output"]: entry
                for entry in audit["supplemental_builds"][0]["transforms"]
            }
            self.assertEqual(
                transforms["user/build/tool.o"]["tools"], ["host_c_compiler"]
            )
            self.assertEqual(
                transforms["user/build/shared.o"]["inputs"], ["shared.c"]
            )
            self.assertEqual(
                transforms["user/build/tool"]["tools"], ["host_linker"]
            )
            self.assertEqual(transforms["user/build"]["tools"], ["host_shell"])

    def test_inventory_distinguishes_c_objects_from_host_executables(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                CFLAGS = -c
                .PHONY: all
                all: module.o
                module.o: module.c
                \t$(CC) $(CFLAGS) $< -o $@
                """,
            )
            _write(root / "module.c", "int module(void) { return 0; }\n")
            _write(
                root / "toolchain" / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: build/contract
                build/contract: build/contract.o
                \t$(CC) $< -o $@
                build/contract.o: contract.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "toolchain" / "contract.c",
                "int main(void) { return 0; }\n",
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            root_transforms = {
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
            }
            host_transforms = {
                entry["output"]: entry
                for entry in audit["supplemental_builds"][0]["transforms"]
            }
            self.assertEqual(
                root_transforms["module.o"]["operation"],
                "compile_c_to_elf32_object",
            )
            self.assertEqual(
                host_transforms["toolchain/build/contract"]["operation"],
                "compile_and_link_host_executable",
            )
            self.assertEqual(
                host_transforms["toolchain/build/contract.o"]["operation"],
                "compile_c_to_host_object",
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn(
                "c.output.elf32_relocatable", sources["module.c"]["features"]
            )
            self.assertNotIn(
                "c.output.elf32_relocatable",
                sources["toolchain/contract.c"]["features"],
            )

    def test_inventory_records_the_i386_abi_and_linker_script_subset(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                LD = host-ld
                CFLAGS = -m32 -ffreestanding
                LDFLAGS = -m elf_i386 -T link.ld
                .PHONY: all
                all: kernel.elf
                kernel.o: kernel.c
                \t$(CC) $(CFLAGS) -c $< -o $@
                kernel.elf: kernel.o
                \t$(LD) $(LDFLAGS) -o $@ $^
                """,
            )
            _write(root / "kernel.c", "int main(void) { return 0; }\n")
            _write(
                root / "link.ld",
                """
                ENTRY(main)
                SECTIONS {
                    . = 1M;
                    .text ALIGN(16) : { *(.text*) }
                    .bss : { *(COMMON) }
                    end = .;
                    ASSERT(end < 2M, "too large")
                }
                """,
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            abi = json.loads(output.read_text(encoding="utf-8"))["abi"]
            self.assertEqual(abi["architecture"], "i386")
            self.assertEqual(abi["data_model"], "ILP32")
            self.assertEqual(
                abi["required_relocations"], ["R_386_32", "R_386_PC32"]
            )
            self.assertEqual(
                abi["linker_script"]["features"],
                [
                    "ALIGN",
                    "ASSERT",
                    "COMMON",
                    "ENTRY",
                    "SECTIONS",
                    "input_section_wildcards",
                    "location_counter",
                    "symbol_definitions",
                ],
            )

    def test_inventory_hashes_use_the_canonical_lf_checkout_policy(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: main.o
                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            source = root / "main.c"
            source.write_bytes(b"int main(void) {\r\n    return 0;\r\n}\r\n")
            first = root / "first.json"
            second = root / "second.json"

            first_result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(first),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(first_result.returncode, 0, first_result.stderr)

            source.write_bytes(b"int main(void) {\n    return 0;\n}\n")
            second_result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(second),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(second_result.returncode, 0, second_result.stderr)
            self.assertEqual(first.read_bytes(), second.read_bytes())
