import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
INTERPRETER_PATH = REPO_ROOT / "bin" / "browser" / "js_interp.cc"
MAIN_PATH = REPO_ROOT / "bin" / "browser" / "main.cc"


def _compact(source):
    return re.sub(r"\s+", " ", source)


class BrowserCupidCNumberTests(unittest.TestCase):
    def setUp(self):
        self.interpreter = INTERPRETER_PATH.read_text(encoding="utf-8")
        self.main = MAIN_PATH.read_text(encoding="utf-8")
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
            "return left == right;",
            "bv = na < nb;",
            "bv = na > nb;",
            "bv = na <= nb;",
            "bv = na >= nb;",
            "else if (op == JS_TOK_SLASH)  v = na / nb;",
            "else if (op == JS_TOK_SLASH_EQ) r = na / nb;",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, self.interpreter)

        self.assertNotIn("js_dcmp", self.interpreter)
        self.assertNotIn("js_dnz", self.interpreter)
        self.assertIn("else v = 0.0 / 0.0;", self.interpreter)

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
            "reject=%d",
        ):
            with self.subTest(field=field):
                self.assertEqual(self.interpreter.count(field), 2)
        self.assertIn(
            "int reject_ok = js_run(bad_script, b_strlen(bad_script)) != 0;",
            self.compact_interpreter,
        )

    def test_decimal_literals_keep_double_tokens_and_ast_values(self):
        lexer = (REPO_ROOT / "bin" / "browser" / "js_lex.cc").read_text(
            encoding="utf-8"
        )
        parser = (REPO_ROOT / "bin" / "browser" / "js_parse.cc").read_text(
            encoding="utf-8"
        )
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
            lexer,
        )
        self.assertIn("place = place * 0.1;", lexer)
        self.assertIn("exponent < 400", lexer)
        self.assertIn(
            'js_set_err("js: expected exponent digits");',
            lexer,
        )
        self.assertIn("return -1;", lexer)
        self.assertIn("if (js_tokenize(src, len) != 0)", parser)
        self.assertIn("jn_num[node] = v;", parser)
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
        ):
            with self.subTest(binding=binding):
                self.assertIn(
                    f'js_selftest_binding_is_true("{binding}")',
                    self.interpreter,
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
