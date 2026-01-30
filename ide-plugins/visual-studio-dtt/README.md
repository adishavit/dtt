# DTT for Visual Studio

Visual Studio extension for **DTT (Dependent Type Tracking)** - compile-time memory safety analysis for C++.

## Features

DTT detects three major classes of memory safety bugs at compile-time:

- **Buffer overflows / empty container access**: `vector::front()` on empty vector
- **Null pointer dereferences**: Dereferencing potentially-null pointers
- **Use-after-move/free**: Accessing moved-from or freed objects

This extension integrates DTT into Visual Studio 2022, providing:

- Error List integration with clickable diagnostics
- Analyze on save (configurable)
- Solution-wide analysis
- Support for all DTT analysis modes
- WSL integration for running Clang on Windows

## Requirements

- **Visual Studio 2022** (17.0 or later)
- **Windows Subsystem for Linux (WSL)** with:
  - Clang 18+
  - DTT plugin compiled for Linux
  - Z3 solver

> **Note**: DTT is a Clang plugin that requires a Linux environment. On Windows, the extension uses WSL to run the analysis.

## Installation

1. Download the `.vsix` file from releases
2. Double-click to install, or use Extensions > Manage Extensions > Install from VSIX
3. Restart Visual Studio
4. Configure DTT settings in Tools > Options > DTT

## Configuration

Navigate to **Tools > Options > DTT > General**:

### DTT Plugin Settings

| Setting | Description | Default |
|---------|-------------|---------|
| Plugin Path | WSL path to libDTTPlugin.so | (required) |
| Clang Path | Path to clang++ in WSL | `clang++` |

### Analysis Settings

| Setting | Description | Default |
|---------|-------------|---------|
| Analysis Mode | `Inform`, `Enforce`, or `Quiet` | `Inform` |
| C++ Standard | C++17, C++20, or C++23 | `C++20` |
| Analyze on Save | Auto-analyze when saving | `true` |
| Analyze on Build | Run analysis during build | `false` |
| Include Paths | Additional include paths (semicolon-separated) | |
| Additional Flags | Extra compiler flags | |

### WSL Settings

| Setting | Description | Default |
|---------|-------------|---------|
| Use WSL | Run analysis through WSL | `true` |
| WSL Distribution | Specific distro (empty = default) | |

### Example Setup

1. Install DTT in WSL:
   ```bash
   # In WSL
   cd /path/to/dtt
   cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
   cmake --build build
   ```

2. Configure in Visual Studio:
   - **Plugin Path**: `/home/user/dtt/build/libDTTPlugin.so`
   - **Clang Path**: `clang++-18`
   - **Use WSL**: `true`

## Analysis Modes

| Mode | Description |
|------|-------------|
| Inform | Default. Warnings for coverage gaps, errors for proven bugs |
| Enforce | Strict. Errors for everything including coverage gaps |
| Quiet | Permissive. Errors only for proven bugs |

## Menu Commands

Access via **Tools > DTT Analysis**:

- **Analyze Current File** - Run DTT on the active file
- **Analyze Solution** - Run DTT on all C++ files in solution
- **Clear DTT Diagnostics** - Clear all DTT entries from Error List
- **DTT Settings...** - Open DTT options page

Right-click context menu also includes **DTT: Analyze File** for C++ files.

## Understanding Diagnostics

Diagnostics appear in the **Error List** window with source "DTT":

| Category | Type | Meaning |
|----------|------|---------|
| E/F | Error | **Proven violation** - definite bug found |
| C | Warning/Error | **Lost tracking** - state was known but lost |
| D | Warning/Error | **Undecidable** - solver couldn't verify |
| G | Error | **Annotation conflict** - malformed annotations |

Double-click any diagnostic to navigate to the source location.

### Example Diagnostic

```
Error: DTT: vector::front requires _size > 0, but data._size = 0
```

This means DTT proved that `front()` is called on an empty vector.

## Building from Source

### Prerequisites

- Visual Studio 2022 with:
  - .NET desktop development workload
  - Visual Studio extension development workload

### Build Steps

1. Open `DTTAnalyzer.sln` in Visual Studio
2. Build the solution (Debug or Release)
3. The `.vsix` file is created in `bin/Debug` or `bin/Release`

### Debug the Extension

1. Set DTTAnalyzer as startup project
2. Press F5 to launch experimental instance
3. Test in the experimental Visual Studio

## Troubleshooting

### "Plugin path is not configured"

Set the plugin path in Tools > Options > DTT > General.

### WSL errors

1. Ensure WSL is installed: `wsl --list`
2. Check that Clang is installed in WSL: `wsl clang++-18 --version`
3. Verify the plugin path is a valid WSL path (e.g., `/home/user/...`)

### No diagnostics appear

1. Check the Output window (DTT pane) for errors
2. Ensure the file compiles successfully without DTT
3. Verify include paths are correct (use WSL-style paths)

### Path conversion issues

Windows paths are automatically converted to WSL paths:
- `C:\Users\name\file.cpp` → `/mnt/c/Users/name/file.cpp`

If you have custom paths, ensure they use the `/mnt/` prefix.

## Architecture

The extension runs DTT through WSL:

```
Visual Studio → WSL → clang++ -fplugin=libDTTPlugin.so → Parse output → Error List
```

Diagnostics are parsed from Clang's standard error output format and displayed in the Visual Studio Error List.

## License

MIT License - see LICENSE file for details.

## Links

- [DTT Repository](https://github.com/adishavit/dtt)
- [Report Issues](https://github.com/adishavit/dtt/issues)
