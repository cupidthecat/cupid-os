## @file
#  @brief Check the inventory of C, preprocessor, and assembly directives.
import ast
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path
from tests.build_graph_audit_fixtures import (
    ACTIVE_BUILD_MANIFEST,
    AUDIT_TOOL,
    CUPIDC_PP_CONTRACT,
    REPO_ROOT,
    TOOLCHAIN_MAKEFILE,
    _conditional_manifest_records,
    _load_audit_module,
    _write,
    _write_source_suffix_policy,
)


class DirectiveInventoryCases:
    def test_inventory_reports_source_features_with_stable_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ASM = nasm
                CUPIDOBJ = seed/cupidbuild.exe run \
                    --seed-manifest seed/manifest.json --root . \
                    --tool cupidobj --

                .PHONY: all
                all: feature.o app.o entry.o

                feature.o: feature.c
                \t$(CC) -c $< -o $@

                app.o: app.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@

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
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=["app.cc"],
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
            self.assertTrue(
                expected.issubset(features), expected - set(features)
            )
            self.assertEqual(
                features["asm.addressing.memory"]["occurrences"], 1
            )
            self.assertEqual(features["asm.directive.bits"]["occurrences"], 2)
            self.assertEqual(features["asm.directive.org"]["occurrences"], 2)
            self.assertEqual(
                features["asm.instruction.call"]["occurrences"], 1
            )
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
            self.assertEqual(
                features["c.expression.compound_literal"]["occurrences"], 1
            )
            self.assertEqual(
                features["c.declarator.bit_field"]["occurrences"], 1
            )
            self.assertEqual(
                features["c.initializer.designated"]["occurrences"], 1
            )
            self.assertIn("cupid_c.directive.exe", features)
            self.assertNotIn("c.preprocessor.exe", features)
            self.assertEqual(
                features["c.extension.attribute.packed"]["files"],
                ["feature.c"],
            )
            self.assertEqual(
                features["c.extension.attribute.packed"]["examples"][0][
                    "line"
                ],
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
        collector = module.source_features.FeatureCollector()
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

        module.source_features.scan_c_features("alignof.c", source, "c", collector)
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
            "noreturn": 9,
            "returns_twice": 1,
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
        self.assertEqual(sum(expected.values()), 104)
        self.assertEqual(len(attribute_files), 33)
        for name in ("aligned", "section", "weak"):
            self.assertIn(
                "kernel/cpu/ksyms.cc",
                features[f"c.extension.attribute.{name}"]["files"],
            )

    def test_c_logical_lines_use_only_real_newlines_and_preserve_evidence(
        self,
    ):
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
            module.source_features.c_logical_lines(lf),
            module.source_features.c_logical_lines(crlf)
        )
        self.assertEqual(
            module.source_features.c_logical_lines("#define TRAILING value\\"),
            [(1, "#define TRAILING value\\", "#define TRAILING value\\")],
        )
        controls = "#define ONLY 1\v#define NOT_SECOND 2\fstill_same"
        self.assertEqual(
            module.source_features.c_logical_lines(controls),
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
                r"""
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
            self.assertEqual(features["c.preprocessor.line"]["occurrences"], 3)

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
            self.assertIsNone(forms[("pp_tokens", "%:", 1)]["has_filename"])
            self.assertTrue(forms[("numeric_marker", "#", 1)]["has_filename"])
            self.assertEqual(
                forms[("numeric_marker", "#", 1)]["evidence"][0]["operand"],
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
                [
                    (item["directive"], item["line"])
                    for item in shared["evidence"]
                ],
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
                [
                    (item["path"], item["line"])
                    for item in forms["once"]["evidence"]
                ],
                [("bin/ctxt.cc", 1)],
            )
            self.assertEqual(
                [
                    (item["path"], item["line"])
                    for item in forms["pack(push, 1)"]["evidence"]
                ],
                [("bin/fat16.h", 26), ("kernel/fs/fat16.h", 27)],
            )
            self.assertEqual(
                [
                    (item["path"], item["line"])
                    for item in forms["pack(pop)"]["evidence"]
                ],
                [("bin/fat16.h", 76), ("kernel/fs/fat16.h", 77)],
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
            contract = generated["contracts"]["c_preprocessor_line_directives"]
            self.assertEqual(contract["source_files"], 766)
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
                "0 numeric markers; 766 source files; max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_inventory_contracts_unconditional_cupid_exe_block_forms(self):
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
                all: ordinary.o digraph.o header_user.o

                ordinary.o: ordinary.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@

                digraph.o: digraph.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@

                header_user.o: header_user.cc exe_header.h
                \t$(CUPIDOBJ) wrap-text $< -o $@
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
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=[
                    "digraph.cc",
                    "header_user.cc",
                    "ordinary.cc",
                ],
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
            ]["c_preprocessor_cupid_exe"]
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
                                },
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
                CUPIDOBJ = seed/cupidbuild.exe run \
                    --seed-manifest seed/manifest.json --root . \
                    --tool cupidobj --
                .PHONY: all
                all: app.o
                app.o: app.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@
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
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=["app.cc"],
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
                    CUPIDOBJ = seed/cupidbuild.exe run \
                        --seed-manifest seed/manifest.json --root . \
                        --tool cupidobj --
                    .PHONY: all
                    all: app.o
                    app.o: app.cc
                    \t$(CUPIDOBJ) wrap-text $< -o $@
                    """,
                )
                _write(root / "app.cc", source)
                _write_source_suffix_policy(
                    root,
                    runtime_delivery_sources=["app.cc"],
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
            self.assertEqual(contract["if_occurrences"], 472)
            self.assertEqual(contract["elif_occurrences"], 24)
            self.assertEqual(contract["expression_occurrences"], 496)
            self.assertEqual(contract["unique_expressions"], 63)
            self.assertEqual(contract["directive_expression_pairs"], 66)
            executable_contract = CUPIDC_PP_CONTRACT.read_text(encoding="utf-8")
            totals_guard = re.search(
                r"sizeof\(cases\) / sizeof\(cases\[0\]\)\) != (\d+)u \|\|"
                r"\s*if_occurrences != (\d+)u \|\|\s*elif_occurrences != (\d+)u \|\|"
                r"\s*probe_count != (\d+)u",
                executable_contract,
            )
            self.assertIsNotNone(
                totals_guard, "conditional-active must check all four totals"
            )
            self.assertEqual(
                tuple(int(value) for value in totals_guard.groups()),
                (
                    contract["unique_expressions"],
                    contract["if_occurrences"],
                    contract["elif_occurrences"],
                    contract["directive_expression_pairs"],
                ),
                "conditional-active executable totals differ from the audit",
            )
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
                {
                    expression: values[2]
                    for expression, values in manifest.items()
                },
                {
                    "! defined ( CUPIDBUILD_HOST_FILE_LIMIT )": 1,
                    "! defined ( CUPIDBUILD_HOST_STREAM_LIMIT )": 1,
                    "! defined ( CUPID_HOSTED_I386_LINUX_ABI_H )": 1,
                    "! defined ( CUPID_HOSTED_I386_WINDOWS_H )": 1,
                    "! defined ( CUPID_RUNTIME_WINDOWS )": 1,
                    "! defined ( FILE_ATTRIBUTE_DEVICE )": 1,
                    "! defined ( FILE_LIST_DIRECTORY )": 1,
                    "! defined ( _GNU_SOURCE )": 1,
                    "! defined ( _POSIX_C_SOURCE )": 1,
                    "! defined ( _WIN32 )": 1,
                    "! defined ( _WIN32 ) && ! defined ( __MACOSX__ ) && "
                    "! defined ( __DJGPP__ )": 1,
                    "! defined ( __SIZEOF_POINTER__ ) || __SIZEOF_POINTER__ != 4": 0,
                    "! defined ( __STDC_VERSION__ ) || "
                    "( __STDC_VERSION__ < 202311L )": 1,
                    "! defined ( __cplusplus )": 1,
                    "! defined ( CUPID_HOSTED_SIZE_T_DEFINED )": 1,
                    "( CC_RAW_FUNCTION_POINTER_SIGNATURE_BASE + "
                    "CC_MAX_RAW_FUNCTION_POINTER_SIGNATURES ) > 128": 0,
                    "( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )": 0,
                    "( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )": 1,
                    "0": 0,
                    "1": 1,
                    "OPL_ENABLE_STEREOEXT": 0,
                    "OPL_ENABLE_STEREOEXT && ! defined OPL_SIN": 0,
                    "OPL_QUIRK_CHANNELSAMPLEDELAY": 1,
                    "ORIGCODE": 0,
                    "defined ( DOOM_PORT_CUPIDOS )": 0,
                    "defined ( ORIGCODE ) || defined ( DOOM_PORT_CUPIDOS )": 0,
                    "_MSC_VER < 1400": 1,
                    "_WIN64": 0,
                    "defined ( _MSC_VER ) && ! defined ( __cplusplus )": 0,
                    "defined ( _WIN32 )": 0,
                    "defined ( _WIN32 ) && ( defined ( "
                    "CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST ) || defined ( "
                    "CUPIDBUILD_PUBLICATION_RACE_TEST ) || defined ( "
                    "CUPIDBUILD_HOST_WINDOWS_TERMINATION_TEST ) ) && "
                    "! defined ( _CRT_SECURE_NO_WARNINGS )": 0,
                    "defined ( _WIN32 ) && ! defined ( CUPID_HOSTED_I386_WINDOWS_H )": 0,
                    "defined ( _WIN32 ) && ! defined ( _WIN32_WCE )": 0,
                    "defined ( _WIN32 ) || defined ( __DJGPP__ )": 0,
                    "defined ( _WIN64 )": 0,
                    "defined ( CUPID_HOSTED_I386_LINUX_ABI_H )": 0,
                    "defined ( CUPIDASM_PUBLICATION_TESTING )": 0,
                    "defined ( CUPIDBUILD_HOST_NATIVE_EINTR_TEST )": 0,
                    "defined ( CUPID_RUNTIME_WINDOWS )": 0,
                    "defined ( CUPIDBUILD_CUSTOM_LINUX )": 0,
                    "defined ( CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST )": 0,
                    "defined ( CUPID_HOSTED_I386_WINDOWS_H )": 0,
                    "defined ( CUPIDBUILD_HOST_CLOSE_FAILURE_TEST )": 0,
                    "defined ( CUPIDBUILD_HOST_CLOSE_FAILURE_TEST ) && "
                    "! defined ( _WIN32 )": 0,
                    "defined ( CUPIDBUILD_HOST_LOW_FD_FAILURE_TEST )": 0,
                    "defined ( CUPIDBUILD_HOST_WINDOWS_TERMINATION_TEST )": 0,
                    "defined ( CUPIDBUILD_NOREPLACE_RACE_TEST )": 0,
                    "defined ( CUPIDBUILD_NOREPLACE_RACE_TEST ) && "
                    "! defined ( CUPIDBUILD_CUSTOM_LINUX )": 0,
                    "defined ( CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST ) && "
                    "! defined ( CUPIDBUILD_CUSTOM_LINUX )": 0,
                    "defined ( CUPIDBUILD_PROFILE_PARENT_RACE_TEST )": 0,
                    "defined ( CUPIDBUILD_PUBLICATION_RACE_TEST )": 0,
                    "defined ( CUPIDBUILD_PUBLICATION_RACE_TEST ) && "
                    "! defined ( CUPIDBUILD_CUSTOM_LINUX )": 0,
                    "defined ( CUPID_TOOLCHAIN_CUPIDC_STATIC_LONG_DOUBLE_INTERNAL )": 0,
                    "defined ( CUPID_WINDOWS_BUILD )": 0,
                    "defined ( CUPID_WINDOWS_BUILD ) && defined ( CUPID_WINDOWS_PUBLICATION )": 0,
                    "defined ( CUPID_WINDOWS_BUILD ) || defined ( CUPID_WINDOWS_PUBLICATION )": 0,
                    "defined ( CUPID_WINDOWS_BUILD ) || defined ( "
                    "CUPID_WINDOWS_PUBLICATION ) || defined ( CUPID_WINDOWS_LONG_PATHS )": 0,
                    "defined ( CUPID_WINDOWS_LONG_PATHS )": 0,
                    "defined ( CUPID_WINDOWS_UTF8 )": 0,
                    "defined ( __DJGPP__ )": 0,
                    "defined ( __MACOSX__ )": 0,
                    "defined ( __SIZEOF_POINTER__ ) && ( __SIZEOF_POINTER__ == 8 )": 0,
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
        undefine = source.index("#undef CUPIDC_PP_CONDITIONAL_CASE", include)
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

    def test_x86_catalogue_fixture_is_in_each_contract_source_closure(self):
        makefile = TOOLCHAIN_MAKEFILE.read_text(encoding="utf-8")
        support = re.search(
            r"X86_CONTRACT_SUPPORT :=(.*?)(?:\n[^ \t]|\Z)",
            makefile,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(support)
        self.assertIn("tests/x86_catalogue_contract.inc", support.group(1))
        self.assertIn("tests/x86_integer_contract.inc", support.group(1))
        self.assertIn("tests/x86_operand_contract.inc", support.group(1))

        object_rule = re.search(
            r"\$\(BUILD_DIR\)/x86_contract\.o:(.*?)\n\t",
            makefile,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(object_rule)
        self.assertIn("$(X86_CONTRACT_SUPPORT)", object_rule.group(1))

        test_rule = re.search(
            r"^test:(.*?)\n\t", makefile, flags=re.DOTALL | re.MULTILINE
        )
        self.assertIsNotNone(test_rule)
        self.assertIn("$(X86_CONTRACT_SUPPORT)", test_rule.group(1))
        contract = (
            REPO_ROOT / "toolchain" / "tests" / "x86_contract.cc"
        ).read_text(encoding="utf-8")
        literal = r'"(?:\\.|[^"\\])*"'
        usage = re.search(r'("usage: x86-contract (?:\\.|[^"\\])*"(?:\s*' + literal + r')*)', contract)
        self.assertIsNotNone(usage)
        usage_text = "".join(ast.literal_eval(text) for text in re.findall(literal, usage.group(1)))
        self.assertTrue(usage_text.endswith("\n"))
        public_selectors = set(usage_text.removeprefix("usage: x86-contract ").strip().split("|"))
        required_selectors = set(
            re.findall(
                r"^\t\$\(X86_CONTRACT\) ([a-z0-9-]+)$",
                makefile,
                flags=re.MULTILINE,
            )
        )
        self.assertEqual(required_selectors, public_selectors)

        module = _load_audit_module()
        self.assertIn(
            "toolchain/tests/x86_catalogue_contract.inc",
            module.USER_SYSCALL_ABI_PUBLICATION_INPUTS,
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
            self.assertEqual(
                features["asm.addressing.memory"]["occurrences"], 101
            )
            self.assertEqual(features["asm.directive.bits"]["occurrences"], 8)
            self.assertEqual(features["asm.directive.org"]["occurrences"], 3)
            transforms = {
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
            }
            expected_assembly = {
                "boot/boot.bin": "assemble_flat_binary",
                "kernel/core/context_switch.o": "assemble_elf32_relocatable",
                "kernel/cpu/isr.o": "assemble_elf32_relocatable",
                "kernel/smp_trampoline.bin": "assemble_flat_binary",
            }
            for output_path, operation in expected_assembly.items():
                expected_tools = [
                    "cupid_assembler",
                    "cupid_disassembler",
                    "cupid_builder",
                ]
                self.assertEqual(
                    transforms[output_path]["tools"],
                    expected_tools,
                )
                self.assertEqual(
                    transforms[output_path]["operation"], operation
                )
            self.assertFalse(
                [
                    entry["output"]
                    for entry in audit["build"]["transforms"]
                    if "nasm" in entry["tools"]
                ]
            )
