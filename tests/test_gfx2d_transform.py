import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TRANSFORM_SOURCE = REPO_ROOT / "kernel" / "gfx" / "gfx2d_transform.cc"
GFXGUI_SOURCE = REPO_ROOT / "bin" / "gfxgui_test.cc"
FP_SHIFT = 16
FP_ONE = 1 << FP_SHIFT


class Gfx2DTransformContractTests(unittest.TestCase):
    def test_production_inverse_executes_full_precision_boundary_cases(self):
        compiler_names = (
            ("g++", "clang++")
            if os.name == "nt"
            else ("clang++", "g++")
        )
        compiler = next(
            (path for name in compiler_names if (path := shutil.which(name))),
            None,
        )
        if compiler is None:
            self.skipTest("a hosted C++ compiler is required")

        harness = textwrap.dedent(
            f"""
            #include "{TRANSFORM_SOURCE.as_posix()}"

            void *memcpy(void *dst, const void *src, size_t size) {{
              unsigned char *out = (unsigned char *)dst;
              const unsigned char *in = (const unsigned char *)src;
              size_t i;
              for (i = 0; i < size; i++) out[i] = in[i];
              return dst;
            }}

            void serial_printf(const char *, ...) {{}}
            uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {{
              return dividend / divisor;
            }}
            const uint32_t *gfx2d_image_data(int, int *, int *) {{
              return 0;
            }}
            int gfx2d_width(void) {{ return 640; }}
            int gfx2d_height(void) {{ return 480; }}
            void gfx2d_pixel(int, int, uint32_t) {{}}
            int gfx2d_sprite_width(int) {{ return 0; }}
            int gfx2d_sprite_height(int) {{ return 0; }}
            uint32_t gfx2d_sprite_get_pixel(int, int, int) {{ return 0; }}
            void gfx2d_text_ex(int, int, const char *, uint32_t, int, int) {{}}

            static int expect_inverse(const int words[6],
                                      const int expected[6]) {{
              g2d_mat_t source;
              g2d_mat_t inverse;
              int i;
              for (i = 0; i < 6; i++) source.m[i] = words[i];
              if (mat_invert(&source, &inverse) != 0) return 0;
              for (i = 0; i < 6; i++)
                if (inverse.m[i] != expected[i]) return 0;
              return 1;
            }}

            static int expect_rejected(const int words[6]) {{
              g2d_mat_t source;
              g2d_mat_t inverse;
              int i;
              for (i = 0; i < 6; i++) source.m[i] = words[i];
              return mat_invert(&source, &inverse) < 0;
            }}

            int main(void) {{
              const int identity[6] =
                  {{65536, 0, 0, 65536, 0, 0}};
              const int identity_inverse[6] =
                  {{65536, 0, 0, 65536, 0, 0}};
              const int scale_256[6] =
                  {{16777216, 0, 0, 16777216, 0, 0}};
              const int scale_256_inverse[6] =
                  {{256, 0, 0, 256, 0, 0}};
              const int reflection[6] =
                  {{-65536, 0, 0, 65536, 0, 0}};
              const int reflection_inverse[6] =
                  {{-65536, 0, 0, 65536, 0, 0}};
              const int low_positive_det[6] =
                  {{256, 1, 1, 256, 0, 0}};
              const int low_positive_inverse[6] =
                  {{16777472, -65537, -65537, 16777472, 0, 0}};
              const int low_negative_det[6] =
                  {{1, 256, 256, 1, 0, 0}};
              const int low_negative_inverse[6] =
                  {{-65537, 16777472, 16777472, -65537, 0, 0}};
              const int minimum_coefficient[6] =
                  {{-2, 0, 0, -2147483647 - 1, 0, 0}};
              const int minimum_coefficient_inverse[6] =
                  {{-2147483647 - 1, 0, 0, -2, 0, 0}};
              const int minimum_translation[6] =
                  {{32768, 0, 0, 65536, 1073741824, 0}};
              const int minimum_translation_inverse[6] =
                  {{131072, 0, 0, 65536, -2147483647 - 1, 0}};
              const int two_term_translation[6] =
                  {{65536, 65536, 65536, 131072,
                    196608, 327680}};
              const int two_term_translation_inverse[6] =
                  {{131072, -65536, -65536, 65536,
                    -65536, -131072}};
              const int singular[6] =
                  {{65536, 65536, 65536, 65536, 0, 0}};
              const int coefficient_overflow[6] =
                  {{1, 0, 0, 1, 0, 0}};
              const int translation_overflow[6] =
                  {{32768, 0, 0, 65536, 1310720000, 0}};
              const int positive_translation_overflow[6] =
                  {{32768, 0, 0, 65536, -1073741824, 0}};

              if (!expect_inverse(identity, identity_inverse)) return 1;
              if (!expect_inverse(scale_256, scale_256_inverse)) return 2;
              if (!expect_inverse(reflection, reflection_inverse)) return 3;
              if (!expect_inverse(low_positive_det,
                                  low_positive_inverse)) return 4;
              if (!expect_inverse(low_negative_det,
                                  low_negative_inverse)) return 5;
              if (!expect_inverse(minimum_coefficient,
                                  minimum_coefficient_inverse)) return 6;
              if (!expect_inverse(minimum_translation,
                                  minimum_translation_inverse)) return 7;
              if (!expect_inverse(two_term_translation,
                                  two_term_translation_inverse)) return 8;
              if (!expect_rejected(singular)) return 9;
              if (!expect_rejected(coefficient_overflow)) return 10;
              if (!expect_rejected(translation_overflow)) return 11;
              if (!expect_rejected(positive_translation_overflow)) return 12;
              return 0;
            }}
            """
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "gfx2d-transform-probe.cc"
            executable = root / (
                "gfx2d-transform-probe.exe"
                if os.name == "nt"
                else "gfx2d-transform-probe"
            )
            source.write_text(harness, encoding="utf-8")
            command = [
                compiler,
                "-std=c++17",
                "-fno-builtin",
                str(source),
                "-o",
                str(executable),
            ]
            for include in (
                REPO_ROOT / "kernel" / "core",
                REPO_ROOT / "kernel" / "cpu",
                REPO_ROOT / "kernel" / "gfx",
                REPO_ROOT / "drivers",
            ):
                command.extend(("-I", str(include)))
            built = subprocess.run(
                command,
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
            self.assertEqual(
                built.returncode,
                0,
                msg=built.stdout + built.stderr,
            )
            executed = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            self.assertEqual(
                executed.returncode,
                0,
                msg=executed.stdout + executed.stderr,
            )

    def test_large_scale_divides_coefficients_without_narrowing_determinant(
        self,
    ):
        scale = 256 * FP_ONE
        determinant_32_32 = scale * scale
        inverse_scale = (scale * FP_ONE * FP_ONE) // determinant_32_32
        self.assertGreater(
            determinant_32_32 >> FP_SHIFT,
            0x7FFFFFFF,
        )
        self.assertEqual(inverse_scale, FP_ONE // 256)

        source = TRANSFORM_SOURCE.read_text(encoding="utf-8")
        self.assertIn("static int mat_div_coeff(", source)
        self.assertEqual(source.count("det64, &inv->m["), 4)
        self.assertNotIn("det64 >> FP_SHIFT", source)

    def test_inverse_rejects_coefficients_that_do_not_fit_a_matrix_word(self):
        quotient_for_smallest_positive_det = (FP_ONE * FP_ONE) // 1
        self.assertGreater(quotient_for_smallest_positive_det, 0x7FFFFFFF)

        source = TRANSFORM_SOURCE.read_text(encoding="utf-8")
        self.assertIn("uint64_t negative_limit", source)
        self.assertRegex(
            source,
            r"quotient\s*>\s*negative_limit\)\s*return\s+-1",
        )
        self.assertRegex(
            source,
            r"quotient\s*>\s*\(uint64_t\)2147483647\)\s*"
            r"return\s+-1",
        )
        self.assertIn("*result = -2147483647 - 1;", source)

    def test_inverse_translation_is_accumulated_and_checked_in_64_bits(self):
        source = TRANSFORM_SOURCE.read_text(encoding="utf-8")
        self.assertIn("static int mat_inverse_translation(", source)
        self.assertEqual(source.count("mat_inverse_translation("), 3)
        self.assertNotIn("inv->m[4] = -(FP_MUL", source)

    def test_fractional_demo_scale_uses_two_fixed_point_operands(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        fixed_ratio = "fp_div(fp_from_int(3), fp_from_int(2))"
        self.assertEqual(source.count(fixed_ratio), 2)
        self.assertNotIn("fp_div(fp_from_int(3), 2)", source)

    def test_demo_matrix_translation_uses_fixed_point_words(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        self.assertIn("m[4] = m[4] + fp_from_int(8);", source)
        self.assertIn("m[5] = m[5] + fp_from_int(4);", source)
        self.assertIn("ox != 468 || oy != 154", source)


if __name__ == "__main__":
    unittest.main()
