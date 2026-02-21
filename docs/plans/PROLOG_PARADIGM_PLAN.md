# Prolog Paradigm Integration Plan

## Honest Assessment

This plan originated from brainstorming "what can Prolog bring to DTT?" The
first draft dressed up existing feature requests in logic programming terminology.
This version separates the real gems from the theater.

### What's theater (relabeling existing work)

- "Guard clauses" = conditional annotations. It's a parser + engine feature.
- "Forward chaining rules" = replacing a switch with a lookup table. That's a refactor.
- "Persistent fact database" = serializing inferred postconditions to disk. Build system feature.
- "State machines as logic" = DSL sugar over ghost fields + enums. Annotation design problem.

These are all worth doing. None of them are Prolog ideas. They're on the
expressibility gaps doc already.

### What's genuinely new (things DTT cannot do today)

Two ideas from logic programming that would give DTT capabilities it
fundamentally lacks:

1. **Backward reasoning** — DTT only reasons forward. It can never answer
   "what's missing?" or "what would make this safe?" This is the difference
   between a type *checker* and a type *assistant*.

2. **Fixpoint-based interprocedural analysis** — DTT already has a fixpoint
   loop (`propagatePreconditionsTransitively`, max 100 iterations), but it
   only propagates buffer preconditions. It doesn't propagate general
   property constraints (size, nullness, resource state) across call chains.

Both are explored in depth in Part II.

---

# PART I: Practical Feature Plan (No Theater)

What each item actually is, what it delivers, no Prolog labels.

## 1. Conditional Annotations

**What it is:** Add `when` guards to `.dtt` postconditions and preconditions.

**Why it matters:** Every function that can fail (fopen, malloc, connect, every
allocator in all 10 external library `.dtt` files) currently gets an inaccurate
unconditional postcondition. This is the #1 expressibility gap from PR #34.

**The work:**
- Parse `when <condition>` after ensures/requires lines (~50 lines of parser)
- Add `std::optional<ConstraintExprPtr> guard` to `Postcondition` struct
- In `applyPostconditions()`: evaluate guard against current proof state, skip if false
- Update `libc.dtt` and all 10 external `.dtt` files

**What it doesn't do:** No new reasoning capability. The constraint evaluator
already exists. This just adds a conditional branch in postcondition application.

## 2. Entailment Refactor

**What it is:** Replace the 36-case nested switch in `ProofContext::implies()`
with a `constexpr` rule table.

**Why it matters:** If we ever add proof operators beyond {GT, GE, LT, LE, EQ, NE}
(e.g., `IN_RANGE` for value range analysis, `BITMASK` for flag tracking), the
current approach requires N^2 new cases. A table grows linearly.

**The work:**
- Move each case to a `{known_op, required_op, lambda}` row (~80 lines replacing ~100)
- Index by `(known_op, required_op)` pair for O(1) lookup
- Add transitive chaining: `y == x` AND `x > 5` implies `y > 5` (bounded worklist, max 3 hops)

**What it doesn't do:** This doesn't make proofs stronger. Z3 already handles
complex constraints. It makes the simple entailment layer easier to extend.

## 3. Fix Suggestions (FixItHint)

**What it is:** When DTT reports a precondition violation, also emit a concrete
code suggestion that would fix it.

**Why it matters:** DTT currently has 0 lines of FixItHint code. Users get
"precondition `_size > 0` not satisfied" and have to figure out the fix themselves.
With FixIt, IDEs show a one-click "add guard" action.

**The work:**
- Pattern table: `{field, op, code_template}` for known precondition shapes
- For `_size > 0`: suggest `if (!v.empty()) { ... }`
- For `_nonnull != 0`: suggest `if (ptr != nullptr) { ... }`
- For `arg0 < _size`: suggest `if (idx < v.size()) { ... }`
- Wire into `FixItHint::CreateInsertion`

**What it doesn't do:** This is a lookup table, not backward search. It handles
the 4-5 most common precondition patterns. It doesn't compute arbitrary fixes.
(Part II explores what real backward reasoning would look like.)

## 4. Provenance Chains

**What it is:** Extend `LossProvenance` (which only tracks *where* tracking was
lost) into a full history of every state change for a tracked field.

**Why it matters:** When DTT says `v._size` is 0, the user needs to know:
was it the default constructor? A `clear()` call? An assignment? Currently
DTT tracks loss events and null provenance, but not the positive derivation.

**The work:**
- `StateProvenance` struct: `{loc, kind, description, previous}` linked list
- Record on every `setFieldTyped()` call
- On diagnostic, walk chain and emit notes:
  ```
  note: v._size = 0 (default constructor at line 5)
  note: v._size = 1 (push_back at line 8)
  note: v._size = 0 (clear at line 12)
  ```

**What it doesn't do:** This is bookkeeping, not reasoning. It records history;
it doesn't use history to derive new facts.

## 5. Cross-Object References

**What it is:** Let `.dtt` annotations reference fields on related objects
(parent-child ownership).

**Why it matters:** 6 of 10 external library `.dtt` files need this. libssh2
channels are only valid while the parent session is connected. libuv handles
must be closed before the loop.

**The work:**
- `@owner` ghost field that stores a `FieldPath` to another tracked object
- Resolve `arg0.@owner.@state` by: lookup `arg0.@owner`, follow to target, lookup `@state`
- Bounded depth (max 2 hops)

## 6. Cross-TU Fact Serialization

**What it is:** `--dtt-emit-facts=FILE` serializes inferred postconditions.
`--dtt-annotations=FILE` loads them. Two-pass builds get cross-TU analysis.

**Why it matters:** DTT's auto-inference is single-TU. The second most common
false positive is "function defined in another TU, DTT doesn't know its effects."

**The work:**
- Serialize `InferredFnPostconditions` map to `.dtt` format
- Source hash for staleness
- CMake helper functions

## 7. Protocol Blocks

**What it is:** Syntactic sugar in `.dtt` for state machine definitions.

**Why it matters:** The zlib, libssh2, and libuv `.dtt` files already encode
multi-state protocols as manual `requires`/`ensures` pairs. This is error-prone.
A `protocol` block makes the state machine explicit and generates the constraints.

**The work:**
- Parser for `protocol { states: ..., transition ... via ... }` blocks
- Compile transitions to existing enum + ghost field constraints
- Protocol-aware error messages ("socket in state CREATED, expected CONNECTED")

---

## Dependencies and Ordering

```
Items 1-4: Independent, can proceed in parallel.
Item 5: Benefits from item 1 (when-guards on cross-object constraints).
Item 6: Benefits from existing .dtt parser infrastructure.
Item 7: Benefits from items 1 (guards for transition conditions).
```

---

# PART II: The Real Gems (What Prolog Actually Brings)

## Gem 1: Backward Reasoning

### What DTT does today (forward only)

DTT walks the AST top-to-bottom. At each statement, it updates field states
and proofs. At each call site, it checks: "Do current proofs satisfy the
preconditions?" If yes: safe. If no: error. Done.

```
             FORWARD DIRECTION
             ────────────────►
    [constructor] → [push_back] → [clear] → [front]
    _size = 0       _size = 1     _size = 0   FAIL: _size > 0 not proven

    DTT says: "precondition _size > 0 not satisfied"
    User asks: "OK, but what should I do?"
    DTT says: nothing
```

### What backward reasoning enables

Start from the failure and work backwards: "What fact is missing? What would
establish it? Where could that establishment happen?"

```
             BACKWARD DIRECTION
             ◄────────────────
    [front] requires _size > 0
         ↑ What could establish _size > 0?
         ├─ push_back() establishes _size = old(_size) + 1  [if old >= 0, then > 0]
         ├─ resize(n) establishes _size = n  [if n > 0, then > 0]
         ├─ constructor(n) establishes _size = n  [if n > 0, then > 0]
         ├─ branch guard `if (!v.empty())` establishes _size > 0
         └─ branch guard `if (v.size() > 0)` establishes _size > 0
```

This is NOT a pattern-matching table (which is what Part I item 3 does).
This is an actual search over the annotation registry:

```cpp
/// Search the annotation registry for operations that could establish a goal.
struct GoalSatisfier {
    StringRef field;            // e.g., "_size"
    ProofOp op;                 // e.g., GT
    int64_t value;              // e.g., 0
    SourceRange applicableRange; // Where to insert the establishment
};

SmallVector<GoalSatisfier, 4> searchBackward(
    StringRef objectType,       // e.g., "vector"
    StringRef field,            // e.g., "_size"
    ProofOp op,                 // e.g., GT
    int64_t value               // e.g., 0
) {
    SmallVector<GoalSatisfier, 4> satisfiers;

    // 1. Search all postconditions in the annotation registry that
    //    set 'field' to a value satisfying (op, value)
    for (const auto &[key, info] : MethodAnnotations) {
        if (key.className != objectType) continue;
        for (const auto &post : info.ensures) {
            if (post.field != field) continue;
            // Does this postcondition establish a value satisfying our goal?
            if (postconditionSatisfies(post, op, value)) {
                satisfiers.push_back({...});
            }
        }
    }

    // 2. Search for branch patterns that add the required proof
    //    e.g., `if (!v.empty())` adds proof _size > 0
    satisfiers.push_back(synthesizeGuardSuggestion(field, op, value));

    return satisfiers;
}
```

### Why this is genuinely different from a lookup table

A lookup table says: "if precondition is `_size > 0`, suggest `if (!v.empty())`."
It's a static mapping from precondition pattern to fix template. It handles
exactly the patterns someone manually enumerated.

Backward search queries the entire annotation registry dynamically. If you add
a new `.dtt` file with a custom type that has a `fill(n)` method with
`ensures _size = n`, the backward search would find it automatically:
"You could call `fill()` with n > 0 before this access."

The search space is:
1. **All postconditions in the registry** where `field` matches and the
   established value satisfies the goal.
2. **All branch patterns** that would add the required proof to the store.
3. **All constructor forms** that establish the field.

This gives DTT something it fundamentally lacks: the ability to *suggest*
operations from the domain of the object, not from a hardcoded list.

### Where this gets really interesting: precondition inference

DTT auto-infers postconditions by watching function bodies. It does NOT
auto-infer preconditions (except for buffer bounds via the fixpoint loop).

Backward reasoning gives you precondition inference for free:

```cpp
void process(std::vector<int>& v) {
    int x = v.front();    // requires: v._size > 0
    v.pop_back();         // requires: v._size > 0
    int y = v[3];         // requires: 3 < v._size, i.e., v._size > 3
}
```

Forward analysis sees these precondition failures. Backward analysis asks:
"What precondition on `process` would make all of these safe?" Answer:
the strongest one — `v._size > 3`. This gets recorded as:

```
fn process(1) {
  requires arg0._size > 3
}
```

Then at call sites:
```cpp
std::vector<int> v(10);
process(v);  // Safe: 10 > 3
```

DTT's existing `propagatePreconditionsTransitively()` does this for buffer
bounds only (it's 145 lines of buffer-specific code at lines 5471-5616).
Backward reasoning would generalize this to ALL tracked properties — size,
nullness, resource state, variant index — without property-specific code.

### The guarantee

Forward-only analysis is **incomplete for diagnostics**: it can say "this is
unsafe" but not "this is the minimal fix." Backward analysis computes the
**weakest precondition** — the most permissive assumption that makes the
code safe. This is a provable guarantee: if the weakest precondition is
satisfied, the code IS safe (given the annotation model).

This is the difference between:
- "error: `_size > 0` not satisfied" (forward: tells you what failed)
- "error: `_size > 0` not satisfied. `process()` requires `_size > 3`
   because of `v[3]` at line 5. Call site passes vector of size 2."
   (backward: tells you the root cause and the precise requirement)

### Concrete implementation sketch

Phase 1 (within item 3): Simple lookup table for the 4-5 most common patterns.
This ships immediately and covers most cases users hit.

Phase 2 (new capability): True backward search over annotation registry.
```cpp
class BackwardSearcher {
    const AnnotationRegistry &Registry;
    const ProofStore &Proofs;

public:
    /// Find the weakest precondition for a function body.
    /// Walks all precondition-consuming call sites, collects requirements,
    /// returns the conjunction of requirements on each parameter.
    SmallVector<InferredPrecondition, 4>
    inferPreconditions(const FunctionDecl *FD);

    /// Given a failed precondition at a call site, search for operations
    /// on the object type that could establish the needed property.
    SmallVector<GoalSatisfier, 4>
    searchEstablishers(StringRef objectType, const Proof &goal);

    /// Find the minimal set of assumptions that would make a given
    /// line safe. Used by --dtt-assume.
    SmallVector<Proof, 4>
    minimalAssumptions(const CallExpr *CE, const ConstraintExpr &pre);
};
```

Phase 3 (generalized fixpoint): Replace `propagatePreconditionsTransitively()`
with a general backward propagation that works on all tracked properties,
not just buffer bounds.

---

## Gem 2: Generalized Fixpoint Propagation

### What DTT does today

DTT has two fixpoint loops, both specialized:

1. **`propagatePreconditionsTransitively()`** (line 5471, 145 lines):
   Propagates buffer size preconditions through call chains. If `memcpy`
   requires `dst` to have `n` bytes, and `wrapper()` calls `memcpy(arg0, src, arg1)`,
   then `wrapper()` gets the precondition `arg0` size >= `arg1`. Iterates to
   fixpoint, max 100 iterations.

2. **`propagateFrameEffectsTransitively()`** (line 5627):
   Propagates field write effects. If `inner()` writes `_size` through its
   parameter, and `outer()` passes its parameter to `inner()`, then `outer()`
   also writes `_size`. Used for frame inference (knowing which fields are
   preserved across calls).

Both are specialized to their domain. Neither propagates general property
constraints (nullness, resource state, variant index, has_value, etc.).

### What general fixpoint enables

Replace both specialized loops with a single generic propagation engine:

```cpp
struct PropertyConstraint {
    unsigned paramIndex;       // Which parameter
    StringRef field;           // Which field (_size, _nonnull, @open, etc.)
    ProofOp op;                // What must hold
    SymValue bound;            // Bound value (may reference other params)
};

/// Propagate ALL property constraints through the call graph to fixpoint.
void propagateConstraintsTransitively() {
    bool changed = true;
    unsigned iterations = 0;

    while (changed && iterations < kMaxFixpointIterations) {
        changed = false;
        ++iterations;

        for (const auto &[calleeDecl, constraints] : FnConstraints) {
            for (const CallSiteInfo &site : getCallSitesOf(calleeDecl)) {
                const FunctionDecl *caller = site.callerFunc;

                for (const auto &constraint : constraints) {
                    // Map callee parameter → caller parameter through call arguments
                    if (auto mapped = mapConstraintToCaller(constraint, site)) {
                        if (addConstraint(caller, *mapped)) {
                            changed = true;
                        }
                    }
                }
            }
        }
    }
}
```

### Concrete example: null propagation through call chains

```cpp
void use_file(FILE *f) {
    fprintf(f, "hello");    // requires: f._nonnull != 0  [from libc.dtt]
}

void process() {
    FILE *f = fopen("x", "r");  // f._nonnull = unknown (can fail)
    use_file(f);                // DTT can't prove f._nonnull != 0
}
```

**Today:** DTT reports error at `use_file(f)` in `process()`. It knows
`use_file` has a precondition (from `libc.dtt` on `fprintf`), but it
discovered this by analyzing `use_file`'s body — it doesn't propagate
this precondition back to `process`.

**Wait — actually it does for buffers.** The `propagatePreconditionsTransitively()`
loop handles this for buffer sizes. But ONLY buffer sizes. Not nullness,
not resource state, not any other property.

**With generalized fixpoint:** The same loop propagates `f._nonnull != 0`
from `fprintf`'s annotation through `use_file` to `process`. The error at
`process()` says:

```
error: FILE* may be null (fopen can fail)
  note: use_file() requires arg0._nonnull != 0
  note: because fprintf() at use_file:2 requires arg0._nonnull != 0
  note: f was returned by fopen() which can return NULL
```

### Why this matters: the transitive chain

The real power is depth > 1:

```cpp
void log_to_file(FILE *f, const char *msg) { fprintf(f, "%s", msg); }
void log_error(FILE *f) { log_to_file(f, "error"); }
void handle_request(FILE *log) { log_error(log); }
void server_main() {
    FILE *log = fopen("server.log", "a");
    handle_request(log);  // 3 levels deep: handle_request → log_error → log_to_file → fprintf
}
```

The `_nonnull` requirement on `fprintf`'s first parameter needs to propagate
through 3 call levels. DTT's current fixpoint loop handles this for buffer
sizes (it iterates until convergence). The generalization handles it for
every tracked property.

### The guarantee

Fixpoint propagation computes the **transitive closure** of preconditions
through the call graph. If a property constraint exists anywhere in a call
chain, it propagates to every caller that passes an unverified value.

Specifically:
- **Soundness:** Every propagated precondition is a real requirement. If
  `inner()` requires `_nonnull`, and `outer()` passes its parameter through,
  then `outer()` really does require `_nonnull`. No false positives from
  propagation itself.
- **Completeness (bounded):** Given max N iterations and max call depth D,
  all chains up to depth min(N, D) are covered. The existing bound of 100
  iterations is more than sufficient for any realistic codebase.

### What it costs

The existing specialized loop is ~145 lines and iterates over buffer-specific
structures. The general version would be similar in size but operate on a
unified `PropertyConstraint` type. The performance cost scales with
`O(iterations * call_sites * constraints_per_function)`, same as today but
with more constraint types in the mix. For realistic codebases (where most
functions have 0-3 constraints), this is negligible.

### Implementation path

1. Define `PropertyConstraint` — unifies `InferredPrecondition` (buffer-specific)
   with general property constraints
2. Refactor `propagatePreconditionsTransitively()` to use `PropertyConstraint`
   instead of `InferredPrecondition` (same behavior, general structure)
3. Feed non-buffer preconditions (from `.dtt` annotations) into the same loop
4. The loop now propagates all properties transitively

Step 2 is a refactor — same test results. Step 3 is the actual capability gain.

---

## What "Prolog" Actually Means Here

Stripped of the jargon:

**Forward reasoning** (what DTT does) = "Given what I know, what can I conclude?"
DTT walks code top-to-bottom and accumulates facts.

**Backward reasoning** (Gem 1) = "Given what I need, what's missing?"
Start from a goal and search for what would satisfy it.

**Fixpoint** (Gem 2) = "Repeat until nothing changes."
DTT already does this for buffers. The generalization extends it to all properties.

Prolog happens to combine all three (forward chaining, backward chaining, and
fixpoint via tabling). That's why the brainstorm started there. But the value
isn't in Prolog the language — it's in these two reasoning directions that DTT
currently lacks (backward) or under-uses (fixpoint for general properties).

---

# Prioritized Roadmap

## Tier 1: Highest value, lowest risk — COMPLETE

| Item | What | Status |
|------|------|--------|
| Conditional annotations | `when` guards in `.dtt` | Done |
| Provenance chains | Full state history on error | Done |
| Simple fix suggestions | Pattern table + FixItHint | Done |
| Generalized fixpoint | Datalog-style transitive propagation of all properties | Done |
| Libc null annotations | Null-check requirements for 30+ C functions | Done |
| Backward search | Query annotation registry for goal satisfiers | Done |

## Tier 2: High value, no blockers (do next)

| Item | What | Depends on | Value |
|------|------|------------|-------|
| Cross-TU serialization | `--dtt-emit-facts` / `--dtt-annotations=` | Nothing (ready now) | Eliminates cross-TU false positives |

## Tier 3: Independent improvements

| Item | What | Depends on | Value |
|------|------|------------|-------|
| ~~Entailment refactor~~ | Rule table replacing 36-case switch — **done** | Nothing | Extensibility (only urgent if adding new ProofOps) |
| Cross-object refs | `@owner` ghost field type | when-guards (done) | Parent-child resource hierarchies |
| Protocol blocks | State machine DSL in `.dtt` | when-guards (done) | Error-proof typestate annotations |

## Tier 4: Exploratory (evaluate later)

| Item | What | Depends on | Value |
|------|------|------------|-------|
| `--dtt-assume` mode | "What assumptions make line N safe?" | Backward search | Interactive safety debugging |
| Spec mining | Auto-discover protocols from code | Cross-TU + protocols | Bootstrap annotations for new codebases |

---

## Dependency Graph (Updated)

```
Tier 1 (all done):
  when-guards ──► cross-object refs (Tier 3)
  when-guards ──► protocol blocks (Tier 3)
  fixpoint    ──► (no dependents)

Tier 2 (one item left):
  backward search (done) ──► --dtt-assume (Tier 4)
  cross-TU serialization ──► spec mining (Tier 4)

Tier 3 (independent):
  entailment refactor (done) ──► (nice-to-have, no dependents)
```

Note: Original tiers were ordered by risk/effort, not dependencies. The
generalized fixpoint was Tier 2 but implemented early because it was the
natural extension of the when-guards work. Backward search was Tier 3 but
has zero blockers — it only needs the annotation registry (exists since v1)
and FixItHint infrastructure (done in Tier 1).

---

## Summary: What's Real

| Claim | Verdict |
|-------|---------|
| "DTT is doing logic programming" | **True** but unhelpful — so is every constraint checker |
| "Guard clauses from Prolog" | **Theater** — it's a conditional branch on postcondition application |
| "Forward chaining rule table" | **Done** — `constexpr` entailment table replacing 36-case switch + deduplication. Fixed NE soundness bugs along the way |
| "Backward reasoning for diagnostics" | **Real gem** — done: searches annotation registry for all operations that could establish a failed precondition |
| "Fixpoint for all properties" | **Real gem** — done: propagates null/resource/size through call chains |
| "Typestate as logic" | **Half-real** — the DSL is useful, the Prolog connection is cosmetic |
| "Spec mining needs Datalog" | **Plausible** — this is the one place a logic engine might earn its keep, but it's far out |
| "No new dependencies" | **True** — everything in Tiers 1-3 is plain C++ |
