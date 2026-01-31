# DTT for Visual Studio Code

**Real-time memory safety analysis for C++** — catch null pointer dereferences, use-after-move, dangling pointers, and buffer overflows as you type.

## Features

- **Instant feedback** — errors appear as red squiggly underlines while you code
- **Zero configuration** — auto-downloads LLVM and DTT plugin on first use
- **Proven bugs vs gaps** — distinguishes between definite bugs and potential issues
- **Cross-platform** — works on Linux, macOS, and Windows

## What DTT Catches

| Category | Example |
|----------|---------|
| **Null pointer dereference** | `int* p = nullptr; *p = 1;` |
| **Use after move** | `auto v2 = std::move(v1); v1.push_back(1);` |
| **Dangling pointer** | `int* p = &vec[0]; vec.push_back(1); *p;` |
| **Buffer overflow** | `int arr[10]; arr[10] = 1;` |
| **Out-of-bounds access** | `vec.at(vec.size());` |

## Quick Start

1. Install from VS Code Marketplace
2. Open any C++ file
3. DTT automatically downloads dependencies on first run
4. Errors appear as you type!

## Requirements

- **Clang 18+** — auto-downloaded if not found
- **C++17 or later** — configurable in settings

## Extension Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `dtt.clangPath` | `clang++` | Path to clang++ executable |
| `dtt.pluginPath` | (auto) | Path to DTT plugin |
| `dtt.standard` | `c++20` | C++ standard (`c++17`, `c++20`, `c++23`) |
| `dtt.mode` | `inform` | Analysis mode (see below) |
| `dtt.debounceMs` | `500` | Delay before analysis (ms) |
| `dtt.analyzeOnSave` | `true` | Analyze when file is saved |
| `dtt.analyzeOnChange` | `true` | Analyze as you type |

### Analysis Modes

| Mode | Behavior |
|------|----------|
| `inform` | Warnings for gaps (unknown state), errors for proven bugs |
| `enforce` | Errors for everything (strictest) |
| `quiet` | Errors only for proven bugs (most lenient) |

## Commands

- **DTT: Analyze Current File** — manually trigger analysis
- **DTT: Clear Diagnostics** — remove all DTT markers
- **DTT: Download/Setup LLVM** — re-run automatic setup

## How It Works

DTT (Dependent Type Tracking) is a Clang compiler plugin that performs deep static analysis:

1. **Tracks value states** — knows when pointers are null, moved-from, or dangling
2. **Propagates through code** — follows assignments, function calls, and control flow
3. **Proves safety violations** — reports errors only when it can prove a bug exists

Unlike traditional linters, DTT understands C++ semantics deeply — it knows that `vec.push_back()` invalidates iterators and that `std::move()` leaves objects in an unspecified state.

## Example

```cpp
#include <vector>

void example() {
    std::vector<int> vec = {1, 2, 3};
    int* ptr = &vec[0];

    vec.push_back(4);  // Invalidates ptr!

    *ptr = 10;  // DTT ERROR: use of dangling pointer
}
```

DTT will underline `*ptr` with a red squiggle and show:
> DTT: potential null pointer dereference of 'ptr' (iterator invalidated)

## Troubleshooting

### "Plugin not found"
Run **DTT: Download/Setup LLVM** from the command palette, or manually set `dtt.pluginPath` in settings.

### "clang++ not found"
Install LLVM 18+ or let DTT download it automatically. On Linux: `apt install clang-18`. On macOS: `brew install llvm@18`.

### Diagnostics not appearing
1. Check the file is saved (if `analyzeOnChange` is disabled)
2. Check Output panel → DTT for errors
3. Ensure the file compiles with clang normally

### Analysis is slow
Increase `dtt.debounceMs` to reduce analysis frequency, or disable `dtt.analyzeOnChange` for large files.

## Platform Notes

### Windows
Uses `-Xclang` syntax for plugin loading. Requires LLVM built with plugin support.

### macOS
Works with Homebrew LLVM (`brew install llvm@18`). System clang does not support plugins.

### Linux
Works with system LLVM 18+ (`apt install clang-18 llvm-18-dev`).

## Privacy

DTT runs entirely locally. No code is sent to external servers. No telemetry is collected.

## Links

- [GitHub Repository](https://github.com/adishavit/dtt)
- [Report Issues](https://github.com/adishavit/dtt/issues)
- [DTT Documentation](https://github.com/adishavit/dtt#readme)

## License

MIT License - see [LICENSE](https://github.com/adishavit/dtt/blob/main/LICENSE)
