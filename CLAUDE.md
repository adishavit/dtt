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
