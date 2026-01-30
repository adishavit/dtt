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

## Platforms

| OS | VS Code | Visual Studio | Plugin File |
|----|---------|---------------|-------------|
| Linux | ✅ | - | `libDTTPlugin.so` |
| macOS | ✅ | - | `libDTTPlugin.dylib` |
| Windows | ✅ | ✅ | `DTTPlugin.dll` |

**Windows**: Install [LLVM for Windows](https://github.com/llvm/llvm-project/releases) and build DTT as a DLL.

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
| `dtt.pluginPath` | (required) | Path to DTT plugin |
| `dtt.clangPath` | `clang++` | Path to clang++ |
| `dtt.standard` | `c++20` | C++ standard |
| `dtt.mode` | `inform` | Analysis mode |
| `dtt.debounceMs` | `500` | Delay before analysis |
| `dtt.analyzeOnSave` | `true` | Auto-analyze on save |
| `dtt.analyzeOnChange` | `true` | Auto-analyze while typing |

**Windows example:**
```json
{
  "dtt.pluginPath": "C:\\LLVM\\lib\\DTTPlugin.dll",
  "dtt.clangPath": "C:\\LLVM\\bin\\clang++.exe"
}
```

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
| Plugin Path | (required) | `C:\LLVM\lib\DTTPlugin.dll` |
| Clang Path | `clang++` | `C:\LLVM\bin\clang++.exe` |
| Standard | `c++20` | C++ standard |
| Mode | `inform` | Analysis mode |
| Debounce (ms) | `500` | Delay before analysis |

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

1. **LLVM/Clang 18+** with plugin support
   - Linux: `apt install clang-18`
   - macOS: `brew install llvm@18`
   - Windows: [LLVM releases](https://github.com/llvm/llvm-project/releases)

2. **DTT Plugin** compiled for your platform
   ```bash
   cmake -B build -DCMAKE_CXX_COMPILER=clang++
   cmake --build build
   ```
