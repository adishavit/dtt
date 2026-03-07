# DTT Heuristics Registry

All heuristics that assume intent or suppress diagnostics based on convention
rather than proof. Each must be explicitly approved before implementation.

---

## 1. Out-Parameter Initialization

**Location:** `src/DTTPlugin.cpp` — `VisitCallExpr`, after argument invalidation loop

**What:** When `&local_var` is passed to a non-const `T*` parameter, mark the local
variable as initialized after the call.

**Tradeoff:**
- *Eliminates:* False positives on the standard C/C++ out-parameter idiom
  (`decode(input, &code, &extra_bits)`)
- *May mask:* True uninitialized reads if the callee doesn't actually write through
  the pointer (rare for this calling convention)

**Scope:** Local variables only (`hasLocalStorage`), scalars and structs.

**Approved:** Sprint 2, 2025-02-22

---

## 2. Signed Array Index Non-Negativity

**Location:** `src/DTTPlugin.cpp` — array bounds check Z3 fallback path

**What:** When Z3 finds a counterexample for `idx >= 0 && idx < size` but confirms
`idx < size` (upper bound only), suppress the diagnostic. The assumption: signed
integers used as array indices are non-negative by convention.

**Tradeoff:**
- *Eliminates:* False positives on `if (x < N) return arr[x]` where `x` is a signed
  parameter or function return value (distance tables, strlen results)
- *May mask:* True negative-index bugs where a signed variable happens to have a
  valid upper bound but is actually negative

**Scope:** Signed integer indices only. Unsigned indices and indices without any
upper-bound proof are unaffected.

**Approved:** Sprint 3, 2025-02-23

---

## 3. Taint Bounds-Check Attenuation (SIZE Sinks Only)

**Location:** `src/DTTPlugin.cpp` — generic guard fallback in `TraverseIfStmt`, sink checker in `handleTaintInCall`

**What:** When a tainted variable appears in a comparison condition with early return
(the "generic fallback" path, for conditions too complex for `extractAllGuards`), its
taint status is changed from `Active` to `BoundsChecked` instead of being cleared entirely.
At sink check time, `BoundsChecked` taint suppresses warnings only for SIZE sinks
(CWE-190 integer overflow in allocation, CWE-120 buffer overflow via copy length).
CONTENT sinks (CWE-78 command injection, CWE-22 path traversal, CWE-134 format string,
CWE-426 library injection) always warn regardless of bounds-check status.

Additionally, for tainted variables that go through the `extractAllGuards` path (simple
`var > N` comparisons), the sink checker queries the proof system for `_value` upper
bound proofs. If an upper bound exists, SIZE sinks are suppressed.

**Tradeoff:**
- *Eliminates:* False positives on Heartbleed-class patterns where bounds-checked tainted
  values are used in allocation/copy operations (the bounds check makes these safe)
- *May mask:* True SIZE sink bugs where the bounds check exists but is insufficient
  (e.g., `if (len > 0) return` checks positivity but not upper bound). However, this is
  strictly better than the previous behavior which cleared ALL taint for ALL sinks.

**Replaces:** The previous unsound behavior that blanket-cleared taint for ALL tainted
variables mentioned in ANY guard condition, regardless of sink type. That approach had
false negatives for command injection, path traversal, and format string sinks.

**CWE Classification:**
- SIZE sinks (bounds check sufficient): CWE-190, CWE-120
- CONTENT sinks (bounds check insufficient): CWE-78, CWE-22, CWE-134, CWE-426

**Approved:** 2026-03-07 (user requested "Make it SOTA")
