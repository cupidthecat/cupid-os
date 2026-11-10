import unittest

from tests.c_source_spelling import _c_spelling_ranges, c_block, c_literal_text


class CSourceSpellingTests(unittest.TestCase):
    def test_blocks_ignore_literal_and_comment_braces_and_reject_ambiguity(self):
        source = 'before(); if (ready) { f("}"); /* } */ if (x) { g(); } } after();'
        start, end = c_block(source, 'if(ready)')
        self.assertEqual(source[start:end],
                         'if (ready) { f("}"); /* } */ if (x) { g(); } }')
        for invalid in ('if (ready) f();', 'if (ready) { f();',
                        'if (ready) {} if (ready) {}'):
            with self.subTest(source=invalid), self.assertRaises(ValueError):
                c_block(invalid, 'if (ready)')

    def test_marker_literals_join_only_across_adjacent_string_tokens(self):
        text = c_literal_text('f("PASS " /* join */ "count=%d\\n", n); g("other");')
        self.assertIn('PASS count=%d\\n', text)
        self.assertNotIn('count=%d\\nother', text)
        self.assertNotIn('PASS count=%d', c_literal_text('f("FAIL " "count=%d");'))

    def test_code_checks_preserve_operators_identifiers_and_literal_bytes(self):
        fragment = 'if (value >= 0) return call("two words");'
        source = 'if ( value /* comment */ >= 0 )\n    return call ( "two words" );'
        self.assertEqual(len(_c_spelling_ranges(source, fragment)), 1)
        for changed in (source.replace('>=', '>'), source.replace('return', 'returning'),
                        source.replace('two words', 'twowords'), '/* ' + source + ' */'):
            with self.subTest(source=changed):
                self.assertFalse(_c_spelling_ranges(changed, fragment))


if __name__ == '__main__':
    unittest.main()
