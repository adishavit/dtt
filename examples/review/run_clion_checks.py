"""Run standalone CLion checks against the cached SDK used by Gradle."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cache = Path.home() / '.gradle/caches'
sdk = next(cache.glob('transforms-*/**/CLion-2025.1/lib/app.jar')).parent
modules = cache / 'modules-2/files-2.1'
compiler = next(modules.glob('org.jetbrains.kotlin/kotlin-compiler-embeddable/2.0.21/*/*.jar'))
stdlib = next(modules.glob('org.jetbrains.kotlin/kotlin-stdlib/2.0.21/*/*.jar'))
trove = next(modules.glob('org.jetbrains.intellij.deps/trove4j/*/*/*.jar'))
coroutines = next(modules.glob('org.jetbrains.kotlinx/kotlinx-coroutines-core-jvm/*/*/*.jar'))
classpath = os.pathsep.join(map(str, [root / 'ide-plugins/clion-dtt/build/classes/kotlin/main', *sdk.glob('*.jar')]))
with tempfile.TemporaryDirectory(prefix='dtt-clion-') as work:
    compilerpath = os.pathsep.join(map(str, [compiler, stdlib, trove, coroutines, sdk / 'annotations.jar']))
    subprocess.run(['java', '-cp', compilerpath, 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
                    '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', classpath,
                    '-d', work, str(root / 'examples/review/clion_process.kt')], check=True)
    subprocess.run(['java', '-cp', work + os.pathsep + classpath, 'Clion_processKt',
                    str(root / 'examples/review/fake_clang.cjs'), work,
                    str(root / 'build/libDTTPlugin.so')], check=True, timeout=60)
