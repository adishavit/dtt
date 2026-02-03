# DTT for CLion / IntelliJ

**Real-time memory safety analysis for C++** — catch null pointer dereferences, use-after-move, dangling pointers, and buffer overflows directly in CLion.

## Features

- **Inline error highlighting** — errors appear as you type with full IDE integration
- **Gutter icons** — see issues at a glance in the editor margin
- **Quick navigation** — click diagnostics to jump to the problem
- **Configurable strictness** — choose between strict and lenient analysis modes

## What DTT Catches

| Category | Example |
|----------|---------|
| **Null pointer dereference** | `int* p = nullptr; *p = 1;` |
| **Use after move** | `auto v2 = std::move(v1); v1.push_back(1);` |
| **Dangling pointer** | `int* p = &vec[0]; vec.push_back(1); *p;` |
| **Buffer overflow** | `int arr[10]; arr[10] = 1;` |
| **Out-of-bounds access** | `vec.at(vec.size());` |

## Installation

### From JetBrains Marketplace
1. Open CLion → Settings → Plugins
2. Search for "DTT"
3. Click Install

### Manual Installation
1. Download `dtt-clion-x.x.x.zip` from [Releases](https://github.com/adishavit/dtt/releases)
2. Open CLion → Settings → Plugins → ⚙️ → Install from Disk
3. Select the downloaded zip file

## Requirements

- **CLion 2023.1+** (or any IntelliJ-based IDE with C++ support)
- **Clang 18+** with plugin support
- **DTT Plugin** (`libDTTPlugin.so` / `.dylib` / `.dll`)

## Configuration

Open Settings → Tools → DTT:

| Setting | Default | Description |
|---------|---------|-------------|
| Clang Path | `clang++` | Path to clang++ executable |
| Plugin Path | (auto-detected) | Path to DTT plugin library |
| C++ Standard | `c++20` | Language standard for analysis |
| Mode | `inform` | Analysis strictness |

### Analysis Modes

| Mode | Behavior |
|------|----------|
| `inform` | Warnings for gaps (unknown state), errors for proven bugs |
| `enforce` | Errors for everything (strictest) |
| `quiet` | Errors only for proven bugs (most lenient) |

## Usage

DTT runs automatically when you open or modify C++ files. Diagnostics appear:

- As **red/yellow underlines** in the editor
- In the **Problems** tool window
- As **gutter icons** in the left margin

### Manual Analysis
- Use **Analyze → Run DTT Analysis** from the menu
- Or right-click in editor → **Run DTT Analysis**

### Clear Diagnostics
- Use **Analyze → Clear DTT Diagnostics**

## How It Works

DTT (Dependent Type Tracking) is a Clang compiler plugin that performs deep static analysis:

1. **Tracks value states** — knows when pointers are null, moved-from, or dangling
2. **Propagates through code** — follows assignments, function calls, and control flow
3. **Proves safety violations** — reports errors only when it can prove a bug exists

The plugin runs clang with DTT loaded via `-fplugin`, parses the diagnostic output, and displays results using IntelliJ's external annotator API.

## Troubleshooting

### "DTT plugin not found"
Set the plugin path manually in Settings → Tools → DTT. Common locations:
- Linux: `~/.dtt/bin/libDTTPlugin.so` or `/usr/local/lib/libDTTPlugin.so`
- macOS: `~/.dtt/bin/libDTTPlugin.dylib` or `/opt/homebrew/lib/libDTTPlugin.dylib`
- Windows: `%LOCALAPPDATA%\DTT\bin\DTTPlugin.dll`

### "clang++ not found"
Install LLVM 18+ and ensure it's in your PATH, or set the full path in settings.

### No diagnostics appearing
1. Check that the file is a C/C++ file (`.cpp`, `.c`, `.hpp`, `.h`)
2. Check Settings → Tools → DTT for configuration errors
3. Try running clang manually: `clang++ -fplugin=/path/to/libDTTPlugin.so -fsyntax-only yourfile.cpp`

### Analysis is slow
DTT runs a full semantic analysis which can take time on large files. Consider:
- Closing files you're not actively editing
- Using `quiet` mode for faster feedback on proven bugs only

## Platform Notes

### Windows
Requires `-Xclang` syntax for plugin loading. The plugin handles this automatically.

### macOS
Requires Homebrew LLVM (`brew install llvm@18`). Apple's system clang does not support plugins.

### Linux
Works with system LLVM 18+ or custom builds.

## Building from Source

```bash
# Clone the repository
git clone https://github.com/adishavit/dtt.git
cd dtt/ide-plugins/clion-dtt

# Build the plugin
./gradlew buildPlugin

# Output: build/distributions/dtt-clion-x.x.x.zip
```

## Links

- [GitHub Repository](https://github.com/adishavit/dtt)
- [Report Issues](https://github.com/adishavit/dtt/issues)
- [JetBrains Marketplace](https://plugins.jetbrains.com/plugin/dtt-clion)

## License

BSD 2-Clause License - see [LICENSE](../LICENSE)
