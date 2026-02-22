# Datalog-Style Generalized Fixpoint Design

**Status: IMPLEMENTED** (see commits on `claude/prolog-paradigm-research-7Owxd`)

## Problem Statement

DTT's transitive precondition propagation was hardcoded for one property
type: `SizeGE` (buffer size >= bound). The `InferredPrecondition` struct, the
propagation loop, and the warning emission all contained buffer-specific logic
(pointer arithmetic decomposition, offset scaling, byte-size tracking).

Meanwhile, `.dtt` annotation files define rich preconditions for many property
types (`_nonnull`, `@open`, `_has_value`, `_size`, etc.), but these are only
checked at the immediate call site — never propagated transitively through
internal call chains.

## Goal

Replace the property-specific fixpoint with a **property-agnostic** system where:

1. Any `.dtt` `requires` directive automatically becomes propagatable
2. The fixpoint loop doesn't know or care what the properties mean
3. Buffer-specific pointer arithmetic is a specialization, not the core

## Design: Facts and Rules

### Fact Schema (Datalog-style)

```
// A function requires a constraint on a parameter's property
requires(FnDecl, ArgIdx, Field, Op, Value)

// A function calls another function, mapping caller arg K to callee arg J
calls(CallerDecl, CalleeDecl, CallExpr, CallerArgK, CalleeArgJ)

// A function has internal linkage (all call sites are known)
internal(FnDecl)
```

### Propagation Rule

```
// If Callee requires a constraint on parameter J,
// and Caller calls Callee passing its parameter K to J,
// and Caller has internal linkage,
// then Caller also requires that constraint on parameter K.
requires(Caller, K, Field, Op, Value) :-
    requires(Callee, J, Field, Op, Value),
    calls(Caller, Callee, _, K, J),
    internal(Caller).
```

This single rule replaces ~150 lines of buffer-specific propagation code for
the generic case.

### Buffer-Specific Extension Rule

```
// Buffer pointer arithmetic: if Callee requires buf._size_bytes >= bound,
// and Caller passes (base + offset) as the buffer argument,
// then Caller requires base._size_bytes >= bound + offset.
requires(Caller, BaseK, "_size_bytes", GE, bound + offset) :-
    requires(Callee, J, "_size_bytes", GE, bound),
    calls(Caller, Callee, _, K, J),
    pointer_arith(K, BaseK, offset),
    internal(Caller).
```

## Implementation: PropertyConstraint

Replace `PreconditionKind { NonNull, SizeGE }` with a generic constraint:

```cpp
struct PropertyConstraint {
    SmallString<32> field;    // "_nonnull", "_size", "@open", "_size_bytes"
    ProofOp op;               // NE, GE, GT, EQ, etc.
    SymValue bound;            // The required value/expression

    // Buffer-specific extension (optional, only for _size_bytes)
    std::optional<unsigned> boundArgIndex;
    int64_t boundConstant = 0;
    std::optional<unsigned> offsetArgIndex;
    int64_t offsetConstant = 0;
    int64_t offsetElemSize = 1;
    bool offsetNegated = false;
};
```

### What Changes

| Component | Before | After |
|-----------|--------|-------|
| `PreconditionKind` | `{NonNull, SizeGE}` | Removed — replaced by `{field, op}` |
| `InferredPrecondition` | Buffer-specific fields | `PropertyConstraint` + argIndex |
| Propagation loop | `decomposePointerArg` always | Generic path + buffer specialization |
| Entry points | Only `emitInferredPrecondition` | Also `.dtt` `requires` recording |
| Warning emission | Buffer-size-only checking | Generic constraint checking |

### Propagation Loop Refactor

```
for each (callee, constraints) in FnPreconditions:
    for each call site where callee is called:
        for each constraint on callee:
            if constraint.field == "_size_bytes":
                // Use existing buffer-specific decomposition
                propagateBufferConstraint(...)
            else:
                // Generic path: simple parameter index mapping
                propagateGenericConstraint(...)
```

The generic path is ~20 lines: map callee's argJ to caller's argK, copy the
constraint with remapped index. No pointer arithmetic, no offset tracking.

## Fixpoint Properties

### Monotonicity
Each iteration only adds constraints (never removes). The constraint set for
each function grows monotonically.

### Termination
- Finite number of functions × finite number of parameters × finite number of
  distinct (field, op, value) triples = finite lattice
- Each iteration must add at least one new constraint to continue
- Bounded by `kMaxFixpointIterations` as safety net

### Soundness
- Only propagate through internal-linkage functions (all call sites known)
- Constraints are necessary conditions: if callee requires P, every caller
  must establish P before the call

## Migration Path

1. **Add `PropertyConstraint` alongside existing code** — no breaking changes
2. **Record `.dtt` `requires` as `PropertyConstraint`** — new capability
3. **Add generic propagation path** — simple param mapping for non-buffer
4. **Refactor buffer path to use `PropertyConstraint`** — unify representations
5. **Remove `PreconditionKind` enum** — fully replaced

Steps 1-3 are additive. Steps 4-5 are refactoring. Each step is independently
testable.

## Relation to Prolog/Datalog

The current implementation is an **imperative fixpoint** — a hand-written
`while(changed)` loop with property-specific code baked in. The Datalog
perspective reveals that:

1. The fixpoint computation is a standard **semi-naive evaluation** — each
   iteration derives new facts from facts added in the previous iteration
2. The property-specific code (buffer decomposition) is a **built-in predicate**
   — it can be factored into a separate rule without changing the core engine
3. Adding new properties (null, resource state, optional) requires **only new
   facts**, not new engine code — if the engine is generic

The prototype doesn't need a full Datalog engine. It needs the engine to be
**parameterized** by the constraint type rather than having the constraint type
baked into the loop structure.

## Test Plan

| Test | Property | Expected |
|------|----------|----------|
| 1720_transitive_null_prop | `_nonnull != 0` | Propagate null-check through wrappers |
| 1721_transitive_resource_prop | `_nonnull != 0` (resource) | Propagate through resource wrappers |
| 1722_transitive_generic_prop | Multiple properties | Mixed property propagation |
| 1723_transitive_null_safe_pass | `_nonnull != 0` | Safe code not flagged |
| 950_transitive_precond_pass | `_size_bytes >= n` | Existing buffer tests still pass |
