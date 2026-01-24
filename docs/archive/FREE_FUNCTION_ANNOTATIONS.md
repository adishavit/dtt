# Free Function Annotation Support

**Status**: Partially implemented - any_cast `_has_value` checking done (commit b584226)

## Remaining Work

- [ ] std::get for variant (requires _type_index tracking)
- [ ] std::holds_alternative for variant (boolean postcondition)
- [ ] std::any_cast type tracking (typeid)

---

## Problem

DTT's `MethodAnnotations` system only supports **class member methods**:
```cpp
MethodAnnotations[{"optional", "value", 0}] = { requires: [_has_value == 1] };
```

This works for `std::optional` which uses member methods (`opt.value()`, `*opt`).

However, `std::variant` and `std::any` use **free functions**:
```cpp
std::get<int>(v);              // Free function in std namespace
std::holds_alternative<T>(v);  // Free function
std::any_cast<int>(a);         // Free function
```

## Existing Free Function Handling

We **already** handle free functions in `VisitCallExpr` for C functions:
```cpp
// DTTPlugin.cpp lines 9565+
if (fn == "memcpy" || fn == "memmove") { /* bounds checking */ }
if (fn == "memset") { /* bounds checking */ }
if (fn == "swap" && isStdSwap) { swapProperties(path1, path2); }
```

So the infrastructure exists - we just need to extend it for `std::get`, `std::any_cast`, etc.

## Current Coverage Impact

| Type | Coverage | Reason |
|------|----------|--------|
| optional | 56% | Member methods (value, operator*, operator->) |
| variant | 6% | Free functions (std::get, std::holds_alternative) |
| any | **29%** | _has_value checking via any_cast (was 6%) |

## Proposed Solution

### 1. Free Function Annotation Registry

Add a new registry for free functions:
```cpp
struct FreeFunctionKey {
    std::string namespaceName;  // "std"
    std::string functionName;   // "get"
    unsigned arity;             // argument count
};

llvm::DenseMap<FreeFunctionKey, FunctionInfo> FreeFunctionAnnotations;
```

### 2. FunctionInfo Structure

```cpp
struct FunctionInfo {
    // Preconditions on arguments
    // e.g., arg0._type_index == <template param index>
    SmallVector<ConstraintExprPtr, 2> preconditions;

    // Postconditions on return value
    SmallVector<Postcondition, 2> ensures;

    // Boolean postconditions for guard functions
    SmallVector<BoolPostcondition, 2> boolPostconds;
};
```

### 3. Template Parameter Tracking

For `std::get<T>(v)`, need to:
1. Extract template argument T
2. Map T to variant's type index
3. Check `v._type_index == index_of<T>`

This requires:
- Tracking variant's template parameter list
- Computing type index at compile time
- Storing `_type_index` field for variants

### 4. Implementation in VisitCallExpr

Follow existing pattern (memcpy, std::swap) - add cases for std::get, std::any_cast:

```cpp
bool VisitCallExpr(CallExpr *E) {
    // ... existing memcpy/memset/swap handling ...

    // Handle std::get<T>(variant) and std::get<I>(variant)
    if (fn == "get" && isInStdNamespace(Callee) && E->getNumArgs() == 1) {
        // Extract template argument (type T or index I)
        // Check arg0._type_index matches
        // Error if mismatch
    }

    // Handle std::holds_alternative<T>(variant)
    if (fn == "holds_alternative" && isInStdNamespace(Callee)) {
        // Set up boolean postcondition for guards
    }

    // Handle std::any_cast<T>(any)
    if (fn == "any_cast" && isInStdNamespace(Callee)) {
        // Check arg0._has_value == 1
        // Optionally track type (complex)
    }
}
```

**Key challenge**: Extracting template arguments from `std::get<int>` or `std::get<0>`.

## Specific Annotations Needed

### std::variant

```cpp
// std::get<T>(v) - requires v holds type T
FreeFunctionAnnotations[{"std", "get", 1}] = {
    requires: [arg0._type_index == template_param_index<0>]
};

// std::get<I>(v) - requires v.index() == I
FreeFunctionAnnotations[{"std", "get", 1}] = {
    requires: [arg0._type_index == template_param_value<0>]
};

// std::holds_alternative<T>(v) - returns true if v holds T
FreeFunctionAnnotations[{"std", "holds_alternative", 1}] = {
    boolPostconds: [{field: "_type_index", onTrue: template_param_index<0>}]
};

// std::get_if<T>(&v) - returns nullptr if wrong type
FreeFunctionAnnotations[{"std", "get_if", 1}] = {
    returns: [_nonnull == (arg0._type_index == template_param_index<0>)]
};
```

### std::any

```cpp
// std::any_cast<T>(a) - requires a holds type T (throws otherwise)
FreeFunctionAnnotations[{"std", "any_cast", 1}] = {
    requires: [arg0._has_value == 1, arg0._type_id == typeid(template_param<0>)]
};

// std::any_cast<T>(&a) - returns nullptr if wrong type or empty
FreeFunctionAnnotations[{"std", "any_cast", 1}] = {
    returns: [_nonnull == (arg0._has_value == 1 && arg0._type_id == typeid(template_param<0>))]
};
```

## Complexity Estimate

| Component | Effort |
|-----------|--------|
| Add cases to VisitCallExpr | Low (follow memcpy pattern) |
| Template parameter extraction | Medium |
| Type index tracking for variant | Medium |
| Boolean postcondition for holds_alternative | Medium |
| typeid tracking for any | High (optional - can skip) |

**Minimal implementation** (std::get + holds_alternative): ~100-150 lines
**Full implementation** (including any_cast type tracking): ~200-300 lines

### Simplest First Step - ✅ DONE

Check `_has_value` for `std::any_cast` without type tracking: **Implemented in commit b584226**

Improved `any` coverage from 7% to 29%.

## Alternatives Considered

1. **Wrapper types**: Create `dtt::variant<Ts...>` with member methods - rejected (invasive)
2. **Macro-based annotations**: User annotates call sites - rejected (verbose)
3. **Pattern matching**: Detect `if (holds_alternative<T>) { get<T> }` patterns - partial solution

## Dependencies

- None (can be implemented independently)

## Related Work

- operator*/operator-> checking (completed)
- Boolean postcondition proof propagation (completed)
- std::optional support (56% coverage)
