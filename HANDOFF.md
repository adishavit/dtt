# DTT Plugin v3: Handoff Document

## What's Implemented

| # | Feature | Status |
|---|---------|--------|
| 1 | Track any property | ✓ |
| 2 | Compound constraints (AND/OR) | ✓ |
| 3 | Symbolic arithmetic | ✓ |
| 4 | **Postconditions** | ✓ NEW |
| 5 | **Else-branch guards** | ✓ NEW |

## New Feature: Postconditions (#4)

Functions can declare what properties hold on return values:

```cpp
[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

Vec a; a.len = 5;
Vec b = tail(a);  // Plugin tracks: b.len = 5 - 1 = 4
```

**Implementation:**
- `parsePostcondition()` parses `result.field == expr`
- `FnPostconditions` map stores parsed postconditions per function
- `applyPostconditions()` called when detecting `var = fn(args)`
- Substitutes `arg0.field` with actual argument values from Env

## New Feature: Else-Branch Guards (#5)

Guards now work in else branches with negated conditions:

```cpp
if (v.len > 0) {
    head(v);         // OK: len > 0
} else {
    handleEmpty(v);  // OK: len <= 0 (negated)
}
```

**Implementation:**
- `extractGuard()` returns both `guardStr` and `negGuardStr`
- `VisitIfStmt` applies guard to then-branch, negated guard to else-branch

## Test Coverage

All 11 tests pass:
- 01-07: Original features
- 08-09: Postconditions (pass/fail)
- 10-11: Else-guards (pass/fail)

## What's NOT Implemented

| # | Feature | Complexity |
|---|---------|------------|
| 6 | Dependent return types (full) | Medium-High |
| 7 | Proof terms as values | High |
| 8 | Inductive proofs | Very High |
| 9 | Totality checking | Very High |
| 10 | Proof search/SMT | Research-level |

## Usage

```bash
./build.sh                    # Build + test
clang++ -fplugin=./DTTPlugin.so your_file.cpp
```

## Claude Code Continuation Prompt

```
Continue work on DTT plugin in ./dtt_plugin_package/

Current state: v3 with postconditions and else-guards working.
All 11 tests pass (run ./build.sh to verify).

Next potential tasks:
1. More postcondition patterns (result.len == arg0.len + arg1.len for concat)
2. Switch statement guards
3. Loop invariants
4. Better error messages with source locations
5. Integration with static_assert

Read src/DTTPlugin.cpp (~500 lines) to understand architecture.
```
