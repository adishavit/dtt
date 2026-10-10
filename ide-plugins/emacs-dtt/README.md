# dtt.el

**Real-time memory safety analysis for C++ in Emacs** — using [Flycheck](https://www.flycheck.org/).

## Features

- Integrates with Flycheck's on-the-fly checking
- Fringe markers and underlines for errors
- Error list integration
- Cross-platform (Linux, macOS, Windows)

## What DTT Catches

| Category | Example |
|----------|---------|
| **Null pointer dereference** | `int* p = nullptr; *p = 1;` |
| **Use after move** | `auto v2 = std::move(v1); v1.push_back(1);` |
| **Use-after-free** | `int* p = new int; delete p; *p = 1;` |
| **Buffer overflow** | `int arr[10]; arr[10] = 1;` |

## Requirements

- **Emacs 25.1+**
- **[Flycheck](https://www.flycheck.org/)** (`M-x package-install RET flycheck`)
- **Clang 18+** with plugin support
- **DTT plugin** (`libDTTPlugin.so` / `.dylib` / `.dll`)

## Installation

### MELPA (when available)
```elisp
M-x package-install RET dtt
```

### use-package
```elisp
(use-package dtt
  :after flycheck
  :hook ((c-mode c++-mode) . dtt-mode))
```

### Manual
1. Copy `dtt.el` to your `~/.emacs.d/lisp/` directory
2. Add to your init file:
```elisp
(add-to-list 'load-path "~/.emacs.d/lisp/")
(require 'dtt)
(add-hook 'c-mode-hook #'dtt-mode)
(add-hook 'c++-mode-hook #'dtt-mode)
```

### straight.el
```elisp
(straight-use-package
 '(dtt :type git :host github :repo "adishavit/dtt"
       :files ("ide-plugins/emacs-dtt/*.el")))
```

## Configuration

```elisp
;; Path to clang++ (default: "clang++")
(setq dtt-clang-path "/usr/bin/clang++-18")

;; Path to DTT plugin (nil = auto-detect)
(setq dtt-plugin-path "/usr/local/lib/libDTTPlugin.so")

;; C++ standard (default: "c++20")
(setq dtt-standard "c++20")

;; Analysis mode (default: "inform")
;; Options: "inform", "enforce", "quiet"
(setq dtt-mode-arg "inform")
```

### Analysis Modes

| Mode | Behavior |
|------|----------|
| `inform` | Warnings for gaps (unknown state), errors for proven bugs |
| `enforce` | Errors for everything (strictest) |
| `quiet` | Errors only for proven bugs (most lenient) |

## Usage

Enable DTT mode in C/C++ buffers:

```elisp
;; Auto-enable for all C/C++ files
(add-hook 'c-mode-hook #'dtt-mode)
(add-hook 'c++-mode-hook #'dtt-mode)
```

Or enable manually: `M-x dtt-mode`

### Commands

| Command | Description |
|---------|-------------|
| `M-x dtt-mode` | Toggle DTT checking in current buffer |
| `M-x flycheck-list-errors` | Show error list (`C-c ! l`) |
| `M-x flycheck-next-error` | Jump to next error (`C-c ! n`) |
| `M-x flycheck-previous-error` | Jump to previous error (`C-c ! p`) |
| `M-x flycheck-buffer` | Check current buffer (`C-c ! c`) |

## Flycheck Integration

DTT defines a Flycheck checker named `dtt`. When `dtt-mode` is enabled:

1. The `dtt` checker is added to `flycheck-checkers`
2. Flycheck mode is enabled
3. Analysis runs automatically on file changes

### Checker Priority

If you use other C++ checkers, you can chain them:

```elisp
;; Run clang first, then DTT
(flycheck-add-next-checker 'c/c++-clang 'dtt)
```

### Customize Flycheck Display

```elisp
;; Use left fringe for indicators
(setq flycheck-indication-mode 'left-fringe)

;; Customize fringe indicators
(define-fringe-bitmap 'flycheck-fringe-bitmap-double-arrow
  [0 24 60 126 126 60 24 0])

;; Delay before checking (seconds)
(setq flycheck-idle-change-delay 0.5)
```

## Plugin Path Auto-Detection

DTT searches these locations automatically:

### Linux
- `~/.dtt/bin/libDTTPlugin.so`
- `/usr/local/lib/libDTTPlugin.so`
- `/usr/lib/libDTTPlugin.so`

### macOS
- `~/.dtt/bin/libDTTPlugin.dylib`
- `/opt/homebrew/lib/libDTTPlugin.dylib`
- `/usr/local/lib/libDTTPlugin.dylib`

### Windows
- `~/.dtt/bin/DTTPlugin.dll`
- `%LOCALAPPDATA%/DTT/bin/DTTPlugin.dll`
- `C:/Program Files/DTT/bin/DTTPlugin.dll`
- `C:/Program Files/LLVM/lib/DTTPlugin.dll`

## Troubleshooting

### "Cannot find DTT plugin"
Set the path manually:
```elisp
(setq dtt-plugin-path "/path/to/libDTTPlugin.so")
```

### No errors appearing
1. Check Flycheck is enabled: `M-x flycheck-mode`
2. Verify DTT checker: `M-x flycheck-verify-setup`
3. Check for errors: `M-x flycheck-compile`
4. View checker output: `*Flycheck error messages*` buffer

### "dtt" checker not available
Ensure:
1. Flycheck is installed and loaded
2. DTT plugin file exists at configured path
3. `dtt-mode` is enabled in the buffer

### Filtering non-DTT errors
DTT automatically filters output to show only DTT diagnostics (messages containing "DTT:"). Regular Clang errors are filtered out.

## Customization Group

All DTT settings are in the `dtt` customization group:

```elisp
M-x customize-group RET dtt
```

## Building DTT Plugin

```bash
git clone https://github.com/adishavit/dtt.git
cd dtt
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
# Plugin: build/libDTTPlugin.so
```

## License

BSD 2-Clause License - see [LICENSE](../LICENSE)
