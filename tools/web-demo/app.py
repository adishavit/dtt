#!/usr/bin/env python3
"""DTT web demo: a minimal live-compile editor for the DTT plugin.

Serves a Monaco editor and a /compile endpoint that runs clang++-18 with the
DTT plugin on the submitted source. It executes the compiler on whatever is
posted, so it binds to localhost only.
"""

import os
import re
import subprocess
import tempfile
from flask import Flask, request, jsonify, send_from_directory

app = Flask(__name__)

# $DTT_PLUGIN, else the repository's build/libDTTPlugin.so
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PLUGIN_PATH = os.environ.get("DTT_PLUGIN") or os.path.join(
    SCRIPT_DIR, "..", "..", "build", "libDTTPlugin.so")

# Clang diagnostic format: file:line:col: type: message
DIAG_PATTERN = re.compile(r'^input\.cpp:(\d+):(\d+):\s+(error|warning|note):\s+(.*)$', re.MULTILINE)


def parse_diagnostics(output):
    """Extract structured diagnostic info from clang output."""
    return [
        {
            'line': int(m.group(1)),
            'column': int(m.group(2)),
            'severity': m.group(3),
            'message': m.group(4),
        }
        for m in DIAG_PATTERN.finditer(output)
    ]


@app.route("/")
def index():
    return send_from_directory(SCRIPT_DIR, "index.html")


@app.route("/compile", methods=["POST"])
def compile_code():
    data = request.get_json()
    code = data.get("code", "")
    mode = data.get("mode", "enforce")  # "enforce" or "inform"

    with tempfile.NamedTemporaryFile(mode="w", suffix=".cpp", delete=False) as f:
        f.write(code)
        temp_file = f.name

    try:
        cmd = [
            "clang++-18",
            f"-fplugin={PLUGIN_PATH}",
            "-std=c++20",
            "-fsyntax-only",
            "-Wno-unused-result",
            "-Wno-unused-value",
        ]

        # The plugin's default mode is inform; only enforce needs a flag.
        if mode == "enforce":
            cmd.append("-fplugin-arg-dtt--dtt-mode=enforce")

        cmd.append(temp_file)

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)

        output = (result.stdout + result.stderr).replace(temp_file, "input.cpp")

        return jsonify({
            "success": result.returncode == 0,
            "output": output if output.strip() else "(No output - compilation successful)",
            "returncode": result.returncode,
            "diagnostics": parse_diagnostics(output),
        })

    except subprocess.TimeoutExpired:
        return jsonify({
            "success": False,
            "output": "Error: Compilation timed out (10s limit)",
            "returncode": -1,
        })
    except Exception as e:
        return jsonify({
            "success": False,
            "output": f"Error: {e}",
            "returncode": -1,
        })
    finally:
        if os.path.exists(temp_file):
            os.unlink(temp_file)


if __name__ == "__main__":
    if not os.path.exists(PLUGIN_PATH):
        print(f"Warning: DTT plugin not found at {PLUGIN_PATH}")
        print("Build it first: cmake -B build -DCMAKE_CXX_COMPILER=clang++-18 && cmake --build build")

    print(f"Using DTT plugin: {PLUGIN_PATH}")
    app.run(host="127.0.0.1", port=int(os.environ.get("PORT", "5000")))
