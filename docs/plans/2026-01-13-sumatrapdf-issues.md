# Plan: SumatraPDF-Discovered Issues (51-55)

## TDD Workflow (MANDATORY)

For each issue:
1. **Write test FIRST** in `examples/` folder
2. **Run test** - confirm FAILS (proves gap exists)
3. **Implement fix**
4. **Run test** - confirm PASSES

## Real Bugs Found

**RegistryInstaller.cpp:594,599** - Null dereference (not a DTT issue, actual SumatraPDF bug)

## Issue Priority & Feasibility

| Issue | Pattern | Complexity | Impact |
|-------|---------|------------|--------|
| 52 | Pre-increment while guard | Low | Medium |
| 53 | For-loop init assignment | Low | Medium |
| 55 | If-else branch assignment | Medium | High |
| 51 | Enum mutual exclusion | High | High |
| 54 | Intentional crash suppress | Low | Low |

## Implementation Order

### Phase 1: Issue 52 - Pre-increment while guard ✅ DONE

**TDD Steps:**
1. [x] Write `examples/752_pre_increment_while_guard_xfail.cpp`
   - `while (++i <= n) { arr[i-1]; }` pattern
   - Multiple variants in same file
2. [x] Run test - confirm FAILS (gap exists)
3. [x] Implement fix in `TraverseWhileStmt`
   - Added `extractPreIncDecGuard` helper
   - Establish `i >= 1` proof in body
4. [x] Run test - confirm PASSES
5. [x] Renamed to `752_pre_increment_while_guard_pass.cpp`

### Phase 2: Issue 53 - For-loop init assignment

**TDD Steps:**
1. [ ] Write `examples/753_for_init_assignment_xfail.cpp`
   - `for (p = val; *p; p++)` pattern
   - Multiple variants in same file
2. [ ] Run test - confirm FAILS (gap exists)
3. [ ] Implement fix in `TraverseForStmt`
   - Track assignment in for-init
   - Propagate non-null from RHS to LHS
4. [ ] Run test - confirm PASSES
5. [ ] Rename to `_pass.cpp`, update CMakeLists.txt

### Phase 3: Issue 51 - Enum mutual exclusion (Complex)

**TDD Steps:**
1. [ ] Write `examples/751_enum_mutual_exclusion_xfail.cpp`
   - `if (x == A) {...} if (x == B) {...}` pattern
   - Multiple variants in same file
2. [ ] Run test - confirm FAILS (gap exists)
3. [ ] Design approach:
   - Option A: Track equality constraints per variable
   - Option B: Path-sensitive analysis (major change)
4. [ ] Implement fix
5. [ ] Run test - confirm PASSES
6. [ ] Rename to `_pass.cpp`, update CMakeLists.txt

### Phase 4: Issue 55 - If-else branch assignment

**TDD Steps:**
1. [ ] Write `examples/755_if_else_branch_assignment_xfail.cpp`
   - `T* p; if (c) p = f1(); else p = f2(); use(p);` pattern
   - Multiple variants in same file
2. [ ] Run test - confirm FAILS (gap exists)
3. [ ] Implement fix:
   - Track assignments in both branches of if-else
   - Mark variable as initialized if assigned in all branches
4. [ ] Run test - confirm PASSES
5. [ ] Rename to `_pass.cpp`, update CMakeLists.txt

### Phase 5: Issue 54 - Intentional crash patterns

**TDD Steps:**
1. [ ] Write `examples/754_intentional_crash_silent.cpp`
   - `CrashMe()` pattern with deliberate null deref
   - Multiple variants in same file
2. [ ] Run test - confirm FAILS (warning emitted)
3. [ ] Implement suppression:
   - Option A: Function name heuristic ("Crash*", "Die*")
   - Option B: `dtt:suppress` annotation
4. [ ] Run test - confirm PASSES (silent)
5. [ ] Update CMakeLists.txt

## Next Action

Start with Issue 52 (pre-increment while guard) - lowest complexity, medium impact.
