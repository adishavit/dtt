"""Finite Railway capability probe. No server, credentials, or user input."""

import json
import resource
import subprocess
import tempfile


def limits():
    resource.setrlimit(resource.RLIMIT_AS, (512 * 1024**2,) * 2)
    resource.setrlimit(resource.RLIMIT_CPU, (5, 5))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(name, command, source=None):
    try:
        result = subprocess.run(command, input=source, text=True,
                                capture_output=True, timeout=10,
                                preexec_fn=limits, env={"PATH": "/usr/bin:/bin"})
        report = {"test": name, "exit": result.returncode,
                  "output": (result.stdout + result.stderr)[:8000]}
    except subprocess.TimeoutExpired:
        report = {"test": name, "timeout": True}
    print(json.dumps(report), flush=True)


run("compiler", ["clang++-18", "--version"])
compiler = ["clang++-18", "-fplugin=/probe/libDTTPlugin.so", "-std=c++20",
            "-fsyntax-only", "-Wno-unused-result", "-x", "c++", "-"]
run("safe", compiler,
    "#include <vector>\nvoid f(){std::vector<int> v; v.push_back(1); v.front();}\n")
run("unsafe", compiler,
    "#include <vector>\nvoid f(){std::vector<int> v; v.front();}\n")

# Capability checks only; this script is not a production sandbox.
run("bubblewrap", ["bwrap", "--unshare-all", "--die-with-parent",
                   "--ro-bind", "/usr", "/usr", "--symlink", "usr/bin", "/bin",
                   "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
                   "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
                   "/bin/true"])
run("user_namespace", ["unshare", "--user", "--map-root-user", "true"])
run("network_namespace", ["unshare", "--net", "true"])
with tempfile.TemporaryDirectory() as root:
    run("chroot_capability", ["python3", "-c",
        "import os; os.chroot(" + repr(root) + "); os.chdir('/'); print('ok')"])
print(json.dumps({"probe_complete": True}), flush=True)
