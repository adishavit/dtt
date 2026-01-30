# DTT for VS Code

Visual Studio Code extension for **DTT (Dependent Type Tracking)** - compile-time memory safety analysis for C++.

## Features

DTT detects three major classes of memory safety bugs at compile-time:

- **Buffer overflows / empty container access**: `vector::front()` on empty vector
- **Null pointer dereferences**: Dereferencing potentially-null pointers
- **Use-after-move/free**: Accessing moved-from or freed objects

This extension integrates DTT into VS Code, providing:

- Real-time diagnostics in the Problems panel
- Syntax-highlighted error squiggles
- Analyze on save (configurable)
- Support for all DTT analysis modes
- Context menu integration

## Requirements

- **Clang 18+** with plugin support
- **DTT plugin** (`libDTTPlugin.so` or `libDTTPlugin.dylib`)
- **Z3 solver** (required by DTT)

## Installation

1. Install the extension from the VS Code Marketplace (or install from VSIX)
2. Configure the DTT plugin path in settings

## Configuration

Open VS Code settings and search for "DTT":

| Setting | Description | Default |
|---------|-------------|---------|
| `dtt.pluginPath` | Path to DTT plugin library | (required) |
| `dtt.clangPath` | Path to clang++ compiler | `clang++` |
| `dtt.mode` | Analysis mode: `inform`, `enforce`, or `quiet` | `inform` |
| `dtt.cppStandard` | C++ standard version | `c++20` |
| `dtt.analyzeOnSave` | Auto-analyze when saving | `true` |
| `dtt.analyzeOnOpen` | Auto-analyze when opening | `false` |
| `dtt.includePaths` | Additional include paths | `[]` |
| `dtt.additionalFlags` | Extra compiler flags | `[]` |

### Example Configuration

```json
{
    "dtt.pluginPath": "/path/to/libDTTPlugin.so",
    "dtt.clangPath": "clang++-18",
    "dtt.mode": "inform",
    "dtt.cppStandard": "c++20",
    "dtt.analyzeOnSave": true,
    "dtt.includePaths": [
        "/usr/include",
        "${workspaceFolder}/include"
    ]
}
```

## Analysis Modes

| Mode | Description |
|------|-------------|
| `inform` | Default. Warnings for coverage gaps, errors for proven bugs |
| `enforce` | Strict. Errors for everything including coverage gaps |
| `quiet` | Permissive. Errors only for proven bugs |

## Commands

Access via Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`):

- **DTT: Analyze Current File** - Run DTT on the active file
- **DTT: Analyze Workspace** - Run DTT on all C++ files
- **DTT: Clear Diagnostics** - Clear all DTT diagnostics
- **DTT: Show Output** - Open DTT output channel
- **DTT: Set Analysis Mode** - Quick-switch analysis mode

## Understanding Diagnostics

DTT categorizes issues by severity:

| Category | Type | Meaning |
|----------|------|---------|
| E/F | Error | **Proven violation** - definite bug found |
| C | Warning/Error | **Lost tracking** - state was known but lost |
| D | Warning/Error | **Undecidable** - solver couldn't verify |
| G | Error | **Annotation conflict** - malformed annotations |

### Example Diagnostic

```
error: DTT: vector::front requires _size > 0, but data._size = 0
```

This means DTT proved that `front()` is called on an empty vector.

## Building from Source

```bash
cd ide-plugins/vscode-dtt
npm install
npm run compile
npm run package  # Creates .vsix file
```

## Troubleshooting

### "Plugin path not configured"

Set `dtt.pluginPath` in VS Code settings to point to your DTT plugin.

### "clang++: command not found"

Either install Clang 18+ or set `dtt.clangPath` to the full path.

### Analysis shows no diagnostics

1. Check the DTT output panel for errors
2. Ensure the file compiles without DTT
3. Verify include paths are correct

## License

MIT License - see LICENSE file for details.

## Links

- [DTT Repository](https://github.com/adishavit/dtt)
- [Report Issues](https://github.com/adishavit/dtt/issues)
