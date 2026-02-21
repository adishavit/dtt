# Resource-Centric Tracking Design

> **STATUS: IMPLEMENTED** (v34, enhanced v36)
>
> Resource-centric tracking is now implemented with automatic C library annotations.
> See `annotations/libc.dtt` for the full list of tracked functions.
> v36 added deferred guard evaluation: `when`-guarded postconditions are stored
> as pending and applied retroactively when null checks prove the guard.

## Current State (Variable-Centric)

```cpp
// Ghost state stored per-variable
TypedEnv["f.@open"] = 1
TypedEnv["g.@open"] = 1  // Copied from f

// Alias tracking (single level)
PointerAliases["g"] = "f"

// Constraint on variable
ScopeExitConstraints: [{varPath: "g", field: "@open", required: 0}]
```

**Problem**: State is duplicated, aliases need manual propagation.

## Proposed: Resource-Centric

### Data Structures

```cpp
// Resource identity (allocation site)
using ResourceId = SourceLocation;  // Or unique ID

// Which resource each variable points to
llvm::DenseMap<std::string, ResourceId> VarToResource;
// e.g., VarToResource["f"] = loc:15:22
//       VarToResource["g"] = loc:15:22  // Same resource!

// Ghost state per resource (not per variable)
llvm::DenseMap<ResourceId, llvm::StringMap<SymValue>> ResourceState;
// e.g., ResourceState[loc:15:22]["@open"] = 1

// Scope-exit constraints per resource
struct ResourceConstraint {
    ResourceId resource;
    std::string field;
    ProofOp op;
    int64_t requiredValue;
    SourceLocation acquireLoc;
};
llvm::SmallVector<ResourceConstraint, 4> ResourceConstraints;
```

### Key Operations

#### 1. Resource Acquisition
```cpp
FILE* f = fopen(...);
```
```cpp
ResourceId rid = Call->getBeginLoc();  // Allocation site as ID
VarToResource["f"] = rid;
ResourceState[rid]["@open"] = 1;
ResourceConstraints.push_back({rid, "@open", EQ, 0, loc});
```

#### 2. Pointer Copy/Alias
```cpp
FILE* g = f;
```
```cpp
ResourceId rid = VarToResource["f"];
VarToResource["g"] = rid;  // Points to same resource
// No state copying needed - state is on resource
```

#### 3. Close via Any Alias
```cpp
fclose(f);  // or fclose(g) - same effect
```
```cpp
ResourceId rid = VarToResource["f"];
ResourceState[rid]["@open"] = 0;
// All aliases automatically see updated state
```

#### 4. Double Close Detection
```cpp
fclose(f);
fclose(g);  // ERROR
```
```cpp
ResourceId rid = VarToResource["g"];
// Check: ResourceState[rid]["@open"] == 1 required
// Actual: ResourceState[rid]["@open"] == 0
// ERROR: double close
```

#### 5. Null Assignment (Release Alias)
```cpp
f = nullptr;
```
```cpp
VarToResource.erase("f");  // f no longer points to resource
// Resource state unchanged
// Constraint still exists on resource
```

#### 6. Scope Exit Check
```cpp
} // end of scope where f was declared
```
```cpp
// For each variable going out of scope:
if (VarToResource.contains("f")) {
    ResourceId rid = VarToResource["f"];
    // Check if this is the LAST alias to the resource
    bool lastAlias = true;
    for (auto& [var, r] : VarToResource) {
        if (r == rid && var != "f" && stillInScope(var)) {
            lastAlias = false;
            break;
        }
    }
    if (lastAlias) {
        // Check constraint: ResourceState[rid]["@open"] == 0
    }
}
```

## Effort Estimate

| Task | Lines of Code | Risk |
|------|---------------|------|
| Add ResourceId type and maps | ~50 | Low |
| Refactor acquisition to use ResourceId | ~100 | Medium |
| Refactor alias tracking | ~150 | Medium |
| Update ghost state access | ~200 | Medium - many call sites |
| Update scope-exit checking | ~100 | Medium |
| Update precondition checking | ~50 | Low |
| Handle reassignment (f = fopen2) | ~50 | Low |
| Testing and edge cases | ~200 | High - many scenarios |

**Total: ~900 lines changed, 2-3 days focused work**

## Benefits

1. **Simpler mental model**: Resource has state, pointers are just handles
2. **No state propagation**: Close via any alias automatically visible
3. **Correct nullptr semantics**: `f = nullptr` just removes alias, doesn't affect resource
4. **Better error messages**: "Resource acquired at line 15 not closed" vs "variable f not closed"

## Edge Cases

1. **Reassignment**: `f = fopen(); f = fopen();` - first resource leaked
2. **Conditional aliases**: `if (c) g = f;` - g may or may not alias f
3. **Function parameters**: `void foo(FILE* p)` - p aliases caller's resource
4. **Return values**: `return f;` - transfers resource to caller

## Alternative: Hybrid Approach

Keep variable-centric for simple cases, switch to resource-centric only when:
- Multiple aliases detected
- Complex control flow

This reduces refactoring but adds complexity in deciding which mode to use.
