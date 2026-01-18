# DTT Code Review: What Lattner Would Say

A critical review of DTT's code quality from an LLVM project perspective.

---

## Executive Summary

**Good news:** Major refactoring completed! Code now uses proper LLVM idioms:
- ✅ DenseMap with typed keys (MethodKey, VarBoundKey, WarningKey)
- ✅ SmallDenseSet for O(1) operations (ProofStore, VarBoundProofs)
- ✅ StringSet/DenseSet replacing std::set
- ✅ SmallVector for local temporaries
- ✅ StringRef for zero-copy string handling
- ✅ constexpr for static data (ProofOpStrings)

**Remaining:** Some API-level changes (prefix index for TypedEnv, SmallVectorImpl out-params) would require architectural work.

---

## Category A: String Concatenation Still Everywhere (CRITICAL) ✅ FIXED

### Problem (RESOLVED)

Despite FieldPath migration, the code still has **60+ instances** of string concatenation for paths:

```cpp
// Still pervasive patterns:
std::string key = className + "::" + methodName + "/" + std::to_string(M->getNumParams());
std::string argKey = "arg" + std::to_string(i);
std::string prefix = path.str() + ".";
```

### Impact
- Heap allocations on hot paths
- No caching of computed keys
- Inconsistent key construction across call sites

### Fix ✅ IMPLEMENTED

Created typed key structures with DenseMapInfo specializations:
- `MethodKey` for method annotations lookup
- `VarBoundKey` for variable bound proofs
- `WarningKey` for deduplicating warnings

All use `llvm::hash_combine` for efficient hashing.

**Status:** Complete

---

## Category B: `rfind(..., 0) == 0` Anti-Pattern (HIGH) ✅ FIXED

### Problem (RESOLVED)

**30+ instances** of `rfind(prefix, 0) == 0` instead of `starts_with()`:

```cpp
// Current (inefficient, unclear intent):
if (s.rfind("dtt:requires=", 0) == 0) { ... }
if (pathStr.rfind(objPath + ".", 0) == 0) { ... }

// Should be:
if (s.starts_with("dtt:requires=")) { ... }
if (pathStr.starts_with(objPath + ".")) { ... }  // or better, avoid concatenation
```

### Impact
- Unnecessary string scan
- Intent unclear to readers
- Some already converted to `starts_with`, creating inconsistency

### Fix
Global find-replace `rfind(X, 0) == 0` → `starts_with(X)`

**Effort:** Low (1-2 hours)
**Priority:** P0 (trivial fix)

---

## Category C: `getNameAsString()` Proliferation (HIGH)

### Problem

**80+ calls** to `getNameAsString()` which allocates a new `std::string` each time:

```cpp
std::string className = Method->getParent()->getNameAsString();
std::string methodName = Method->getNameAsString();
// Then immediately used for comparison or concatenation
```

### Impact
- Each call allocates a new string
- Same name often retrieved multiple times in same function
- Clang provides `getName()` returning `StringRef` for many decls

### Fix

```cpp
// Bad:
std::string name = Decl->getNameAsString();

// Good (when StringRef is sufficient):
StringRef name = Decl->getName();  // No allocation

// Good (when you need std::string):
std::string name = Decl->getName().str();  // Explicit allocation
```

**Note:** `getNameAsString()` is needed for some decls (operators, destructors with special names), but most uses can switch to `getName()`.

**Effort:** Medium (4-6 hours)
**Priority:** P1

---

## Category D: O(n) Linear Scans (MEDIUM)

### Problem

Multiple O(n) scans that could be O(1) or O(log n):

1. **TypedEnv full scans** (15 instances):
   ```cpp
   for (const auto& [fieldPath, value] : TypedEnv) {
       if (pathStr.starts_with(prefix)) { ... }
   }
   ```
   Scans ALL entries to find those with a prefix.

2. ✅ **VarBoundProofs linear searches** - **FIXED** (DenseMap with VarBoundKey):
   Now uses `DenseMap<VarBoundKey, ProofOp>` for O(1) lookup by (varName, objPath, field).

3. ✅ **ProofStore linear find** - **FIXED** (SmallDenseSet):
   Now uses `SmallDenseSet<Proof, 4>` for O(1) has/add/remove operations.

### Fix
- Add secondary indices for prefix queries (e.g., `DenseMap<StringRef, SmallVector<FieldPath*>>`)
- ✅ Use `DenseMap` for VarBoundProofs with VarBoundKey - **DONE**
- ✅ Use `SmallDenseSet` for ProofStore proof sets - **DONE**

**Effort:** High (1-2 days per data structure)
**Priority:** P2 (profile first to confirm bottleneck)

---

## Category E: Unused LLVM Infrastructure (MEDIUM)

### Not Using

| Should Use | Currently Using | Benefit |
|------------|-----------------|---------|
| `SmallVector<T, N>` | `std::vector<T>` | Stack allocation for small cases |
| `SmallString<N>` | `std::string` | Stack allocation for small strings |
| `Twine` | `std::string +` concatenation | Zero-copy string building |
| ✅ `llvm::DenseSet` | `std::set` | Better cache locality |
| ✅ `llvm::StringSet` | `std::set<std::string>` | Optimized string storage |
| `ArrayRef<T>` | `const std::vector<T>&` | Non-owning view |

### Specific Instances

```cpp
// Current:
std::vector<FieldPath> collectPathsWithPrefix(...);  // Allocates

// Better:
void collectPathsWithPrefix(..., SmallVectorImpl<FieldPath>& result);  // No allocation
```

```cpp
// ✅ FIXED: Now uses DenseSet<WarningKey> with custom DenseMapInfo
llvm::DenseSet<WarningKey> WarnedLostTracking;
```

**Effort:** Medium (scattered changes)
**Priority:** P2

---

## Category F: Global State (MEDIUM)

### Problem

Massive global state makes the plugin non-reentrant:

```cpp
// At file scope:
ASTContext *GlobalCtx = nullptr;
DTTMode GlobalMode = DTTMode::Inform;
ProofStore GlobalProofs;
ProofContext GlobalProofContext(GlobalProofs);
llvm::DenseMap<FieldPath, SymValue> TypedEnv;
llvm::StringMap<std::vector<Postcondition>> FnPostconditions;
// ... 15+ more global maps
```

### Impact
- Can't run multiple analyses in parallel
- State leaks between translation units
- Testing requires careful state reset

### Fix
Encapsulate in an `AnalysisState` class passed to all functions:

```cpp
struct AnalysisState {
    ASTContext& Ctx;
    DTTMode Mode;
    ProofStore Proofs;
    DenseMap<FieldPath, SymValue> Env;
    // ...
};
```

**Effort:** High (architectural change)
**Priority:** P3 (works for current use case)

---

## Category G: Missing `const` (LOW)

### Problem

Many functions that don't modify state aren't marked `const`:

```cpp
// Methods that should be const:
bool hasProof(...);
SymValue getField(...);
std::optional<bool> checkConstraint(...);
```

### Impact
- Can't call from const contexts
- Intent unclear

**Effort:** Low (add `const` progressively)
**Priority:** P3

---

## Category H: Inefficient Parsing (LOW)

### Problem

`atoi()` and `std::stoll()` used for simple parsing:

```cpp
unsigned argIdx = std::atoi(argPart.c_str() + 3);  // Parsing "arg0" -> 0
```

### Impact
- `atoi` doesn't report errors
- Unnecessary conversion to C string

### Fix

```cpp
// Use StringRef parsing:
unsigned argIdx;
if (argPart.drop_front(3).getAsInteger(10, argIdx)) {
    // Handle error
}
```

**Effort:** Low
**Priority:** P3

---

## Category I: Missing `constexpr` (LOW)

### Problem

Static data that could be `constexpr`:

```cpp
std::map<std::string, std::vector<TemplateParamAlias>> TemplateAliases = {
    {"std::array",  {{1, "_size", std::nullopt}}},
    ...
};
```

### Progress

✅ **ProofOpStrings**: Consolidated 7 duplicate `const char* ops[]` into single `constexpr const char* ProofOpStrings[]`

⚠️ **TemplateAliases**: Would require C++20 constexpr std::string or major refactor to constexpr-friendly types

**Effort:** Low for simple arrays, Medium for map-like structures
**Priority:** P3

---

## Prioritized Action Plan

### Immediate (P0) - Do This Week
1. ✅ Convert `rfind(..., 0) == 0` → `starts_with()` (30+ instances) - **DONE**

### Short-term (P1) - Do This Month
2. ✅ Create typed `MethodKey` struct for method annotations lookup - **DONE**
3. ⚠️ Replace `getNameAsString()` with `getName()` where possible - **PARTIAL** (crashes with operators/special names)
4. ✅ Replace `atoi()`/`stoll()` with `StringRef::getAsInteger()` - **DONE**
5. ✅ Replace manual trim with `StringRef::trim()` - **DONE**
6. ✅ Use `SmallVector` for local temporary vectors - **DONE** (8 instances converted)

### Medium-term (P2) - Next Quarter
5. Add secondary indices for prefix queries
6. ✅ Replace `std::set` with `DenseSet`/`StringSet` - **DONE** (4 sets converted)
7. Use `Twine` for string building in diagnostics

### Long-term (P3) - When Refactoring
8. Encapsulate global state in `AnalysisState`
9. Add `const` to query methods
10. Use `constexpr` for static data

---

## Metrics

| Metric | Current | Target | Status |
|--------|---------|--------|--------|
| String concatenations for keys | 60+ | 0 | ✅ **0** (MethodKey struct) |
| `rfind(..., 0) == 0` | 30+ | 0 | ✅ **0** (converted to starts_with) |
| `getNameAsString()` calls | 80+ | <20 | ⚠️ **77** (blocked by special names) |
| Global variables | 15+ | 0 | - |
| `std::vector` (could be `SmallVector`) | 40+ | <10 | ✅ **~30** (8 local temps converted) |
| `std::set` (could be `DenseSet`) | 8 | 0 | ✅ **0** (all converted: TotalFunctions, WarnedLostTracking, paramDereferenced, enum sets, findIncrementedPointers, uniqueValues) |

---

## What's Actually Good

To be fair, recent work has improved things:

1. **FieldPath with hash caching** - Proper lazy caching pattern
2. **DenseMap for TypedEnv** - Correct container choice
3. **StringMap for string-keyed maps** - Appropriate LLVM container
4. **StringRef parameters** - Zero-copy where applicable
5. **ProofStore abstraction** - Clean separation of concerns
6. **DenseMapInfo specialization** - Correct sentinel key handling

The foundation is solid. The remaining issues are "cleanup" rather than "architectural".

---

## Conclusion

**Would this pass LLVM code review?** Not yet, but it's closer than before.

**Key blockers:**
1. String concatenation for keys (fix with typed keys)
2. `rfind` anti-pattern (trivial fix)
3. `getNameAsString()` proliferation (moderate fix)

**Realistic assessment:** 2-3 weeks of focused cleanup would bring this to LLVM-quality code. The recent DenseMap/StringMap/StringRef work was the right direction - now it's about consistency and completing the migration.
