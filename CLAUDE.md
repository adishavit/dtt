# DTT Project Instructions

## Documentation Rules

- **NEVER write the number of tests in docs** - test counts change frequently and become stale

## Bug Fix Workflow (TDD - Non-negotiable)

When fixing bugs or addressing code review findings:

1. **Write test FIRST** that demonstrates the bug
2. **Run test** - confirm it FAILS (proves bug exists)
3. **Implement fix**
4. **Run test** - confirm it PASSES

No exceptions. Test first, fix second.

### MANDATORY TDD Rules

1. **Tests go in the repo** - Write all debugging/test files in `examples/` folder, NEVER in `/tmp`
2. **Multiple variants per gap** - ALWAYS create multiple test variants for each detected gap, ALL in the same test file (e.g., `509_feature_pass.cpp` with variant1, variant2, etc.)
3. **Fix uncovered xfail tests** - When your fix causes an xfail test to pass (DTT now catches the issue), rename it from `*_xfail.cpp` to `*_fail.cpp` and update CMakeLists.txt
4. **MUST run test before implementing** - Confirm test FAILS first (proves the gap/bug exists)
5. **MUST run test after implementing** - Confirm test PASSES (proves the fix works)
6. **Never skip verification steps** - Both pre and post test runs are mandatory, not optional

### TDD Mantra

See **@TDD.md** - recite before every bug fix.

## Test Naming Convention

- `*_pass.cpp` - Should compile without DTT errors
- `*_fail.cpp` - Should produce DTT errors (compilation fails)
- `*_xfail.cpp` - Known gaps (false negatives) - DTT should catch but doesn't yet
- `*_silent.cpp` - Should compile silently (no errors, no warnings)

## Build & Test

```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
ctest --test-dir build --output-on-failure  # Run ALL tests locally
```

## xfail Test Semantics

xfail tests document **false negatives** - unsafe code that DTT should catch but doesn't yet.

- Uses `WILL_FAIL TRUE` in CMake
- Gap exists → compilation succeeds → test "fails" (expected)
- Gap fixed → compilation fails (DTT catches it) → test passes (update to `*_fail.cpp`)

**Local runs:** Always run ALL tests including xfail to see current gap status.

**CI:** Runs xfail tests separately with `continue-on-error: true` so gaps don't block CI.

## Debugging Reference

See **@DEBUGGING_NOTES.md** for platform-specific debugging patterns (macOS/libc++ quirks, CI debugging workflow).
