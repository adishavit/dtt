# DTT IDE Plugins

Real-time memory safety analysis for C++ in your IDE.

## Architecture

Both plugins share the same design:

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   Parser     │     │    Runner    │     │   IDE Glue   │
│              │     │              │     │              │
│ Pure funcs   │◄────│ Clang exec   │◄────│ VS/VSCode    │
│ No deps      │     │ Cancellation │     │ integration  │
└──────────────┘     └──────────────┘     └──────────────┘
```

- **Parser**: Converts clang output → structured diagnostics
- **Runner**: Executes `clang++ -fplugin=DTT`, handles cancellation
- **IDE Glue**: Thin layer connecting to VS Code / Visual Studio APIs

---

## VS Code Extension

### Install

```bash
cd ide-plugins/vscode-dtt
npm install
npm run compile
npm run package    # Creates dtt-0.1.0.vsix
```

Then: `code --install-extension dtt-0.1.0.vsix`

### Configure

Settings (`Ctrl+,`):

| Setting | Default | Description |
|---------|---------|-------------|
| `dtt.pluginPath` | (required) | Path to `libDTTPlugin.so` |
| `dtt.clangPath` | `clang++` | Path to clang++ |
| `dtt.standard` | `c++20` | C++ standard |
| `dtt.mode` | `inform` | Analysis mode |
| `dtt.debounceMs` | `500` | Delay before analysis |
| `dtt.analyzeOnSave` | `true` | Auto-analyze on save |
| `dtt.analyzeOnChange` | `true` | Auto-analyze while typing |

### Platforms

| OS | Status |
|----|--------|
| Linux | Native |
| macOS | Native |
| Windows + WSL | Set WSL clang path |

---

## Visual Studio Extension

### Install

1. Open `vs-dtt/DTT.csproj` in Visual Studio 2022
2. Build (Release)
3. Double-click `bin/Release/DTT.vsix`

### Configure

**Tools → Options → DTT**:

| Setting | Default | Description |
|---------|---------|-------------|
| Plugin Path | (required) | WSL path to plugin |
| Clang Path | `clang++-18` | Clang in WSL |
| Use WSL | `true` | Run via WSL |
| Standard | `c++20` | C++ standard |
| Mode | `inform` | Analysis mode |
| Debounce (ms) | `500` | Delay before analysis |

### Platforms

| OS | Status |
|----|--------|
| Windows + WSL | Runs clang via WSL |

---

## Usage

1. Open a C++ file
2. Edit or save → DTT analyzes automatically
3. Errors appear as squiggles + in Problems/Error List

### Commands

| VS Code | Visual Studio |
|---------|---------------|
| `DTT: Analyze Current File` | Tools → DTT: Analyze File |
| `DTT: Clear Diagnostics` | Tools → DTT: Clear Diagnostics |

---

## Modes

| Mode | Behavior |
|------|----------|
| `inform` | Warnings for gaps, errors for bugs (default) |
| `enforce` | Errors for everything |
| `quiet` | Errors only for proven bugs |

---

## Project Structure

```
vscode-dtt/
├── src/
│   ├── parser.ts      # Diagnostic parsing (pure, no deps)
│   ├── runner.ts      # Clang execution + cancellation
│   └── extension.ts   # VS Code glue (thin)
├── package.json
└── tsconfig.json

vs-dtt/
├── Parser.cs          # Diagnostic parsing (pure, no deps)
├── Runner.cs          # Clang execution + cancellation
├── Options.cs         # Settings page
├── Commands.cs        # Menu commands
├── DTTPackage.cs      # VS glue (thin)
└── DTT.csproj
```

---

## Prerequisites

1. **DTT Plugin**: Build with `cmake -B build && cmake --build build`
2. **Clang 18+**: Required for `-fplugin` support
3. **WSL** (Windows only): For running clang on Windows
