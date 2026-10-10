"""Isolated analyzer regressions: each case must produce its intended diagnostic."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--clang', required=True)
parser.add_argument('--plugin', type=Path, required=True)
parser.add_argument('--libcxx', action='store_true')
ARGS, REMAINING = parser.parse_known_args()
ARGS.plugin = ARGS.plugin.resolve()


class CoreRegressions(unittest.TestCase):
    def compile_case(self, fixture, case, mode='enforce', extra=()):
        return subprocess.run([
            ARGS.clang, '-std=c++20', '-fsyntax-only',
            f'-fplugin={ARGS.plugin}', f'-fplugin-arg-dtt---dtt-mode={mode}',
            f'-DCASE={case}', *extra, str(ROOT / 'examples/review' / fixture),
        ], text=True, capture_output=True, timeout=30)

    def check(self, fixture, case, diagnostic=None, **kwargs):
        p = self.compile_case(fixture, case, **kwargs)
        self.assertEqual(p.returncode, 1 if diagnostic else 0, p.stderr)
        if diagnostic:
            self.assertRegex(p.stderr, r'error: DTT:.*' + diagnostic)

    def test_variable_guard_join(self):
        for case in range(9):
            with self.subTest(case=case):
                self.check('branch_join.cpp', case, '_size' if case in (0, 1, 2, 6) else None)
        for case in (0, 1, 2):
            with self.subTest(case=case, mode='quiet'):
                # An uncertain access must not become a proven violation.
                self.check('branch_join.cpp', case, mode='quiet')

    def test_union_discriminant_join(self):
        for case in range(4):
            with self.subTest(case=case):
                self.check('union_join.cpp', case, 'inactive union member' if case == 2 else None)

    def test_presence_state(self):
        for case in range(22):
            with self.subTest(case=case):
                field = '_has_value' if case in (8, 9, 13, 15, 17, 18) else '_nonnull'
                self.check('presence_state.cpp', case, None if case in (3, 5, 10, 11, 16, 19, 21) else field)

    def test_expected_callback(self):
        for case in range(5):
            with self.subTest(case=case):
                self.check('expected_callback.cpp', case, '_has_value' if case < 3 else None)
        for case in (0, 1):
            with self.subTest(case=case, mode='quiet'):
                self.check('expected_callback.cpp', case, mode='quiet')

    def test_member_initialization(self):
        for case in range(17):
            with self.subTest(case=case):
                self.check('member_initialization.cpp', case,
                           (r'_state = 0' if case in (0, 1, 2, 11) else
                            r'_state = 2' if case in (12, 13, 14) else None),
                           mode='quiet')

    def test_list_construction(self):
        libraries = ((), ('-stdlib=libc++',)) if ARGS.libcxx else ((),)
        for library in libraries:
            for case in range(10):
                with self.subTest(case=case, library=library):
                    self.check('list_construction.cpp', case,
                               '_size' if case in (2, 3, 4, 6, 9) else None,
                               extra=library)
                    if case in (2, 3, 4, 6, 9):
                        self.check('list_construction.cpp', case, '_size',
                                   mode='quiet', extra=library)

    def test_annotation_errors(self):
        cases = {
            'unknown_top_level': 'nonsense\n',
            'unknown_trait': 'type Broken : MissingTrait {\n}\n',
            'bad_arity': 'fn broken(nope) {\n}\n',
            'fn_directive': 'fn broken(1) {\n requries arg0._nonnull != 0\n}\n',
            'method_directive': 'type Broken {\n broken(1):\n requries _size > 0\n}\n',
            'trait_directive': 'trait Broken {\n broken(1):\n requries _size > 0\n}\n',
            'bad_requires': 'fn broken(1) {\n requires arg0._nonnull\n}\n',
            'bad_guard': 'fn broken(1) {\n ensures result.@open = 1 when nonsense\n}\n',
            'missing_brace': 'fn broken(0) {\n',
            'missing_file': None,
            'valid': 'fn valid(1) {\n requires arg0._nonnull != 0\n}\n',
        }
        with tempfile.TemporaryDirectory(prefix='annotations-', dir=ARGS.plugin.parent) as directory:
            for name, content in cases.items():
                with self.subTest(case=name):
                    annotation = Path(directory) / f'{name}.dtt'
                    if content is not None:
                        annotation.write_text(content)
                    self.check('union_join.cpp', 3, None if name == 'valid' else 'annotation',
                               extra=[f'-fplugin-arg-dtt---dtt-annotations={annotation}'])


if __name__ == '__main__':
    unittest.main(argv=[__file__, *REMAINING])
