# Prolog Paradigm Integration Plan

## Executive Summary

DTT's core reasoning — entailment checking, constraint evaluation, auto-inference —
is logic programming in disguise, implemented imperatively in C++. This plan
introduces Prolog-inspired ideas **gradually**, each phase delivering standalone
value. The Clang AST visitor stays. The C++ plugin stays. We add declarative
thinking where it reduces complexity and unlocks new capabilities.

**Non-goal:** Embedding a Prolog runtime or rewriting the engine.
**Goal:** Adopt the *paradigm* — conditional rules, backward chaining, relational
reasoning — incrementally, in idiomatic C++ that fits the existing architecture.

### Relationship to PR #34 (.dtt Annotation Files)

PR #34 already delivers externalized declarative annotations — the `.dtt` file
format with traits, enums, type/fn/template blocks, and a 1000-line parser.
This plan treats PR #34 as **foundation** and builds on top of it. The phases
below address the **expressibility gaps** that PR #34 documented but couldn't
solve with facts alone, because they require *rules* — conditional logic that
goes beyond simple pre/postcondition declarations.

---

## Current State (Post PR #34)

### What We Have

| Component | Status | Format |
|-----------|--------|--------|
| STL annotations | 96 method annotations | `std.dtt` (traits + types) |
| C lib annotations | 52 function annotations | `libc.dtt` (fn blocks) |
| External library annotations | 10 libraries | `annotations/external/*.dtt` |
| Named enumerations | Working | `enum` blocks in `.dtt` |
| Trait inheritance | Working | `type X : Trait1, Trait2 { }` |
| Template param aliases | Working | `template` blocks |
| Parser | ~1000 LOC | `DTTAnnotationFile.cpp` |

### What We're Missing (PR #34's Expressibility Gaps)

| Gap | Impact | Why Facts Aren't Enough |
|-----|--------|-------------------------|
| **Conditional postconditions** | HIGH | `fopen` returns null on failure, non-null on success — need branching logic |
| **Cross-object constraints** | MEDIUM | Parent-child ownership: `arg0.@parent.@state != 0` — need relational reasoning |
| **Conditional preconditions** | MEDIUM | "if arg0 is type X, then require Y" — need guards on rules |
| **Ownership transfer** | MEDIUM | `consumes arg0` — need effect beyond field assignment |
| **Indirect out-parameters** | MEDIUM | `*arg1 = result` — need pointer indirection in postconditions |
| **No fix suggestions** | HIGH | 0 lines of FixIt code — users get "unsafe" with no guidance |
| **No provenance** | MEDIUM | "Why does DTT think `_size` is 0?" — no explanation chain |
| **Brittle entailment** | LOW | 36 hand-coded implication rules in a 6x6 switch matrix |

**Key insight:** Every gap above is a place where DTT needs *rules* (conditional
logic with guards, implications, search) rather than *facts* (flat declarations).
This is exactly what Prolog brings: rules over facts.

---

## Technology Strategy: No New Dependencies

Each phase is implemented in **plain C++ within the existing plugin**. We adopt
the *paradigm*, not the *runtime*:

- **Conditional rules** → `if` guards in `.dtt` expressions, evaluated by existing constraint engine
- **Backward chaining** → Goal-directed search functions in C++
- **Relational reasoning** → Extended `FieldPath` to support cross-object references
- **Rule tables** → `constexpr` arrays replacing switch matrices

If a future phase proves that a real logic engine would pay for itself (Phase 7),
it can be evaluated then with concrete evidence from Phases 1-6.

---

## Phase 1: Conditional Rules in .dtt Format

**Goal:** Close the #1 expressibility gap — conditional postconditions.
**Value:** Enables accurate annotation of every function that can fail (fopen,
malloc, connect, etc.). Currently these need hardcoded C++ fallbacks.
**Depends on:** PR #34 merged.

### 1.1 The Problem

Every allocator, every syscall, every I/O function can fail. The current `.dtt`
format can only say "the result has property X" — it can't say "the result has
property X *if* it succeeds, and property Y *if* it fails":

```
# Current: always sets @open = 1, even when fopen returns NULL
fn fopen(2) {
  ensures result.@open = 1
  scope_exit result.@open == 0
}

# Reality: @open = 1 only if result != NULL
```

This is the single most impactful gap. It affects ALL 10 external library
annotation files and every resource-acquiring function in `libc.dtt`.

### 1.2 Proposed Syntax: Guard Clauses

Add Prolog-style guard clauses with `when`:

```
fn fopen(2) {
  ensures result.@open = FileIO.OPEN         when result._nonnull != 0
  ensures result.@open = FileIO.CLOSED        when result._nonnull == 0
  scope_exit result.@open == FileIO.CLOSED    when result._nonnull != 0
}

fn malloc(1) {
  ensures result.@allocated = Alloc.ALLOCATED when result._nonnull != 0
  scope_exit result.@allocated == Alloc.FREED when result._nonnull != 0
}
```

The `when` clause is a Prolog guard: the postcondition only fires if the
guard condition holds at the point where the fact is consumed (not where
it's produced). This is *conditional forward chaining*.

### 1.3 Conditional Preconditions

The same `when` syntax works for preconditions:

```
fn SSL_write(3) {
  requires arg0.@ssl_state == SSLState.CONNECTED
  requires arg2 > 0 when arg1._nonnull != 0    # Only check length if buffer non-null
}
```

This addresses the "conditional preconditions" gap.

### 1.4 Implementation

The parser already builds `ConstraintExpr` trees. Add a `guard` field:

```cpp
struct Postcondition {
    SmallString<32> field;
    SymValue expr;
    bool usesOld;
    std::optional<ConstraintExprPtr> guard;  // NEW: when-clause
};
```

At application time in `applyPostconditions()`:

```cpp
for (const auto &post : annotations.ensures) {
    if (post.guard) {
        // Evaluate guard against current proof state
        if (!checkConstraintExprSimple(*post.guard, ...)) {
            continue;  // Guard not satisfied, skip this postcondition
        }
    }
    // Apply postcondition as before
    setFieldTyped(targetPath, post.field, post.expr);
}
```

The guard reuses the **existing** constraint evaluation engine — no new
proof machinery needed.

### 1.5 Deliverables

- [ ] `when` clause parsing in `DTTAnnotationFile.cpp`
- [ ] `guard` field on `Postcondition` and precondition structs
- [ ] Guard evaluation in `applyPostconditions()` and `checkPreconditions()`
- [ ] Update `libc.dtt`: conditional annotations for fopen, malloc, calloc, etc.
- [ ] Update external `.dtt` files that need conditional postconditions
- [ ] Tests: guarded postconditions (pass and fail variants)

---

## Phase 2: Rule-Based Entailment

**Goal:** Replace the hand-coded 6×6 implication matrix with a declarative rule table.
**Value:** Easier to extend with new proof operators. Enables transitive reasoning.
**Depends on:** Nothing (can proceed in parallel with Phase 1).

### 2.1 Current Pain

`ProofContext::implies()` is a nested switch with 36 cases:
```cpp
case ProofOp::GT:
    switch (req.op) {
        case ProofOp::GT:  return ev >= rv;      // x>5 implies x>3
        case ProofOp::GE:  return ev >= rv;      // x>5 implies x>=3
        case ProofOp::NE:  return true;          // x>5 implies x!=5
        case ProofOp::LT:  return false;         // x>5 never implies x<anything
        ...
    }
```

Adding a new proof operator (e.g., `IN_RANGE`, `DIVISIBLE_BY`) requires touching
every case. The matrix grows quadratically.

### 2.2 Entailment as a Rule Table

Replace the switch matrix with a data-driven `constexpr` array:

```cpp
struct EntailmentRule {
    ProofOp known_op;
    ProofOp required_op;
    bool (*check)(int64_t known_val, int64_t req_val);
};

static constexpr EntailmentRule Rules[] = {
    {GT, GT, [](int64_t kv, int64_t rv) { return kv >= rv; }},
    {GT, GE, [](int64_t kv, int64_t rv) { return kv >= rv; }},
    {GT, NE, [](int64_t, int64_t) { return true; }},
    {GT, LT, [](int64_t, int64_t) { return false; }},
    {EQ, GT, [](int64_t kv, int64_t rv) { return kv > rv; }},
    {EQ, GE, [](int64_t kv, int64_t rv) { return kv >= rv; }},
    {EQ, EQ, [](int64_t kv, int64_t rv) { return kv == rv; }},
    {EQ, NE, [](int64_t kv, int64_t rv) { return kv != rv; }},
    // ... all 36 rules as data, not code
};
```

Each row is a Horn clause. The engine looks up `(known_op, required_op)` and
evaluates — this is forward-chaining resolution in a `constexpr` table.

### 2.3 Compound Entailment (Chained Rules)

The rule table enables **transitive entailment**:

```
Known: x > 5, y == x
Required: y > 3

Current DTT: Cannot prove (different base variables, no chaining)
With rules:  y == x ∧ x > 5  →  y > 5  →  y > 3  ✓
```

Implement as a bounded worklist:
```cpp
bool canProveTransitive(FieldPath base, Proof requirement, unsigned maxDepth = 3);
```

### 2.4 Deliverables

- [ ] `EntailmentRule` table replacing `implies()` switch matrix
- [ ] Indexed lookup by `(known_op, required_op)` pair
- [ ] Transitive entailment with bounded depth
- [ ] Tests: entailment edge cases, transitive chains
- [ ] Benchmark: zero regression in constraint checking speed

---

## Phase 3: Backward Chaining for Diagnostics

**Goal:** When DTT reports "unsafe," also explain *why* and suggest *how to fix it*.
**Value:** Dramatically better developer experience. The #1 user complaint is
"DTT tells me something is wrong but doesn't tell me what to do about it."
**Depends on:** None (can start immediately).

### 3.1 Provenance Tracking (The "Why" Chain)

DTT already has `LossProvenance` for tracking where information was lost.
Extend to track the *full derivation* of every field state change:

```cpp
struct StateProvenance {
    SourceLocation loc;
    enum Kind { Constructor, Assignment, FunctionCall, BranchGuard, Move, ScopeExit };
    Kind kind;
    SmallString<64> description;
    StateProvenance *previous;  // Linked list back to origin
};
```

When reporting a failure, walk the chain:
```
error: [F] precondition '_size > 0' not satisfied for 'front()'
  note: v._size = 0 (default constructor at line 5)
  note: no size-modifying operations on v between lines 5-13
  note: v.front() at line 13 requires _size > 0
```

### 3.2 Fix Suggestion Engine (Backward Chaining)

Backward chaining asks: "What would satisfy this precondition?" and searches
backwards through available operations:

```cpp
struct FixSuggestion {
    SourceLocation insertLoc;
    SmallString<128> code;
    SmallString<256> explanation;
};

/// Data-driven fix strategies
struct FixStrategy {
    StringRef fieldName;        // Which field the precondition targets
    ProofOp requiredOp;         // What comparison
    StringRef codeTemplate;     // Suggestion template
    StringRef explanation;      // Human-readable description
};

static const FixStrategy Strategies[] = {
    {"_size",      ProofOp::GT, "if (!${var}.empty()) { ",  "guard with emptiness check"},
    {"_nonnull",   ProofOp::NE, "if (${var} != nullptr) { ", "guard with null check"},
    {"_has_value", ProofOp::NE, "if (${var}.has_value()) { ", "guard with value check"},
    {"_size",      ProofOp::GT, "if (${idx} < ${var}.size()) { ", "guard with bounds check"},
};
```

This is Prolog's backward chaining expressed as a pattern-matching table.
The engine tries each strategy, checks if the pattern matches the failed
precondition, and emits a `FixItHint` if it does.

### 3.3 FixItHint Integration

Wire suggestions into Clang's structured fix mechanism:
```cpp
auto hint = FixItHint::CreateInsertion(CE->getBeginLoc(), suggestedCode);
Diag << hint;
```

IDEs (clangd, CLion, VS Code) surface these as "quick fix" actions.

### 3.4 Deliverables

- [ ] `StateProvenance` tracking integrated into `FieldState`
- [ ] Provenance chain emitted as diagnostic notes
- [ ] `suggestFixes()` with data-driven strategies
- [ ] `FixItHint` integration for: null checks, emptiness checks, bounds checks, has_value
- [ ] Tests: each strategy verified in `*_fail.cpp` files
- [ ] Documentation: "Understanding DTT Diagnostics"

---

## Phase 4: Cross-Object Relational Constraints

**Goal:** Close the "cross-object constraints" expressibility gap.
**Value:** Enable annotation of parent-child resource hierarchies (6 of 10
external libraries need this).
**Depends on:** Phase 1 (guard clauses).

### 4.1 The Problem

Many real APIs have ownership hierarchies:

```cpp
// libssh2: session owns channel
LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);
// ch is only valid while session.@state == CONNECTED
libssh2_channel_write(ch, data, len);  // requires ch.@parent_session.@state == CONNECTED
```

Current `.dtt` format can only reference fields on the object itself or on
arguments. It can't express "the parent of this object must be in state X."

### 4.2 Proposed: Ownership Declarations

```
fn libssh2_channel_open_session(1) {
  ensures result.@channel_state = SSHChannel.OPEN
  ensures result.@owner = arg0                          # NEW: ownership link
  requires arg0.@session_state == SSHSession.CONNECTED
  scope_exit result.@channel_state == SSHChannel.CLOSED
}

fn libssh2_channel_write(3) {
  requires arg0.@channel_state == SSHChannel.OPEN
  requires arg0.@owner.@session_state == SSHSession.CONNECTED  # Cross-object
}
```

### 4.3 Implementation

The `@owner` field stores a `FieldPath` reference to another tracked object.
Cross-object constraints like `arg0.@owner.@session_state` are resolved by:

1. Look up `arg0.@owner` → get the `FieldPath` of the owning object
2. Look up `owner_path.@session_state` → get the field state
3. Check the constraint against that state

This is **relational reasoning** — following links between objects. In Prolog
terms, it's resolving a chain of predicates: `owns(Channel, Session), state(Session, connected)`.

### 4.4 Deliverables

- [ ] `@owner` field type that stores `FieldPath` references
- [ ] Cross-object field path resolution in constraint checker
- [ ] Parser support for `arg0.@owner.@field` syntax
- [ ] Tests: parent-child resource hierarchies
- [ ] Update `libssh2.dtt`, `libdbus.dtt` with ownership annotations

---

## Phase 5: Query Interface ("Why?" Mode)

**Goal:** Let developers interactively interrogate DTT's analysis.
**Value:** Debugging tool for understanding and resolving DTT diagnostics.
**Depends on:** Phase 3 (provenance tracking).

### 5.1 Query Flags

```bash
# "Why is line 42 flagged?"
clang++ -fplugin-arg-dtt---dtt-query=42 ...

# "What does DTT know about variable v at line 30?"
clang++ -fplugin-arg-dtt---dtt-inspect=v:30 ...

# "What assumptions would make line 42 safe?"
clang++ -fplugin-arg-dtt---dtt-assume=42 ...
```

### 5.2 Implementation

Each mode reuses the normal analysis with modified output:

- **`--dtt-query=LINE`**: Full provenance chain + fix suggestions for the
  diagnostic at LINE. Suppresses other diagnostics for focus.

- **`--dtt-inspect=VAR:LINE`**: Dump all known facts about VAR at LINE:
  field states, proofs, tracking state, provenance. Answers "why doesn't
  DTT know X here?"

- **`--dtt-assume=LINE`**: Backward chaining to find the minimal set of
  assumptions that would make LINE safe:
  ```
  note: line 42 would be safe if: v._size > 0
  note: could be established by: guard `if (!v.empty())` at line 40
  ```

### 5.3 Deliverables

- [ ] `--dtt-query`, `--dtt-inspect`, `--dtt-assume` flags
- [ ] Structured output for each mode
- [ ] Tests for each query mode
- [ ] Documentation: "Debugging with DTT Query Mode"

---

## Phase 6: Cross-TU Fact Persistence

**Goal:** Share inferred postconditions across translation units.
**Value:** Whole-program analysis without a link-time plugin.
**Depends on:** PR #34 (`.dtt` format for serialization).

### 6.1 The Problem

DTT's auto-inference discovers postconditions from function bodies. But in
multi-TU projects, callers in other TUs don't benefit:

```cpp
// utils.cpp — DTT infers: ensures buf._size = n
void resize_buffer(Buffer& buf, int n) { buf.data.resize(n); }

// main.cpp — DTT has no idea what resize_buffer does
resize_buffer(buf, 100);
buf.data[50];  // WARNING: can't prove 50 < _size (false positive)
```

### 6.2 Emit/Consume Cycle

**Emit** (per-TU):
```bash
clang++ -fplugin-arg-dtt---dtt-emit-facts=utils.dtt.inferred -fsyntax-only utils.cpp
```

Generates `.dtt.inferred` in the existing annotation format:
```
# Auto-inferred by DTT — do not edit
# Source: utils.cpp (hash: a3f8c2d)
fn resize_buffer(2) {
  ensures arg0._size = arg1
}
```

**Consume** (dependent TU):
```bash
clang++ -fplugin-arg-dtt---dtt-annotations=utils.dtt.inferred main.cpp
```

The existing parser loads it — inferred facts are just annotations.

### 6.3 Build System Integration

```cmake
# CMake helper
dtt_analyze(TARGET mylib SOURCES utils.cpp helpers.cpp)
dtt_check(TARGET myapp SOURCES main.cpp DEPENDS mylib)
```

```makefile
# Make rule
%.dtt.inferred: %.cpp
    $(CXX) -fplugin=$(DTT) -fplugin-arg-dtt---dtt-emit-facts=$@ -fsyntax-only $<
```

### 6.4 Incremental Caching

Inferred fact files include a source hash. Stale facts trigger re-analysis.
Teams can commit `.dtt.inferred` files for header-only libraries.

### 6.5 Deliverables

- [ ] `--dtt-emit-facts=FILE` flag
- [ ] Serialization of `InferredFnPostconditions` to `.dtt` format
- [ ] Source hash for staleness detection
- [ ] CMake helper functions
- [ ] Tests: multi-file scenario with inferred facts
- [ ] Documentation: "Cross-TU Analysis"

---

## Phase 7: Typestate Protocols

**Goal:** Generalize resource tracking from acquire/release pairs to arbitrary
state machines.
**Value:** Verify complex API protocols (database connections, iterators,
builders, network sockets) at compile time.
**Depends on:** Phase 1 (guard clauses — needed for transition guards).

### 7.1 Motivation

PR #34's external annotation files already describe multi-state resources
(zlib streams, SSH sessions, USB handles) using ghost fields and enums.
But the annotations are manual — every valid transition must be hand-written
as individual `requires`/`ensures` pairs. A `protocol` block would make
the state machine *explicit* and let the engine generate the constraints.

### 7.2 Protocol Syntax

```
protocol TCPSocket {
  states:   CREATED, BOUND, LISTENING, CONNECTED, CLOSED
  initial:  CREATED
  final:    CLOSED
  field:    @tcp_state

  CREATED    -> BOUND       via bind()
  BOUND      -> LISTENING   via listen()
  LISTENING  -> CONNECTED   via accept()
  CONNECTED  -> CONNECTED   via send(data)
  CONNECTED  -> CONNECTED   via recv(buf)
  *          -> CLOSED      via close()
}
```

### 7.3 Compilation to Existing Mechanisms

The protocol block compiles down to what `.dtt` already supports:

```
# Auto-generated from protocol TCPSocket:
enum TCPSocket { CREATED=0, BOUND=1, LISTENING=2, CONNECTED=3, CLOSED=4 }

fn bind(1) {
  requires arg0.@tcp_state == TCPSocket.CREATED
  ensures arg0.@tcp_state = TCPSocket.BOUND
}

fn listen(1) {
  requires arg0.@tcp_state == TCPSocket.BOUND
  ensures arg0.@tcp_state = TCPSocket.LISTENING
}

fn close(1) {
  ensures arg0.@tcp_state = TCPSocket.CLOSED
}

# Scope-exit: must reach final state
# (attached to the function that creates the resource)
```

No new proof machinery — the existing `@field` + `enum` mechanism does the work.
The protocol block is **syntactic sugar** that prevents hand-authoring errors in
complex state machines.

### 7.4 Error Messages from Protocols

When a typestate violation occurs, the diagnostic references the protocol:

```
error: [F] TCPSocket protocol violation: send() requires state CONNECTED,
       but socket is in state CREATED
  note: socket created at line 5 (state: CREATED)
  note: no transition to CONNECTED on any path to line 10
  note: valid transitions from CREATED: bind()
```

### 7.5 Built-in Protocols

Ship protocols for common patterns, replacing manual annotations:
- `FILE*` lifecycle (upgrade from acquire/release to full typestate)
- POSIX sockets
- `std::mutex` / `lock_guard`
- zlib streams (currently hand-annotated in `zlib.dtt`)

### 7.6 Deliverables

- [ ] `protocol` block parser in `.dtt` format
- [ ] Compilation from protocol → enum + fn constraints
- [ ] Protocol-aware error messages
- [ ] Built-in protocols for FILE*, socket, mutex
- [ ] Migration: convert `zlib.dtt` manual annotations to protocol block
- [ ] Tests: valid protocol usage (pass), violations (fail), custom protocols
- [ ] Documentation: "Defining API Protocols"

---

## Phase 8: Specification Mining (Exploratory)

**Goal:** Automatically discover implicit invariants from existing code.
**Value:** Bootstrap annotations for un-annotated codebases.
**Depends on:** Phase 6 (cross-TU facts), Phase 7 (typestate).

### 8.1 Mining Mode

```bash
clang++ -fplugin-arg-dtt---dtt-mine-specs=learned.dtt ...
```

**Strategies:**

1. **Acquire/release pairing:** If function A's return value is always passed to
   function B before scope exit → infer resource protocol.

2. **Null-check patterns:** If a return value is checked against null in >90%
   of call sites → infer nonnull precondition on usage.

3. **Size-operation correlation:** If `size()` is always called before `operator[]`
   → infer bounds-check precondition.

4. **Call-sequence patterns:** If function A always precedes function B → infer
   ordering protocol.

### 8.2 Confidence Scoring

```
# Mined from 47 call sites (confidence: 0.94)
fn db_query(2) {
  requires arg0._nonnull != 0    # 44/47 sites check null
}
```

Users review and promote high-confidence rules to their project's `.dtt` file.

### 8.3 When to Consider a Real Logic Engine

This is the phase where an embedded Datalog (e.g., Soufflé compiled to C++)
might pay for itself. The mining problem is naturally expressed as:

```prolog
candidate_resource_pair(Acquire, Release) :-
    call_site(Site, Acquire, Result),
    reachable(Site, LaterSite),
    call_site(LaterSite, Release, Result),
    dominates_scope_exit(LaterSite, Site).
```

If Phases 1-7 demonstrate enough value, Phase 8 is where evaluating a real
logic engine becomes justified by concrete need.

### 8.4 Deliverables

- [ ] `--dtt-mine-specs` mode
- [ ] Mining strategies for null checks, resource pairs, call ordering
- [ ] Confidence-scored output in `.dtt` format
- [ ] Validation: mined specs vs. hand-written specs for STL containers

---

## Phase Dependency Graph

```
PR #34 (.dtt format) ─────────────────────────────────┐
    │                                                  │
Phase 1: Conditional Rules (when-clauses)              │
    │   closes: conditional pre/postconditions gap     │
    │                                                  │
    ├──── Phase 4: Cross-Object Constraints            │
    │         closes: ownership/relational gap         │
    │                                                  │
    ├──── Phase 7: Typestate Protocols                 │
    │         sugar over enums + ghost fields          │
    │                                                  │
Phase 2: Rule-Based Entailment ──── (independent) ─────┤
    │   replaces 6×6 switch matrix                     │
    │   enables transitive proofs                      │
    │                                                  │
Phase 3: Backward Chaining ──── (independent) ─────────┤
    │   provenance tracking                            │
    │   fix suggestions + FixItHint                    │
    │                                                  │
    └──── Phase 5: Query Interface                     │
              --dtt-query, --dtt-inspect, --dtt-assume │
                                                       │
Phase 6: Cross-TU Fact Persistence ◄───────────────────┘
    │   --dtt-emit-facts, build system integration
    │
Phase 8: Specification Mining ◄── 6, 7
      learning mode, confidence scoring
      (evaluate real Datalog engine here)
```

**Key:** Phases 1, 2, and 3 have no mutual dependencies and can proceed
in parallel. Each delivers standalone value.

---

## What Each Phase Adopts from Prolog

| Phase | Prolog Concept | How Adopted | No New Dependencies |
|-------|---------------|-------------|---------------------|
| 1 | Guard clauses | `when` conditions on pre/postconditions | Reuse existing constraint evaluator |
| 2 | Forward chaining rules | `constexpr` rule table replaces switch | Pure refactor |
| 3 | Backward chaining | Goal-directed search for fix suggestions | Pattern-matching table in C++ |
| 4 | Relational reasoning | `@owner` links between objects | Extended FieldPath resolution |
| 5 | Interactive queries | Query mode flags | Reuse provenance from Phase 3 |
| 6 | Persistent fact database | Serialize inferred facts to `.dtt` | Reuse existing parser |
| 7 | State machines as logic | `protocol` blocks → constraints | Sugar over existing ghost fields |
| 8 | Abductive inference | Spec mining from code patterns | Maybe Datalog here — evaluate then |

---

## What We Deliberately Don't Do

1. **No embedded Prolog runtime.** Every phase is C++ using the existing
   constraint engine. A Datalog engine is only evaluated if Phase 8 proves
   the need.

2. **No new AST representation.** The Clang AST stays. The `RecursiveASTVisitor`
   remains the "sensor." Logic layers are the "brain" consuming its observations.

3. **No Turing-complete annotation language.** The `.dtt` format gains `when`
   guards and `protocol` blocks, but no loops, no recursion, no user-defined
   functions. Deliberate limitation.

4. **No abandoning libclang.** The Clang plugin API is the foundation. Every
   phase builds on `RecursiveASTVisitor`. The visitor is the interface to the
   compiler; nothing replaces it.

5. **No big-bang migration.** Each phase is independently valuable. Stop after
   any phase and you still have useful functionality. No phase is wasted if
   later phases don't happen.

---

## Recommended Starting Point

**Phases 1 + 2 + 3 in parallel** — they have no mutual dependencies:

- **Phase 1** (conditional rules): Highest impact on annotation expressiveness.
  Directly addresses the #1 gap from PR #34.

- **Phase 2** (rule-based entailment): Smallest change, lowest risk. Replace
  one switch statement with a rule table. Quick proof of the paradigm.

- **Phase 3** (backward chaining): Highest impact on user experience. Fix
  suggestions are the most-requested feature.

Then **Phase 7** (typestate protocols) — this leverages Phase 1 and provides
the most visible new capability.

Phases 4-6 and 8 are evaluated based on how 1-3 land.

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| `when` guards make parser too complex | Limit to single comparison expressions (no nesting); extend later if needed |
| Rule table slower than switch | `constexpr` table + indexed lookup = same performance; benchmark to verify |
| Fix suggestions are wrong/misleading | Conservative: only suggest well-known patterns; label as "suggestion" not "fix" |
| Cross-object resolution blows up | Bound depth to 2 hops (object → owner → owner's field); no unbounded chains |
| Protocol blocks under-specified | Start with linear protocols only; no branching state machines in v1 |
| Spec mining produces garbage | Confidence scoring + human review gate; never auto-apply mined specs |
| Phase 8 actually needs Datalog | That's fine — by then we'll have 7 phases of evidence to justify the dependency |
