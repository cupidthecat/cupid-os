import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
INTERPRETER_PATH = REPO_ROOT / "bin" / "browser" / "js_interp.cc"
LEXER_PATH = REPO_ROOT / "bin" / "browser" / "js_lex.cc"
MAIN_PATH = REPO_ROOT / "bin" / "browser" / "main.cc"
JS_PARSER_PATH = REPO_ROOT / "bin" / "browser" / "js_parse.cc"
HTML_PARSER_PATH = REPO_ROOT / "bin" / "browser" / "parser.cc"
DOM_PATH = REPO_ROOT / "bin" / "browser" / "js_dom.cc"


def _compact(source):
    return re.sub(r"\s+", " ", source)


class BrowserCupidCNumberTests(unittest.TestCase):
    def setUp(self):
        self.interpreter = INTERPRETER_PATH.read_text(encoding="utf-8")
        self.lexer = LEXER_PATH.read_text(encoding="utf-8")
        self.main = MAIN_PATH.read_text(encoding="utf-8")
        self.parser = JS_PARSER_PATH.read_text(encoding="utf-8")
        self.html_parser = HTML_PARSER_PATH.read_text(encoding="utf-8")
        self.dom = DOM_PATH.read_text(encoding="utf-8")
        self.compact_interpreter = _compact(self.interpreter)

    def test_number_truth_keeps_tiny_values_and_rejects_nan(self):
        self.assertIn(
            "int js_number_truth(double v) { "
            "return v != 0.0 && v == v; }",
            self.compact_interpreter,
        )
        self.assertIn("js_number_truth(tiny_nonzero)", self.interpreter)
        self.assertIn("!js_number_truth(number_nan)", self.interpreter)

    def test_interpreter_uses_native_double_comparisons(self):
        for fragment in (
            "return jvs_num[a] == jvs_num[b];",
            "bv = na < nb;",
            "bv = na > nb;",
            "bv = na <= nb;",
            "bv = na >= nb;",
            "else if (op == JS_TOK_SLASH)  v = na / nb;",
            "else if (op == JS_TOK_SLASH_EQ) r = na / nb;",
            "else if (op == JS_TOK_PERCENT) v = fmod(na, nb);",
            "else if (op == JS_TOK_PERCENT_EQ) r = fmod(na, nb);",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, self.interpreter)

        self.assertNotIn("js_dcmp", self.interpreter)
        self.assertNotIn("js_dnz", self.interpreter)

    def test_unary_minus_no_longer_uses_subtraction_from_zero(self):
        self.assertNotRegex(self.interpreter, r"\b0\.0\s*-\s*")
        self.assertIn("return -v;", self.interpreter)
        self.assertIn("double v = -js_to_number_at(t);", self.interpreter)

    def test_selftest_computes_each_reported_boundary(self):
        for fragment in (
            "double close_high = 1.0000005;",
            "double large_positive = 5000.0;",
            "double negative_zero = -0.0;",
            "double negative_zero_reciprocal = 1.0 / negative_zero;",
            "double number_nan = 0.0 / 0.0;",
            "double positive_infinity = 1.0 / 0.0;",
            "double negative_infinity = -1.0 / 0.0;",
            "double tiny_nonzero = 0.0000001;",
            "var cupidClose=1.0000005;",
            "var cupidTiny=.0000001;",
            "var cupidLarge=5e3;",
            "var cupidNegativeZero=-0;",
            "var cupidSignedExp=5e+3===5000&&5e-3>0.004&&5e-3<0.006;",
            "var cupidUpperExp=2E2===200&&2E-2>0.019&&2E-2<0.021;",
            "var cupidOrder=1<2&&2<=2&&3>=2;",
            "var cupidDivideAssign=1;cupidDivideAssign/=0;",
            "var cupidRemainder=1%0;",
            "var cupidCap=1e999999999999999999999999999999999999999;",
            "var cupidRadix=0xff===255&&0X10===16&&",
            "var cupidSeparators=1_000===1000&&0xff_ff===65535&&",
            "var cupidStringInvalid=+'12x';",
            "var cupidLooseEq=0==false&&'0'==false&&'1'==true&&",
            "var cupidStringOrder='10'<'2'&&'2'>'10'&&",
            "var cupidLargeRemainder=1e20%3;",
            "cupidModAssign%=3;",
            "cupidConcat+=42;",
            'char *bad_hex_script = "var cupidBad=0x;";',
            'char *bad_binary_script = "var cupidBad=0b2;";',
            'char *bad_octal_script = "var cupidBad=0o8;";',
            'char *bad_separator_script = "var cupidBad=1__0;";',
            'char *bad_radix_separator_script = "var cupidBad=0x_1;";',
            'char *bad_fraction_separator_script = "var cupidBad=1._0;";',
            'char *bad_exponent_separator_script = "var cupidBad=1e_2;";',
            'char *bad_leading_zero_separator_script = "var cupidBad=00_1;";',
            'char *bad_suffix_script = "var cupidBad=12foo;";',
            'char *recovery_script = "var cupidRecovery=',
            'char *bad_script = "var cupidBad=1e;";',
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, self.interpreter)

        self.assertEqual(
            self.interpreter.count('"[browser-js-number] PASS'), 1
        )
        self.assertEqual(
            self.interpreter.count('"[browser-js-number] FAIL'), 1
        )
        self.assertNotIn(
            '"[browser-js-number] PASS close=1 large=1',
            self.interpreter,
        )
        for field in (
            "close=%d",
            "large=%d",
            "negzero=%d",
            "nan=%d",
            "truth=%d",
            "nanformat=%d",
            "posinfformat=%d",
            "neginfformat=%d",
            "literal=%d",
            "signedexp=%d",
            "upperexp=%d",
            "order=%d",
            "divide=%d",
            "divideassign=%d",
            "remainder=%d",
            "expcap=%d",
            "radix=%d",
            "separators=%d",
            "tonumber=%d",
            "looseeq=%d",
            "stringrel=%d",
            "largefmod=%d",
            "modassign=%d",
            "strplusassign=%d",
            "reject=%d",
            "recovery=%d",
        ):
            with self.subTest(field=field):
                self.assertEqual(self.interpreter.count(field), 2)
        self.assertIn(
            "int exponent_reject_ok = "
            "js_run(bad_script, b_strlen(bad_script)) != 0;",
            self.compact_interpreter,
        )

    def test_decimal_literals_keep_double_tokens_and_ast_values(self):
        for declaration in (
            "double jtk_num  [8192];",
            "double jn_num  [8192];",
            "double jvs_num    [1024];",
            "double jb_num     [1024];",
            "double jp_num     [4096];",
        ):
            with self.subTest(declaration=declaration):
                self.assertIn(declaration, self.main)
        self.assertIn(
            "void js_emit_tok(int kind, double num,",
            self.lexer,
        )
        self.assertIn("place = place * 0.1;", self.lexer)
        self.assertIn("exponent < 400", self.lexer)
        self.assertIn(
            'js_set_err("js: expected exponent digits");',
            self.lexer,
        )
        self.assertIn("return -1;", self.lexer)
        self.assertIn("if (js_tokenize(src, len) != 0)", self.parser)
        self.assertIn("jn_num[node] = v;", self.parser)
        self.assertIn("js_push_num(jn_num[node]);", self.interpreter)

    def test_special_number_formatting_is_explicit(self):
        self.assertIn('special = "NaN";', self.interpreter)
        self.assertIn('special = "Infinity";', self.interpreter)
        self.assertIn("cancelled = v + -v;", self.interpreter)
        for fragment in (
            "js_format_num(number_nan, nan_text) == 3",
            'b_streq(nan_text, "NaN")',
            "js_format_num(positive_infinity, positive_infinity_text) == 8",
            'b_streq(positive_infinity_text, "Infinity")',
            "js_format_num(negative_infinity, negative_infinity_text) == 9",
            'b_streq(negative_infinity_text, "-Infinity")',
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, self.interpreter)

        for fragment in (
            "int js_format_positive_integer(double value, char *buf)",
            "int js_append_fraction_six(double fraction, char *buf, int b)",
            "var cupidFiniteFormat=''+4294967295==='4294967295'&&",
            "''+1e20==='100000000000000000000'&&",
            "''+1e-7==='1e-7';",
            "js_selftest_binding_is_true(\"cupidFiniteFormat\")",
        ):
            with self.subTest(finite_fragment=fragment):
                self.assertIn(_compact(fragment), self.compact_interpreter)

        self.assertNotIn("int int_part = (int)v;", self.interpreter)

    def test_selftest_reads_each_script_result_from_the_runtime_scope(self):
        for binding in (
            "cupidLiteral",
            "cupidSignedExp",
            "cupidUpperExp",
            "cupidOrder",
            "cupidDivide",
            "cupidDivideAssignOk",
            "cupidNegativeZeroOk",
            "cupidRemainderOk",
            "cupidCapOk",
            "cupidRadix",
            "cupidSeparators",
            "cupidToNumber",
            "cupidLooseEq",
            "cupidStringOrder",
            "cupidLargeFmod",
            "cupidModAssignOk",
            "cupidConcatOk",
        ):
            with self.subTest(binding=binding):
                self.assertIn(
                    f'js_selftest_binding_is_true("{binding}")',
                    self.interpreter,
                )

    def test_radix_literals_and_separators_have_strict_lexer_rules(self):
        self.assertIn("int js_digit_value(int c)", self.lexer)
        self.assertIn("JS_TOK_PERCENT_EQ", self.main)
        self.assertIn("JS_TOK_PERCENT_EQ", self.lexer)
        self.assertIn("JS_TOK_PERCENT_EQ", self.parser)
        for diagnostic in (
            "js: expected hexadecimal digits",
            "js: invalid binary digit",
            "js: invalid octal digit",
            "js: invalid numeric separator",
            "js: identifier follows numeric literal",
        ):
            with self.subTest(diagnostic=diagnostic):
                self.assertIn(diagnostic, self.lexer)

    def test_primitive_number_conversion_consumes_the_whole_string(self):
        for fragment in (
            "int js_number_space(int c)",
            "int js_utf8_next(char *s, int end, int *index)",
            "void js_trim_number_string(char *s, int length,",
            "c == 0x00A0",
            "c == 0xFEFF",
            "c >= 0x2000 && c <= 0x200A",
            "int end = jvs_str_len[idx];",
            "if (i != end) return js_nan_value();",
            "if (t == JS_VAL_UNDEF) return js_nan_value();",
            "if (sign_negative) return -v;",
            "unicode_whitespace_ok",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, self.interpreter)

    def test_equality_and_string_order_use_primitive_semantics(self):
        self.assertIn("int js_same_type_eq_at(int a, int b)", self.interpreter)
        self.assertIn("int js_string_compare_at(int a, int b)", self.interpreter)
        self.assertIn("int js_utf16_next(char *s, int length,", self.interpreter)
        self.assertIn("int left_pending = -1;", self.interpreter)
        self.assertIn("int right_pending = -1;", self.interpreter)
        self.assertIn("utf16_order_ok", self.interpreter)
        self.assertIn("if (ta == JS_VAL_BOOL)", self.interpreter)
        self.assertIn("if (tb == JS_VAL_BOOL)", self.interpreter)
        self.assertIn("int string_order = js_string_compare_at(a, b);", self.interpreter)

    def test_compound_remainder_and_string_addition_match_binary_operators(self):
        self.assertIn("void js_concat_at(int a, int b)", self.interpreter)
        self.assertIn("char *left = left_scratch;", self.interpreter)
        self.assertIn("char *right = right_scratch;", self.interpreter)
        self.assertIn("int available = JS_STR_POOL - js_str_pool_pos;", self.interpreter)
        self.assertNotIn("char joined[512];", self.interpreter)
        self.assertIn("var cupidLongExpected=", self.interpreter)
        self.assertIn("cupidLongPlus===cupidLongExpected", self.interpreter)
        self.assertIn("cupidLongA===cupidLongExpected", self.interpreter)
        self.assertIn(
            'b_streq(js_last_error, "js: string pool full")',
            self.interpreter,
        )
        self.assertIn(
            "string_plus_assign_ok = string_plus_assign_ok && "
            "finite_format_ok && concat_pool_reject_ok;",
            self.compact_interpreter,
        )
        self.assertIn(
            "if (op == JS_TOK_PLUS_EQ && "
            "(jvs_tag[a] == JS_VAL_STR || jvs_tag[b] == JS_VAL_STR))",
            self.compact_interpreter,
        )
        self.assertIn("else if (op == JS_TOK_PERCENT_EQ) r = fmod(na, nb);", self.interpreter)

    def test_compound_assignments_resolve_side_effecting_targets_once(self):
        for fragment in (
            "int js_resolve_target(int target_node, js_target_ref_t *ref)",
            "void js_load_target(js_target_ref_t *ref)",
            "void js_store_target(js_target_ref_t *ref)",
            "cupidPlusMemberTarget().value+=cupidPlusMemberRhs();",
            "cupidPlusIndex[cupidPlusIndexKey()]+='B';",
            "cupidModMemberTarget().value%=cupidModMemberRhs();",
            "cupidModIndex[cupidModIndexKey()]%=4;",
            "while(cupidAssignmentCount<1100)",
            "cupidPlusMemberCalls===1",
            "cupidPlusIndexCalls===1",
            "cupidModMemberCalls===1",
            "cupidModIndexCalls===1",
            "cupidPlusMemberOriginal.value==='AB'",
            "cupidPlusMemberOther.value==='Z'",
            "cupidModMemberOriginal.value===2",
            "cupidModMemberOther.value===9",
            "index_key_pool_reject_ok",
            'js_set_err("js: string pool full");',
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, self.interpreter)

        self.assertNotIn("js_assign_to_target(lhs)", self.interpreter)

    def test_bindings_keep_their_owning_scope_after_nested_rhs_calls(self):
        combined = _compact(self.interpreter + self.main)
        for fragment in (
            "int jb_scope [1024];",
            "jb_scope[b] = scope;",
            "if (jb_scope[b] == scope &&",
            "int js_lookup_binding_in_scope(int scope,",
            "function cupidBindingRhs(cupidBindingParam)",
            "var cupidBindingLocal=7;",
            "cupidOuterAfterCall=cupidBindingRhs(3);",
            "cupidOuterAfterCall===3",
            "cupidBindingLocal===undefined",
            "cupidBindingParam===undefined",
            "function cupidMakeShadow()",
            "var cupidShadow=2;",
            "cupidShadowClosure()===2",
            "cupidShadow===3",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, combined)

        self.assertNotIn("jsc_first", self.interpreter + self.main)
        self.assertNotIn("jsc_count", self.interpreter + self.main)

    def test_computed_assignment_pool_exhaustion_is_atomic_and_recovers(self):
        for fragment in (
            "char *pool_assignment_script =",
            "cupidPoolObject[456]=cupidPoolRhs();",
            "cupidPoolObject.stable+='B';",
            "function cupidPoolRhs()",
            "cupidPoolRhsCalls+=1",
            "js_exec_program(pool_assignment_root);",
            "js_exec_program(pool_concat_root);",
            "pool_assignment_stack_ok",
            "pool_concat_stack_ok",
            "pool_saved_prop_count == jp_count",
            "pool_saved_first_prop == jobj_first_prop[pool_object_idx]",
            'b_streq(js_last_error, "js: string pool full")',
            "cupidPoolObject[456]===undefined",
            "cupidPoolObject[123]===7",
            "cupidPoolObject.stable==='A'",
            "cupidPoolRhsCalls===0",
            "cupidPoolRecovery",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(_compact(fragment), self.compact_interpreter)

    def test_string_pool_failures_never_publish_invalid_offsets(self):
        compact_lexer = _compact(self.lexer)
        compact_dom = _compact(self.dom)
        for fragment in (
            'js_set_err("js: string pool full");',
            "if (off < 0) return -1;",
            "i + k < js_str_pool_pos",
            "i + n < js_str_pool_pos",
        ):
            with self.subTest(lexer_fragment=fragment):
                self.assertIn(_compact(fragment), compact_lexer)

        for fragment in (
            "int jsd_push_interned_string(char *source, int length)",
            "if (offset < 0) return -1;",
            "jsd_push_interned_string(buf, n);",
            "jsd_push_interned_string(v, vl);",
        ):
            with self.subTest(dom_fragment=fragment):
                self.assertIn(_compact(fragment), compact_dom)

        for fragment in (
            "int js_str_eq_text(int off, int len, char *text)",
            "char *pool_array_script =",
            "char *pool_typeof_script =",
            "js_exec_program(pool_array_root);",
            "js_exec_program(pool_typeof_root);",
            "pool_array_existing_key_ok",
            "pool_lexer_reject_ok",
            "pool_typeof_reject_ok",
            "pool_dom_reject_ok",
            "pool_embedded_boundary_result",
            "pool_embedded_boundary_ok",
            "pool_intern_boundary_ok",
            "if (name_off < 0) return 0;",
            "unicode_number_off >= 0",
            "supplementary_off >= 0 && bmp_off >= 0",
            "if (pool_object_name >= 0)",
            "if (stack_target_name >= 0)",
        ):
            with self.subTest(interpreter_fragment=fragment):
                self.assertIn(_compact(fragment), self.compact_interpreter)

        self.assertNotIn(
            _compact('js_str_intern("length", 6)'),
            self.compact_interpreter,
        )

    def test_failed_global_installation_blocks_queued_scripts(self):
        install_offset = self.html_parser.index("js_install_globals();")
        guard_offset = self.html_parser.index(
            "if (js_last_error[0] == 0)", install_offset
        )
        run_offset = self.html_parser.index("js_run_queued_scripts();")

        self.assertLess(install_offset, guard_offset)
        self.assertLess(guard_offset, run_offset)
        self.assertIn(
            '[browser] js: global install failed: %s\\n', self.html_parser
        )

    def test_native_function_ids_survive_user_function_round_trips(self):
        for fragment in (
            "jb_native_id[b]= jvs_native_id[src];",
            "jvs_native_id[dst] = jvs_native_id[src];",
            "function cupidNativeIdentity(cupidNativeValue)",
            "cupidNativeIdentity(cupidNativeProbe)",
            "var cupidNativeCallResult=cupidNativeReturned();",
            "var cupidNativeRoundTrip=cupidNativeCallResult===null;",
            "native_round_trip_ok",
            "stack_assignment_recovery_ok && native_round_trip_ok",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(_compact(fragment), self.compact_interpreter)

    def test_array_writes_grow_length_and_length_assignment_is_explicit(self):
        for fragment in (
            "int js_array_index_from_key(int off, int len, int *out_index)",
            "jobj_arr_len[ref->obj_idx] = array_index + 1;",
            'js_set_err("js: array length assignment unsupported");',
            "char *array_length_script =",
            "char *array_index_limit_script =",
            "char *array_numeric_limit_script =",
            "js_exec_program(array_length_root);",
            "js_exec_program(array_index_limit_root);",
            "js_exec_program(array_numeric_limit_root);",
            "array_length_reject_ok",
            "array_index_limit_reject_ok",
            "array_numeric_limit_reject_ok",
            "int js_array_numeric_index_is_unsupported(int value_top)",
            "int js_format_large_uint_key(double value, char *buf)",
            'js_set_err("js: array index exceeds runtime limit");',
            "cupidPoolArray.length===0",
            "cupidPoolArray.length===3",
            "cupidPoolArray[2]===11",
            "cupidPoolArray[4294967295]===12",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(_compact(fragment), self.compact_interpreter)

    def test_value_stack_overflow_unwinds_expressions_and_recovers(self):
        for fragment in (
            "int js_push_slot()",
            'js_set_err("js: value stack overflow");',
            "int js_copy_top_from(int src)",
            "if (js_copy_top_from(jvs_top - 1) != 0)",
            "char *stack_assignment_script =",
            "char *stack_full_assignment_script =",
            "char *stack_binary_script =",
            "char *stack_call_script =",
            "char *stack_var_script =",
            "jvs_top = MAX_JS_VS - 1;",
            "jvs_top = MAX_JS_VS;",
            "jvs_top = MAX_JS_VS - 2;",
            "stack_assignment_reject_ok",
            "stack_full_assignment_reject_ok",
            "stack_binary_reject_ok",
            "stack_call_reject_ok",
            "stack_var_reject_ok",
            "cupidStackCallCount===0",
            "stack_assignment_unchanged_ok",
            "stack_assignment_recovery_ok",
            "int expr_stack_base = jvs_top;",
            "if (expr_stack_base >= MAX_JS_VS)",
            "jvs_top = expr_stack_base;",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, self.interpreter)

        for fragment in (
            "void jsd_dom_member_get(int dom_idx, int koff, int klen)",
            "void js_native_return(int saved, int result_top)",
            "int t = js_push_slot();",
            "if (t < 0) return;",
        ):
            with self.subTest(dom_fragment=fragment):
                self.assertIn(fragment, self.dom)

        self.assertNotRegex(
            self.interpreter + self.dom,
            r"js_push_undef\(\);\s*int t = jvs_top - 1",
        )

    def test_selftest_returns_before_window_creation(self):
        args_offset = self.main.index("char *raw = (char*)get_args();")
        selftest_offset = self.main.index("js_number_selftest();")
        window_offset = self.main.index('gui_win_create("Browser"')

        self.assertLess(args_offset, selftest_offset)
        self.assertLess(selftest_offset, window_offset)
        self.assertIn('b_streq(raw, "--selftest")', self.main)


if __name__ == "__main__":
    unittest.main()
