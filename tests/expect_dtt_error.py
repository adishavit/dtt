"""Require a DTT error, optionally at each // dtt-error {{regex}} location.

Legacy files without markers require at least one DTT error. Ordinary compiler
errors, crashes, missing executables, and successful compilations never pass.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('compiler command required after --')
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f'Compiler execution failed: {error}', file=sys.stderr)
        return 1
    output = result.stdout + result.stderr
    errors = [line for line in output.splitlines() if re.match(r'^(?:.+:\d+(?::\d+)?: |[^\s:]+: )?(?:fatal )?error:', line)]
    failures = []
    if result.returncode != 1:
        failures.append(f'Expected compiler exit 1, got {result.returncode}')
    if not any('error: DTT:' in line for line in errors):
        failures.append('Expected a DTT error')
    if any('error: DTT:' not in line for line in errors):
        failures.append('Unexpected compiler error unrelated to DTT')
    locations = []
    for line in errors:
        match = re.match(r'^(.*):(\d+):\d+: error: DTT:(.*)$', line)
        if match and Path(match[1]).resolve() == args.source.resolve():
            locations.append((int(match[2]), match[3]))
    for number, line in enumerate(args.source.read_text().splitlines(), 1):
        for pattern in re.findall(r'\bdtt-error\s*\{\{(.*?)\}\}', line):
            try:
                found = next((i for i, (at, message) in enumerate(locations)
                              if at == number and re.search(pattern, message)), None)
            except re.error as error:
                failures.append(f'{args.source}:{number}: invalid expectation: {error}')
                continue
            if found is None:
                failures.append(f'{args.source}:{number}: missing DTT error matching {pattern!r}')
            else:
                locations.pop(found)
    if failures:
        print(output, end='', file=sys.stderr)
        print('\n'.join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
