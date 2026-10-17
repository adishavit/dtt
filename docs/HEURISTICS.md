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

**Approved:** Sprint 2, 2026-02-22

---

## 2. Signed Array Index Non-Negativity

**Location:** `src/DTTPlugin.cpp` — array bounds check Z3 fallback path

**What:** When Z3 cannot prove `idx >= 0 && idx < size` (a counterexample exists or
the solver gives up) but proves `idx < size` (upper bound only), suppress the
diagnostic. The assumption: signed integers used as array indices are
non-negative by convention. An index the known facts force out of bounds (for
example `if (x < 0) return a[x];`) is still reported.

**Tradeoff:**
- *Eliminates:* False positives on `if (x < N) return arr[x]` where `x` is a signed
  parameter or function return value (distance tables, strlen results)
- *May mask:* True negative-index bugs where a signed variable happens to have a
  valid upper bound but is actually negative

**Scope:** Signed integer indices only. Unsigned indices and indices without any
upper-bound proof are unaffected.

**Approved:** Sprint 3, 2026-02-23

---

## 3. Signed Arithmetic Does Not Overflow

**Location:** `src/DTTPlugin.cpp` — `reducesIntoType` / `machineValue` (MACHINE INTEGERS)

**What:** Signed `+`, `-` and `*` in `int` or wider are taken at their
mathematical value: DTT assumes they do not overflow, which C and C++ leave
undefined. Unsigned arithmetic, shifts, integer conversions and `++`/`--` on
types narrower than `int` are reduced into their type (wraparound modeled).

**Tradeoff:**
- *Eliminates:* "Cannot verify" on every signed loop counter and index
  expression without an upper bound (`i + 1`, `++i` for `int i`).
- *May mask:* An out-of-bounds access that follows a signed overflow
  (`int len = a + b;` overflowing to a negative size).

**Scope:** Signed types `int` and wider; `+`, `-`, `*`, unary `-`.

**Approved:** Not yet. This was DTT's behavior before machine integers were
modeled (2026-10-17); it is recorded here pending a decision.
