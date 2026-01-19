# DTT Debugging Notes: macOS/libc++ Compatibility

This document captures learnings from debugging macOS CI failures that didn't reproduce on Linux.

## Problem Summary

Tests for copy/move assignment tracking (`66_assignment_pass`, `70_move_invalidate_pass`, `71_move_invalidate_fail`) passed on Linux but failed on macOS. The error showed `v2._size = 0` after `v2 = std::move(v1)` when it should have copied the tracked properties.

## Root Cause

**libc++ uses inline namespaces.** On macOS with libc++, `std::move` is actually `std::__1::move`:

```cpp
namespace std {
  inline namespace __1 {
    template<class T> T&& move(T&& t) noexcept;
  }
}
```

The original `isStdMove()` only checked the immediate parent namespace:
```cpp
if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
    return NS->getNameAsString() == "std";  // Fails! DC is __1, not std
}
```

## Fix

Walk up the parent chain to find "std" anywhere in the namespace hierarchy:
```cpp
while (DC) {
    if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
        if (NS->getNameAsString() == "std") {
            return true;
        }
    }
    DC = DC->getParent();
}
```

## Debugging Approach

1. **Add diagnostic output** to key visitor methods to see which code paths are hit:
   ```cpp
   auto &DE = Ctx.getDiagnostics();
   unsigned ID = DE.getCustomDiagID(DiagnosticsEngine::Warning,
       "DEBUG: lhsPath='%0' rhsPath='%1'");
   DE.Report(E->getBeginLoc(), ID) << lhsPath << rhsPath;
   ```

2. **Push to CI** and examine logs - this revealed the same AST visitor was hit on both platforms, but `std::move` wasn't being detected on macOS.

3. **Add more targeted debug** to `isStdMove()` showing the qualified name:
   ```cpp
   DE.Report(E->getBeginLoc(), ID) << name << Callee->getQualifiedNameAsString();
   ```
   This showed `std::__1::move` on macOS vs `std::move` on Linux.

4. **Use the `/council` skill** to get multiple LLM perspectives on the problem. The council analysis helped identify the AST representation differences between libstdc++ and libc++.

## Key Learnings

### 1. libstdc++ vs libc++ AST Differences
- **Inline namespaces**: libc++ uses `std::__1::*` for most standard library functions
- **operator= representation**: May appear as `CXXOperatorCallExpr`, `CXXMemberCallExpr`, or `BinaryOperator` depending on stdlib

### 2. String Matching is Fragile
Matching function names like "move" and "swap" by string is implementation-dependent. More robust alternatives:
- Use `Callee->getQualifiedNameAsString().find("std::") == 0` for prefix matching
- Check the function's template origin or canonical declaration
- Use Clang's semantic analysis APIs when available

### 3. Effective Debugging Strategy
1. Add targeted diagnostic output
2. Test locally first (catches obvious errors)
3. Push to CI for cross-platform verification
4. Use `gh run view --log-failed` to examine CI output
5. Iteratively narrow down the issue

### 4. Namespace Traversal Pattern
When checking if something is in a specific namespace, always traverse the full parent chain:
```cpp
DeclContext *DC = Decl->getDeclContext();
while (DC) {
    if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
        if (NS->getNameAsString() == "target_namespace") {
            return true;
        }
    }
    DC = DC->getParent();
}
```

## Files Modified

- `src/DTTPlugin.cpp`: `isStdMove()` function - namespace traversal fix

## Test Commands

```bash
# Build
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/66_assignment_pass.cpp

# Check CI status (requires gh CLI)
gh run list --repo adishavit/dtt --limit 5
gh run view <run-id> --log-failed
```
