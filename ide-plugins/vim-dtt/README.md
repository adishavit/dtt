# vim-dtt

**Real-time memory safety analysis for C++ in Vim** — using [ALE](https://github.com/dense-analysis/ale) (Asynchronous Lint Engine).

## Features

- Integrates with ALE's async linting system
- Non-blocking analysis
- Location list integration
- Sign column markers
- Cross-platform (Linux, macOS, Windows)

## What DTT Catches

| Category | Example |
|----------|---------|
| **Null pointer dereference** | `int* p = nullptr; *p = 1;` |
| **Use after move** | `auto v2 = std::move(v1); v1.push_back(1);` |
| **Dangling pointer** | `int* p = &vec[0]; vec.push_back(1); *p;` |
| **Buffer overflow** | `int arr[10]; arr[10] = 1;` |

## Requirements

- **Vim 8.0+** or **Neovim**
- **[ALE](https://github.com/dense-analysis/ale)** (Asynchronous Lint Engine)
- **Clang 18+** with plugin support
- **DTT plugin** (`libDTTPlugin.so` / `.dylib` / `.dll`)

## Installation

### vim-plug
```vim
Plug 'dense-analysis/ale'
Plug 'adishavit/dtt', { 'rtp': 'ide-plugins/vim-dtt' }
```

### Vundle
```vim
Plugin 'dense-analysis/ale'
Plugin 'adishavit/dtt'
```

### Manual
Copy `plugin/dtt.vim` to your `~/.vim/plugin/` directory.

## Configuration

Add to your `.vimrc`:

```vim
" Path to clang++ (default: clang++)
let g:dtt_clang_path = 'clang++'

" Path to DTT plugin (empty = auto-detect)
let g:dtt_plugin_path = ''

" C++ standard (default: c++20)
let g:dtt_standard = 'c++20'

" Analysis mode: inform, enforce, quiet (default: inform)
let g:dtt_mode = 'inform'
```

### Analysis Modes

| Mode | Behavior |
|------|----------|
| `inform` | Warnings for gaps (unknown state), errors for proven bugs |
| `enforce` | Errors for everything (strictest) |
| `quiet` | Errors only for proven bugs (most lenient) |

## Enabling DTT in ALE

DTT registers itself as an ALE linter. Enable it:

```vim
" Enable DTT for C/C++
let g:ale_linters = {
\   'cpp': ['dtt'],
\   'c': ['dtt'],
\}

" Or add to existing linters
let g:ale_linters = {
\   'cpp': ['clang', 'dtt'],
\}
```

## ALE Configuration

Customize ALE's display for better DTT experience:

```vim
" Show errors in sign column
let g:ale_sign_error = '✗'
let g:ale_sign_warning = '⚠'

" Show errors inline (Vim 8.1.1564+ or Neovim)
let g:ale_virtualtext_cursor = 1

" Update on save only (less CPU usage)
let g:ale_lint_on_text_changed = 'never'
let g:ale_lint_on_insert_leave = 0
let g:ale_lint_on_save = 1

" Or update while typing (more responsive)
let g:ale_lint_on_text_changed = 'always'
let g:ale_lint_delay = 500
```

## Commands

ALE provides these commands for DTT diagnostics:

| Command | Description |
|---------|-------------|
| `:ALELint` | Run linters manually |
| `:ALEDetail` | Show full error message |
| `:ALENext` / `:ALEPrevious` | Jump between errors |
| `:ALEPopulateLocList` | Fill location list with errors |
| `:lopen` | Open location list |

## Plugin Path Auto-Detection

DTT searches these locations automatically:

### Linux
- `~/.dtt/bin/libDTTPlugin.so`
- `/usr/local/lib/libDTTPlugin.so`
- `/usr/lib/libDTTPlugin.so`

### macOS
- `~/.dtt/bin/libDTTPlugin.dylib`
- `/opt/homebrew/lib/libDTTPlugin.dylib`

### Windows
- `~/.dtt/bin/DTTPlugin.dll`
- `%LOCALAPPDATA%/DTT/bin/DTTPlugin.dll`
- `C:/Program Files/DTT/bin/DTTPlugin.dll`

## Troubleshooting

### "DTT: Plugin not found"
Set the path manually:
```vim
let g:dtt_plugin_path = '/path/to/libDTTPlugin.so'
```

### No errors appearing
1. Check ALE is loaded: `:ALEInfo`
2. Verify DTT is listed: Look for "dtt" in linters
3. Run manually: `:ALELint`
4. Check for errors: `:messages`

### ALE not finding DTT
Ensure the plugin loads after ALE:
```vim
" Load ALE first
Plug 'dense-analysis/ale'
" Then DTT
Plug 'adishavit/dtt', { 'rtp': 'ide-plugins/vim-dtt' }
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

MIT License
