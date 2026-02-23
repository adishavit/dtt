# Universal Ghost Fields Plan

> **Status:** Proposal. No implementation yet.

## Problem Statement

DTT tracks object properties through 7+ parallel data structures that are
independently saved, restored, and merged at every branch point. This happened
organically — each tracking dimension was added when needed, with its own map
and its own save/restore logic. The result works, but is fragile and expensive
to extend.

### The parallel maps (cleared in `clearPerFunctionState()`)

| Map | Keyed by | Tracks |
|-----|----------|--------|
| `TypedEnv` | `FieldPath` (variable) | `_size`, `_nonnull`, `_has_value`, `@open`, proofs, etc. |
| `PointerLifetimes` | `string` (variable) | Valid / Freed / Unknown + AllocSource |
| `HeapObjectStates` | `ResourceId` (alloc site) | Heap object initialized? |
| `VarToResource` | `string` (variable) | Variable → allocation site binding |
| `StackVarPaths` | `ResourceId` | Alloc site → stack variable reverse map |
| `FieldStates` | `string` ("var.field") | TrackingState + loss provenance |
| `AnyContainedTypes` | `string` (variable) | QualType of std::any contents |

Plus `GlobalProofs`, `VarBoundProofs`, `NullProvenanceMap`, `ProvenanceChains`,
`CurrentScopeExitConstraints`, `PendingGuardedPostconditions`.

### Concrete costs of the current approach

**Branch save/restore duplication.** Every `if/else` handler saves and restores
3 maps in lockstep (`savedLifetimes`, `savedTypedEnv`, `savedVarToResource`).
There are 15 save sites and 34 restore sites — 49 copy operations across the
branch-handling code, each copying a separate map. Adding a new tracked
dimension means finding and updating all 49 sites.

**Inconsistency risk.** Nothing structurally prevents `PointerLifetimes["p"]`
saying `Freed` while `TypedEnv` still has `p._nonnull == 1`. These maps are
maintained by different code paths, and consistency is by convention, not
construction.

**Hardcoded inference.** `inferNonnullFromExpr()` (60 lines) hardcodes that
`new` → nonnull, `&x` → nonnull, string literals → nonnull, etc. Similar
hardcoded logic exists for `_initialized`, `_variant_index`, and string literal
`_size`. Each property has its own ad-hoc inference scattered through the
16k-line visitor. A new property (say, `_alignment` for aligned_alloc) would
require touching multiple unrelated functions.

---

## The Idea

Every object in the program carries a **type-determined set of ghost fields**,
automatically provisioned when the variable enters scope. Instead of ad-hoc
"is this a pointer? then maybe track nonnull" checks scattered through the
visitor, a **type schema registry** declares what properties each type category
gets, and the plugin initializes them uniformly.

This is what DTT already does via `.dtt` annotations for *annotated* types.
The proposal extends it to:
1. Built-in type categories (pointers, scalars, arrays) that currently use
   hardcoded logic instead of the annotation system.
2. A unified internal representation that eliminates the parallel maps.

---

## Honest Assessment

### What this is

An internal refactor that unifies parallel tracking mechanisms into a single
per-object state representation, and makes the property inference system
declarative rather than scattered through the visitor.

### What this is NOT

- Not a new analysis capability. Same bugs detected, same annotations, same
  diagnostics.
- Not a solution to aliasing. The fundamental single-TU, limited-aliasing
  model remains. `VarToResource` handles direct pointer copies; this doesn't
  extend that.
- Not a performance improvement. Probably net neutral — fewer maps to copy at
  branch points, but each map entry is larger.

### Where it becomes genuinely interesting

If pushed further, type schemas could be auto-derived from the C++ type system
rather than requiring manual annotation. A type with `.size()` returning
`size_t` automatically gets `_size` tracking. A type with `explicit operator
bool()` automatically gets `_has_value` semantics. This is a separate, larger
project — noted here for completeness but not proposed for Phase 1.

---

## Design

### Phase 1: Unify variable-centric maps

Merge `PointerLifetimes` into `TypedEnv` as synthetic fields. This is the
lowest-risk, highest-payoff change.

**Currently:**
```cpp
// Two separate maps, separately saved/restored at branches
llvm::StringMap<PointerLifetimeState> PointerLifetimes;
llvm::DenseMap<FieldPath, SymValue> TypedEnv;

// At every if/else:
auto savedLifetimes = PointerLifetimes;
auto savedTypedEnv = TypedEnv;
// ...traverse branches...
PointerLifetimes = savedLifetimes;
TypedEnv = savedTypedEnv;
```

**Proposed:**
```cpp
// PointerLifetimes eliminated. Lifetime tracked as synthetic fields in TypedEnv:
//   p._lifetime = 0 (Valid), 1 (Freed)
//   p._alloc_source = 0 (Malloc), 1 (New), 2 (NewArray), ...
//   p._alloc_loc stored in a side table (SourceLocation, not a SymValue)

// At every if/else:
auto savedTypedEnv = TypedEnv;
// ...traverse branches...
TypedEnv = savedTypedEnv;
// One save, one restore. Done.
```

**Merge rules for `_lifetime`:**
- Both branches say Valid → Valid
- Both branches say Freed → Freed
- Disagree → Unknown (potential UAF on one path)

This follows the same pattern as ghost field merging (`mergeAbsentField`),
extended with a `mergeFieldByCategory()` function that knows the merge
semantics per field type.

**What changes:**
- `PointerLifetimeState` struct eliminated
- `PointerLifetimes` map eliminated
- `allocLocation` / `freeLocation` move to a lightweight side table
  (these are diagnostic metadata, not analysis state — they don't need
  branch-sensitive save/restore)
- All 15 `savedLifetimes` save sites and 34 restore sites eliminated
- UAF/double-free checks read from `TypedEnv` instead of `PointerLifetimes`

**What doesn't change:**
- Resource-centric tracking (`VarToResource`, `HeapObjectStates`,
  `StackVarPaths`) stays separate — it's fundamentally keyed by allocation
  site, not variable path, for aliasing correctness
- `.dtt` file format unchanged
- All existing tests pass identically

### Phase 2: Type schema registry

Replace hardcoded inference with a declarative registry that maps C++ type
categories to default ghost fields.

**Currently (scattered through visitor):**
```cpp
// In VisitVarDecl, around line 8988:
if (auto nonnull = inferNonnullFromExpr(Init)) {
    setFieldTyped(varPath, "_nonnull", SymValue::Const(*nonnull));
}
// In VisitVarDecl, around line 9264:
if (VD->getType()->isScalarType() && !Init) {
    setFieldTyped(varPath, "_initialized", SymValue::Const(0));
}
// In VisitVarDecl, around line 9295:
// POD struct without initializer: mark each member _initialized = 0
```

**Proposed:**
```cpp
// Type schema registry (populated once at plugin init)
struct TypeSchema {
    SmallVector<std::pair<StringRef, InferRule>, 4> fields;
};

// InferRule describes how to compute initial value
struct InferRule {
    enum Kind {
        Constant,        // Always this value (e.g., _initialized = 0 for uninit)
        FromExpr,        // Infer from initializer expression
        FromAnnotation,  // From .dtt file (existing system)
        FromTemplateArg, // From template parameter (e.g., array<T,N> → _size=N)
    };
    Kind kind;
    int64_t constantVal;    // For Constant kind
    // For FromExpr: pointer to inference function
    std::optional<int64_t> (*inferFn)(Expr*);
};

// Registry entries:
//   pointer<T>  → { _nonnull: FromExpr(inferNonnullFromExpr),
//                    _initialized: Constant(0) if no init,
//                    _lifetime: Constant(Valid) if new/malloc }
//   scalar      → { _initialized: Constant(0) if no init }
//   pod_struct  → { <member>._initialized: Constant(0) for each member }
```

The visitor's `VisitVarDecl` becomes:
```cpp
auto schema = getSchemaForType(VD->getType());
for (auto& [field, rule] : schema.fields) {
    auto val = rule.evaluate(Init, VD);
    if (val) setFieldTyped(varPath, field, *val);
}
// Then apply .dtt annotations (existing logic, unchanged)
```

**What this buys:**
- New tracking dimensions are one registry entry instead of scattered visitor
  changes. Want `_alignment` tracking? Add a schema entry.
- The inference logic (`inferNonnullFromExpr` etc.) still exists as functions,
  but they're *registered* rather than *called ad-hoc*.
- Self-documenting: `dumpSchemaRegistry()` shows exactly what DTT tracks for
  each type category.

**What doesn't change:**
- `.dtt` annotations still override/supplement schemas (they're applied after
  schema defaults)
- The proof system, Z3 integration, diagnostic categories — all unchanged

### Phase 3: Unified branch state

With Phase 1 done (lifetime in TypedEnv) and Phase 2 done (schema-driven
initialization), the branch save/restore simplifies to:

```cpp
struct BranchState {
    DenseMap<FieldPath, SymValue> env;       // TypedEnv (now includes lifetime)
    StringMap<ResourceId> varToResource;     // Aliasing (still separate, still needed)
};

BranchState saveBranchState() {
    return { TypedEnv, VarToResource };
}

void restoreBranchState(const BranchState& s) {
    TypedEnv = s.env;
    VarToResource = s.varToResource;
}
```

Two maps instead of three (or more). The merge operation calls
`mergeTypedEnvBranches()` (already exists) with extended per-field merge rules
from Phase 1.

### Phase 4 (future): Merge resource-centric tracking

This is the harder problem. `HeapObjectStates` and `StackVarPaths` exist
because initialization is a property of the *allocation*, not the *variable*.
Multiple variables can alias the same heap object.

**Option A — Indirection field:**
Store `_resource_id` as a synthetic field in TypedEnv. Resource properties
(`_heap_initialized`) live in a shared resource state map keyed by ResourceId.
Variable-centric and resource-centric tracking unify at the lookup level:

```cpp
// Getting heap init state for pointer p:
ResourceId rid = getFieldTyped(pPath, "_resource_id");  // from TypedEnv
bool init = ResourceStates[rid].initialized;             // shared state
```

This collapses `VarToResource` into TypedEnv (as `_resource_id`) and keeps
`HeapObjectStates` as the only remaining side table, but reframes it as
"resource state" rather than "heap init state" — extensible to future
resource properties.

**Option B — Keep resource maps, just reduce count:**
Merge `HeapObjectStates` + `StackVarPaths` into a single `ResourceStates` map.
Keep `VarToResource` as-is. Net: 2 maps instead of 3 for resource tracking.
Less elegant but lower risk.

**Recommendation:** Option B first (simple, safe), Option A later if the
pattern proves itself.

---

## Migration Strategy

Each phase is independently valuable and independently shippable. No phase
depends on a later phase.

| Phase | Estimated scope | Risk | Payoff |
|-------|----------------|------|--------|
| 1: Lifetime → TypedEnv | ~200 lines changed | Low (mechanical) | Eliminates 49 save/restore sites |
| 2: Type schema registry | ~300 lines new, ~150 removed | Medium (new abstraction) | New dimensions become trivial to add |
| 3: Unified branch state | ~100 lines changed | Low (follows from Phase 1) | Code clarity, single save/restore |
| 4: Resource map consolidation | ~150 lines changed | Medium (aliasing correctness) | Fewer maps, cleaner model |

**Testing strategy:** Each phase must pass all existing tests unchanged. No
new tests needed for Phase 1/3 (pure refactor). Phase 2 should add a test
exercising schema-driven initialization. Phase 4 needs careful aliasing tests.

**Rollback:** Each phase touches only internal data structures. The `.dtt`
format, user-facing annotations, diagnostic messages, and test expectations
are all unchanged. Any phase can be reverted independently.

---

## What the .dtt Format Could Look Like (Future)

If Phase 2 proves the schema concept, the `.dtt` format could gain `schema`
blocks to let users define type-category defaults:

```
# Built-in type category schemas
schema pointer {
    fields: _nonnull, _initialized, _lifetime
    init _nonnull from_expr          # uses inferNonnullFromExpr
    init _initialized = 0 when no_init
    init _lifetime = 0 when new      # 0 = Valid
}

schema scalar {
    fields: _initialized
    init _initialized = 0 when no_init
    init _initialized = 1 when has_init
}

# User-defined schemas (composable with existing traits)
schema handle : pointer {
    fields: @resource_state
    init @resource_state = 0         # 0 = not acquired
}
```

This is aspirational. The immediate value is in the internal refactor
(Phases 1-3), not in new syntax.

---

## Appendix: Current State Inventory

Exact counts from `DTTPlugin.cpp` (16,170 lines) as of this writing:

- **15** `savedLifetimes = PointerLifetimes` save sites
- **34** `PointerLifetimes = saved*` restore sites
- **15** `savedTypedEnv = TypedEnv` save sites
- **15** `savedVarToResource = VarToResource` save sites
- **7** properties with hardcoded inference logic: `_nonnull`, `_initialized`,
  `_size` (string literals), `_variant_index`, `_has_value` (expected from
  unexpected), `_any_type`, and struct member `_initialized`
- **1** unified merge function (`mergeTypedEnvBranches`) — only for TypedEnv.
  PointerLifetimes has no merge; it uses last-writer-wins from whichever
  branch ran last, which is technically unsound for paths where both branches
  modify lifetime state.

That last point — **PointerLifetimes lacks a proper branch merge** — is itself
a latent bug that Phase 1 would fix by bringing lifetime under
`mergeTypedEnvBranches`'s proper join semantics.
