# DTT Project Instructions

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
- `*_xfail.cpp` - Known limitations, expected to fail
- `*_silent.cpp` - Should compile silently (no errors, no warnings)

## Build & Test

```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
ctest --test-dir build -LE xfail  # Exclude xfail tests
```
