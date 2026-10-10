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
parser.add_argument('--isolated', action='store_true',
                    help='Also test the archive with bubblewrap, hiding host Z3 and the repository')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='runtime-package-', dir=args.build.resolve()) as temp:
    stage = Path(temp) / 'stage'
    subprocess.run(['cmake', '--install', str(args.build), '--prefix', str(stage)], check=True,
                   stdout=subprocess.DEVNULL)
    relocated = Path(temp) / 'relocated prefix'
    archive = Path(temp) / 'dtt-linux-x86_64.tar.gz'
    subprocess.run(['tar', '-czf', str(archive), '-C', str(stage), '.'], check=True)
    relocated.mkdir()
    subprocess.run(['tar', '-xf', str(archive), '-C', str(relocated)], check=True)
    shutil.rmtree(stage)
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
    if args.isolated:
        bwrap = shutil.which('bwrap')
        assert bwrap, '--isolated requires bubblewrap'
        compiler = Path(shutil.which(args.clang) or args.clang).resolve()
        assert compiler.is_relative_to('/usr'), 'isolated compiler must be installed under /usr'
        fixtures = Path(temp) / 'fixtures'
        fixtures.mkdir()
        cases = [('01_basic_pass.cpp', 0), ('02_basic_fail.cpp', 1),
                 ('45_std_vector_pass.cpp', 0), ('46_std_vector_fail.cpp', 1),
                 ('954_z3_evaluation_pass.cpp', 0)]
        for fixture, _ in cases:
            shutil.copy2(root / 'examples' / fixture, fixtures / fixture)
        sandbox = [bwrap, '--die-with-parent', '--unshare-all', '--clearenv',
                   '--ro-bind', '/usr', '/usr', '--symlink', 'usr/bin', '/bin',
                   '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib64', '/lib64',
                   '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp',
                   '--ro-bind', str(relocated), '/package with spaces',
                   '--ro-bind', str(fixtures), '/fixtures', '--chdir', '/fixtures',
                   '--setenv', 'PATH', '/usr/bin', '--setenv', 'LD_DEBUG', 'libs']
        # Mask real targets as well as symlinks: no installed Z3 can rescue a
        # broken archive. The repository and original installation are absent.
        system_z3 = {path.resolve() for directory in ['/usr/lib', '/usr/local/lib']
                     for pattern in ['libz3.so*', '*/libz3.so*']
                     for path in Path(directory).glob(pattern)}
        for path in sorted(system_z3):
            sandbox += ['--ro-bind', '/dev/null', str(path)]
        isolated_plugin = '/package with spaces/lib/libDTTPlugin.so'
        for fixture, expected in cases:
            run = subprocess.run(sandbox + [str(compiler), f'-fplugin={isolated_plugin}',
                                 '-std=c++20', '-fsyntax-only', fixture],
                                 capture_output=True, text=True)
            assert run.returncode == expected, run.stderr
            assert any('calling init:' in line and '/package with spaces/lib/libz3' in line
                       for line in run.stderr.splitlines()), run.stderr
            if expected:
                assert 'error: DTT:' in run.stderr, run.stderr
        # Negative control: without bundled Z3 this same environment must fail
        # to load the plugin, rather than silently using a host installation.
        without_z3 = Path(temp) / 'without-z3'
        without_z3.mkdir()
        shutil.copy2(plugin, without_z3 / plugin.name)
        run = subprocess.run(sandbox + ['--ro-bind', str(without_z3), '/without-z3',
                             str(compiler), '-fplugin=/without-z3/libDTTPlugin.so',
                             '-std=c++20', '-fsyntax-only', '01_basic_pass.cpp'],
                             capture_output=True, text=True)
        assert run.returncode != 0 and 'unable to load plugin' in run.stderr, run.stderr
        assert 'libz3' in run.stderr, run.stderr
        print('Archive passes isolated probes; removing bundled Z3 fails as expected')
print('Relocated bundle resolves its own Z3 and passes compile probes')
