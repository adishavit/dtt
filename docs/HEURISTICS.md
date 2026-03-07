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

## 3. [REMOVED] Taint Bounds-Check Attenuation

**Status:** Removed on 2026-03-07 in favor of sound proof-aware approach.

The previous heuristic (#3) blanket-cleared taint for variables mentioned in guard
conditions with early return. This was replaced with sound proof-aware taint suppression:
taint warnings at SIZE sinks (CWE-190, CWE-120) are only suppressed when the proof system
has an actual upper bound proof (`_value<=N` or `_value<N`) from `extractAllGuards`.
No heuristic attenuation — taint persists unless proven safe or explicitly sanitized.
