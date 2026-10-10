"""Install, relocate, and load DTT with its bundled Z3 runtime (Linux)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--clang', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='runtime-package-', dir=args.build.resolve()) as temp:
    stage = Path(temp) / 'stage'
    subprocess.run(['cmake', '--install', str(args.build), '--prefix', str(stage)], check=True,
                   stdout=subprocess.DEVNULL)
    relocated = Path(temp) / 'relocated prefix'
    stage.rename(relocated)
    plugin = relocated / 'lib/libDTTPlugin.so'
    libraries = list((relocated / 'lib').glob('libz3.so*'))
    assert libraries, 'installed package omits the Z3 runtime'
    env = dict(os.environ)
    env.pop('LD_LIBRARY_PATH', None)
    env.pop('LD_PRELOAD', None)
    deps = subprocess.check_output(['ldd', str(plugin)], text=True, env=env)
    z3 = next(line for line in deps.splitlines() if 'libz3.' in line)
    assert str(relocated / 'lib') in z3, f'plugin loads system Z3 instead of bundle: {z3}'
    assert 'not found' not in deps, deps
    for fixture, expected in [('01_basic_pass.cpp', 0), ('02_basic_fail.cpp', 1),
                              ('954_z3_evaluation_pass.cpp', 0)]:
        run_env = dict(env, LD_DEBUG='libs')
        run = subprocess.run([args.clang, f'-fplugin={plugin}', '-std=c++20', '-fsyntax-only',
                              str(root / 'examples' / fixture)], capture_output=True, text=True, env=run_env)
        assert run.returncode == expected, run.stderr
        assert any('calling init:' in line and str(relocated / 'lib/libz3') in line
                   for line in run.stderr.splitlines()), 'Clang did not load the bundled Z3'
        if expected:
            assert 'error: DTT:' in run.stderr, run.stderr
print('Relocated bundle resolves its own Z3 and passes compile probes')
