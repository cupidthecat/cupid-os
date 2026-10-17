from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "bin" / "godsong.cc"


class GodsongSeedReportingTests(unittest.TestCase):
    def test_reported_seed_is_not_the_advanced_rng_state(self):
        text = SOURCE.read_text(encoding="utf-8")

        self.assertIn("int gs_input_seed;", text)
        self.assertGreaterEqual(text.count("gs_input_seed = gs_seed;"), 2)
        self.assertIn(
            'print("godsong seed=");\n  print_int(gs_input_seed);',
            text,
        )
        self.assertNotIn(
            'print("godsong seed=");\n  print_int(gs_seed);',
            text,
        )


if __name__ == "__main__":
    unittest.main()
