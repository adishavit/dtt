"""Runs inside a disposable Railway VM; compiles only inside Bubblewrap."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile

OUTPUT_LIMIT = 131072


def compile_source(code, mode):
    with tempfile.TemporaryDirectory(prefix='dtt-') as directory:
        source = Path(directory) / 'input.cpp'
        source.write_text(code)
        source.chmod(0o644)
        command = [
            'bwrap', '--unshare-pid', '--unshare-net', '--unshare-ipc',
            '--unshare-uts', '--die-with-parent', '--new-session', '--clearenv',
            '--setenv', 'PATH', '/usr/bin:/bin', '--setenv', 'LANG', 'C.UTF-8',
            '--ro-bind', '/usr', '/usr', '--symlink', 'usr/bin', '/bin',
            '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib64', '/lib64',
            '--ro-bind', '/opt/dtt/libDTTPlugin.so', '/plugin.so',
            '--ro-bind', str(source), '/input.cpp', '--tmpfs', '/tmp',
            '--dev', '/dev', '--proc', '/proc', '--chdir', '/',
            'setpriv', '--reuid=65534', '--regid=65534', '--clear-groups',
            '--no-new-privs', '--bounding-set=-all',
            'prlimit', '--as=536870912', '--cpu=5', '--fsize=131072',
            '--nofile=64', '--nproc=16', '--core=0',
            'clang++-18', '-fplugin=/plugin.so', '-std=c++20', '-fsyntax-only',
            '-fno-color-diagnostics', '-ferror-limit=20', '-Wno-unused-result',
            '-Wno-unused-value', '-fplugin-arg-dtt--dtt-mode=' + mode, '/input.cpp',
        ]
        with tempfile.TemporaryFile() as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True, env={'PATH': '/usr/bin:/bin'})
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                return {'returncode': -1, 'output': 'Compilation timed out (10 seconds).'}
            output.seek(0)
            text = output.read(OUTPUT_LIMIT).decode('utf8', errors='replace').replace('/input.cpp', 'input.cpp')
            if process.returncode not in (0, 1) or text.startswith('bwrap:'):
                raise RuntimeError(f'Compiler infrastructure failure ({process.returncode}): {text}')
            return {'returncode': process.returncode, 'output': text}


if __name__ == '__main__':
    request = json.loads(Path('/opt/dtt/request.json').read_text())
    if (not isinstance(request.get('code'), str) or
            len(request['code'].encode()) > 32768 or
            request.get('mode') not in ('enforce', 'inform', 'quiet')):
        raise ValueError('Invalid compilation request')
    print(json.dumps(compile_source(request['code'], request['mode'])))
