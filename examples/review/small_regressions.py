"""Behavioral checks for the small review fixes; all C++ fixtures live here."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--clang', required=True)
parser.add_argument('--plugin', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
ARGS, REMAINING = parser.parse_known_args()
ARGS.plugin = ARGS.plugin.resolve()
ARGS.build = ARGS.build.resolve()


class SmallRegressions(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='review-', dir=ARGS.build)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)

    def run_command(self, cmd, **kwargs):
        return subprocess.run(cmd, text=True, capture_output=True, timeout=30, **kwargs)

    def compile_case(self, fixture, case, plugin=None, extra=()):
        return self.run_command([
            ARGS.clang, '-std=c++20', '-fsyntax-only',
            f'-fplugin={plugin or ARGS.plugin}',
            '-fplugin-arg-dtt---dtt-mode=enforce', f'-DCASE={case}',
            *extra, str(FIXTURES / fixture),
        ])

    def check_guard_cases(self, plugin, extra):
        for case in range(5):
            with self.subTest(case=case):
                p = self.compile_case('method_guard.cpp', case, plugin, extra)
                if case < 3:
                    self.assertEqual(p.returncode, 0, p.stderr)
                else:
                    self.assertEqual(p.returncode, 1, p.stderr)
                    self.assertRegex(p.stderr, r'error: DTT:.*_size')

    def test_external_method_guards(self):
        self.check_guard_cases(ARGS.plugin, [
            f'-fplugin-arg-dtt---dtt-annotations={ROOT / "annotations/test_when.dtt"}'
        ])

    @unittest.skipIf(os.name == 'nt', 'Built-in relocation currently uses dladdr')
    def test_builtin_method_guards(self):
        lib = self.work / 'lib'
        lib.mkdir()
        annotations = self.work / 'annotations'
        annotations.mkdir()
        staged = lib / ARGS.plugin.name
        shutil.copy2(ARGS.plugin, staged)
        shutil.copy2(ROOT / 'annotations/test_when.dtt', annotations / 'std.dtt')
        self.check_guard_cases(staged, [])

    def test_libc_contracts(self):
        for case in range(9):
            with self.subTest(case=case):
                p = self.compile_case('libc_contracts.cpp', case)
                if case not in (5, 6):
                    self.assertEqual(p.returncode, 0, p.stderr)
                else:
                    self.assertEqual(p.returncode, 1, p.stderr)
                    self.assertRegex(p.stderr, r'error: DTT:.*' + ('@fd' if case == 5 else '_nonnull'))

    def test_standalone_builtins(self):
        # Remove access to the build machine's source fallback without renaming
        # the real source tree or interfering with concurrent tests.
        staged = self.work / ARGS.plugin.name
        source_path = str(ROOT / 'annotations').encode()
        staged.write_bytes(ARGS.plugin.read_bytes().replace(
            source_path, b'/' + b'x' * (len(source_path) - 1)))
        for case in range(4):
            with self.subTest(case=case):
                p = self.compile_case('relocated_builtins.cpp', case, staged)
                self.assertEqual(p.returncode, 1 if case in (0, 2) else 0, p.stderr)
                if case in (0, 2):
                    self.assertRegex(p.stderr, r'error: DTT:')

    def test_stdin_sarif(self):
        for source in ('int main() { return 0; }', 'void f() { int* p = nullptr; *p = 1; }'):
            with self.subTest(source=source):
                output = self.work / 'stdin.sarif'
                p = self.run_command([
                    ARGS.clang, '-std=c++20', '-fsyntax-only', '-x', 'c++', '-',
                    f'-fplugin={ARGS.plugin}', f'-fplugin-arg-dtt---sarif={output}',
                ], input=source)
                self.assertIn(p.returncode, (0, 1), p.stderr)
                self.assertTrue(output.exists(), p.stderr)
                data = json.loads(output.read_text())
                artifact = data['runs'][0]['artifacts'][0]
                self.assertEqual(artifact['location']['uri'], 'stdin.cpp')
                self.assertEqual(artifact['contents']['text'], source)
                results = data['runs'][0]['results']
                self.assertEqual(bool(results), 'nullptr' in source)
                if results:
                    loc = results[0]['locations'][0]['physicalLocation']['artifactLocation']
                    self.assertEqual(loc['uri'], 'stdin.cpp')
                    self.assertNotIn('uriBaseId', loc)
                validation = self.run_command([
                    sys.executable, str(ROOT / 'tests/sarif/validate_sarif.py'), str(output),
                ])
                self.assertEqual(validation.returncode, 0, validation.stdout + validation.stderr)

    def test_sarif_runner_compiler(self):
        # The supplied compiler must win over any clang++-18 on PATH.
        compiler = self.work / 'configured compiler'
        marker = self.work / 'compiler-invoked'
        compiler.write_text('#!/bin/sh\nprintf invoked > "$DTT_COMPILER_MARKER"\nexec "$DTT_REAL_CLANG" "$@"\n')
        compiler.chmod(0o755)
        env = dict(os.environ, DTT_COMPILER_MARKER=str(marker), DTT_REAL_CLANG=ARGS.clang)
        p = self.run_command([
            'bash', str(ROOT / 'tests/sarif/run_sarif_test.sh'), str(compiler),
            str(ROOT / 'examples/2000_sarif_output_pass.cpp'), str(ARGS.plugin),
            '--expect-no-results',
        ], env=env)
        self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
        self.assertTrue(marker.exists(), 'configured compiler was not invoked')

    def test_relative_sarif_paths(self):
        for directory, source in (
            (ROOT / 'examples', '131_nullptr_deref_fail.cpp'),
            (ROOT / 'examples/review', '../131_nullptr_deref_fail.cpp'),
            (ROOT, 'examples/../examples/131_nullptr_deref_fail.cpp'),
        ):
            with self.subTest(directory=directory, source=source):
                output = self.work / 'relative.sarif'
                p = self.run_command([
                    ARGS.clang, '-std=c++20', '-fsyntax-only', f'-fplugin={ARGS.plugin}',
                    f'-fplugin-arg-dtt---sarif={output}', source,
                ], cwd=directory)
                self.assertEqual(p.returncode, 1, p.stderr)
                results = json.loads(output.read_text())['runs'][0]['results']
                self.assertTrue(results)
                for result in results:
                    loc = result['locations'][0]['physicalLocation']['artifactLocation']
                    self.assertEqual(loc['uri'], 'examples/131_nullptr_deref_fail.cpp')

    def test_sarif_ctest_checkout_permissions(self):
        p = self.run_command(['ctest', '--test-dir', str(ARGS.build), '--show-only=json-v1'])
        self.assertEqual(p.returncode, 0, p.stderr)
        tests = [t for t in json.loads(p.stdout)['tests'] if t['name'].startswith('sarif_')]
        if not tests:
            self.skipTest('DTT_TEST_SARIF is disabled')
        self.assertEqual(len(tests), 4)
        for test in tests:
            with self.subTest(test=test['name']):
                command = test['command']
                self.assertEqual(Path(command[0]).name, 'bash', 'must work with a non-executable checkout')
                self.assertEqual(command[2], ARGS.clang)

    @unittest.skipUnless(shutil.which('lua'), 'Lua unavailable')
    def test_nvim_parser(self):
        p = self.run_command(['lua', str(FIXTURES / 'nvim_parser.lua'),
                              str(ROOT / 'ide-plugins/nvim-dtt/lua/dtt/init.lua')])
        self.assertEqual(p.returncode, 0, p.stderr)

    @unittest.skipUnless(shutil.which('vim') and os.name != 'nt', 'Unix Vim unavailable')
    def test_vim_command_paths(self):
        directory = self.work / "path with spaces and 'quote"
        directory.mkdir()
        compiler = directory / 'clang++'
        compiler.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        compiler.chmod(0o755)
        plugin = directory / 'libDTTPlugin.so'
        plugin.touch()
        errors = self.work / 'vim-errors'
        env = dict(os.environ, DTT_TEST_CLANG=str(compiler), DTT_TEST_PLUGIN=str(plugin),
                   DTT_TEST_ROOT=str(ROOT), DTT_TEST_ERRORS=str(errors))
        p = self.run_command(['vim', '-Nu', 'NONE', '-i', 'NONE', '-n', '-es',
                              '-S', str(FIXTURES / 'vim_command.vim')], env=env)
        self.assertEqual(p.returncode, 0, errors.read_text() if errors.exists() else p.stderr)


if __name__ == '__main__':
    unittest.main(argv=[__file__, *REMAINING])
