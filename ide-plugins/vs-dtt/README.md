# DTT for Visual Studio

**Real-time memory safety analysis for C++** — catch null pointer dereferences, use-after-move, dangling pointers, and buffer overflows directly in Visual Studio.

## Features

- **Error List integration** — errors appear in Visual Studio's Error List with navigation
- **Real-time analysis** — runs as you type and on save
- **Configurable strictness** — choose between strict and lenient analysis modes
- **Auto-setup** — automatically downloads LLVM and DTT plugin on first use

## What DTT Catches

| Category | Example |
|----------|---------|
| **Null pointer dereference** | `int* p = nullptr; *p = 1;` |
| **Use after move** | `auto v2 = std::move(v1); v1.push_back(1);` |
| **Use-after-free** | `int* p = new int; delete p; *p = 1;` |
| **Buffer overflow** | `int arr[10]; arr[10] = 1;` |
| **Out-of-bounds access** | `vec.at(vec.size());` |

## Requirements

- **Visual Studio 2022** or later
- **Windows 10/11**
- **.NET Framework 4.8**
- **Clang 18+** — auto-downloaded if not found

## Installation

### From VSIX (Recommended)

1. Download the latest `dtt-vs.vsix` from [Releases](https://github.com/adishavit/dtt/releases)
2. Double-click to install in Visual Studio
3. Restart Visual Studio

### Build from Source

```powershell
cd vs-dtt
dotnet build
```

The built extension will be in `bin/Debug/` or `bin/Release/`.

## Configuration

Access settings via **Tools → Options → DTT**:

| Setting | Default | Description |
|---------|---------|-------------|
| Clang Path | (auto) | Path to clang++ executable |
| Plugin Path | (auto) | Path to DTTPlugin.dll |
| C++ Standard | `c++20` | Language standard (`c++17`, `c++20`, `c++23`) |
| Mode | `inform` | Analysis strictness |
| Debounce | `500` | Delay before analysis (ms) |

### Analysis Modes

| Mode | Behavior |
|------|----------|
| `inform` | Warnings for gaps (unknown state), errors for proven bugs |
| `enforce` | Errors for everything (strictest) |
| `quiet` | Errors only for proven bugs (most lenient) |

## Usage

1. Open a C/C++ file (`.cpp`, `.c`, `.hpp`, `.h`)
2. DTT will automatically download required binaries on first use
3. Analysis runs automatically on save (configurable)
4. View results in the **Error List** window (View → Error List)

### Commands

Access via **Tools → DTT** menu:

| Command | Description |
|---------|-------------|
| **Analyze Current File** | Run analysis immediately |
| **Clear Diagnostics** | Clear all DTT errors |
| **Setup** | Re-run binary download/setup |

## Example

```cpp
void example() {
    int* ptr = nullptr;
    *ptr = 10;  // DTT reports a null pointer dereference
}
```

DTT will show this error in the Error List:
> DTT: potential null pointer dereference of 'ptr' (known to be null)

Double-click to navigate to the error location.

## How It Works

DTT (Dependent Type Tracking) is a Clang compiler plugin that performs deep static analysis:

1. **Tracks value states** — knows when pointers are null, moved-from, or dangling
2. **Propagates through code** — follows assignments, function calls, and control flow
3. **Checks tracked properties** — reports modeled violations and, depending on mode, uncertainty

The extension runs clang with the DTT plugin via `-Xclang -load -Xclang DTTPlugin.dll`, parses diagnostic output, and displays results in Visual Studio's Error List.

## Plugin Path Auto-Detection

DTT searches these Windows locations automatically:

- `%USERPROFILE%\.dtt\bin\DTTPlugin.dll`
- `%LOCALAPPDATA%\DTT\bin\DTTPlugin.dll`
- `C:\Program Files\DTT\bin\DTTPlugin.dll`
- `C:\Program Files\LLVM\lib\DTTPlugin.dll`

## Troubleshooting

### "Clang not found"

DTT will attempt to download LLVM automatically. If this fails:

1. Install LLVM 18+ from [releases.llvm.org](https://releases.llvm.org/)
2. Set the path in **Tools → Options → DTT → Clang Path**

### "Plugin not found"

Set the plugin path manually in **Tools → Options → DTT → Plugin Path**.

### No errors appearing

1. Check the file is saved
2. Check **View → Output → DTT** for errors
3. Ensure the file compiles with clang normally
4. Try **Tools → DTT → Analyze Current File**

### Build Errors (when building from source)

The VS extension requires the Visual Studio SDK workload:

```powershell
# Via Visual Studio Installer, add:
# "Visual Studio extension development" workload

# Or via command line:
dotnet workload install visualstudio
```

### Analysis is slow

Increase the debounce delay in **Tools → Options → DTT → Debounce** to reduce analysis frequency.

## Platform Notes

This extension is **Windows-only**. For other platforms, use:

- **VS Code** — cross-platform (Linux, macOS, Windows)
- **CLion** — cross-platform
- **Emacs/Neovim/Vim** — cross-platform

## Building DTT Plugin

If you need to build the DTT plugin from source on Windows:

```powershell
git clone https://github.com/adishavit/dtt.git
cd dtt
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_CXX_COMPILER=clang++
cmake --build build --config Release
# Plugin: build/Release/DTTPlugin.dll
```

## Privacy

DTT runs entirely locally. No code is sent to external servers. No telemetry is collected.

## Links

- [GitHub Repository](https://github.com/adishavit/dtt)
- [Report Issues](https://github.com/adishavit/dtt/issues)
- [DTT Documentation](https://github.com/adishavit/dtt#readme)

## License

BSD 2-Clause License - see [LICENSE](../LICENSE)
