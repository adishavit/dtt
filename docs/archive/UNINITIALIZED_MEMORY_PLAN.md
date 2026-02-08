# Uninitialized Memory Detection Plan

**Status:** Tier 1 Complete, Tier 2 Partial
**Author:** DTT Team
**Date:** 2026-02-03

## Overview

This document outlines the phased implementation plan for comprehensive uninitialized memory detection in DTT. Reading uninitialized memory is undefined behavior in C++ (CWE-457) and a common source of security vulnerabilities.

## Current State

DTT detects uninitialized memory across several categories:

**Pointers** (commit `7c2dfcf`):
- Comparing uninitialized pointers (`if (p != nullptr)`)
- Dereferencing uninitialized pointers (`*p`)
- Copying uninitialized pointers (`int* q = p`)
- Casting uninitialized pointers (`static_cast<int*>(p)`)

**Scalars and function arguments** (v35):
- Uninitialized scalars passed to functions (`int x; sink(x);`)
- Uninitialized pointers passed by value (`int* p; sink(p);`)
- C varargs (`int x; printf("%d", x);`)
- Variadic template parameter packs (`int x; consume(a, x, c);`)
- Fold expressions with uninitialized values

**POD struct members** (v35):
- Individual member tracking (`Point p; sink(p.x);`)
- Partial struct initialization (`Point p; p.x = 1; sink_point(p);`)
- Nested struct member tracking (`Line l; sink(l.start.x);`)

This plan extends detection to **all** uninitialized memory access.

---

## Tier 1: Low Complexity (Extend Existing Infrastructure) — COMPLETE

**Estimated effort:** ~50-100 lines per gap
**Prerequisites:** None (uses existing `_initialized` field)
**Status:** All items implemented (v35)

### 1.1 Uninitialized Scalars

```cpp
int x;           // Uninitialized
return x;        // ERROR: use of uninitialized variable
```

**Implementation:**
- Extend `_initialized` tracking from pointers to all scalar types
- Set `_initialized=0` for uninitialized local variables in `VisitVarDecl`
- Check in `VisitDeclRefExpr` when variable is read

### 1.2 Uninitialized in Expressions

```cpp
int x;
int y = x + 1;   // ERROR: reading uninitialized x
```

**Implementation:**
- Check operands in `VisitBinaryOperator`
- Check operand in `VisitUnaryOperator` (for `++x`, `-x`, etc.)

### 1.3 Passing Uninitialized to Functions

```cpp
int x;
foo(x);          // ERROR: passing uninitialized value
printf("%d", x); // ERROR: passing uninitialized value
```

**Implementation:**
- Check arguments in `VisitCallExpr`
- Exclude address-of (`&x`) - taking address is OK

### 1.4 Returning Uninitialized Value

```cpp
int foo() {
    int x;
    return x;    // ERROR: returning uninitialized value
}
```

**Implementation:**
- Add `VisitReturnStmt` to check returned expression

### 1.5 Compound Assignment

```cpp
int x;
x += 1;          // ERROR: reading x before writing
```

**Implementation:**
- Compound assignment reads then writes
- Check LHS is initialized for `+=`, `-=`, `*=`, etc.

---

## Tier 2: Medium Complexity (New Tracking Structures) — PARTIAL

**Estimated effort:** ~200-500 lines per gap
**Prerequisites:** Tier 1 complete
**Status:** 2.1 (Struct Members) implemented for POD types (v35). 2.2-2.4 remain open.

### 2.1 Struct Members

```cpp
struct S { int a, b; };
S s;
s.a = 1;
return s.b;      // ERROR: s.b never initialized
```

**Implementation:**
- Track `_initialized` per struct field: `s.a._initialized`, `s.b._initialized`
- Handle designated initializers: `S s = {.a = 1}` initializes only `a`
- Handle aggregate initialization: `S s = {1, 2}` initializes both

### 2.2 Array Elements

```cpp
int arr[10];
arr[0] = 1;
return arr[5];   // ERROR: arr[5] never initialized
```

**Implementation:**
- Conservative approach: track `arr._all_initialized`
- After `memset(arr, 0, sizeof(arr))` → all initialized
- Individual element assignment doesn't prove all initialized
- Alternative: use Value Concepts `forall(i, 0, n, arr[i] is initialized)`

### 2.3 Reference Binding

```cpp
int x;
int& r = x;      // Binding is OK (no read)
int y = r;       // ERROR: reading through reference to uninitialized
```

**Implementation:**
- Track reference targets in alias map
- When reading through reference, check target's init state

### 2.4 Union Members

```cpp
union U { int i; float f; };
U u;
return u.i;      // ERROR: no member initialized
```

**Implementation:**
- Extend existing union active-member tracking
- `_initialized` applies to the union as a whole
- Writing any member initializes the union

---

## Tier 3: High Complexity (Path-Sensitive Analysis)

**Estimated effort:** ~500-1000 lines per gap
**Prerequisites:** Tier 2 complete, may need Scope Exit PR (#11)

### 3.1 Conditional Initialization

```cpp
int x;
if (cond) x = 1;
return x;        // ERROR: x uninitialized when !cond
```

**Implementation:**
- Track init state per branch
- At merge point, `_initialized` = AND of all paths
- Leverage scope exit detection (PR #11)

### 3.2 All-Paths Initialization

```cpp
int x;
if (cond) x = 1;
else x = 2;
return x;        // OK: initialized on all paths
```

**Implementation:**
- If all paths to a use initialize the variable, it's safe
- Requires control flow analysis

### 3.3 Loop Initialization

```cpp
int x;
for (int i = 0; i < n; i++) {
    x = i;
}
return x;        // ERROR: loop may run 0 times
```

**Implementation:**
- Loop body initialization doesn't prove post-loop init
- Unless loop is proven to run at least once

### 3.4 Partial Struct Initialization

```cpp
S s = {.a = 1};  // Only a initialized
use(s.a);        // OK
use(s.b);        // ERROR: b not initialized
```

**Implementation:**
- Parse designated initializer list
- Track which fields are explicitly initialized

---

## Tier 4: Interprocedural (Annotations Required)

**Estimated effort:** ~500-1000 lines per gap
**Prerequisites:** Tier 3 complete, annotation framework

### 4.1 Output Parameters

```cpp
void init(int* out) { *out = 42; }

int x;
init(&x);
return x;        // Should be OK with annotation
```

**Annotation:**
```cpp
[[clang::annotate("dtt:out=arg0")]]
void init(int* out);
```

**Implementation:**
- Parse `dtt:out` annotation
- After call, mark output parameters as initialized

### 4.2 Scanf Family

```cpp
int x;
scanf("%d", &x);
return x;        // ERROR: scanf may fail
```

```cpp
int x;
if (scanf("%d", &x) == 1) {
    return x;    // OK: success checked
}
```

**Implementation:**
- Model scanf return value semantics
- Track conditional initialization based on return check
- Use bounded quantifiers: `ret >= n` implies first n args initialized

### 4.3 Conditional Output

```cpp
bool tryParse(const char* s, int* out);

int x;
if (tryParse(str, &x)) {
    return x;    // OK: function returned true
}
```

**Annotation:**
```cpp
[[clang::annotate("dtt:out=arg1:if_returns=true")]]
bool tryParse(const char* s, int* out);
```

---

## Tier 5: Advanced (Partially Simplified)

**Estimated effort:** Variable - see individual sections
**Prerequisites:** All previous tiers

**Note:** Heap object tracking (5.2) reduced from "Very High" to "Medium" complexity
by reusing the ResourceId pattern from scope-exit tracking.

### 5.1 Aliasing

```cpp
int x;
int* p = &x;
*p = 1;
return x;        // OK: initialized via alias
```

**Implementation:**
- Must-alias analysis
- When writing through pointer, update aliased variable's init state

### 5.2 Heap Objects (ResourceId Approach)

```cpp
int* p = new int;    // Uninitialized
int* q = new int();  // Value-initialized (zero)
return *p;           // ERROR
return *q;           // OK
```

**Implementation using ResourceId pattern (like scope-exit):**

```cpp
// Reuse existing infrastructure
using ResourceId = SourceLocation;  // Allocation site = unique ID

// NEW: Track initialization state per heap allocation
struct HeapObjectState {
    bool initialized;           // Is the pointed-to value initialized?
    SourceLocation allocLoc;    // Where allocated (for diagnostics)
};
llvm::DenseMap<ResourceId, HeapObjectState> HeapObjectStates;

// EXISTING: Already tracks pointer -> resource mapping
llvm::StringMap<ResourceId> VarToResource;
```

**Key insight:** Track initialization per ALLOCATION SITE, not per variable.
This solves aliasing naturally:

```cpp
int* p = new int;      // rid=loc1, HeapObjectStates[loc1]={init:false}
                       // VarToResource["p"] = loc1

int* q = p;            // VarToResource["q"] = loc1 (same resource!)

*q = 42;               // rid = VarToResource["q"] = loc1
                       // HeapObjectStates[loc1].initialized = true

int x = *p;            // rid = VarToResource["p"] = loc1
                       // Check HeapObjectStates[loc1].initialized → true → OK!
```

**Detection points:**

1. **VisitCXXNewExpr**: Set initial state
   ```cpp
   ResourceId rid = NewExpr->getBeginLoc();
   bool isValueInit = NewExpr->hasInitializer() ||
                      NewExpr->getInitializationStyle() != CXXNewExpr::NoInit;
   HeapObjectStates[rid] = {isValueInit, rid};
   VarToResource[resultVar] = rid;
   ```

2. **VisitUnaryOperator (dereference write)**: Mark initialized
   ```cpp
   if (UO->getOpcode() == UO_Deref && isLValueContext(UO)) {
       auto rid = VarToResource[ptrPath];
       if (rid.isValid()) HeapObjectStates[rid].initialized = true;
   }
   ```

3. **VisitUnaryOperator (dereference read)**: Check initialized
   ```cpp
   if (UO->getOpcode() == UO_Deref && !isLValueContext(UO)) {
       auto rid = VarToResource[ptrPath];
       if (rid.isValid() && !HeapObjectStates[rid].initialized) {
           Diag.Report(loc, ErrUninitializedHeapRead) << ptrPath;
       }
   }
   ```

4. **Pointer copy**: Propagate ResourceId (already done!)
   ```cpp
   // EXISTING CODE handles this:
   VarToResource[dstPath] = VarToResource[srcPath];
   ```

**Complexity reduction:** From "Very High" to "Medium" by reusing ResourceId infrastructure

### 5.3 Memory Functions

```cpp
int x;
memset(&x, 0, sizeof(x));
return x;        // OK: memset initialized
```

**Implementation:**
- Model `memset`, `memcpy`, `memmove`
- Track destination as initialized after call

---

## Implementation Strategy

### Phase 1: Quick Wins — COMPLETE (v35)
- ✅ Tier 1 implemented: scalars, expressions, function args, return values
- ✅ Variadic support: C varargs, parameter packs, fold expressions
- ✅ xfail tests for Tier 2-5 document remaining gaps

### Phase 2: Structured Data — PARTIAL (v35)
- ✅ Tier 2.1: POD struct member tracking (individual + nested + partial init)
- Remaining: Array elements (2.2), reference binding (2.3), union members (2.4)
- May require Value Concepts for arrays

### Phase 3: Path Sensitivity
- Implement Tier 3 with scope exit detection
- Integrate with proof system for branch tracking
- ~800 lines of plugin code

### Phase 4: Interprocedural
- Implement Tier 4 annotation framework
- Model common C library functions (scanf, etc.)
- ~600 lines of plugin code + stdlib models

### Phase 5: Advanced Analysis
- Implement Tier 5 as needed for specific CVE patterns
- May require external alias analysis
- Scope TBD

---

## Test Matrix

| Tier | xfail Tests | Pass Tests | Total |
|------|-------------|------------|-------|
| 1    | 710-714     | 710-714    | 10    |
| 2    | 715-719     | 715-719    | 10    |
| 3    | 720-724     | 720-724    | 10    |
| 4    | 203, 211, 843 + new | same | 10    |
| 5    | 725-729     | 725-729    | 10    |

---

## Success Criteria

1. **Tier 1 complete:** All scalar/expression/arg/return cases detected
2. **No false positives:** Initialized variables compile cleanly
3. **No regressions:** All existing tests pass
4. **Clear diagnostics:** Error messages show declaration location

---

## References

- CWE-457: Use of Uninitialized Variable
- CVE-2019-15947 (Bitcoin Core) - Uninitialized variable
- CVE-2020-6819 (Firefox) - Use after free via uninitialized
- CVE-2021-30551 (Chrome V8) - Type confusion via uninitialized
- C++ Standard [dcl.init] - Initialization rules
- CERT C Rule EXP33-C: Do not read uninitialized memory
