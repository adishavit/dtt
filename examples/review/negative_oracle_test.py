"""A compiler failure must not masquerade as a successful DTT regression."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'tests/expect_dtt_error.py'


class NegativeOracle(unittest.TestCase):
    def check(self, output, code=1, marker='// dtt-error {{_nonnull}}', expected=1):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'case with spaces.cpp'
            source.write_text('int x; ' + marker + '\n')
            diagnostic = output.replace('SOURCE', str(source))
            child = 'import sys; sys.stderr.write(sys.argv[1]); sys.exit(int(sys.argv[2]))'
            result = subprocess.run([sys.executable, str(RUNNER), str(source), '--',
                                     sys.executable, '-c', child, diagnostic, str(code)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_rejects_non_dtt_failures(self):
        for output in ['SOURCE:1:3: error: expected expression\n',
                       'error: unable to load plugin\n', 'segmentation fault\n',
                       'SOURCE:1:3: warning: DTT: tracking lost\n']:
            with self.subTest(output=output):
                self.check(output)

    def test_requires_normal_error_exit(self):
        for code in (0, 2, -11):
            with self.subTest(code=code):
                self.check('SOURCE:1:3: error: DTT: _nonnull violated\n', code=code)

    def test_rejects_missing_location_expectations(self):
        self.check('SOURCE:1:3: error: DTT: _nonnull violated\n', marker='')

    def test_requires_each_independent_diagnostic(self):
        self.check('SOURCE:1:3: error: DTT: _nonnull violated\n',
                   marker='// dtt-error {{_nonnull}} dtt-error {{_size}}')

    def test_accepts_dtt_error(self):
        self.check('SOURCE:1:3: error: DTT: _nonnull violated\n', expected=0)

    def test_rejects_mixed_syntax_error(self):
        self.check('SOURCE:1:3: error: DTT: _nonnull violated\nSOURCE:2:1: error: unknown type\n')

    def test_checks_location_and_message(self):
        marker = '// dtt-error {{_nonnull}}'
        for output, expected in [('SOURCE:1:3: error: DTT: _nonnull violated\n', 0),
                                 ('SOURCE:2:3: error: DTT: _nonnull violated\n', 1),
                                 ('SOURCE:1:3: error: DTT: _size violated\n', 1),
                                 ('other.cpp:1:3: error: DTT: _nonnull violated\n', 1)]:
            with self.subTest(output=output):
                self.check(output, marker=marker, expected=expected)

    def test_ignores_error_text_in_source_excerpt(self):
        self.check('SOURCE:1:3: error: DTT: _nonnull violated\n'
                   '   1 | *p; // Should error: null pointer\n', expected=0)

    def test_unrelated_windows_path_does_not_satisfy_marker(self):
        # A diagnostic for another file cannot satisfy the source marker.
        self.check('C:/work/src.cpp:7:3: error: DTT: _nonnull violated\n')


if __name__ == '__main__':
    unittest.main()
