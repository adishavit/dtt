# DTT IDE Plugins

This directory contains IDE integrations for DTT (Dependent Type Tracking).

## Available Plugins

### VS Code Extension (`vscode-dtt/`)

Full-featured VS Code extension with:
- Real-time diagnostics
- Problem matcher integration
- Configurable analysis modes
- Context menu support

**Build:**
```bash
cd vscode-dtt
npm install
npm run compile
npm run package
```

**Install:** Install the generated `.vsix` file in VS Code.

See [vscode-dtt/README.md](vscode-dtt/README.md) for details.

### Visual Studio Extension (`visual-studio-dtt/`)

Visual Studio 2022 extension with:
- Error List integration
- Solution-wide analysis
- WSL support for Windows
- Configurable options

**Build:** Open `DTTAnalyzer/DTTAnalyzer.csproj` in Visual Studio and build.

**Install:** Install the generated `.vsix` file.

See [visual-studio-dtt/README.md](visual-studio-dtt/README.md) for details.

## Quick Setup

1. Build the DTT plugin:
   ```bash
   cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
   cmake --build build
   ```

2. Note the path to `libDTTPlugin.so` (e.g., `/path/to/dtt/build/libDTTPlugin.so`)

3. Configure the IDE extension with this path

## Common Configuration

Both plugins support:

| Setting | Description |
|---------|-------------|
| Plugin Path | Path to `libDTTPlugin.so` |
| Clang Path | Path to `clang++` (18+) |
| Mode | `inform` / `enforce` / `quiet` |
| C++ Standard | `c++17` / `c++20` / `c++23` |
| Include Paths | Additional `-I` paths |

## Diagnostic Categories

| Category | Severity | Meaning |
|----------|----------|---------|
| E/F | Error | Proven bug |
| C | Warning | Lost tracking |
| D | Warning | Undecidable |
| G | Error | Annotation error |
