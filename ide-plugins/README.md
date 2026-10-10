# DTT IDE Plugins

**Real-time memory safety analysis for C++** — catch bugs as you type, not at runtime.

## What DTT Catches

| Bug Type | Example | What Happens |
|----------|---------|--------------|
| **Null pointer dereference** | `int* p = nullptr; *p = 1;` | Crash / undefined behavior |
| **Use-after-move** | `auto v2 = std::move(v1); v1.size();` | Undefined behavior |
| **Use-after-free** | `int* p = new int; delete p; *p = 1;` | Access to freed memory |
| **Buffer overflow** | `int arr[10]; arr[10] = 1;` | Memory corruption / security vulnerability |

Checks depend on tracked state and annotations. General iterator invalidation and
pointers into reallocated containers are not modeled. Moving an object is not
itself an error; DTT invalidates selected properties and checks later uses.

---

## Supported Editors

| Editor | Install Method | Plugin Type | Documentation |
|--------|---------------|-------------|---------------|
| **VS Code** | Marketplace / `.vsix` | Native extension | [README](vscode-dtt/README.md) |
| **Visual Studio** | `.vsix` installer | VSIX package | [README](vs-dtt/README.md) |
| **CLion** | JetBrains Marketplace | IntelliJ plugin | [README](clion-dtt/README.md) |
| **Neovim** | lazy.nvim / packer | Lua plugin | [README](nvim-dtt/README.md) |
| **Vim** | vim-plug / manual | ALE linter | [README](vim-dtt/README.md) |
| **Emacs** | use-package / MELPA | Flycheck checker | [README](emacs-dtt/README.md) |

### Platform Support

| Platform | VS Code | Visual Studio | CLion | Neovim | Vim | Emacs |
|----------|---------|---------------|-------|--------|-----|-------|
| Linux | ✅ | — | ✅ | ✅ | ✅ | ✅ |
| macOS | ✅ | — | ✅ | ✅ | ✅ | ✅ |
| Windows | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

---

## Quick Start

### VS Code (Recommended for beginners)

1. Install from [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=dtt.dtt)
2. Open any `.cpp` file
3. DTT auto-downloads dependencies and starts analyzing!

### CLion

1. Settings → Plugins → Marketplace → Search "DTT"
2. Install and restart
3. Configure clang path in Settings → Tools → DTT

### Neovim

```lua
-- lazy.nvim
{ "adishavit/dtt", ft = { "cpp", "c" }, config = function() require("dtt").setup() end }
```

### Vim

```vim
Plug 'dense-analysis/ale'
Plug 'adishavit/dtt', { 'rtp': 'ide-plugins/vim-dtt' }
let g:ale_linters = {'cpp': ['dtt']}
```

### Emacs

```elisp
(use-package dtt
  :hook ((c-mode c++-mode) . dtt-mode))
```

---

## How It Works

```
┌─────────────┐      ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│  Your Code  │ ──►  │   Clang +   │ ──►  │   Parser    │ ──►  │ IDE Shows   │
│   .cpp      │      │ DTT Plugin  │      │ (per-lang)  │      │  Squiggles  │
└─────────────┘      └─────────────┘      └─────────────┘      └─────────────┘
                           │
                           ▼
                    Clang diagnostic output:
                    file.cpp:10:5: error: DTT: null pointer dereference
```

1. **DTT Plugin** — Clang compiler plugin that performs deep semantic analysis
2. **Runner** — Executes clang with DTT, handles timeouts and cancellation
3. **Parser** — Converts clang output to structured diagnostics
4. **IDE Integration** — Displays errors as underlines, gutter icons, etc.

---

## Analysis Modes

All plugins support three strictness levels:

| Mode | Behavior | Use Case |
|------|----------|----------|
| `inform` | Warnings for gaps, errors for proven bugs | Default — balanced feedback |
| `enforce` | Errors for everything | Strict — treat all issues as errors |
| `quiet` | Errors only for proven bugs | Lenient — minimal noise |

**Proven bugs** = DTT can mathematically prove the code will fail (e.g., dereferencing a known-null pointer)

**Gaps** = DTT cannot determine if code is safe (e.g., pointer with unknown nullability)

---

## Configuration Reference

### Common Settings (all editors)

| Setting | Default | Description |
|---------|---------|-------------|
| Clang path | `clang++` | Path to clang++ executable |
| Plugin path | (auto) | Path to DTT plugin library |
| C++ Standard | `c++20` | Language standard for analysis |
| Mode | `inform` | Analysis strictness |
| Debounce | `500ms` | Delay before analysis starts |

### Plugin File Locations

| Platform | File | Auto-detection paths |
|----------|------|---------------------|
| Linux | `libDTTPlugin.so` | `~/.dtt/bin/`, `/usr/local/lib/`, `/usr/lib/` |
| macOS | `libDTTPlugin.dylib` | `~/.dtt/bin/`, `/opt/homebrew/lib/`, `/usr/local/lib/` |
| Windows | `DTTPlugin.dll` | `%LOCALAPPDATA%\DTT\bin\`, `C:\Program Files\DTT\bin\` |

---

## Building from Source

### VS Code Extension

```bash
cd ide-plugins/vscode-dtt
npm install
npm run compile
npm run package    # Creates dtt-x.x.x.vsix
code --install-extension dtt-*.vsix
```

### CLion Plugin

```bash
cd ide-plugins/clion-dtt
./gradlew buildPlugin
# Output: build/distributions/dtt-clion-*.zip
```

### DTT Plugin (required by all editors)

```bash
# From repository root
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
# Output: build/libDTTPlugin.so (or .dylib/.dll)
```

---

## Troubleshooting

### "Plugin not found"

Set the path manually in your editor's settings, or copy the plugin to an auto-detection location:

```bash
# Linux/macOS
mkdir -p ~/.dtt/bin
cp build/libDTTPlugin.so ~/.dtt/bin/   # or .dylib

# Windows (PowerShell)
mkdir $env:LOCALAPPDATA\DTT\bin -Force
copy build\Release\DTTPlugin.dll $env:LOCALAPPDATA\DTT\bin\
```

### "clang++ not found"

Install LLVM 18+:
- **Linux**: `sudo apt install clang-18`
- **macOS**: `brew install llvm@18`
- **Windows**: Download from [LLVM releases](https://github.com/llvm/llvm-project/releases)

### No diagnostics appearing

1. Ensure the file is a C/C++ file (`.cpp`, `.c`, `.hpp`, `.h`)
2. Check that the file compiles normally with clang
3. Try running manually:
   ```bash
   clang++ -fplugin=/path/to/libDTTPlugin.so -fsyntax-only -std=c++20 yourfile.cpp
   ```
4. Check your editor's output/log panel for errors

### Analysis is slow

- Increase debounce delay in settings
- Disable "analyze on change" (keep "analyze on save")
- Large files with many templates take longer

---

## Project Structure

```
ide-plugins/
├── vscode-dtt/           # VS Code extension
│   ├── src/
│   │   ├── extension.ts  # Main entry point
│   │   ├── parser.ts     # Diagnostic parsing
│   │   ├── runner.ts     # Clang execution
│   │   └── setup.ts      # Auto-download logic
│   ├── package.json
│   └── README.md
│
├── vs-dtt/               # Visual Studio extension
│   ├── Parser.cs
│   ├── Runner.cs
│   ├── Setup.cs
│   └── DTT.csproj
│
├── clion-dtt/            # CLion/IntelliJ plugin
│   ├── src/main/kotlin/com/dtt/clion/
│   │   ├── DTTExternalAnnotator.kt
│   │   ├── DTTSettings.kt
│   │   └── DTTConfigurable.kt
│   ├── build.gradle.kts
│   └── README.md
│
├── nvim-dtt/             # Neovim plugin
│   ├── lua/dtt/init.lua
│   └── README.md
│
├── vim-dtt/              # Vim plugin (ALE)
│   ├── plugin/dtt.vim
│   ├── doc/dtt.txt       # Vim help (:help dtt)
│   └── README.md
│
└── emacs-dtt/            # Emacs package (Flycheck)
    ├── dtt.el
    └── README.md
```

---

## Architecture Notes

### Why Not LSP?

Currently, each plugin implements its own diagnostic parsing. The pattern is identical:

```regex
^(.+):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$
```

A future improvement would be a **DTT Language Server** that centralizes this logic. Then each editor would need only ~20 lines of LSP client configuration.

### Design Principles

1. **Zero-config by default** — auto-detect clang and plugin paths
2. **Async/non-blocking** — never freeze the editor
3. **Cancellation support** — abort stale analyses when file changes
4. **Timeout protection** — 30-second limit prevents hangs
5. **Thin IDE layer** — most logic in parser/runner, minimal IDE-specific code

---

## Privacy

DTT runs entirely locally. No code is sent to external servers. No telemetry is collected.

---

## Links

- [DTT Repository](https://github.com/adishavit/dtt)
- [Report Issues](https://github.com/adishavit/dtt/issues)
- [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=dtt.dtt)
- [JetBrains Marketplace](https://plugins.jetbrains.com/plugin/dtt-clion)

## License

MIT License — see [LICENSE](../LICENSE)
