# Handoff: Review + refine tests for guard scope, tracking leaks, and constraint parsing

## Context
The prior change added four example-based tests intended to document suspected analyzer issues:
- Guard proofs leaking past `if` without `else` (silent test).
- Lost-tracking state leaking across function boundaries (xfail test).
- Expression constraint parsing that ignores subtraction (xfail test).
- Numeric `!= 0` misclassified as nullability (xfail test).

These tests live in:
- `examples/308_if_guard_persists_silent.cpp`
- `examples/309_lost_tracking_leak_xfail.cpp`
- `examples/310_minus_expr_constraint_xfail.cpp`
- `examples/311_nonzero_constraint_xfail.cpp`

The intent is to verify these tests *actually surface* the issues described and refine them if they don’t.

## Why a fresh CLI session is needed
The current environment lacks a complete Clang/LLVM dev package. `find_package(Clang REQUIRED CONFIG)` in the root `CMakeLists.txt` fails because required libraries (e.g., `libclangBasic.a`) are missing. A fresh CLI session with a full LLVM/Clang dev install is needed to configure and run tests.

## Detailed problem descriptions (what each test is proving)

### 1) Guard proofs leak past `if` without `else`
**Where it lives:** `src/DTTPlugin.cpp`, `DTTVisitor::TraverseIfStmt`.

**Observed behavior:** When an `if` has no `else`, the guard proof is **not** removed after the `then` block. This means any proof established in the condition (e.g., `_size > 0` or `_nonnull != 0`) is treated as globally true after the statement, even though the condition may be false.

**Why it’s unsound:** In `if (cond) { ... }`, the condition only holds inside the `then` block. Outside, the program must be considered in a merged state where the condition may be true or false. Retaining the proof past the `if` incorrectly assumes the condition always held.

**How it hides real bugs:** If `_size > 0` is retained globally, a later `front()` or `operator[]` access can avoid diagnostics even though the container could still be empty. Similarly, `if (ptr)` can cause `ptr` to be treated as non-null in the rest of the function.

**Test to validate:** `examples/308_if_guard_persists_silent.cpp`.

**Expected current outcome:** The test compiles cleanly with no diagnostics (silent false negative), proving the leak.

---

### 2) Lost-tracking state leaks across functions
**Where it lives:** `src/DTTPlugin.cpp`, `DTTVisitor::TraverseFunctionDecl` and tracking state.

**Observed behavior:** When entering a new function body, the visitor clears `Env` and `VarBoundProofs`, but **does not clear** `FieldStates` or `WarnedLostTracking`. These structures carry “lost tracking” provenance and “warn once” flags from previous functions.

**Why it’s incorrect:** Each function should be analyzed in isolation. Variable names are often reused (`v`, `i`, `ptr`). If lost-tracking state persists, a variable in a later function may incorrectly be treated as “tracking lost” even if it was never tracked in that function.

**How it breaks diagnostics:** A second function can receive a Category C diagnostic because an identically named variable in a previous function lost tracking. This is both noisy and misleading. It can also suppress warnings due to the “warn once” cache crossing scopes.

**Test to validate:** `examples/309_lost_tracking_leak_xfail.cpp`.

**Expected current outcome:** In enforce mode, the test should produce a Category C error in the second function (xfail), demonstrating the leak.

---

### 3) Subtraction in constraints is mis-parsed
**Where it lives:** `src/DTTPlugin.cpp`, `parseSimpleConstraint` and `hasArithmeticOps`.

**Observed behavior:** `hasArithmeticOps` intentionally ignores `-` to avoid ambiguity with negative numbers. This prevents detection of valid arithmetic expressions like `arg0 - 1 < _size`, causing them to be treated as non-expression constraints. The parser then misinterprets the constraint as `arg0 < _size`.

**Why it’s incorrect:** `arg0 - 1 < _size` is a different constraint than `arg0 < _size`. Ignoring subtraction changes the meaning of the contract and can incorrectly reject valid code or allow invalid code depending on the case.

**How it shows up:** If `arg0 == _size`, the expression `arg0 - 1 < _size` is true, but the mis-parsed version `arg0 < _size` is false, triggering an erroneous error.

**Test to validate:** `examples/310_minus_expr_constraint_xfail.cpp`.

**Expected current outcome:** In enforce mode, the test should fail (xfail) because the constraint is mis-parsed, proving the bug.

---

### 4) Numeric `!= 0` treated as nullability
**Where it lives:** `src/DTTPlugin.cpp`, `parseSimpleConstraint`.

**Observed behavior:** The parser treats any `... != 0` or `... == 0` constraint as a nullability constraint, regardless of the argument type. This is correct for pointer constraints but incorrect for integer constraints.

**Why it’s incorrect:** A numeric requirement like `arg0 != 0` should be treated as a numeric constraint on the argument value, not a pointer null check. The current logic maps it to `_nonnull` checks, which are unrelated to integers.

**How it breaks diagnostics:** A function annotated with `dtt:requires=arg0 != 0` on an `int` parameter can produce nullability diagnostics (e.g., “potential null pointer dereference of 'arg0'”) rather than the intended numeric constraint checking.

**Test to validate:** `examples/311_nonzero_constraint_xfail.cpp`.

**Expected current outcome:** In enforce mode, the test should fail (xfail) with a nullability-style diagnostic, proving the misclassification.

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
