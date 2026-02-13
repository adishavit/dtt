# Integer Overflow Detection Implementation Guide

This document outlines the implementation approach for adding integer overflow detection to DTT.

## Current State Analysis

### Existing Infrastructure (Excellent - 8/10 Ready)

| Component | Status | Location |
|-----------|--------|----------|
| Symbolic value tracking | **EXISTS** | `src/DTTTypes.h:39-292` |
| Arithmetic operations (ADD, MUL, etc.) | **EXISTS** | `SymValue::Kind` enum |
| Overflow-safe helpers | **EXISTS** | `src/DTTPlugin.cpp:1019-1035` |
| Z3 SMT solver integration | **EXISTS** | `src/DTTPlugin.cpp:4244-4797` |
| Property tracking (`_size`) | **EXISTS** | `TypedEnv` global map |
| Buffer bounds checking | **EXISTS** | `VisitArraySubscriptExpr()` |

### The Gap

**`SymValue::eval()` uses unsafe arithmetic** (DTTTypes.h:162-192):

```cpp
// CURRENT (UNSAFE)
case ADD: return *l + *r;    // Can overflow silently!
case MUL: return *l * *r;    // Can overflow silently!
case SUB: return *l - *r;    // Can underflow!

// Safe helpers exist but ARE NOT USED:
safeAdd(a, b)  // Returns nullopt on overflow
safeMul(a, b)  // Returns nullopt on overflow
safeSub(a, b)  // Returns nullopt on underflow
```

## Implementation Plan

### Phase 1: Make eval() Overflow-Aware (Low Effort)

**File:** `src/DTTTypes.h` lines 162-192

```cpp
// Change from:
case ADD: if (left && right) {
    auto l = left->eval(), r = right->eval();
    if (l && r) return *l + *r;  // UNSAFE
} return std::nullopt;

// To (using safe helpers - need to make available in header):
case ADD: if (left && right) {
    auto l = left->eval(), r = right->eval();
    if (l && r) return safeAdd(*l, *r);  // Returns nullopt on overflow
} return std::nullopt;
```

**Impact:** When overflow occurs, `eval()` returns `nullopt` instead of wrapped value. This means:
- Bounds checks become conservative (can't prove safe → error)
- Z3 fallback used for symbolic analysis

### Phase 2: Add Overflow Tracking Field (Medium Effort)

Extend `SymValue` to track overflow risk:

```cpp
struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, ... };
    int64_t constVal;
    std::shared_ptr<SymValue> left, right;
    bool overflowPossible = false;  // NEW: Track if overflow might occur

    // New method for overflow-checked evaluation
    std::pair<std::optional<int64_t>, bool> evalWithOverflowCheck() const;
};
```

### Phase 3: Z3 Overflow Constraints (Medium Effort)

Add overflow constraints to Z3 solver context:

```cpp
// In symValueToZ3() at DTTPlugin.cpp:4263
z3::expr symValueToZ3WithOverflow(const SymValue& sv, Z3SolverContext& ctx) {
    if (sv.kind == SymValue::ADD) {
        z3::expr left = symValueToZ3(sv.left, ...);
        z3::expr right = symValueToZ3(sv.right, ...);
        z3::expr result = left + right;

        // Add overflow constraint
        z3::expr noOverflow =
            (result >= INT64_MIN) && (result <= INT64_MAX);
        ctx.solver.add(noOverflow);  // Prove no overflow

        return result;
    }
    // ... similar for MUL, SUB
}
```

### Phase 4: Visitor Checks (Medium Effort)

Add visitor for detecting overflow at allocation sites:

```cpp
// In DTTVisitor class
bool checkAllocationOverflow(const CallExpr* CE) {
    // For malloc(count * size):
    SymValue sizeArg = exprToSymValueImpl(CE->getArg(0));

    if (sizeArg.kind == SymValue::MUL) {
        // Check if multiplication can overflow
        auto [result, overflowed] = sizeArg.evalWithOverflowCheck();
        if (overflowed) {
            Diag(CE->getBeginLoc(), diag::err_integer_overflow_in_allocation)
                << sizeArg.str();
            return false;
        }
    }
    return true;
}
```

### Phase 5: Diagnostic Messages (Low Effort)

Add new diagnostics:

```cpp
// Diagnostic IDs to add
DiagnosticIDs::getCustomDiagID(DiagnosticsEngine::Error,
    "potential integer overflow in size calculation: %0");

DiagnosticIDs::getCustomDiagID(DiagnosticsEngine::Warning,
    "size expression may overflow on large inputs: %0");
```

## CVE Patterns to Catch

After implementation, DTT should catch these patterns:

```cpp
// CVE-2023-4863 (libwebp) - Multiplication overflow
size_t total = width * height * 4;  // ERROR: can overflow
uint8_t* buf = malloc(total);

// CVE-2021-3156 (sudo) - Addition overflow
size_t alloc = len1 + len2 + 1;  // ERROR: can overflow
char* buf = malloc(alloc);

// CVE-2014-0160 (Heartbleed) - Unchecked length
size_t total = header_len + payload_len;  // ERROR: can wrap
memcpy(dst, src, total);
```

## Test Files to Convert from xfail to fail

Once implemented, these should start catching errors:

| Test File | Variants | Expected Errors |
|-----------|----------|-----------------|
| `703_integer_overflow_size_xfail.cpp` | 15 | Multiplication, addition overflow |
| `851_integer_overflow_size_xfail.cpp` | 30+ | All integer overflow patterns |

## Estimated Effort

| Phase | Effort | Lines Changed |
|-------|--------|---------------|
| Phase 1: Safe eval() | 1-2 hours | ~20 lines |
| Phase 2: Overflow field | 2-4 hours | ~50 lines |
| Phase 3: Z3 constraints | 4-8 hours | ~100 lines |
| Phase 4: Visitor checks | 4-8 hours | ~150 lines |
| Phase 5: Diagnostics | 1-2 hours | ~20 lines |
| **Total** | **12-24 hours** | **~340 lines** |

## Quick Win: Phase 1 Only

Even just Phase 1 (using safe helpers in eval()) would provide significant value:
- Overflowing expressions evaluate to `nullopt`
- Buffer bounds checks become conservative
- False negatives become "can't prove safe" warnings
- No new diagnostics needed, existing bounds checks trigger

**Recommendation:** Start with Phase 1 as proof-of-concept, then iterate.

## Architecture Diagram

```
                    ┌─────────────────────┐
                    │    Source Code      │
                    │  size = a * b       │
                    └──────────┬──────────┘
                               │ Parse
                               ▼
                    ┌─────────────────────┐
                    │  SymValue::Mul      │
                    │  (left=a, right=b)  │
                    └──────────┬──────────┘
                               │
          ┌────────────────────┼────────────────────┐
          │                    │                    │
          ▼                    ▼                    ▼
   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
   │  eval()      │    │  Z3 Solver   │    │  Visitor     │
   │  safeMul()   │    │  Overflow    │    │  Allocation  │
   │  → nullopt   │    │  Constraints │    │  Check       │
   └──────────────┘    └──────────────┘    └──────────────┘
          │                    │                    │
          └────────────────────┼────────────────────┘
                               ▼
                    ┌─────────────────────┐
                    │  Diagnostic Output  │
                    │  "overflow possible"│
                    └─────────────────────┘
```

## Related Files

- `src/DTTTypes.h` - SymValue definition
- `src/DTTPlugin.cpp:1019-1035` - Safe arithmetic helpers
- `src/DTTPlugin.cpp:4007-4106` - Expression to SymValue conversion
- `src/DTTPlugin.cpp:4244-4797` - Z3 constraint solving
- `examples/703_integer_overflow_size_xfail.cpp` - Test cases
- `examples/851_integer_overflow_size_xfail.cpp` - More test cases
