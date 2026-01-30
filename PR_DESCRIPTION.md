# DTT IDE Plugins - Complete Multi-Editor Support

## Summary

Adds real-time memory safety analysis (IntelliSense-style squiggly underlines) to **7 IDEs/editors** with zero-config setup and automated marketplace publishing.

## Features

### IDE/Editor Support

| Editor | Integration | Auto-Setup | Marketplace |
|--------|-------------|------------|-------------|
| VS Code | Native extension | ✅ | VS Code + Open VSX |
| Visual Studio | Native extension | ✅ | - |
| CLion | IntelliJ plugin | ✅ | JetBrains |
| Neovim | Lua plugin | ✅ | - |
| Vim | ALE linter | - | - |
| Emacs | Flycheck | - | - |

### Zero-Config Experience

- **Auto-detection**: Finds existing clang in PATH and common locations
- **Auto-download**: Downloads LLVM 18 from official GitHub releases if needed
- **Auto-plugin**: Downloads DTT plugin from releases automatically
- Users just install and open a C++ file - everything else is automatic

### CI/CD Pipeline

```
Push tag v* → Build all platforms → Create Release → Publish to Marketplaces
```

- Linux, Windows (vcpkg), macOS builds
- VS Code, CLion extension builds
- Automatic GitHub Release with checksums
- VS Code Marketplace + Open VSX publishing
- JetBrains Marketplace publishing
- Pre-releases skip marketplace (test first)

## Files Added/Modified

### New IDE Plugins
- `ide-plugins/vscode-dtt/` - VS Code extension with auto-setup
- `ide-plugins/vs-dtt/` - Visual Studio extension with auto-setup
- `ide-plugins/clion-dtt/` - CLion/IntelliJ plugin
- `ide-plugins/nvim-dtt/` - Neovim plugin
- `ide-plugins/vim-dtt/` - Vim ALE linter
- `ide-plugins/emacs-dtt/` - Emacs Flycheck checker

### CI/CD
- `.github/workflows/release.yml` - Full release automation

### Documentation
- `RELEASING.md` - Semantic versioning, release checklist, marketplace registration
- `ide-plugins/README.md` - Installation guides for all 7 editors

## Architecture

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   Parser     │     │    Runner    │     │   IDE Glue   │
│              │     │              │     │              │
│ Pure funcs   │◄────│ Clang exec   │◄────│ Editor-      │
│ No deps      │     │ Cancellation │     │ specific     │
└──────────────┘     └──────────────┘     └──────────────┘
```

Each plugin follows the same pattern - only the IDE glue layer differs.

## Required Secrets (One-Time Setup)

| Secret | Purpose |
|--------|---------|
| `VSCODE_MARKETPLACE_TOKEN` | VS Code publishing |
| `OPENVSX_TOKEN` | Open VSX publishing |
| `JETBRAINS_MARKETPLACE_TOKEN` | CLion publishing |

See `RELEASING.md` for detailed registration steps.

## Test Plan

- [ ] VS Code: Install .vsix, open C++ file, verify squiggles appear
- [ ] CLion: Install .zip, open C++ file, verify annotations appear
- [ ] Neovim: `require("dtt").setup()`, open C++ file, verify diagnostics
- [ ] Vim: Enable ALE, open C++ file, verify `:ALEInfo` shows dtt linter
- [ ] Emacs: Enable `dtt-mode`, open C++ file, verify flycheck errors
- [ ] CI: Push tag, verify all artifacts built and uploaded
- [ ] Auto-download: Test on clean system without clang installed

## Breaking Changes

None - this is purely additive.

## Future Work

- [ ] Create DTT Language Server (LSP) to DRY up the diagnostic parsing
- [ ] Add Sublime Text support (via LSP)
- [ ] Add Qt Creator support
