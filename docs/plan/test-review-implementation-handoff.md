# Handoff: Review + refine tests for guard scope, tracking leaks, and constraint parsing

## Context
The prior change added four example-based tests intended to document suspected analyzer issues:
- Guard proofs leaking past `if` without `else` (silent test).
- Lost-tracking state leaking across function boundaries (xfail test).
- Expression constraint parsing that ignores subtraction (xfail test).
- Numeric `!= 0` misclassified as nullability (xfail test).

These tests live in `examples/308_if_guard_persists_silent.cpp`,
`examples/309_lost_tracking_leak_xfail.cpp`,
`examples/310_minus_expr_constraint_xfail.cpp`, and
`examples/311_nonzero_constraint_xfail.cpp`.

The intent is to verify these tests *actually surface* the issues described and refine them if they don’t.

## Why a fresh CLI session is needed
The current environment lacks a complete Clang/LLVM dev package. `find_package(Clang REQUIRED CONFIG)` in the root `CMakeLists.txt` fails because required libraries (e.g., `libclangBasic.a`) are missing. A fresh CLI session with a full LLVM/Clang dev install is needed to configure and run tests.

## Relevant code paths
You’ll likely reference these paths while validating expectations:
- `src/DTTPlugin.cpp`
  - `DTTVisitor::TraverseIfStmt` retains proof after `if` with no `else`.
  - `DTTVisitor::TraverseFunctionDecl` clears `Env` and `VarBoundProofs` but does **not** clear `FieldStates` / `WarnedLostTracking`.
  - `parseSimpleConstraint` uses `hasArithmeticOps` that ignores `-` and treats `arg0 != 0` as nullptr constraints.

## Checklist for your fresh session

### 1) Configure & build
Use a system with Clang + LLVM dev packages installed.

```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
```

If your system provides a newer Clang, use that but keep versions consistent with LLVM packages.

### 2) Run targeted tests
Run only the new tests first:

```bash
ctest --test-dir build -R "308_if_guard_persists_silent|309_lost_tracking_leak_xfail|310_minus_expr_constraint_xfail|311_nonzero_constraint_xfail" --output-on-failure
```

Expected behavior (current state):
- `308_if_guard_persists_silent` should **pass silently** (documenting a false negative).
- `309_lost_tracking_leak_xfail` should **fail** (and be counted as a passing xfail test).
- `310_minus_expr_constraint_xfail` should **fail** (xfail) due to mis-parse.
- `311_nonzero_constraint_xfail` should **fail** (xfail) due to nullability misclassification.

If any of these don’t happen, refine the test or update the expected classification.

### 3) Sanity check diagnostic output
If you want to validate diagnostic messages directly, run clang syntax checks:

```bash
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/309_lost_tracking_leak_xfail.cpp
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/310_minus_expr_constraint_xfail.cpp
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/311_nonzero_constraint_xfail.cpp
```

You should see error messages consistent with the intended failure modes.

### 4) Adjust tests if needed
If any test fails to reproduce the issue, update it in place:
- **Guard leakage**: make sure the guard is the only proof source. Avoid other proof sources (e.g., `front()` or inferred invariants) that could hide the issue.
- **Lost tracking leak**: confirm reuse of the same variable name across two functions and ensure the second function does not create tracking. If it still doesn’t reproduce, add a field access that triggers a Category C path (e.g., `requires=_size > 0` with no state).
- **Minus expression parsing**: ensure the expression uses subtraction (`arg0 - 1 < _size`) and that it’s part of a requires constraint so the parser is exercised.
- **Nonzero numeric constraint**: keep the argument type `int` and verify that `arg0 != 0` is parsed as a nullptr constraint (current behavior) and produces the wrong diagnostic.

### 5) Confirm classification in CMake
`CMakeLists.txt` uses file suffix conventions:
- `_silent.cpp` → PASS, no warnings/errors.
- `_xfail*.cpp` → WILL_FAIL in enforce mode.

If you rename or reclassify a test, ensure its suffix matches the intended behavior.

## Notes for follow-on implementation
If you plan to fix the underlying issues after test confirmation:
- **Guard leakage**: in `TraverseIfStmt`, remove the proof after the `then` block when there is no `else`.
- **Lost tracking leak**: in `TraverseFunctionDecl`, clear `FieldStates` and `WarnedLostTracking` alongside `Env` and `VarBoundProofs`.
- **Constraint parsing**: improve `hasArithmeticOps` to detect `-` correctly or replace it with a proper expression parser with operator precedence.
- **Nonzero constraint**: only treat `0` as nullptr when the LHS is a pointer or a special pointer marker (`_result`), not for generic numeric arguments.

## Files to review
- `examples/308_if_guard_persists_silent.cpp`
- `examples/309_lost_tracking_leak_xfail.cpp`
- `examples/310_minus_expr_constraint_xfail.cpp`
- `examples/311_nonzero_constraint_xfail.cpp`
- `src/DTTPlugin.cpp`
- `CMakeLists.txt`

