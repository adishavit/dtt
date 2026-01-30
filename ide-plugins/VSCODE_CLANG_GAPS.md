# VS Code Clang Command Adapter - DTT Integration Gaps

Analysis of using [mitaki28/vscode-clang](https://github.com/mitaki28/vscode-clang) for real-time DTT diagnostics.

## Critical Gap #1: stdin-Only Parsing

**Problem:** The extension **only parses diagnostics from `<stdin>`**, not file paths.

The regex in `diagnostic.ts`:
```typescript
/^<stdin>:(\d+):(\d+):(?:((?:\{.+?\})+):)? ((?:fatal )?error|warning): (.*?)$/
```

**How it works:**
1. Extension pipes file content to clang via stdin
2. Runs: `cat file.cpp | clang -x c++ -fsyntax-only -`
3. Clang outputs: `<stdin>:10:5: error: ...`
4. Extension regex matches `<stdin>` prefix

**DTT outputs:**
```
examples/02_basic_fail.cpp:10:5: error: DTT: vector::front requires _size > 0
```

**Result:** DTT diagnostics are **silently ignored** because they don't match `<stdin>`.

---

## Critical Gap #2: Plugin Loading with stdin

**Problem:** Clang plugins may not work correctly with stdin input.

When using stdin (`-`), clang loses:
- The actual file path (needed for `#include` resolution relative to file)
- Proper source location context for multi-file projects
- Precompiled header associations

**Test needed:** Verify if `-fplugin` works with stdin input:
```bash
cat file.cpp | clang++ -fplugin=libDTTPlugin.so -x c++ -fsyntax-only -
```

---

## Gap #3: Include Path Resolution

**Problem:** When piping via stdin, include paths become tricky.

The extension doesn't automatically:
- Set working directory to file's parent
- Add the file's directory to include paths
- Handle `compile_commands.json`

Users must manually configure all include paths in `clang.cxxflags`.

---

## Gap #4: No compile_commands.json Support

**Problem:** Modern C++ projects use `compile_commands.json` for build flags.

vscode-clang doesn't read this file. Users must manually replicate all:
- Include paths (`-I`)
- Defines (`-D`)
- Standard flags (`-std=c++20`)
- Other project-specific flags

---

## Potential Solutions

### Solution A: Fork vscode-clang

Modify the extension to:
1. Run clang with file path instead of stdin
2. Change regex to match any path: `/^(.+?):(\d+):(\d+):/`
3. Add `compile_commands.json` support

**Effort:** Medium
**Compatibility:** Would still work for non-DTT use cases

### Solution B: Create DTT-Specific Extension

Build a new extension specifically for DTT:
1. Run `clang++ -fplugin=DTT.so -fsyntax-only file.cpp`
2. Parse standard clang diagnostic format
3. Support `compile_commands.json`
4. Debounce on typing (300-500ms delay)

**Effort:** Medium-High
**Quality:** Best for DTT users

### Solution C: Create Proper LSP Server

Build a Language Server Protocol server for DTT:
1. Full LSP compliance (diagnostics, hover, code actions)
2. Works with any LSP-compatible editor
3. Can show DTT preconditions on hover
4. Can suggest fixes (add guards, null checks)

**Effort:** High
**Quality:** Professional-grade solution

### Solution D: Wrapper Script

Create a wrapper that:
1. Receives stdin
2. Writes to temp file
3. Runs DTT on temp file
4. Transforms output to use `<stdin>` prefix

```bash
#!/bin/bash
TEMP=$(mktemp --suffix=.cpp)
cat > "$TEMP"
clang++ -fplugin=libDTTPlugin.so -fsyntax-only "$TEMP" 2>&1 | \
    sed "s|$TEMP|<stdin>|g"
rm "$TEMP"
```

**Effort:** Low
**Quality:** Hacky, but might work

---

## Recommendation

**Short-term:** Try Solution D (wrapper script) to validate the approach works.

**Long-term:** Solution B or C - build a proper DTT extension or LSP server.

The vscode-clang extension wasn't designed for custom plugins, and forcing DTT into it will always be fragile.
