import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
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
CUPIDC_PP_CONTRACT = REPO_ROOT / "toolchain" / "tests" / "cupidc_pp_contract.c"
TOOLCHAIN_MAKEFILE = REPO_ROOT / "toolchain" / "Makefile"


def _write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")


def _json_list_recipe(values):
    python = Path(sys.executable).resolve().as_posix()
    return (
        f'\t@"{python}" -c "import json; '
        f"print(json.dumps({list(values)!r}))\""
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
            raise AssertionError(f"duplicate conditional manifest: {expression}")
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
                #define SPANNED( \\
                    left, \\
                    right) left ## \\
                    right
                #define TRACE(fmt, ...) log(fmt, __VA_ARGS__)
                #define JOIN(a, b) a ## b
                #define NAME(value) #value
                #define DEBUG(fmt, ...) log(fmt, ##__VA_ARGS__)
                #define STRINGIFIED(value) \\
                    #value
                #define GNU_MORE(fmt, ...) log(fmt, \\
                    ## __VA_ARGS__)
                %:define DIGRAPH_JOIN(a, b) a %:%: b
                %:define DIGRAPH_NAME(value) %: value
                %:define DIGRAPH_GNU(fmt, ...) log(fmt, %:%: __VA_ARGS__)
                #define ADJACENT_HASH(value) left ### value
                %:define ADJACENT_DIGRAPH(value) left %:%:%: value
                // phase-two splice keeps the next line in this comment \\
                #define NOT_REAL(value) value ## value
                %:pragma pack(push, 1)
                struct __attribute__((packed)) packet {
                    unsigned kind : 3;
                    int values[];
                };
                static const char blob[]
                    __attribute__((weak, section(".meta"),
                                   aligned(16)));
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
                "c.extension.attribute.aligned",
                "c.extension.attribute.section",
                "c.extension.attribute.weak",
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
            self.assertEqual(
                features["c.preprocessor.function_macro"]["occurrences"], 12
            )
            self.assertEqual(
                features["c.preprocessor.function_macro"]["examples"][0][
                    "line"
                ],
                1,
            )
            self.assertTrue(
                features["c.preprocessor.function_macro"]["examples"][0][
                    "text"
                ].startswith("#define SPANNED(")
            )
            self.assertEqual(
                features["c.preprocessor.variadic_macro"]["occurrences"], 4
            )
            self.assertEqual(
                features["c.preprocessor.token_paste"]["occurrences"], 8
            )
            self.assertEqual(
                features["c.preprocessor.stringify"]["occurrences"], 5
            )
            self.assertEqual(
                features["c.preprocessor.gnu_variadic_comma_elision"][
                    "occurrences"
                ],
                3,
            )
            self.assertEqual(
                features["c.preprocessor.define"]["occurrences"], 12
            )
            self.assertNotIn("c.preprocessor.value", features)
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
                21,
            )
            for attribute_name in ("aligned", "section", "weak"):
                feature = features[f"c.extension.attribute.{attribute_name}"]
                self.assertEqual(feature["occurrences"], 1)
                self.assertEqual(feature["files"], ["feature.c"])
                self.assertEqual(feature["examples"][0]["line"], 26)
                self.assertTrue(
                    feature["examples"][0]["text"].startswith("__attribute__")
                )

            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn(
                "cupid_c.type.u0",
                sources["app.cc"]["features"],
            )

    def test_inventory_distinguishes_gnu_and_c11_alignof_operators(self):
        module = _load_audit_module()
        collector = module.FeatureCollector()
        source = textwrap.dedent(
            """
            int gnu_plain = __alignof(int);
            int gnu_wrapped = __alignof__(int);
            int c11 = _Alignof(int);
            int __alignof_suffix = 0;
            const char *documentation = "__alignof __alignof__";
            /* __alignof(int) and __alignof__(int) are masked documentation. */
            """
        ).lstrip()

        module._scan_c_features("alignof.c", source, "c", collector)
        features = {entry["id"]: entry for entry in collector.inventory()}

        self.assertEqual(features["c.extension.gnu_alignof"]["occurrences"], 2)
        self.assertEqual(
            [
                example["line"]
                for example in features["c.extension.gnu_alignof"]["examples"]
            ],
            [1, 2],
        )
        self.assertEqual(features["c.expression.alignof"]["occurrences"], 1)

    def test_checked_attribute_inventory_matches_active_sources(self):
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        features = {entry["id"]: entry for entry in audit["features"]}
        expected = {
            "packed": 30,
            "aligned": 12,
            "noreturn": 3,
            "section": 2,
            "weak": 5,
            "used": 18,
            "noinline": 18,
            "unused": 5,
            "naked": 3,
            "target": 1,
        }
        attribute_files = set()
        for name, occurrences in expected.items():
            feature = features[f"c.extension.attribute.{name}"]
            self.assertEqual(feature["occurrences"], occurrences)
            attribute_files.update(feature["files"])
        self.assertEqual(sum(expected.values()), 97)
        self.assertEqual(len(attribute_files), 30)
        for name in ("aligned", "section", "weak"):
            self.assertIn(
                "kernel/cpu/ksyms.c",
                features[f"c.extension.attribute.{name}"]["files"],
            )

    def test_c_logical_lines_use_only_real_newlines_and_preserve_evidence(self):
        spec = importlib.util.spec_from_file_location(
            "_cupid_build_graph_audit_test", AUDIT_TOOL
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        try:
            spec.loader.exec_module(module)
        finally:
            del sys.modules[spec.name]

        lf = "#define JOIN( \\\n left) left\n"
        crlf = lf.replace("\n", "\r\n")
        self.assertEqual(
            module._c_logical_lines(lf), module._c_logical_lines(crlf)
        )
        self.assertEqual(
            module._c_logical_lines("#define TRAILING value\\"),
            [(1, "#define TRAILING value\\", "#define TRAILING value\\")],
        )
        controls = "#define ONLY 1\v#define NOT_SECOND 2\fstill_same"
        self.assertEqual(
            module._c_logical_lines(controls),
            [(1, controls, controls)],
        )

    def test_inventory_contracts_direct_c_line_directives(self):
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
            _write(root / "main.c", '#line 40 "virtual.c"\nint value;\n')

            output = root / "audit.json"
            summary = root / "AUDIT.md"
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
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_line_directives"]
            self.assertEqual(contract["status"], "pass")
            self.assertEqual(contract["source_files"], 1)
            self.assertEqual(contract["named_line_occurrences"], 1)
            self.assertEqual(contract["direct_line_occurrences"], 1)
            self.assertEqual(contract["pp_token_line_occurrences"], 0)
            self.assertEqual(contract["filename_occurrences"], 1)
            self.assertEqual(contract["ordinary_marker_occurrences"], 1)
            self.assertEqual(contract["digraph_marker_occurrences"], 0)
            self.assertEqual(contract["numeric_marker_occurrences"], 0)
            self.assertEqual(contract["max_conditional_depth"], 0)
            self.assertEqual(
                contract["forms"],
                [
                    {
                        "conditional_depth": 0,
                        "evidence": [
                            {
                                "line": 1,
                                "operand": '40 "virtual.c"',
                                "path": "main.c",
                                "text": '#line 40 "virtual.c"',
                            }
                        ],
                        "files": ["main.c"],
                        "form": "direct_decimal_filename",
                        "has_filename": True,
                        "marker": "#",
                        "occurrences": 1,
                    }
                ],
            )
            self.assertIn(
                "1 named #line directive (1 direct, 0 pp-token; 1 filename); "
                "0 numeric markers; 1 source file; max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_inventory_classifies_all_c_line_forms_after_phase_two(self):
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
            _write(
                root / "main.c",
                r'''
                /* #line 1 "comment.c" */
                static const char ignored[] = "#line 2 \"string.c\"";
                #define LINE_NUMBER 70
                #if FEATURE
                #li\
                ne 20
                %:line LINE_NUMBER FILE_NAME
                # 88 "generated.c" 1 3
                #endif
                #line 90 /* separator */ "direct.c"
                int value;
                ''',
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
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_line_directives"]
            self.assertEqual(contract["named_line_occurrences"], 3)
            self.assertEqual(contract["direct_line_occurrences"], 2)
            self.assertEqual(contract["pp_token_line_occurrences"], 1)
            self.assertEqual(contract["filename_occurrences"], 1)
            self.assertEqual(contract["ordinary_marker_occurrences"], 2)
            self.assertEqual(contract["digraph_marker_occurrences"], 1)
            self.assertEqual(contract["numeric_marker_occurrences"], 1)
            self.assertEqual(contract["max_conditional_depth"], 1)
            features = {
                entry["id"]: entry
                for entry in json.loads(output.read_text(encoding="utf-8"))[
                    "features"
                ]
            }
            self.assertEqual(
                features["c.preprocessor.line"]["occurrences"], 3
            )

            forms = {
                (
                    entry["form"],
                    entry["marker"],
                    entry["conditional_depth"],
                ): entry
                for entry in contract["forms"]
            }
            self.assertEqual(
                set(forms),
                {
                    ("direct_decimal", "#", 1),
                    ("direct_decimal_filename", "#", 0),
                    ("numeric_marker", "#", 1),
                    ("pp_tokens", "%:", 1),
                },
            )
            self.assertEqual(
                forms[("direct_decimal", "#", 1)]["evidence"],
                [
                    {
                        "line": 5,
                        "operand": "20",
                        "path": "main.c",
                        "text": "#line 20",
                    }
                ],
            )
            self.assertEqual(
                forms[("pp_tokens", "%:", 1)]["evidence"][0]["operand"],
                "LINE_NUMBER FILE_NAME",
            )
            self.assertIsNone(
                forms[("pp_tokens", "%:", 1)]["has_filename"]
            )
            self.assertTrue(
                forms[("numeric_marker", "#", 1)]["has_filename"]
            )
            self.assertEqual(
                forms[("numeric_marker", "#", 1)]["evidence"][0][
                    "operand"
                ],
                '88 "generated.c" 1 3',
            )

    def test_line_directive_contract_rejects_unclassifiable_operands(self):
        cases = {
            "empty": ("#line\n", "unclassified active #line form"),
            "comment only": (
                "#line /* no operand */\n",
                "unclassified active #line form",
            ),
            "invalid token": (
                "%:line @\n",
                "unrecognized preprocessing token",
            ),
        }
        for name, (source, message) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
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
                _write(root / "main.c", source)

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
                self.assertIn("main.c:1", result.stderr)
                self.assertIn(message, result.stderr)

    def test_line_directive_contract_rejects_active_templeos_edges(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: TempleOS/reference.o

                TempleOS/reference.o: TempleOS/reference.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "TempleOS" / "reference.c",
                '#line 900 "temple-reference.c"\nint reference;\n',
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
            self.assertIn("TempleOS/reference.c", result.stderr)
            self.assertIn(
                "TempleOS reference tree cannot be an active C preprocessing input",
                result.stderr,
            )

    def test_inventory_contracts_active_conditional_expression_tokens(self):
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
            _write(
                root / "main.c",
                r"""
                /* #if COMMENTED_OUT */
                static const char ignored[] = "#elif STRING_LITERAL";
                #if FLAG && \
                    defined(OTHER)
                #elif (VALUE + 1) == 2
                #endif
                %:if defined /* separator */ THIRD || '\x41' == 'A'
                %:elif FLAG && defined(OTHER)
                %:endif
                """,
            )

            output = root / "audit.json"
            summary = root / "AUDIT.md"
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
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_conditionals"]
            self.assertEqual(contract["status"], "pass")
            self.assertEqual(contract["if_occurrences"], 2)
            self.assertEqual(contract["elif_occurrences"], 2)
            self.assertEqual(contract["expression_occurrences"], 4)
            self.assertEqual(contract["unique_expressions"], 3)
            self.assertEqual(contract["directive_expression_pairs"], 4)
            expressions = {
                entry["expression"]: entry for entry in contract["expressions"]
            }
            self.assertEqual(
                set(expressions),
                {
                    "FLAG && defined ( OTHER )",
                    "( VALUE + 1 ) == 2",
                    "defined THIRD || '\\x41' == 'A'",
                },
            )
            shared = expressions["FLAG && defined ( OTHER )"]
            self.assertEqual(shared["if_occurrences"], 1)
            self.assertEqual(shared["elif_occurrences"], 1)
            self.assertEqual(shared["occurrences"], 2)
            self.assertEqual(shared["files"], ["main.c"])
            self.assertEqual(
                [(item["directive"], item["line"]) for item in shared["evidence"]],
                [("if", 3), ("elif", 8)],
            )
            self.assertIn(
                "4 conditional expressions (2 #if, 2 #elif); "
                "3 normalized expressions; 4 directive/expression pairs",
                summary.read_text(encoding="utf-8"),
            )

    def test_inventory_contracts_active_pragma_once_form(self):
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
            _write(root / "main.c", "#pragma once\nint main_value;\n")

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
            contract = audit["contracts"]["c_preprocessor_pragmas"]
            self.assertEqual(contract["status"], "pass")
            self.assertEqual(contract["pragma_occurrences"], 1)
            self.assertEqual(contract["once_occurrences"], 1)
            self.assertEqual(contract["pack_occurrences"], 0)
            self.assertEqual(
                contract["forms"],
                [
                    {
                        "action": "once",
                        "alignment": None,
                        "evidence": [
                            {
                                "line": 1,
                                "path": "main.c",
                                "text": "#pragma once",
                            }
                        ],
                        "files": ["main.c"],
                        "form": "once",
                        "occurrences": 1,
                    }
                ],
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn(
                "c.preprocessor.pragma.once", sources["main.c"]["features"]
            )

    def test_inventory_contracts_normalized_pack_actions_and_depth(self):
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
            _write(
                root / "main.c",
                """
                #pragma pack(push, 1)
                struct outer { char value; };
                %:pragma pack(push, 2)
                struct inner { char value; };
                #pragma pack(pop)
                #pragma pack(\\
                pop)
                struct natural { char value; };
                """,
            )

            output = root / "audit.json"
            summary = root / "AUDIT.md"
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
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_pragmas"]
            self.assertEqual(contract["pragma_occurrences"], 4)
            self.assertEqual(contract["once_occurrences"], 0)
            self.assertEqual(contract["pack_occurrences"], 4)
            self.assertEqual(contract["pack_push_occurrences"], 2)
            self.assertEqual(contract["pack_pop_occurrences"], 2)
            self.assertTrue(contract["pack_balanced"])
            self.assertEqual(contract["max_pack_depth"], 2)
            self.assertEqual(contract["pack_underflow_occurrences"], 0)
            self.assertEqual(contract["unmatched_pack_pushes"], 0)
            forms = {entry["form"]: entry for entry in contract["forms"]}
            self.assertEqual(
                set(forms),
                {
                    "pack(pop)",
                    "pack(push, 1)",
                    "pack(push, 2)",
                },
            )
            self.assertEqual(
                (
                    forms["pack(push, 1)"]["action"],
                    forms["pack(push, 1)"]["alignment"],
                ),
                ("pack_push", 1),
            )
            self.assertEqual(
                (
                    forms["pack(push, 2)"]["action"],
                    forms["pack(push, 2)"]["alignment"],
                ),
                ("pack_push", 2),
            )
            self.assertEqual(
                (
                    forms["pack(pop)"]["action"],
                    forms["pack(pop)"]["alignment"],
                    forms["pack(pop)"]["occurrences"],
                ),
                ("pack_pop", None, 2),
            )
            self.assertIn(
                "4 pragmas (0 once, 2 pack pushes, 2 pack pops); "
                "pack balanced: yes; max pack depth 2",
                summary.read_text(encoding="utf-8"),
            )

    def test_pragma_inventory_fails_closed_on_unclassified_active_forms(self):
        cases = {
            "vendor": "#pragma cupid_vendor frobnicate\n",
            "malformed-pack": "#pragma pack(push, 3)\n",
            "pragma-operator": '#define APPLY _Pragma("once")\n',
        }
        for name, source in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
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
                _write(root / "main.c", source)

                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(root / "audit.json"),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("main.c:1", result.stderr)
                self.assertIn(
                    "unclassified active #pragma form", result.stderr
                )

    def test_checked_pragma_manifest_matches_active_source_contract(self):
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
            contract = generated["contracts"]["c_preprocessor_pragmas"]
            self.assertEqual(
                checked["contracts"]["c_preprocessor_pragmas"], contract
            )
            self.assertEqual(contract["pragma_occurrences"], 5)
            self.assertEqual(contract["once_occurrences"], 1)
            self.assertEqual(contract["pack_occurrences"], 4)
            self.assertEqual(contract["pack_push_occurrences"], 2)
            self.assertEqual(contract["pack_pop_occurrences"], 2)
            self.assertTrue(contract["pack_balanced"])
            self.assertEqual(contract["max_pack_depth"], 1)
            self.assertEqual(contract["pack_underflow_occurrences"], 0)
            self.assertEqual(contract["unmatched_pack_pushes"], 0)

            forms = {entry["form"]: entry for entry in contract["forms"]}
            self.assertEqual(
                set(forms),
                {
                    "once",
                    "pack(pop)",
                    "pack(push, 1)",
                },
            )
            self.assertEqual(
                [(item["path"], item["line"]) for item in forms["once"]["evidence"]],
                [("bin/ctxt.cc", 1)],
            )
            self.assertEqual(
                [
                    (item["path"], item["line"])
                    for item in forms["pack(push, 1)"]["evidence"]
                ],
                [("bin/fat16.h", 26), ("kernel/fs/fat16.h", 26)],
            )
            self.assertEqual(
                [
                    (item["path"], item["line"])
                    for item in forms["pack(pop)"]["evidence"]
                ],
                [("bin/fat16.h", 76), ("kernel/fs/fat16.h", 76)],
            )
            self.assertTrue(
                all(
                    not item["path"].casefold().startswith("templeos/")
                    for form in contract["forms"]
                    for item in form["evidence"]
                )
            )
            features = {entry["id"]: entry for entry in generated["features"]}
            self.assertEqual(
                features["c.preprocessor.pragma.once"]["occurrences"], 1
            )

    def test_checked_line_directive_contract_matches_active_sources(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            summary = Path(td) / "audit.md"
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
                    "--summary",
                    str(summary),
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
                "c_preprocessor_line_directives"
            ]
            self.assertEqual(contract["source_files"], 658)
            self.assertEqual(contract["named_line_occurrences"], 0)
            self.assertEqual(contract["direct_line_occurrences"], 0)
            self.assertEqual(contract["pp_token_line_occurrences"], 0)
            self.assertEqual(contract["filename_occurrences"], 0)
            self.assertEqual(contract["ordinary_marker_occurrences"], 0)
            self.assertEqual(contract["digraph_marker_occurrences"], 0)
            self.assertEqual(contract["numeric_marker_occurrences"], 0)
            self.assertEqual(contract["max_conditional_depth"], 0)
            self.assertEqual(contract["forms"], [])
            self.assertEqual(
                checked["contracts"]["c_preprocessor_line_directives"],
                contract,
            )
            self.assertNotIn(
                "c.preprocessor.line",
                {entry["id"] for entry in generated["features"]},
            )
            self.assertIn(
                "`c_preprocessor_line_directives` | `pass` | "
                "0 named #line directives (0 direct, 0 pp-token; 0 filename); "
                "0 numeric markers; 658 source files; max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_inventory_contracts_unconditional_cupid_exe_block_forms(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                OBJCOPY = host-objcopy

                .PHONY: all
                all: ordinary.o digraph.o header_user.o

                ordinary.o: ordinary.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                digraph.o: digraph.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                header_user.o: header_user.cc exe_header.h
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                """,
            )
            _write(
                root / "ordinary.cc",
                """
                I32 ordinary_value;
                #exe {
                    ordinary_value = 1;
                }
                """,
            )
            _write(
                root / "digraph.cc",
                """
                I32 digraph_value;
                %:exe { digraph_value = 2; }
                """,
            )
            _write(
                root / "header_user.cc",
                '#include "exe_header.h"\nI32 header_value;\n',
            )
            _write(
                root / "exe_header.h",
                "#exe { header_value = 3; }\n",
            )

            output = root / "audit.json"
            summary = root / "AUDIT.md"
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
            contract = json.loads(output.read_text(encoding="utf-8"))["contracts"][
                "c_preprocessor_cupid_exe"
            ]
            self.assertEqual(
                contract,
                {
                    "status": "pass",
                    "exe_occurrences": 3,
                    "block_occurrences": 3,
                    "ordinary_marker_occurrences": 2,
                    "digraph_marker_occurrences": 1,
                    "max_conditional_depth": 0,
                    "forms": [
                        {
                            "form": "block",
                            "marker": "#",
                            "conditional_depth": 0,
                            "occurrences": 2,
                            "files": ["exe_header.h", "ordinary.cc"],
                            "evidence": [
                                {
                                    "path": "exe_header.h",
                                    "line": 1,
                                    "text": "#exe { header_value = 3; }",
                                },
                                {
                                    "path": "ordinary.cc",
                                    "line": 2,
                                    "text": "#exe {",
                                }
                            ],
                        },
                        {
                            "form": "block",
                            "marker": "%:",
                            "conditional_depth": 0,
                            "occurrences": 1,
                            "files": ["digraph.cc"],
                            "evidence": [
                                {
                                    "path": "digraph.cc",
                                    "line": 2,
                                    "text": "%:exe { digraph_value = 2; }",
                                }
                            ],
                        },
                    ],
                },
            )
            self.assertIn(
                "3 Cupid #exe blocks (2 #, 1 %:); max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_cupid_exe_inventory_fails_closed_on_conditional_form(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                OBJCOPY = host-objcopy
                .PHONY: all
                all: app.o
                app.o: app.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                """,
            )
            _write(
                root / "app.cc",
                """
                #if ENABLED
                #exe {
                }
                #endif
                """,
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(root / "audit.json"),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("app.cc:2", result.stderr)
            self.assertIn(
                "unclassified active Cupid #exe form: conditional depth 1",
                result.stderr,
            )

    def test_cupid_exe_inventory_fails_closed_on_non_block_forms(self):
        cases = {
            "empty": "#exe\n",
            "string": '#exe "script.cc"\n',
            "angle-file": "#exe <script.cc>\n",
            "identifier": "#exe body\n",
            "parenthesized": "#exe()\n",
            "case-variant": "#EXE {\n}\n",
            "brace-digraph": "#exe <%\n%>\n",
            "invalid-token": "#exe @\n",
        }
        for name, source in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    """
                    .SUFFIXES:
                    OBJCOPY = host-objcopy
                    .PHONY: all
                    all: app.o
                    app.o: app.cc
                    \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                    """,
                )
                _write(root / "app.cc", source)

                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(root / "audit.json"),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("app.cc:1", result.stderr)
                self.assertIn(
                    "unclassified active Cupid #exe form", result.stderr
                )

    def test_checked_cupid_exe_manifest_matches_active_source_contract(self):
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
            contract = generated["contracts"]["c_preprocessor_cupid_exe"]
            self.assertEqual(
                checked["contracts"]["c_preprocessor_cupid_exe"], contract
            )
            self.assertEqual(
                contract,
                {
                    "status": "pass",
                    "exe_occurrences": 1,
                    "block_occurrences": 1,
                    "ordinary_marker_occurrences": 1,
                    "digraph_marker_occurrences": 0,
                    "max_conditional_depth": 0,
                    "forms": [
                        {
                            "form": "block",
                            "marker": "#",
                            "conditional_depth": 0,
                            "occurrences": 1,
                            "files": ["bin/feature6_exe.cc"],
                            "evidence": [
                                {
                                    "path": "bin/feature6_exe.cc",
                                    "line": 7,
                                    "text": "#exe {",
                                }
                            ],
                        }
                    ],
                },
            )
            self.assertTrue(
                all(
                    not item["path"].casefold().startswith("templeos/")
                    for form in contract["forms"]
                    for item in form["evidence"]
                )
            )

    def test_conditional_inventory_fails_closed_on_unknown_tokens(self):
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
            _write(root / "main.c", "#if VALUE @ 1\n#endif\n")

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(root / "audit.json"),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("main.c:1", result.stderr)
            self.assertIn("unrecognized preprocessing token", result.stderr)

    def test_checked_conditional_manifest_matches_active_source_contract(self):
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
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_conditionals"]
            self.assertEqual(contract["if_occurrences"], 97)
            self.assertEqual(contract["elif_occurrences"], 4)
            self.assertEqual(contract["expression_occurrences"], 101)
            self.assertEqual(contract["unique_expressions"], 21)
            self.assertEqual(contract["directive_expression_pairs"], 22)
            self.assertTrue(
                all(
                    not item["path"].casefold().startswith("templeos/")
                    for expression in contract["expressions"]
                    for item in expression["evidence"]
                )
            )

            manifest = _conditional_manifest_records()
            inventory = {
                entry["expression"]: entry for entry in contract["expressions"]
            }
            self.assertEqual(set(manifest), set(inventory))
            for expression, entry in inventory.items():
                self.assertEqual(len(entry["evidence"]), entry["occurrences"])
                self.assertEqual(
                    entry["files"],
                    sorted({item["path"] for item in entry["evidence"]}),
                )
                self.assertEqual(
                    manifest[expression][:2],
                    (entry["if_occurrences"], entry["elif_occurrences"]),
                )
            self.assertEqual(
                {expression: values[2] for expression, values in manifest.items()},
                {
                    "! defined ( _WIN32 ) && ! defined ( __MACOSX__ ) && "
                    "! defined ( __DJGPP__ )": 1,
                    "! defined ( __STDC_VERSION__ ) || "
                    "( __STDC_VERSION__ < 202311L )": 1,
                    "! defined ( __cplusplus )": 1,
                    "( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )": 0,
                    "( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )": 1,
                    "0": 0,
                    "1": 1,
                    "OPL_ENABLE_STEREOEXT": 0,
                    "OPL_ENABLE_STEREOEXT && ! defined OPL_SIN": 0,
                    "OPL_QUIRK_CHANNELSAMPLEDELAY": 1,
                    "ORIGCODE": 0,
                    "_MSC_VER < 1400": 1,
                    "_WIN64": 0,
                    "defined ( _MSC_VER ) && ! defined ( __cplusplus )": 0,
                    "defined ( _WIN32 )": 0,
                    "defined ( _WIN32 ) && ! defined ( _WIN32_WCE )": 0,
                    "defined ( _WIN32 ) || defined ( __DJGPP__ )": 0,
                    "defined ( __DJGPP__ )": 0,
                    "defined ( __MACOSX__ )": 0,
                    "defined ( __SIZEOF_POINTER__ ) && "
                    "( __SIZEOF_POINTER__ == 8 )": 0,
                    "defined ( __cplusplus ) || "
                    "defined ( __bool_true_false_are_defined )": 1,
                },
            )

    def test_checked_conditional_manifest_is_a_c_contract_prerequisite(self):
        source = CUPIDC_PP_CONTRACT.read_text(encoding="utf-8")
        define = source.index(
            "#define CUPIDC_PP_CONDITIONAL_CASE(expression, if_count, "
            "elif_count, expected)"
        )
        include = source.index(
            '#include "cupidc_pp_conditional_cases.inc"', define
        )
        undefine = source.index(
            "#undef CUPIDC_PP_CONDITIONAL_CASE", include
        )
        self.assertLess(define, include)
        self.assertLess(include, undefine)

        makefile = TOOLCHAIN_MAKEFILE.read_text(encoding="utf-8")
        rule = re.search(
            r"\$\(BUILD_DIR\)/cupidc_pp_contract\.o:(.*?)\n\t",
            makefile,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(rule)
        self.assertIn("tests/cupidc_pp_conditional_cases.inc", rule.group(1))

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
            _write(root / "include" / "api.h", "%:include \"types.h\"\n")
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
                    CFLAGS = {case['flags']}

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
                self.assertIn(
                    "macro-expanded #include operand", result.stderr
                )
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
            self.assertEqual(contract["source_files"], 658)
            self.assertEqual(contract["include_occurrences"], 2343)
            self.assertEqual(contract["direct_quoted_occurrences"], 2132)
            self.assertEqual(contract["direct_angle_occurrences"], 211)
            self.assertEqual(contract["pp_token_operand_occurrences"], 0)

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

    def test_checked_cupidc_active_manifest_has_exact_source_cohorts(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        profile_pattern = re.compile(
            r"^CUPIDC_PP_PROFILE\(([A-Z0-9_]+), ([A-Z0-9_]+), "
            r"(CTOOL_(?:TRUE|FALSE)), (CTOOL_(?:TRUE|FALSE))\)$"
        )
        active_pattern = re.compile(
            r'^CUPIDC_PP_ACTIVE_CASE\(([A-Z0-9_]+), "([^"]+)"\)$'
        )
        generated_pattern = re.compile(
            r'^CUPIDC_PP_GENERATED_CASE\(([A-Z0-9_]+), "([^"]+)"\)$'
        )
        profiles = [
            match.groups()
            for line in lines
            if (match := profile_pattern.fullmatch(line)) is not None
        ]
        active = [
            match.groups()
            for line in lines
            if (match := active_pattern.fullmatch(line)) is not None
        ]
        generated = [
            match.groups()
            for line in lines
            if (match := generated_pattern.fullmatch(line)) is not None
        ]

        self.assertEqual(
            profiles,
            [
                (
                    "KERNEL_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                ),
                (
                    "DOOM_COMPAT_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                ),
                (
                    "DOOM_TREE_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                ),
                (
                    "USER_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                ),
                (
                    "CUPID_RUNTIME",
                    "CTOOL_C_PP_MODE_CUPID",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_TOOLCHAIN_64",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                ),
                (
                    "HOSTED_KERNEL_BRIDGE_64",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                ),
            ],
        )
        self.assertEqual(
            {name: sum(case_name == name for case_name, _ in active)
             for name, _, _, _ in profiles},
            {
                "KERNEL_I386": 152,
                "DOOM_COMPAT_I386": 6,
                "DOOM_TREE_I386": 80,
                "USER_I386": 3,
                "CUPID_RUNTIME": 105,
                "HOSTED_TOOLCHAIN_64": 12,
                "HOSTED_KERNEL_BRIDGE_64": 1,
            },
        )
        self.assertEqual(len(active), 359)
        for expected in (
            ("KERNEL_I386", "/kernel/core/kernel.c"),
            ("DOOM_COMPAT_I386", "/kernel/audio/nuked_opl3.c"),
            ("DOOM_TREE_I386", "/kernel/doom/i_sound_cupidos.c"),
            ("DOOM_TREE_I386", "/kernel/doom/src/d_main.c"),
            ("USER_I386", "/user/examples/hello.c"),
            ("CUPID_RUNTIME", "/bin/browser.cc"),
            ("HOSTED_TOOLCHAIN_64", "/toolchain/ctool.c"),
            ("HOSTED_TOOLCHAIN_64", "/toolchain/cupidc_emit.c"),
            ("HOSTED_TOOLCHAIN_64", "/toolchain/cupidc_ir.c"),
            ("HOSTED_TOOLCHAIN_64", "/toolchain/x86.c"),
            ("HOSTED_KERNEL_BRIDGE_64", "/kernel/lang/as_elf.c"),
        ):
            self.assertIn(expected, active)
        self.assertEqual(
            generated,
            [
                ("KERNEL_I386", "/kernel/cpu/ksyms_data.c"),
                ("KERNEL_I386", "/kernel/util/bin_programs_gen.c"),
                ("KERNEL_I386", "/kernel/util/demos_programs_gen.c"),
                ("KERNEL_I386", "/kernel/util/docs_programs_gen.c"),
            ],
        )

    def test_checked_cupidc_active_manifest_keeps_profiles_isolated(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        roots = [line for line in lines if line.startswith("CUPIDC_PP_INCLUDE_ROOT(")]
        macros = [line for line in lines if line.startswith("CUPIDC_PP_MACRO(")]
        forced = [
            line for line in lines if line.startswith("CUPIDC_PP_FORCED_INCLUDE(")
        ]
        forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        kernel_roots = [
            "/kernel",
            "/kernel/audio",
            "/kernel/core",
            "/kernel/cpu",
            "/kernel/crypto",
            "/kernel/doom",
            "/kernel/fs",
            "/kernel/gfx",
            "/kernel/gui",
            "/kernel/lang",
            "/kernel/mm",
            "/kernel/network",
            "/kernel/smp",
            "/kernel/tls",
            "/kernel/usb",
            "/kernel/util",
            "/drivers",
            "/toolchain",
        ]
        expected_roots = {
            "KERNEL_I386": kernel_roots,
            "DOOM_COMPAT_I386": [
                *kernel_roots,
                "/kernel/doom/src",
                "/kernel/doom/src/include_stubs",
            ],
            "DOOM_TREE_I386": [
                *kernel_roots,
                "/kernel/doom/src",
                "/kernel/doom/src/include_stubs",
            ],
            "USER_I386": ["/user"],
            "CUPID_RUNTIME": [],
            "HOSTED_TOOLCHAIN_64": ["/toolchain"],
            "HOSTED_KERNEL_BRIDGE_64": ["/toolchain", "/kernel/lang"],
        }
        root_pattern = re.compile(
            r'^CUPIDC_PP_INCLUDE_ROOT\(([A-Z0-9_]+), "([^"]+)", '
            r"(.+)\)$"
        )
        actual_roots = {name: [] for name in expected_roots}
        for line in roots:
            match = root_pattern.fullmatch(line)
            self.assertIsNotNone(match, line)
            name, path, actual_forms = match.groups()
            self.assertEqual(actual_forms, forms)
            actual_roots[name].append(path)
        self.assertEqual(actual_roots, expected_roots)

        self.assertEqual(
            roots[0],
            f'CUPIDC_PP_INCLUDE_ROOT(KERNEL_I386, "/kernel", {forms})',
        )
        common_macros = [
            ("__GNUC__", "1"),
            ("__SIZEOF_POINTER__", "4"),
            ("__ORDER_LITTLE_ENDIAN__", "1234"),
            ("__ORDER_BIG_ENDIAN__", "4321"),
            ("__ORDER_PDP_ENDIAN__", "3412"),
            ("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__"),
        ]
        expected_macros = {
            "KERNEL_I386": [
                *common_macros,
                ("__SSE2__", "1"),
                ("DEBUG", "1"),
            ],
            "DOOM_COMPAT_I386": [*common_macros, ("__SSE2__", "1")],
            "DOOM_TREE_I386": [
                *common_macros,
                ("__SSE2__", "1"),
                ("DEFAULT_SAVEGAMEDIR", '\"/home/doom/\"'),
                ("DOOM_PORT_CUPIDOS", "1"),
            ],
            "USER_I386": common_macros,
            "CUPID_RUNTIME": [],
            "HOSTED_TOOLCHAIN_64": [("__SIZEOF_POINTER__", "8")],
            "HOSTED_KERNEL_BRIDGE_64": [("__SIZEOF_POINTER__", "8")],
        }

        def macro_line(profile, name, replacement):
            escaped_name = name.replace("\\", "\\\\").replace('"', '\\"')
            escaped_replacement = replacement.replace("\\", "\\\\").replace(
                '"', '\\"'
            )
            return (
                f'CUPIDC_PP_MACRO({profile}, "{escaped_name}", '
                f'"{escaped_replacement}")'
            )

        self.assertEqual(
            macros,
            [
                macro_line(profile, name, replacement)
                for profile in expected_macros
                for name, replacement in expected_macros[profile]
            ],
        )
        self.assertEqual(
            forced,
            [
                'CUPIDC_PP_FORCED_INCLUDE(DOOM_TREE_I386, '
                '"/kernel/doom/dglibc_compat.h")'
            ],
        )

    def test_cupidc_active_manifest_fails_closed_on_compile_recipe_shape(self):
        module = _load_audit_module()

        def audit(inputs, recipe, sources=None):
            return {
                "build": {
                    "directory": ".",
                    "transforms": [
                        {
                            "output": "unit.o",
                            "inputs": inputs,
                            "tools": ["host_c_compiler"],
                            "operation": "compile_c_to_elf32_object",
                            "recipe": [recipe],
                        }
                    ],
                },
                "supplemental_builds": [],
                "sources": [] if sources is None else sources,
            }

        def hosted_audit(
            recipe,
            source="toolchain/unit.c",
            *,
            origin=None,
        ):
            return {
                "build": {"directory": ".", "transforms": []},
                "supplemental_builds": [
                    {
                        "directory": "toolchain",
                        "transforms": [
                            {
                                "output": "build/unit.o",
                                "inputs": [source],
                                "tools": ["host_c_compiler"],
                                "operation": "compile_c_to_host_object",
                                "recipe": [recipe],
                            }
                        ],
                    }
                ],
                "sources": (
                    []
                    if origin is None
                    else [{"path": source, "origin": origin}]
                ),
            }

        cases = {
            "unknown marker": (
                audit(["unit.c"], "$(CC) $(NEW_CFLAGS) -c $< -o $@"),
                r"unknown recipe marker.*NEW_CFLAGS",
            ),
            "brace-form unknown marker": (
                audit(["unit.c"], "${CC} ${NEW_CFLAGS} -c $< -o $@"),
                r"unknown recipe marker.*NEW_CFLAGS",
            ),
            "escaped shell marker": (
                audit(["unit.c"], "$(CC) $(CFLAGS) $${NEW_CFLAGS} -c $< -o $@"),
                r"unmodeled recipe dollar reference",
            ),
            "computed marker": (
                audit(["unit.c"], "$(CC) $(CFLAGS) $(value NEW_CFLAGS) -c $< -o $@"),
                r"unmodeled recipe Make reference/function",
            ),
            "zero roots": (
                audit(["unit.h"], "$(CC) $(CFLAGS) -c $< -o $@"),
                r"exactly one C translation-unit root.*found 0",
            ),
            "multiple roots": (
                audit(
                    ["first.c", "second.c"],
                    "$(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"exactly one C translation-unit root.*found 2",
            ),
            "literal macro": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -DLOCAL=1 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-DLOCAL=1",
            ),
            "literal contradictory target": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -m64 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-m64",
            ),
            "literal driver pass-through": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -Wp,-DLOCAL=1 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-Wp,-DLOCAL=1",
            ),
            "literal language mode": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -xc++ -c $< -o $@",
                ),
                r"literal preprocessor flag.*-xc\+\+",
            ),
            "literal character mode": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -funsigned-char -c $< -o $@",
                ),
                r"literal preprocessor flag.*-funsigned-char",
            ),
            "literal predefined accelerator macro": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -fopenacc -c $< -o $@",
                ),
                r"literal preprocessor flag.*-fopenacc",
            ),
            "compiler response file": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) @extra.rsp -c $< -o $@",
                ),
                r"literal preprocessor flag.*@extra.rsp",
            ),
            "recipe environment include path": (
                audit(
                    ["unit.c"],
                    "CPATH=private $(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"literal preprocessor flag.*CPATH=private",
            ),
            "hosted recipe omits include profile": (
                hosted_audit("$(CC) $(CFLAGS) -c $< -o $@"),
                r"hosted recipe markers differ.*CPPFLAGS",
            ),
            "hosted recipe duplicates include profile": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CPPFLAGS) $(CFLAGS) "
                    "-c $< -o $@"
                ),
                r"hosted recipe markers differ.*CPPFLAGS",
            ),
            "root recipe duplicates compiler marker": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"recipe markers differ.*CC",
            ),
            "root recipe duplicates profile marker": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) $(CFLAGS) -c $< -o $@",
                ),
                r"recipe markers differ.*CFLAGS",
            ),
            "hosted bridge flag moves to ordinary source": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) "
                    "-c $< -o $@"
                ),
                r"hosted bridge recipe differs.*toolchain/unit\.c",
            ),
            "hosted bridge source loses its include flag": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@",
                    "kernel/lang/as_elf.c",
                ),
                r"hosted bridge recipe differs.*kernel/lang/as_elf\.c",
            ),
            "hosted bridge include precedes common include roots": (
                hosted_audit(
                    "$(CC) -I../kernel/lang $(CPPFLAGS) $(CFLAGS) "
                    "-c $< -o $@",
                    "kernel/lang/as_elf.c",
                ),
                r"compiler argument profile differs"
                r".*kernel/lang/as_elf\.c",
            ),
            "hosted include marker is shell quoted": (
                hosted_audit(
                    '$(CC) "$(CPPFLAGS)" $(CFLAGS) -c $< -o $@'
                ),
                r"compiler argument profile differs.*toolchain/unit\.c",
            ),
            "hosted command substitution injects flags": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) `cat extra.flags` "
                    "-c $< -o $@"
                ),
                r"compile recipe has unmodeled shell substitution",
            ),
            "hosted markers hidden in shell comment": (
                hosted_audit(
                    "$(CC) $(CFLAGS) -c $< -o $@ # $(CPPFLAGS)"
                ),
                r"compile recipe contains a shell comment",
            ),
            "hosted markers belong to a different command": (
                hosted_audit(
                    "echo $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@"
                ),
                r"compile recipe does not invoke.*CC",
            ),
            "hosted deferral moves to freestanding build": (
                audit(
                    ["toolchain/ctool_host.c"],
                    "$(CC) $(CFLAGS) -c $< -o $@",
                    [
                        {
                            "path": "toolchain/ctool_host.c",
                            "origin": "tracked",
                        }
                    ],
                ),
                r"hosted deferral transform differs.*toolchain/ctool_host\.c",
            ),
            "hosted deferral is absent from source inventory": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@",
                    "toolchain/ctool_host.c",
                ),
                r"root is absent from source inventory.*toolchain/ctool_host\.c",
            ),
            "hosted deferral has generated origin": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@",
                    "toolchain/ctool_host.c",
                    origin="generated",
                ),
                r"hosted deferral is not a tracked source"
                r".*toolchain/ctool_host\.c",
            ),
        }
        for name, (synthetic, message) in cases.items():
            with self.subTest(name=name), self.assertRaisesRegex(
                module.AuditError, message
            ):
                module._c_preprocessor_active_cases_manifest(synthetic)

        malformed_wrap = {
            "build": {
                "directory": ".",
                "transforms": [
                    {
                        "output": "bin/malformed.o",
                        "inputs": None,
                        "tools": ["cupid_object"],
                        "operation": "wrap_binary_as_elf32_relocatable",
                        "recipe": ["$(CUPIDOBJ) wrap $< -o $@"],
                    }
                ],
            },
            "supplemental_builds": [],
            "sources": [],
        }
        with self.assertRaisesRegex(
            module.AuditError, r"delivery transform inputs are absent"
        ):
            module._c_preprocessor_active_cases_manifest(malformed_wrap)

    def test_checked_cupidc_active_manifest_classifies_non_roots_and_hosted(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        include_only_pattern = re.compile(
            r'^CUPIDC_PP_INCLUDE_ONLY\("([^"]+)", "([^"]+)"\)$'
        )
        non_root_pattern = re.compile(
            r'^CUPIDC_PP_NON_ROOT\("([^"]+)", "([^"]+)"\)$'
        )
        deferred_pattern = re.compile(
            r'^CUPIDC_PP_DEFERRED_HOSTED\("([^"]+)", "([^"]+)"\)$'
        )
        include_only = [
            match.groups()
            for line in lines
            if (match := include_only_pattern.fullmatch(line)) is not None
        ]
        non_roots = [
            match.groups()
            for line in lines
            if (match := non_root_pattern.fullmatch(line)) is not None
        ]
        deferred = [
            match.groups()
            for line in lines
            if (match := deferred_pattern.fullmatch(line)) is not None
        ]

        self.assertEqual(len(include_only), 22)
        self.assertTrue(
            all(owner == "/bin/browser.cc" for _, owner in include_only)
        )
        self.assertEqual(
            [path for path, _ in non_roots],
            ["/bin/fat16.h", "/bin/shell.h"],
        )
        self.assertEqual(len(deferred), 19)
        self.assertEqual(
            {path for path, _ in deferred},
            {
                "/toolchain/ctool_host.c",
                "/toolchain/cupidasm_main.c",
                "/toolchain/cupiddis_main.c",
                "/toolchain/cupidld_main.c",
                "/toolchain/cupidobj_main.c",
                "/toolchain/tests/core_contract.c",
                "/toolchain/tests/cupidasm_contract.c",
                "/toolchain/tests/cupidasm_demos_contract.c",
                "/toolchain/tests/cupidasm_kernel_elf_contract.c",
                "/toolchain/tests/cupidc_frontend_contract.c",
                "/toolchain/tests/cupidc_ir_contract.c",
                "/toolchain/tests/cupidc_object_contract.c",
                "/toolchain/tests/cupidc_pp_contract.c",
                "/toolchain/tests/cupidc_type_contract.c",
                "/toolchain/tests/cupiddis_contract.c",
                "/toolchain/tests/cupidld_contract.c",
                "/toolchain/tests/cupidobj_contract.c",
                "/toolchain/tests/elf32_contract.c",
                "/toolchain/tests/x86_contract.c",
            },
        )
        self.assertTrue(all(reason for _, reason in [*non_roots, *deferred]))

    def test_cupidc_active_manifest_renderer_is_grouped_and_c_escaped(self):
        module = _load_audit_module()
        manifest = module.CPreprocessorActiveCasesManifest(
            profiles=(
                module.CPreprocessorProfile(
                    name="SYNTH",
                    mode="CTOOL_C_PP_MODE_C11",
                    gnu_extensions="CTOOL_TRUE",
                    hosted_environment="CTOOL_FALSE",
                ),
            ),
            include_roots=(
                (
                    "SYNTH",
                    '/root/??/"quoted"\\tab\n\N{GREEK CAPITAL LETTER OMEGA}',
                    "CTOOL_C_PP_INCLUDE_QUOTED",
                ),
            ),
            macros=(("SYNTH", 'A"B', "line\n\\end"),),
            forced_includes=(("SYNTH", "/forced.h"),),
            active_cases=(("SYNTH", "/active.c"),),
            generated_cases=(("SYNTH", "/generated.c"),),
            include_only=(("/fragment.cc", "/owner.cc"),),
            non_roots=(("/header.h", "delivered header"),),
            deferred_hosted=(("/host.c", "host-only contract"),),
        )

        first = module._render_c_preprocessor_active_cases(manifest)
        second = module._render_c_preprocessor_active_cases(manifest)
        self.assertEqual(first, second)
        self.assertIn(
            'CUPIDC_PP_INCLUDE_ROOT(SYNTH, '
            '"/root/\\?\\?/\\\"quoted\\\"\\\\tab\\012\\316\\251", '
            "CTOOL_C_PP_INCLUDE_QUOTED)",
            first,
        )
        self.assertIn(
            'CUPIDC_PP_MACRO(SYNTH, "A\\\"B", '
            '"line\\012\\\\end")',
            first,
        )
        row_prefixes = [
            "CUPIDC_PP_PROFILE(",
            "CUPIDC_PP_INCLUDE_ROOT(",
            "CUPIDC_PP_MACRO(",
            "CUPIDC_PP_FORCED_INCLUDE(",
            "CUPIDC_PP_ACTIVE_CASE(",
            "CUPIDC_PP_GENERATED_CASE(",
            "CUPIDC_PP_INCLUDE_ONLY(",
            "CUPIDC_PP_NON_ROOT(",
            "CUPIDC_PP_DEFERRED_HOSTED(",
        ]
        self.assertEqual(
            [
                next(index for index, line in enumerate(first.splitlines())
                     if line.startswith(prefix))
                for prefix in row_prefixes
            ],
            sorted(
                next(index for index, line in enumerate(first.splitlines())
                     if line.startswith(prefix))
                for prefix in row_prefixes
            ),
        )

    def test_cupidc_active_manifest_check_rejects_drift(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            summary = Path(td) / "audit.md"
            manifest = Path(td) / "cupidc_pp_active_cases.inc"
            command = [
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
                "--summary",
                str(summary),
                "--c-preprocessor-active-cases",
                str(manifest),
            ]
            generated = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertTrue(summary.read_text(encoding="utf-8").endswith("\n\n"))
            audit_payload = json.loads(output.read_text(encoding="utf-8"))
            contract = audit_payload["contracts"][
                "c_preprocessor_translation_units"
            ]
            self.assertEqual(
                {
                    key: contract[key]
                    for key in (
                        "status",
                        "tracked_translation_units",
                        "generated_translation_units",
                        "total_translation_units",
                        "include_only_fragments",
                        "delivered_non_root_headers",
                        "deferred_hosted_translation_units",
                        "deferred_external_header_units",
                        "deferred_hermetic_units",
                    )
                },
                {
                    "status": "pass",
                    "tracked_translation_units": 359,
                    "generated_translation_units": 4,
                    "total_translation_units": 363,
                    "include_only_fragments": 22,
                    "delivered_non_root_headers": 2,
                    "deferred_hosted_translation_units": 19,
                    "deferred_external_header_units": 19,
                    "deferred_hermetic_units": 0,
                },
            )
            self.assertEqual(
                [
                    (
                        profile["name"],
                        profile["tracked_translation_units"],
                        profile["generated_translation_units"],
                    )
                    for profile in contract["profiles"]
                ],
                [
                    ("KERNEL_I386", 152, 4),
                    ("DOOM_COMPAT_I386", 6, 0),
                    ("DOOM_TREE_I386", 80, 0),
                    ("USER_I386", 3, 0),
                    ("CUPID_RUNTIME", 105, 0),
                    ("HOSTED_TOOLCHAIN_64", 12, 0),
                    ("HOSTED_KERNEL_BRIDGE_64", 1, 0),
                ],
            )
            self.assertEqual(
                audit_payload["summary"],
                {
                    "active_sources": 688,
                    "features": 251,
                    "transforms": 498,
                    "unreachable_sources": 39,
                },
            )
            features = {
                entry["id"]: entry for entry in audit_payload["features"]
            }
            expected_c_expression_inventory = {
                "c.declaration.static_assert": (22, 4),
                "c.expression.sizeof": (3450, 165),
                "c.extension.builtin.offsetof": (12, 6),
                "c.extension.gnu_alignof": (1, 1),
            }
            for feature_id, expected_counts in (
                expected_c_expression_inventory.items()
            ):
                feature = features[feature_id]
                self.assertEqual(
                    (feature["occurrences"], len(feature["files"])),
                    expected_counts,
                )
            self.assertEqual(
                features["c.extension.gnu_alignof"]["files"],
                ["kernel/core/process.c"],
            )
            self.assertEqual(
                features["c.extension.gnu_alignof"]["examples"][0]["line"],
                39,
            )
            root_transform_by_output = {
                transform["output"]: transform
                for transform in audit_payload["build"]["transforms"]
            }
            symbol_transform = root_transform_by_output[
                "kernel/cpu/ksyms_data.c"
            ]
            self.assertEqual(
                symbol_transform["tools"],
                ["cupid_disassembler", "host_python"],
            )
            self.assertEqual(
                symbol_transform["operation"], "generate_c_source"
            )
            self.assertIn(
                "toolchain/build/cupiddis"
                + (".exe" if sys.platform == "win32" else ""),
                symbol_transform["inputs"],
            )
            self.assertFalse(
                any(
                    "host_symbol_reader" in transform["tools"]
                    for transform in audit_payload["build"]["transforms"]
                )
            )
            toolchain_cohort = next(
                cohort
                for cohort in audit_payload["roadmap"]["source_cohort_order"]
                if cohort["id"] == "toolchain_sources"
            )
            self.assertEqual(toolchain_cohort["source_count"], 59)

            source_by_path = {
                source["path"]: source for source in audit_payload["sources"]
            }
            frontend_sources = {
                "toolchain/cupidc_emit.c": "toolchain_core",
                "toolchain/cupidc_emit.h": "toolchain_core",
                "toolchain/cupidc_frontend.c": "toolchain_core",
                "toolchain/cupidc_frontend.h": "toolchain_core",
                "toolchain/cupidc_ir.c": "toolchain_core",
                "toolchain/cupidc_ir.h": "toolchain_core",
                "toolchain/tests/cupidc_frontend_contract.c":
                    "toolchain_contract",
                "toolchain/tests/cupidc_ir_contract.c":
                    "toolchain_contract",
                "toolchain/tests/cupidc_object_contract.c":
                    "toolchain_contract",
            }
            for path, cohort in frontend_sources.items():
                with self.subTest(path=path):
                    self.assertEqual(source_by_path[path]["cohort"], cohort)
                    self.assertEqual(
                        source_by_path[path]["reachability"],
                        "direct_build_input",
                    )
                    self.assertIsNone(source_by_path[path]["runtime_owner"])

            toolchain_build = next(
                build
                for build in audit_payload["supplemental_builds"]
                if build["directory"] == "toolchain"
            )
            self.assertEqual(len(toolchain_build["transforms"]), 51)
            toolchain_transform_by_output = {
                transform["output"]: transform
                for transform in toolchain_build["transforms"]
            }
            frontend_transforms = {
                "toolchain/build/cupidc_emit.o":
                    "compile_c_to_host_object",
                "toolchain/build/cupidc_frontend.o":
                    "compile_c_to_host_object",
                "toolchain/build/cupidc_frontend_contract.o":
                    "compile_c_to_host_object",
                "toolchain/build/cupidc_ir.o":
                    "compile_c_to_host_object",
                "toolchain/build/cupidc_ir_contract.o":
                    "compile_c_to_host_object",
                "toolchain/build/cupidc_object_contract.o":
                    "compile_c_to_host_object",
                "toolchain/build/cupidc-ir-contract"
                + (".exe" if sys.platform == "win32" else ""):
                    "compile_and_link_host_executable",
                "toolchain/build/cupidc-object-contract"
                + (".exe" if sys.platform == "win32" else ""):
                    "compile_and_link_host_executable",
                "toolchain/build/cupidc-frontend-contract"
                + (".exe" if sys.platform == "win32" else ""):
                    "compile_and_link_host_executable",
            }
            for output_path, operation in frontend_transforms.items():
                with self.subTest(output=output_path):
                    transform = toolchain_transform_by_output[output_path]
                    self.assertEqual(transform["operation"], operation)
                    self.assertEqual(transform["tools"], ["host_c_compiler"])
            self.assertIn(
                "`c_preprocessor_translation_units` | `pass` | "
                "359 tracked + 4 generated",
                summary.read_text(encoding="utf-8"),
            )
            audit_payload["build"]["transforms"].append(
                {
                    "output": "bin/new.h.o",
                    "inputs": ["bin/new.h"],
                    "tools": ["cupid_object"],
                    "operation": "wrap_binary_as_elf32_relocatable",
                    "recipe": ["$(CUPIDOBJ) wrap $< -o $@"],
                }
            )
            module = _load_audit_module()
            with self.assertRaisesRegex(
                module.AuditError, r"delivered non-root headers changed"
            ):
                module._c_preprocessor_active_cases_manifest(audit_payload)
            checked = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)

            manifest.write_text(
                manifest.read_text(encoding="utf-8") + "/* stale */\n",
                encoding="utf-8",
            )
            stale = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(stale.returncode, 1)
            self.assertIn(manifest.name, stale.stderr)

    def test_make_include_extraction_keeps_assignment_adjacent_first_root(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                KERNEL_INCLUDES=-I./kernel -I./kernel/core
                CFLAGS = $(KERNEL_INCLUDES)
                .PHONY: all
                all: main.o
                main.o: main.c
                \t$(CC) $(CFLAGS) -c $< -o $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            (root / "kernel" / "core").mkdir(parents=True)
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
            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8"))["build"][
                    "include_search_paths"
                ],
                ["kernel", "kernel/core"],
            )

    def test_cupidc_make_profile_validation_rejects_preprocessor_drift(self):
        module = _load_audit_module()
        kernel_roots = " ".join(
            f"-I.{path}"
            for path in (
                "/kernel",
                "/kernel/audio",
                "/kernel/core",
                "/kernel/cpu",
                "/kernel/crypto",
                "/kernel/doom",
                "/kernel/fs",
                "/kernel/gfx",
                "/kernel/gui",
                "/kernel/lang",
                "/kernel/mm",
                "/kernel/network",
                "/kernel/smp",
                "/kernel/tls",
                "/kernel/usb",
                "/kernel/util",
                "/drivers",
                "/toolchain",
            )
        )

        def fixture(
            root,
            *,
            cflags_extra="",
            simd_extra="",
            opt_extra="",
            roots_extra="",
            hosted_cppflags_extra="",
            hosted_cflags_extra="",
        ):
            _write(
                root / "Makefile",
                f"""
                KERNEL_INCLUDES={kernel_roots} {roots_extra}
                CFLAGS=-m32 -ffreestanding -nostdinc -msse2 -pedantic \\
                       $(KERNEL_INCLUDES) -DDEBUG {cflags_extra}
                CFLAGS_DOOM=-m32 -ffreestanding -nostdinc -msse2 \\
                            $(KERNEL_INCLUDES) -I./kernel/doom/src \\
                            -I./kernel/doom/src/include_stubs
                CFLAGS_DOOM_TREE=$(CFLAGS_DOOM) \\
                    -include kernel/doom/dglibc_compat.h \\
                    -DDEFAULT_SAVEGAMEDIR=\\\"/home/doom/\\\" \\
                    -DDOOM_PORT_CUPIDOS=1
                SIMD_CFLAGS=$(filter-out -pedantic,$(CFLAGS)) \\
                            -msse2 -O2 {simd_extra}
                OPT=-O2 {opt_extra}
                """,
            )
            _write(
                root / "user" / "Makefile",
                "CFLAGS=-m32 -ffreestanding -I.\n",
            )
            _write(
                root / "toolchain" / "Makefile",
                f"CPPFLAGS=-I. {hosted_cppflags_extra}\n"
                f"CFLAGS=-std=c11 {hosted_cflags_extra}\n",
            )

        cases = {
            "strict standard": (
                {"cflags_extra": "-std=c11"},
                r"unmodeled preprocessor flag.*-std=c11",
            ),
            "undefinition": (
                {"cflags_extra": "-UDEBUG"},
                r"unmodeled preprocessor flag.*-UDEBUG",
            ),
            "alternate include": (
                {"cflags_extra": "-iquote./private"},
                r"unmodeled preprocessor flag.*-iquote",
            ),
            "contradictory word size": (
                {"cflags_extra": "-m64"},
                r"unmodeled preprocessor flag.*-m64",
            ),
            "disabled SIMD target": (
                {"simd_extra": "-mno-sse2"},
                r"unmodeled preprocessor flag.*-mno-sse2",
            ),
            "driver preprocessor pass-through": (
                {"cflags_extra": "-Wp,-DLOCAL=1"},
                r"unmodeled preprocessor flag.*-Wp,-DLOCAL=1",
            ),
            "alternate language mode": (
                {"cflags_extra": "-xc++"},
                r"unmodeled preprocessor flag.*-xc\+\+",
            ),
            "unsigned character mode": (
                {"cflags_extra": "-funsigned-char"},
                r"unmodeled preprocessor flag.*-funsigned-char",
            ),
            "negated signed character mode": (
                {"cflags_extra": "-fno-signed-char"},
                r"unmodeled preprocessor flag.*-fno-signed-char",
            ),
            "short wchar mode": (
                {"cflags_extra": "-fshort-wchar"},
                r"unmodeled preprocessor flag.*-fshort-wchar",
            ),
            "accelerator predefined macro": (
                {"cflags_extra": "-fopenacc"},
                r"unmodeled preprocessor flag.*-fopenacc",
            ),
            "extra include root": (
                {"roots_extra": "-I./private"},
                r"include-root order differs",
            ),
            "SIMD-only macro": (
                {"simd_extra": "-DLOCAL=1"},
                r"configured macros differ.*SIMD_CFLAGS",
            ),
            "OPT-only macro": (
                {"opt_extra": "-DLOCAL=1"},
                r"OPT has preprocessor effects",
            ),
            "hosted alternate language mode": (
                {"hosted_cflags_extra": "-std=gnu11"},
                r"unmodeled preprocessor flag.*-std=gnu11",
            ),
            "hosted configured macro": (
                {"hosted_cflags_extra": "-DLOCAL=1"},
                r"configured macros differ.*CPPFLAGS\+CFLAGS",
            ),
            "hosted extra include root": (
                {"hosted_cppflags_extra": "-Iprivate"},
                r"HOSTED_TOOLCHAIN_64 include-root order differs",
            ),
            "hosted contradictory word size": (
                {"hosted_cflags_extra": "-m32"},
                r"HOSTED_TOOLCHAIN_64 has unmodeled preprocessor flag.*-m32",
            ),
            "hosted freestanding mode": (
                {"hosted_cflags_extra": "-ffreestanding"},
                r"HOSTED_TOOLCHAIN_64 has unmodeled preprocessor flag"
                r".*-ffreestanding",
            ),
        }
        for name, (changes, message) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                fixture(root, **changes)
                with self.assertRaisesRegex(module.AuditError, message):
                    module._validate_c_preprocessor_make_profiles(root, "make")

    def test_make_profile_values_use_gnu_make_assignment_timing(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                ROOT=-I/a
                CFLAGS := $(ROOT)
                ROOT=-I/b
                .PHONY: all
                all:
                """,
            )

            values = module._read_evaluated_make_variables(
                root, "make", ("CFLAGS", "ROOT")
            )

            self.assertEqual(values, {"CFLAGS": "-I/a", "ROOT": "-I/b"})

    def test_supported_audit_targets_check_and_consume_active_manifest(self):
        root_makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        toolchain_makefile = TOOLCHAIN_MAKEFILE.read_text(encoding="utf-8")

        self.assertEqual(
            root_makefile.count("--c-preprocessor-active-cases "), 3
        )
        self.assertIn(
            "BOOTSTRAP_CUPIDC_ACTIVE_CASES := "
            "toolchain/tests/cupidc_pp_active_cases.inc",
            root_makefile,
        )
        self.assertIn(
            "$(CUPIDC_PP_ACTIVE_CASES) \\", toolchain_makefile
        )
        self.assertIn(
            '#include "cupidc_pp_active_cases.inc"',
            CUPIDC_PP_CONTRACT.read_text(encoding="utf-8"),
        )


if __name__ == "__main__":
    unittest.main()
