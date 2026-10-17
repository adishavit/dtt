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

---

## 4. Library Functions Do Not Write the Program's Globals

**Location:** `src/DTTPlugin.cpp` — `calleeMayWriteGlobals`, `appendIndirectWrites`

**What:** A call forgets what DTT knows of the globals the calling function
names, unless the callee is known not to write them: a function defined in the
translation unit whose body names no non-constant global and calls no function
that may write one. A function in a system header, in `std`, or with a `.dtt`
contract is assumed not to write the program's globals unless it is handed a
callable (a function pointer, lambda or function object).

**Tradeoff:**
- *Eliminates:* Forgetting every global at each `printf`, `memcpy` or
  container call.
- *May mask:* A library function that writes a global through a pointer it
  kept from an earlier call (`setvbuf` buffers, `strtok`'s saved pointer).

**Approved:** Not yet (2026-10-17), pending a decision.

---

## 5. A Callee Does Not Keep a Pointer It Is Passed

**Location:** `src/DTTPlugin.cpp` — `escapedLocals`

**What:** A local whose address (or a reference to it) is stored anywhere but
as a direct call argument is *escaped*: any write through a pointer, and any
call, forgets what DTT knows of it. A local whose address is only passed
directly to calls is not escaped: the call itself accounts for writes through
its arguments, and the callee is assumed not to keep the pointer for later
calls. A reference that a library function returns into an object (an element,
`std::get`) does not make the object escaped: writing an element does not
change the properties DTT tracks of library objects.

**Tradeoff:**
- *Eliminates:* Forgetting `n` at every call after `scanf("%d", &n)`.
- *May mask:* A callee that stores the pointer and a later call that writes
  through it (callback registration with a context pointer).

**Approved:** Not yet (2026-10-17). DTT has always assumed this; recorded here
pending a decision.

---

## 6. A Function's Callers Are the Calls in This File

**Location:** `src/DTTPlugin.cpp` — `computeFunctionGhostStates` (Pass B/C),
`isCallArgNonnull`

**What:** When every call to a function in the translation unit passes a
non-null pointer for a parameter, the parameter is taken as non-null throughout
the body, and a `return nullptr` guarded by a null check of it is unreachable.
An argument counts as non-null when it is `&x`, `new`, a string literal, an
array, a call whose every return is non-null, or a local initialized with one
of these and never changed afterwards.

**Tradeoff:**
- *Eliminates:* "nullability unknown" errors on helpers that every caller in
  the file passes `&local` or `new T`.
- *May mask:* For a function with external linkage, a call from another
  translation unit with null. For any function, a call through a function
  pointer, which is not a call site DTT sees.

**Approved:** Not yet (2026-10-17). DTT has always assumed this; recorded here
pending a decision.
