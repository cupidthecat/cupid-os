import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
AUDIT_TOOL = REPO_ROOT / "tools" / "build_graph_audit.py"


def _write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")


def _json_list_recipe(values):
    python = Path(sys.executable).resolve().as_posix()
    return (
        f'\t@"{python}" -c "import json; '
        f"print(json.dumps({list(values)!r}))\""
    )


class BuildGraphAuditCliTests(unittest.TestCase):
    def test_inventory_attributes_assembly_outputs_to_cupidasm(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CUPIDASM = cupidasm

                .PHONY: all
                all: boot.bin entry.o

                boot.bin: boot.asm
                \t$(CUPIDASM) -f bin $< -o $@

                entry.o: entry.asm
                \t$(CUPIDASM) -f elf32 $< -o $@
                """,
            )
            _write(root / "boot.asm", "bits 16\norg 0x7c00\nhlt\n")
            _write(root / "entry.asm", "bits 32\nret\n")

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
            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            self.assertEqual(
                transforms["boot.bin"]["tools"], ["cupid_assembler"]
            )
            self.assertEqual(
                transforms["boot.bin"]["operation"], "assemble_flat_binary"
            )
            self.assertEqual(
                transforms["entry.o"]["tools"], ["cupid_assembler"]
            )
            self.assertEqual(
                transforms["entry.o"]["operation"], "assemble_elf32_relocatable"
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(sources["boot.asm"]["runtime_owner"], "CupidASM")
            self.assertEqual(sources["entry.asm"]["runtime_owner"], "CupidASM")

    def test_inventory_maps_reachable_language_inputs_to_tool_owned_outputs(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ASM = nasm
                LD = host-ld
                OBJCOPY = host-objcopy

                .PHONY: all
                all: kernel.elf app.o demo.o

                kernel.elf: main.o boot.bin
                \t$(LD) -o $@ $^

                main.o: main.c api.h
                \t$(CC) -c $< -o $@

                boot.bin: boot.asm
                \t$(ASM) -f bin $< -o $@

                app.o: app.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                demo.o: demo.asm
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                """,
            )
            _write(
                root / "main.c",
                """
                #include "api.h"
                int main(void) { return answer(); }
                """,
            )
            _write(
                root / "api.h",
                """
                #include "types.h"
                static inline word answer(void) { return 42; }
                """,
            )
            _write(root / "types.h", "typedef int word;\n")
            _write(root / "app.cc", "U0 Main() {}\n")
            _write(root / "boot.asm", "bits 16\norg 0x7c00\nhlt\n")
            _write(root / "demo.asm", "bits 32\nmov eax, 1\nret\n")

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
            self.assertEqual(audit["schema"], "cupid.build-graph-audit.v1")
            self.assertEqual(audit["build"]["root_target"], "all")
            self.assertEqual(
                audit["provenance"]["control_files"][0]["path"], "Makefile"
            )

            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                set(sources),
                {"api.h", "app.cc", "boot.asm", "demo.asm", "main.c", "types.h"},
            )
            self.assertEqual(sources["main.c"]["language"], "c")
            self.assertEqual(sources["app.cc"]["language"], "cupid_c")
            self.assertEqual(sources["boot.asm"]["language"], "assembly")
            self.assertEqual(sources["types.h"]["reachability"], "transitive_include")
            self.assertEqual(sources["app.cc"]["runtime_owner"], "CupidC")
            self.assertEqual(sources["demo.asm"]["runtime_owner"], "CupidASM")
            self.assertIsNone(sources["boot.asm"]["runtime_owner"])

            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            self.assertEqual(transforms["main.o"]["tools"], ["host_c_compiler"])
            self.assertEqual(transforms["boot.bin"]["tools"], ["nasm"])
            self.assertEqual(
                transforms["boot.bin"]["operation"], "assemble_flat_binary"
            )
            self.assertEqual(
                transforms["kernel.elf"]["operation"], "link_elf32_executable"
            )
            self.assertEqual(transforms["app.o"]["tools"], ["host_object_copy"])
            self.assertEqual(transforms["demo.o"]["tools"], ["host_object_copy"])
            self.assertEqual(
                transforms["kernel.elf"]["inputs"], ["main.o", "boot.bin"]
            )
            self.assertEqual(
                audit["roadmap"]["capability_priorities"][0]["id"],
                "elf32_relocatable_interchange",
            )

    def test_inventory_attributes_transforms_to_cupid_linker_and_object(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            artifact_recipe = _json_list_recipe(["main.o"])
            _write(
                root / "Makefile",
                f"""
                .SUFFIXES:
                CC = host-cc
                CUPIDLD = cupidld
                CUPIDOBJ = cupidobj

                .PHONY: all print-bootstrap-artifacts
                all: kernel.elf kernel.bin app.o

                kernel.elf: main.o link.ld
                \t$(CUPIDLD) -m elf_i386 -T link.ld -o $@ main.o

                kernel.bin: kernel.elf
                \t$(CUPIDOBJ) flat $< -o $@

                app.o: app.cc
                \t$(CUPIDOBJ) wrap $< -o $@

                main.o: main.c
                \t$(CC) -c $< -o $@

                print-bootstrap-artifacts:
                {artifact_recipe}
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(root / "app.cc", "U0 Main() {}\n")
            _write(
                root / "link.ld",
                "ENTRY(main)\nSECTIONS { . = 0x100000; .text : { *(.text) } }\n",
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
            audit = json.loads(output.read_text(encoding="utf-8"))
            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            self.assertEqual(transforms["kernel.elf"]["tools"], ["cupid_linker"])
            self.assertEqual(
                transforms["kernel.elf"]["operation"], "link_elf32_executable"
            )
            self.assertEqual(transforms["kernel.bin"]["tools"], ["cupid_object"])
            self.assertEqual(
                transforms["kernel.bin"]["operation"], "extract_raw_binary"
            )
            self.assertEqual(transforms["app.o"]["tools"], ["cupid_object"])
            self.assertEqual(
                transforms["app.o"]["operation"],
                "wrap_binary_as_elf32_relocatable",
            )
            self.assertEqual(
                audit["contracts"]["bootstrap_artifact_coverage"]["linked_objects"],
                1,
            )

    def test_make_database_does_not_execute_recursive_recipes(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: main.o hosted-tool
                main.o: main.c
                \t$(CC) -c $< -o $@
                hosted-tool:
                \t$(MAKE) -C child all
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(
                root / "child" / "Makefile",
                """
                .SUFFIXES:
                CC = child-cc
                all: child.o
                child.o: child.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "child" / "child.c", "int child(void) { return 1; }\n")

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
            self.assertEqual(audit["build"]["root_target"], "all")
            self.assertIn(
                "main.o",
                {entry["output"] for entry in audit["build"]["transforms"]},
            )
            self.assertEqual(
                [entry["path"] for entry in audit["sources"]], ["main.c"]
            )

    def test_inventory_reports_source_features_with_stable_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ASM = nasm
                OBJCOPY = host-objcopy

                .PHONY: all
                all: feature.o app.o entry.o

                feature.o: feature.c
                \t$(CC) -c $< -o $@

                app.o: app.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                entry.o: entry.asm
                \t$(ASM) -f elf32 $< -o $@
                """,
            )
            _write(
                root / "feature.c",
                """
                #define TRACE(fmt, ...) log(fmt, __VA_ARGS__)
                #define JOIN(a, b) a ## b
                #define NAME(value) #value
                #define DEBUG(fmt, ...) log(fmt, ##__VA_ARGS__)
                #pragma pack(push, 1)
                struct __attribute__((packed)) packet {
                    unsigned kind : 3;
                    int values[];
                };
                static long long wide_value;
                static void (*handler)(int);
                static void noop(void) {}
                static void (*factory(void))(void) { return noop; }
                static int choose(int cond, int yes, int no) {
                    return cond ? yes : no;
                }
                int inspect(void) {
                    struct packet value = (struct packet){ .kind = 1 };
                    if (handler) { value.values[0] = 2; }
                    __asm__ volatile ("nop" ::: "memory");
                    return value.kind;
                }
                """,
            )
            _write(
                root / "app.cc",
                """
                #exe {
                class Widget {};
                U0 Main() {
                    I64 wide = 1;
                    reg U32 value = 1;
                    noreg U32 other = 2;
                    Widget *widget = new Widget;
                    del widget;
                    float4 lanes;
                    asm { nop }
                }
                """,
            )
            _write(
                root / "entry.asm",
                """
                [bits 16]
                BITS 32
                [org 0x7c00]
                ORG 0x8000
                section .text
                global start
                extern target
                %define COUNT 2
                start:
                    mov eax, [ebx + ecx*4 + 8]
                    rep movsd
                    call target
                table dd 1
                message db "[brackets in data are not an address]"
                    times COUNT db 0
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
            audit = json.loads(output.read_text(encoding="utf-8"))
            features = {entry["id"]: entry for entry in audit["features"]}
            expected = {
                "asm.addressing.base_index_scale",
                "asm.addressing.memory",
                "asm.directive.bits",
                "asm.directive.extern",
                "asm.directive.global",
                "asm.directive.org",
                "asm.directive.section",
                "asm.directive.times",
                "asm.instruction.call",
                "asm.instruction.mov",
                "asm.preprocessor.define",
                "asm.prefix.rep",
                "asm.relocation.pc_relative_external",
                "asm.output.elf32_relocatable",
                "asm.register.eax",
                "asm.register.ebx",
                "asm.register.ecx",
                "c.declarator.function_pointer",
                "c.declarator.unsized_array",
                "c.declarator.variadic",
                "c.extension.attribute.packed",
                "c.extension.inline_assembly",
                "c.initializer.designated",
                "c.preprocessor.function_macro",
                "c.preprocessor.gnu_variadic_comma_elision",
                "c.preprocessor.pragma.pack",
                "c.preprocessor.stringify",
                "c.preprocessor.token_paste",
                "c.preprocessor.variadic_macro",
                "c.type.long_long",
                "c.type.struct",
                "c.output.elf32_relocatable",
                "cupid_c.declaration.class",
                "cupid_c.delivery.embedded_source",
                "cupid_c.expression.del",
                "cupid_c.expression.new",
                "cupid_c.extension.asm_block",
                "cupid_c.storage.noreg",
                "cupid_c.storage.reg",
                "cupid_c.type.float4",
                "cupid_c.type.i64",
                "cupid_c.type.u0",
            }
            self.assertTrue(expected.issubset(features), expected - set(features))
            self.assertEqual(features["asm.addressing.memory"]["occurrences"], 1)
            self.assertEqual(features["asm.directive.bits"]["occurrences"], 2)
            self.assertEqual(features["asm.directive.org"]["occurrences"], 2)
            self.assertEqual(features["asm.instruction.call"]["occurrences"], 1)
            self.assertNotIn("asm.instruction.bits", features)
            self.assertNotIn("asm.instruction.org", features)
            self.assertNotIn("asm.instruction.table", features)
            self.assertEqual(features["c.expression.compound_literal"]["occurrences"], 1)
            self.assertEqual(features["c.declarator.bit_field"]["occurrences"], 1)
            self.assertEqual(features["c.initializer.designated"]["occurrences"], 1)
            self.assertIn("cupid_c.directive.exe", features)
            self.assertNotIn("c.preprocessor.exe", features)
            self.assertEqual(
                features["c.extension.attribute.packed"]["files"],
                ["feature.c"],
            )
            self.assertEqual(
                features["c.extension.attribute.packed"]["examples"][0]["line"],
                6,
            )

            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn(
                "cupid_c.type.u0",
                sources["app.cc"]["features"],
            )

    def test_active_assembly_controls_are_not_memory_operands(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            features = {entry["id"]: entry for entry in audit["features"]}
            self.assertEqual(features["asm.addressing.memory"]["occurrences"], 101)
            self.assertEqual(features["asm.directive.bits"]["occurrences"], 7)
            self.assertEqual(features["asm.directive.org"]["occurrences"], 2)
            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            expected_assembly = {
                "boot/boot.bin": "assemble_flat_binary",
                "kernel/core/context_switch.o": "assemble_elf32_relocatable",
                "kernel/cpu/isr.o": "assemble_elf32_relocatable",
                "kernel/smp_trampoline.bin": "assemble_flat_binary",
            }
            for output_path, operation in expected_assembly.items():
                self.assertEqual(
                    transforms[output_path]["tools"], ["cupid_assembler"]
                )
                self.assertEqual(transforms[output_path]["operation"], operation)
            self.assertFalse(
                [
                    entry["output"]
                    for entry in audit["build"]["transforms"]
                    if "nasm" in entry["tools"]
                ]
            )

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
            _write(root / "TempleOS" / "reference.c", "int reference;\n")

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
                unreachable["bin/build.cup"]["language"], "cupid_script"
            )
            self.assertEqual(
                unreachable["copy.c"]["classification"], "exact_duplicate"
            )
            self.assertEqual(unreachable["copy.c"]["duplicate_of"], ["active.c"])
            self.assertEqual(
                unreachable["filtered.cc"]["classification"],
                "explicitly_excluded",
            )
            self.assertEqual(
                unreachable["unused.c"]["classification"], "not_reached"
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
                CC = host-cc

                .PHONY: all
                all: kernel/lang/cupidc.o

                kernel/lang/cupidc.o: kernel/lang/cupidc.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "kernel" / "lang" / "cupidc.c",
                "int current_compiler(void) { return 2; }\n",
            )
            _write(
                root / "bin" / "cupidc.c",
                "int historical_compiler(void) { return 1; }\n",
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
                        "path": "kernel/lang/cupidc.c",
                        "evidence": "audited project source relationship",
                    }
                ],
            )

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
            _write(root / "include" / "api.h", "#include \"types.h\"\n")
            _write(root / "include" / "types.h", "typedef int word;\n")
            _write(root / "ignored.h", "#define IGNORED 1\n")
            _write(root / "forced.h", "#define FORCED 1\n")
            _write(root / "entry.asm", "%include \"helper.asm\"\nret\n")
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
            self.assertEqual(sources["forced.h"]["reachability"], "forced_include")
            self.assertEqual(
                audit["build"]["include_search_paths"], ["include"]
            )

    def test_inventory_detects_link_inputs_missing_from_artifact_manifest(self):
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
            self.assertIn("# Active build and source audit", summary.read_text())

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

    def test_inventory_is_stable_when_generated_c_has_not_been_materialized(self):
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
            _write(root / "generated.c", '#include "ignored.h"\nint generated;\n')
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
            self.assertEqual(absent_output.read_bytes(), materialized_output.read_bytes())

            audit = json.loads(absent_output.read_text(encoding="utf-8"))
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(sources["generated.c"]["origin"], "generated")
            self.assertIsNone(sources["generated.c"]["sha256"])
            self.assertEqual(sources["generated.c"]["includes"], [])
            self.assertEqual(
                sources["generated.c"]["features"],
                ["c.output.elf32_relocatable"],
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
                "#include \"cupid.h\"\nint tool(void) { return CUPID; }\n",
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
                entry["output"]: entry for entry in audit["build"]["transforms"]
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


if __name__ == "__main__":
    unittest.main()
