# dtt.nvim

**Real-time memory safety analysis for C++ in Neovim** — catch null pointer dereferences, use-after-move, dangling pointers, and buffer overflows as you type.

## Features

- Native `vim.diagnostic` integration — errors appear in the sign column and virtual text
- Debounced analysis — won't slow you down while typing
- Async execution — non-blocking analysis
- Cross-platform — Linux, macOS, Windows

## What DTT Catches

| Category | Example |
|----------|---------|
| **Null pointer dereference** | `int* p = nullptr; *p = 1;` |
| **Use after move** | `auto v2 = std::move(v1); v1.push_back(1);` |
| **Dangling pointer** | `int* p = &vec[0]; vec.push_back(1); *p;` |
| **Buffer overflow** | `int arr[10]; arr[10] = 1;` |

## Installation

### lazy.nvim
```lua
{
  "adishavit/dtt",
  config = function()
    require("dtt").setup()
  end,
  ft = { "cpp", "c" },
}
```

### packer.nvim
```lua
use {
  "adishavit/dtt",
  config = function()
    require("dtt").setup()
  end,
  ft = { "cpp", "c" },
}
```

### vim-plug
```vim
Plug 'adishavit/dtt'

" In your init.lua or after/plugin/dtt.lua:
lua require("dtt").setup()
```

## Requirements

- **Neovim 0.8+** (for `vim.diagnostic` API)
- **Clang 18+** with plugin support
- **DTT plugin** (`libDTTPlugin.so` / `.dylib` / `.dll`)

## Configuration

```lua
require("dtt").setup({
  -- Path to clang++ executable
  clang_path = "clang++",

  -- Path to DTT plugin (nil = auto-detect)
  plugin_path = nil,

  -- C++ standard
  standard = "c++20",

  -- Analysis mode: "inform", "enforce", "quiet"
  mode = "inform",

  -- Debounce delay in milliseconds
  debounce_ms = 500,

  -- Analyze on file save
  analyze_on_save = true,

  -- Analyze on text change (while typing)
  analyze_on_change = true,
})
```

### Analysis Modes

| Mode | Behavior |
|------|----------|
| `inform` | Warnings for gaps (unknown state), errors for proven bugs |
| `enforce` | Errors for everything (strictest) |
| `quiet` | Errors only for proven bugs (most lenient) |

## Commands

| Command | Description |
|---------|-------------|
| `:DTTAnalyze` | Run analysis on current buffer |
| `:DTTClear` | Clear all DTT diagnostics |

## API

```lua
local dtt = require("dtt")

-- Analyze current buffer
dtt.analyze()

-- Analyze specific buffer
dtt.analyze(bufnr)

-- Clear diagnostics
dtt.clear()
dtt.clear(bufnr)
```

## Plugin Path Auto-Detection

DTT automatically searches these locations:

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

## Diagnostic Display

DTT uses Neovim's built-in diagnostic system. Customize appearance with:

```lua
vim.diagnostic.config({
  virtual_text = true,      -- Show inline text
  signs = true,             -- Show signs in gutter
  underline = true,         -- Underline errors
  update_in_insert = false, -- Don't update while typing
  severity_sort = true,     -- Sort by severity
})

-- Custom signs
local signs = { Error = " ", Warn = " ", Hint = " ", Info = " " }
for type, icon in pairs(signs) do
  local hl = "DiagnosticSign" .. type
  vim.fn.sign_define(hl, { text = icon, texthl = hl, numhl = hl })
end
```

## Integration with Other Plugins

### lualine.nvim
DTT diagnostics automatically appear in lualine's diagnostics component.

### trouble.nvim
DTT diagnostics appear in Trouble's diagnostics list.

### telescope.nvim
Use `:Telescope diagnostics` to search DTT errors.

## Troubleshooting

### "DTT: Plugin not found"
Set the plugin path manually:
```lua
require("dtt").setup({
  plugin_path = "/path/to/libDTTPlugin.so"
})
```

### No diagnostics appearing
1. Run `:DTTAnalyze` manually
2. Check `:messages` for errors
3. Verify clang works: `:!clang++ --version`
4. Test DTT directly: `:!clang++ -fplugin=/path/to/plugin -fsyntax-only %`

### Diagnostics are delayed
Increase or decrease debounce:
```lua
require("dtt").setup({
  debounce_ms = 1000,  -- Wait longer
  -- or
  debounce_ms = 200,   -- More responsive
})
```

## Building DTT Plugin

If you need to build the DTT plugin from source:

```bash
git clone https://github.com/adishavit/dtt.git
cd dtt
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
# Plugin: build/libDTTPlugin.so (or .dylib on macOS)
```

## License

MIT License
