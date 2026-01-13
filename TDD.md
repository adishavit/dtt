# TDD Mantra (10 Steps)

1. **Tests in `examples/`** - never `/tmp` to stray
2. **Multiple variants** - in one file they stay
3. **Write the test first** - before you code away
4. **Run and watch it fail** - the bug is on display
5. **Implement the fix** - make the error pay
6. **Run and watch it pass** - green lights the way
7. **Rename xfail to fail** - the gap has had its day
8. **Clean or convert debug files** - don't let them decay
9. **Run all tests again** - ensure nothing's gone astray
10. **Update docs and commit** - ship it right away

*No exceptions to this rhyme,*
*Test first, fix second, every time.*

*Name your tests, don't go too fast:*
- **`_pass`** - safe code, tests fly past
- **`_fail`** - bugs found, errors amassed
- **`_xfail`** - known gap, won't always last
- **`_silent`** - out of scope, DTT bypassed
