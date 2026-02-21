# DTT Architectural Consolidation Analysis

*A systematic audit of duplication, inconsistency, and unification opportunities*

---

## Executive Summary

This analysis identifies **16 consolidation opportunities** in the DTT codebase, organized by impact and implementation complexity. The goal: transform DTT from a capable but accretive codebase into a beautiful work of software craft with unified abstractions and elegant design.

---

## Tier 1: Foundational Unifications (High Impact, Enables Everything Else)

### 1. Dual Path Representation Systems

**Current State:**
Two parallel path systems coexist throughout the codebase:

| System | Type | Usage Count | Example |
|--------|------|-------------|---------|
| String-based | `std::string`, `StringRef` | 150+ sites | `"v._size"` |
| Typed paths | `FieldPath` struct | 194 `getFieldTyped` calls | `FieldPath::fromDecl(VD).withSynthetic("_size")` |

Both are computed for the *same* expression in many places. See lines 1003-1008 where validation code ensures they stay in sync.

**The Smell:**
```cpp
// Computing path TWICE for every expression
std::string path = getExprPath(E);           // String version
FieldPath typedPath = getTypedPath(E);       // Typed version
// Then maintaining both in parallel...
```

**Unified Design:**
```cpp
// Single source of truth
FieldPath path = getPath(E);

// Lazy string conversion (cached after first call)
path.toString();  // Only computed when needed for diagnostics
```

**Impact:** Eliminates ~20% redundant computation, simplifies all path-handling code.

---

### 2. Annotation System Duplication

**Current State:**
Two separate annotation registries with nearly identical semantics:

```cpp
// DTTStdLib.cpp
llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;

// DTTCLib.cpp
llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;
```

Both have:
- Preconditions (different representations: `ConstraintExprPtr` vs `FreeFunctionPrecondition`)
- Postconditions (different representations: `Postcondition` vs `FreeFunctionPostcondition`)
- Scope-exit requirements (only in `FreeFunctionInfo`)

Lookup code duplicated in multiple places (lines 4137-4147, 7088-7091, 7437-7439).

**The Smell:**
Registration helpers are parallel but different:
```cpp
// DTTStdLib.cpp
auto addPre = [](const char* cls, const char* method, unsigned arity, ConstraintExprPtr pre) {...};

// DTTCLib.cpp
auto addFilePreNonnull = [](const char* fn, unsigned arity, int fileArgIdx, ...) {...};
```

**Unified Design:**
```cpp
// Single unified key type
struct FunctionKey {
    llvm::SmallString<64> qualifiedName;  // "std::vector" or "memcpy"
    unsigned numParams;
    bool isMethod;

    // Single hash function, single comparison
};

// Single annotation type
struct FunctionAnnotation {
    llvm::SmallVector<ConstraintExprPtr, 2> preconditions;
    llvm::SmallVector<Postcondition, 2> postconditions;
    llvm::SmallVector<ScopeExitRequirement, 2> scopeExitRequires;
    // All using the same representation!
};

// Single registry
llvm::DenseMap<FunctionKey, FunctionAnnotation> AllAnnotations;
```

**Impact:** Consolidates ~200 lines, enables shared constraint evaluation.

---

### 3. SymValue vs ConstraintExpr Duplication

**Current State:**
Two parallel AST representations for symbolic expressions:

| Type | Location | Purpose | Kinds |
|------|----------|---------|-------|
| `SymValue` | DTTTypes.h:39-292 | Value tracking | CONST, VAR, ADD, SUB, MUL... |
| `ConstraintExpr` | DTTTypes.h:298-459 | Preconditions | Literal, FieldRef, Add, CmpGT... |

Both implement the same arithmetic operations. `ConstraintExpr::toSymValue()` exists to convert between them (lines 441-458).

**The Smell:**
```cpp
// Same concept, two implementations
SymValue::Add(a, b);           // In SymValue
BinaryExpr(Add, left, right);  // In ConstraintExpr

// Then conversion overhead:
auto sym = constraintExpr->toSymValue();  // Why maintain two?
```

**Unified Design:**
```cpp
// SymValue becomes THE expression type
// Add comparison operators to SymValue
struct SymValue {
    enum Kind {
        CONST, VAR, ADD, SUB, MUL, DIV, MOD,
        // NEW: Comparisons (return boolean)
        CMP_GT, CMP_GE, CMP_LT, CMP_LE, CMP_EQ, CMP_NE,
        // NEW: Logical
        LOGICAL_AND, LOGICAL_OR, LOGICAL_NOT
    };

    // Comparison factory methods
    static SymValue CmpGT(SymValue a, SymValue b);
    static SymValue CmpGE(SymValue a, SymValue b);
    // ...
};

// ConstraintExpr becomes a thin pattern matcher over SymValue
// Or: eliminate ConstraintExpr entirely, use SymValue everywhere
```

**Impact:** Eliminates ~160 lines, unified evaluation pipeline.

---

## Tier 2: State & Tracking Consolidations (Medium-High Impact)

### 4. Precondition Evaluation Paths

**Current State:**
Five different code paths for precondition checking:

1. `checkSimpleFieldComparisonWithProofs()` - line 5034
2. `checkConstraintExprSimple()` - line 4997
3. Z3 solver fallback - implicit
4. Method preconditions - lines 7082-7100
5. Free function preconditions - lines 7102+ (different structure)

**The Smell:**
```cpp
// Method preconditions
for (const auto& pre : info.preconditions) {
    checkConstraintExprSimple(basePath, pre, argVals);
}

// Free function preconditions - DIFFERENT STRUCTURE
for (const auto& pre : finfo.preconditions) {
    // Completely different evaluation logic!
    if (pre.checkOp == ProofOp::NE && fieldVal == pre.forbiddenValue) {...}
}
```

**Unified Design:**
```cpp
// Single precondition representation (all use ConstraintExpr)
// Single evaluation function
std::optional<bool> evaluatePrecondition(
    const ConstraintExprPtr& pre,
    const FieldPath& basePath,
    const llvm::SmallVector<SymValue, 4>& argVals
);

// Evaluation pipeline: simple check → symbolic → Z3 fallback
```

**Impact:** Reduces precondition code by ~40%, consistent diagnostics.

---

### 5. Hardcoded Field Names

**Current State:**
109 occurrences of magic field name strings:

```cpp
"_size", "_nonnull", "_has_value", "_lifetime", "_variant_index",
"_size_bytes", "_base_length", "_offset", "_capacity",
"@open", "@allocated", "@fd", "@regex", "@dl", ...
```

Scattered across 50+ string comparisons. No schema defining what fields each type should have.

**The Smell:**
```cpp
// Typo-prone, no compile-time checking
bufSize = getFieldTyped(bufPath, "_size_bytes");
if (bufSize.kind == SymValue::UNKNOWN) {
    SymValue remainingElems = getFieldTyped(bufPath, "_size");  // Different field!
}
```

**Unified Design:**
```cpp
// Type-safe field identifiers
enum class FieldID : uint8_t {
    Size,           // Element count
    SizeBytes,      // Byte count
    Nonnull,        // Pointer validity
    HasValue,       // Optional engagement
    Lifetime,       // Allocation state
    VariantIndex,   // std::variant discriminator
    Offset,         // Pointer arithmetic offset
    BaseLength,     // Original container size
    Capacity,       // Allocated capacity
    // Ghost fields (resource tracking)
    GhostOpen,      // @open
    GhostAllocated, // @allocated
    GhostFd,        // @fd
};

// Field metadata registry
struct FieldMeta {
    const char* name;        // "_size"
    const char* display;     // "size"
    enum { Count, Bytes, Bool, Index } kind;
    bool isGhost;
};

constexpr std::array<FieldMeta, 12> FieldRegistry = {{
    {"_size", "size", Count, false},
    {"_size_bytes", "size in bytes", Bytes, false},
    // ...
}};

// Type-safe access
SymValue getField(const FieldPath& path, FieldID field);
void setField(const FieldPath& path, FieldID field, SymValue val);
```

**Impact:** Compile-time field validation, enables IDE support, eliminates typo bugs.

---

### 6. Field Invalidation Functions

**Current State:**
Four nearly-identical invalidation functions:

| Function | Lines | Purpose |
|----------|-------|---------|
| `invalidatePropertiesWithProvenance()` | 3567 | Marks as lost, records reason |
| `invalidateWrittenFieldsOnly()` | 3595 | Only invalidate certain fields |
| `invalidateProperties()` | 3684 | Generic invalidation |
| `clearProperties()` | 3922 | Remove entirely |

**The Smell:**
Easy to call wrong function, subtle bugs from inconsistent behavior.

**Unified Design:**
```cpp
enum class InvalidationReason : uint8_t {
    UnknownWrite,      // Unknown function call
    FunctionParameter, // Passed to function
    MemoryEscape,      // Pointer exposed
    ReassignedLost,    // Assigned unknown value
    ScopeExit          // Variable goes out of scope
};

struct InvalidationPolicy {
    InvalidationReason reason;
    SourceLocation where;
    llvm::StringRef explanation;
    std::optional<llvm::SmallVector<FieldID, 4>> fieldsToInvalidate;  // nullopt = all
    bool recordProvenance;
};

// SINGLE entry point
void invalidateFields(const FieldPath& path, const InvalidationPolicy& policy);
```

**Impact:** Single point of control, eliminates subtle bugs.

---

### 7. Resource Tracking Fragmentation

**Current State:**
Three separate resource tracking mechanisms:

```cpp
// 1. Pointer lifetime (lines 106-114)
llvm::StringMap<PointerLifetimeState> PointerLifetimes;

// 2. Scope-exit constraints (line 2687 area)
llvm::StringMap<llvm::SmallVector<ScopeExitRequirement, 2>> FnScopeExitRequires;

// 3. Ghost fields (@open, @allocated) stored as regular fields in TypedEnv
```

**The Smell:**
Same concept (resource lifecycle) in three different data structures.

**Unified Design:**
```cpp
enum class ResourceKind : uint8_t {
    Heap,           // malloc/free, new/delete
    File,           // fopen/fclose
    FileDescriptor, // open/close
    Semaphore,      // sem_init/sem_destroy
    DynamicLib,     // dlopen/dlclose
    Regex,          // regcomp/regfree
};

struct ResourceState {
    ResourceKind kind;
    FieldID stateField;           // Which field tracks this (@open, @allocated)
    int64_t acquiredValue;        // Value when acquired (1)
    int64_t releasedValue;        // Value when released (0)
    SourceLocation acquiredAt;
    std::optional<SourceLocation> releasedAt;
};

// Single unified map
llvm::DenseMap<FieldPath, ResourceState, FieldPathHash> Resources;
```

**Impact:** Unified resource checking, easier to add new resource types.

---

## Tier 3: Pattern Consolidations (Medium Impact)

### 8. Guard Detection Structures

**Current State:**
Seven different guard info structures (lines 9932-12276):

```cpp
struct GuardInfo { ... };           // Bounds check guards
struct VarGuardInfo { ... };        // Variable guards
struct PreIncDecGuardInfo { ... };  // Pre/post increment guards
struct BoolCallInfo { ... };        // Bool-returning method guards
struct HoldsAlternativeInfo { ... };// Variant type guards
struct AnyTypeCheckInfo { ... };    // std::any guards
struct InPlaceTagInfo { ... };      // In-place construction tags
```

Each implements similar pattern: condition → guarded path → proven field.

**The Smell:**
Bug fix in guard reasoning requires updates in 7 places.

**Unified Design:**
```cpp
enum class GuardKind : uint8_t {
    BoundsCheck,      // i < v.size()
    Nullability,      // ptr != nullptr
    BoolResult,       // v.empty() → _size == 0
    TypeCheck,        // holds_alternative<T>(v)
    EngagementCheck,  // opt.has_value()
    PreIncrement,     // ++i < n
    PostIncrement,    // i++ < n
};

struct Guard {
    GuardKind kind;
    const Expr* condition;
    FieldPath guardedPath;
    FieldID guardedField;
    int64_t establishedValue;
    bool negated;  // For else-branches

    // Extract proof fact from this guard
    std::optional<Proof> toProof() const;
};

// Single detection function
llvm::SmallVector<Guard, 4> detectGuards(const Expr* condition);

// Single application function
void applyGuardProofs(const llvm::SmallVector<Guard, 4>& guards, bool inElseBranch);
```

**Impact:** Consolidates ~300 lines, trivial to add new guard types.

---

### 9. Nullability Inference Duplication

**Current State:**
Multiple overlapping nullability inference functions:

| Function | Location | Purpose |
|----------|----------|---------|
| `inferNonnullFromExpr()` | 2161-2238 | General nullability |
| `getParamNullCheckIndex()` | 5168-5217 | Parameter null detection |
| `isCallArgNonnull()` | 5294+ | Call argument nullability |
| `varNullability` map | 2743 | Stored nullability facts |

**The Smell:**
Same patterns (nullptr literal, new expression, address-of) reimplemented in each.

**Unified Design:**
```cpp
enum class Nullability : uint8_t {
    Null,       // Definitely null
    NonNull,    // Definitely non-null
    Unknown     // Could be either
};

struct NullabilityFact {
    Nullability state;
    SourceLocation provenAt;
    llvm::StringRef reason;  // "result of new", "address-of", etc.
};

// Single inference function
NullabilityFact inferNullability(const Expr* E);

// Cached facts (computed lazily, reused)
llvm::DenseMap<FieldPath, NullabilityFact, FieldPathHash> NullabilityCache;
```

**Impact:** Eliminates ~200 lines of duplicate logic.

---

### 10. Postcondition System Duplication

**Current State:**
Two postcondition systems:

```cpp
// Explicit (from annotations)
struct Postcondition {
    PostconditionTarget target;
    int argIndex;
    std::string field;
    SymValue expr;
    bool usesOld;
};

// Inferred (from analysis)
struct InferredPostcondition {
    std::string field;
    SymValue expr;
    bool usesOld;
    std::optional<unsigned> exprArgIndex;
    std::optional<unsigned> targetArgIndex;
};
```

Different storage mechanisms (`MethodInfo.ensures` vs `InferredFnPostconditions` map).

**Unified Design:**
```cpp
struct Postcondition {
    PostconditionTarget target;
    std::optional<unsigned> argIndex;  // Which arg (if target == Arg)
    FieldID field;
    SymValue expr;
    bool usesOld;
    bool isInferred;  // Distinguish explicit vs inferred
    SourceLocation inferredFrom;  // Where we inferred this
};

// Single storage, single evaluation
```

**Impact:** Simplifies postcondition handling, cleaner inference pipeline.

---

## Tier 4: Diagnostics & Reporting (Medium Impact)

### 11. Error Reporting Inconsistencies

**Current State:**
- 74 diagnostic emission sites using `DiagnosticsEngine::Error` or `Warning`
- Some use `ModeDependentDiag` (category C/D handling), others don't
- Inconsistent note ordering
- Inconsistent provenance tracking

**The Smell:**
```cpp
// Some errors have rich context
DE.Report(Loc, DiagCallNull) << path;
DE.Report(NullLoc, NoteNullSetHere);

// Others are bare
DE.Report(Loc, DiagBoundsCheck);  // No supporting notes
```

**Unified Design:**
```cpp
enum class DiagCategory : uint8_t {
    Safety,     // Category F/G - always error
    Tracking,   // Category C - lost tracking
    Solver,     // Category D - undecidable
    Resource,   // Resource leak
};

struct DiagnosticTemplate {
    DiagCategory category;
    unsigned primaryDiagId;
    llvm::SmallVector<unsigned, 3> supportingNotes;
    bool requiresProvenance;
};

// Diagnostic catalog
const llvm::StringMap<DiagnosticTemplate> Diagnostics = {
    {"null_deref", {Safety, DiagNull, {NoteNullSetHere, NoteNullNotChecked}, true}},
    {"bounds_violation", {Safety, DiagBounds, {NoteSizeSetHere, NoteIndexValue}, true}},
    // ...
};

// Single emission function
void emitDiagnostic(llvm::StringRef key, SourceLocation loc,
                    const llvm::StringMap<std::string>& context);
```

**Impact:** Consistent user experience, maintainable diagnostic system.

---

### 12. Operator Representation Inconsistency

**Current State:**
```cpp
// Enum
enum class ProofOp { GT, GE, LT, LE, EQ, NE };

// String array (must match enum order!)
constexpr const char* ProofOpStrings[] = {">", ">=", "<", "<=", "==", "!="};

// Parsing (repeated for all 6 operators)
if ((opPos = key.find(">=")) != std::string::npos) { opLen = 2; foundOp = ProofOp::GE; }
else if ((opPos = key.find("<=")) != std::string::npos) { opLen = 2; foundOp = ProofOp::LE; }
// ...
```

**Unified Design:**
```cpp
struct OpInfo {
    const char* symbol;
    const char* name;
    int precedence;
    bool (*evaluate)(int64_t, int64_t);
};

constexpr std::array<OpInfo, 6> OpTable = {{
    {">",  "GT", 10, [](int64_t a, int64_t b) { return a > b; }},
    {">=", "GE", 10, [](int64_t a, int64_t b) { return a >= b; }},
    {"<",  "LT", 10, [](int64_t a, int64_t b) { return a < b; }},
    {"<=", "LE", 10, [](int64_t a, int64_t b) { return a <= b; }},
    {"==", "EQ", 9,  [](int64_t a, int64_t b) { return a == b; }},
    {"!=", "NE", 9,  [](int64_t a, int64_t b) { return a != b; }},
}};

// Safe conversion with compile-time guarantees
constexpr const char* opSymbol(ProofOp op) { return OpTable[static_cast<int>(op)].symbol; }
std::optional<ProofOp> parseOp(llvm::StringRef symbol);
bool evaluateOp(ProofOp op, int64_t lhs, int64_t rhs);
```

**Impact:** Compile-time safety, eliminates array indexing bugs.

---

## Tier 5: Architecture & Organization (Longer-term)

### 13. Visitor Pattern Fragmentation

**Current State:**
Multiple AST visitors with interleaved concerns:

```cpp
class AnalysisCollector : public RecursiveASTVisitor<AnalysisCollector> {
    // Pass 1: Collect call sites, returns
};

class DTTVisitor : public RecursiveASTVisitor<DTTVisitor> {
    // Everything else: field tracking, error reporting, inference
    // 5000+ lines mixing:
    //   - Data collection
    //   - Value tracking
    //   - Constraint checking
    //   - Guard detection
    //   - Diagnostic emission
};
```

**Unified Design:**
```cpp
// Clear separation of concerns
class DataCollectionPass {
    // Just collect facts, no diagnostics
    void collectCallSites(const FunctionDecl* F);
    void collectFieldAccesses(const FunctionDecl* F);
};

class ValueTrackingPass {
    // Track field values through control flow
    void trackValues(const FunctionDecl* F);
};

class ConstraintCheckingPass {
    // Check constraints, emit diagnostics
    void checkPreconditions(const CallExpr* CE);
    void checkBoundsAccess(const ArraySubscriptExpr* ASE);
};

class InferencePass {
    // Infer preconditions/postconditions
    void inferFromImplementation(const FunctionDecl* F);
};

// Pipeline orchestration
void analyzeFunction(const FunctionDecl* F) {
    DataCollectionPass().run(F);
    ValueTrackingPass().run(F);
    ConstraintCheckingPass().run(F);
    InferencePass().run(F);
}
```

**Impact:** Testable in isolation, cleaner control flow.

---

### 14. Type-Specific Special Cases

**Current State:**
Hardcoded type checks scattered throughout:

```cpp
if (className.find("vector") != std::string::npos) { ... }
if (className.find("optional") != std::string::npos) { ... }
if (CE->getConstructor()->getParent()->getName() == "string") { ... }
```

**Unified Design:**
```cpp
// Type traits registry
struct TypeTraits {
    enum Concept { ContainerLike, OptionalLike, SmartPointerLike, StringLike };
    llvm::SmallVector<Concept, 2> concepts;

    struct FieldMapping {
        FieldID field;
        int templateParamIdx;  // -1 if not from template
    };
    llvm::SmallVector<FieldMapping, 4> fields;

    struct ConstructorSemantics {
        unsigned paramIdx;
        FieldID inferredField;
    };
    llvm::SmallVector<ConstructorSemantics, 2> constructors;
};

const llvm::StringMap<TypeTraits> LibraryTypes = {
    {"std::vector", {{ContainerLike}, {{Size, -1}}, {}}},
    {"std::optional", {{OptionalLike}, {{HasValue, -1}}, {}}},
    {"std::string", {{ContainerLike, StringLike}, {{Size, -1}}, {{0, Size}}}},
    // ...
};

// Single lookup
std::optional<TypeTraits> getTypeTraits(const CXXRecordDecl* RD);
```

**Impact:** Easy to add new library types, auditable in one place.

---

### 15. Z3 Integration Optimization

**Current State:**
```cpp
// Fresh context per check
z3::context z3ctx;
z3::solver solver(z3ctx);
```

No result caching, no timeout protection.

**Unified Design:**
```cpp
class Z3Manager {
    thread_local static z3::context ctx;

    struct CacheEntry {
        z3::check_result result;
        std::chrono::steady_clock::time_point timestamp;
    };
    llvm::DenseMap<size_t, CacheEntry> cache;  // Hash of constraint → result

public:
    std::optional<bool> check(const SymValue& constraint, unsigned timeoutMs = 100);
    void clearCache();
};
```

**Impact:** Performance improvement for complex constraints.

---

### 16. Dual Precondition Storage

**Current State:**
Preconditions stored in two places:

```cpp
// In annotation info
FreeFunctionInfo {
    SmallVector<FreeFunctionPrecondition, 2> preconditions;      // Simple
    SmallVector<ConstraintExprPtr, 2> richPreconditions;          // Rich
};

// In function-level storage
DenseMap<const FunctionDecl*, SmallVector<InferredPrecondition, 4>> FnPreconditions;
```

**Unified Design:**
```cpp
// All preconditions use ConstraintExpr
// Convert simple checks at registration time
// Single storage location per function
```

**Impact:** Eliminates synchronization overhead.

---

## Implementation Roadmap

### Phase 1: Foundation (P0)
*Unblocks everything else*

| Item | Effort | Dependencies |
|------|--------|--------------|
| 1. Unify path systems | 2 weeks | None |
| 2. Unify annotation registries | 1 week | None |
| 3. Merge SymValue/ConstraintExpr | 2 weeks | #2 |

### Phase 2: State Management (P1)
*Builds on foundation*

| Item | Effort | Dependencies |
|------|--------|--------------|
| 4. Unify precondition evaluation | 1 week | #2, #3 |
| 5. Field ID enum | 1 week | None |
| 6. Consolidate invalidation | 3 days | #5 |
| 7. Unify resource tracking | 1 week | #5, #6 |

### Phase 3: Patterns (P1)
*Can parallelize with Phase 2*

| Item | Effort | Dependencies |
|------|--------|--------------|
| 8. Unify guard detection | 1 week | #5 |
| 9. Consolidate nullability | 3 days | #5 |
| 10. Merge postcondition types | 3 days | #3 |

### Phase 4: Polish (P2)
*After core consolidations*

| Item | Effort | Dependencies |
|------|--------|--------------|
| 11. Diagnostic catalog | 1 week | None |
| 12. Operator table | 2 days | None |
| 13. Visitor separation | 2 weeks | All above |
| 14. Type traits registry | 1 week | #5 |
| 15. Z3 caching | 3 days | None |
| 16. Precondition unification | 3 days | #3 |

---

## Metrics for Success

After consolidation:

| Metric | Before | Target |
|--------|--------|--------|
| Lines of code | ~15,000 | ~11,000 (-25%) |
| Path computation calls | 2x | 1x |
| Precondition eval paths | 5 | 1 |
| Guard detection structs | 7 | 1 |
| Field name string literals | 109 | 0 |
| Diagnostic inconsistencies | Many | 0 |

---

## Principles for Beautiful Code

1. **Single Source of Truth** - One representation per concept
2. **Typed Over Stringly** - Enums and structs over magic strings
3. **Unified Pipelines** - One path through the code, not five
4. **Explicit Contracts** - Clear ownership, clear lifetimes
5. **Testable Isolation** - Each concern separately testable
6. **Zero Allocation Hot Paths** - LLVM style throughout

*"The difference between a tolerable programmer and a great programmer is not how many programming languages they know... It's whether they can communicate their ideas. By persuading other people, they get leverage." — DHH*

*But also: "The code itself is the best documentation of what the code does." — Let the structure speak.*

---

# Part II: Annotation Content Consolidation

*The infrastructure is one thing. The actual annotations are another.*

The previous analysis focused on **how** annotations are stored and evaluated. This section focuses on **what** is annotated and the semantic inconsistencies in the annotation data itself.

---

## The Annotation Landscape Today

### C Library (DTTCLib.cpp)
- 601 lines
- ~50 annotated functions
- Uses: `FreeFunctionPrecondition` (simple), `richPreconditions` (recent)
- Focus: FILE* null checks, use-after-free, resource tracking

### C++ Standard Library (DTTStdLib.cpp)
- 407 lines
- ~200 annotated methods across 25+ container types
- Uses: `ConstraintExprPtr` throughout
- Focus: Bounds checking, size tracking, optional engagement

### The Problem
These two annotation sets were developed separately, use different mechanisms, and have **semantic gaps** that prevent powerful cross-library reasoning.

---

## Tier A: Semantic Field Inconsistencies (Critical)

### A1. The `_nonnull` Overload Problem

**Current State:**
`_nonnull` is used for three distinct concepts:

| Context | Meaning | Example |
|---------|---------|---------|
| Raw pointers | Pointer is not NULL | `int* p; p._nonnull` |
| FILE* handles | File handle is valid | `FILE* f; f._nonnull` |
| Smart pointers | Smart pointer owns object | `unique_ptr._nonnull` |

**The Problem:**
```cpp
// DTTCLib.cpp line 19
info.preconditions.push_back({fileArgIdx, "_nonnull", 0, ProofOp::NE,
    "FILE* may be null (fopen can fail)"});

// DTTStdLib.cpp line 393
addPostConst("unique_ptr", cls, 0, "_nonnull", 0);  // Default constructor
addPre(cls, "operator*", 0, nonnull());             // Dereference check
```

These are **semantically different**:
- FILE* `_nonnull` means "fopen succeeded"
- Smart pointer `_nonnull` means "owns a pointee"
- Raw pointer `_nonnull` means "not nullptr"

**Unified Design:**
```cpp
// Distinct field IDs for distinct concepts
enum class FieldID {
    // Pointer validity (raw pointers)
    Nonnull,           // ptr != nullptr

    // Handle validity (FILE*, fd, etc.)
    HandleValid,       // fopen succeeded, handle usable

    // Ownership state (smart pointers)
    OwnsObject,        // unique_ptr/shared_ptr owns pointee
};

// Clear semantics in annotations
// FILE*:
info.preconditions.push_back({arg, FieldID::HandleValid, ...});

// unique_ptr:
addPre(cls, "operator*", 0, ownsObject());
```

**Impact:** Eliminates semantic confusion, enables targeted diagnostics.

---

### A2. The `_lifetime` vs `@allocated` Split

**Current State:**
Two overlapping concepts for memory validity:

```cpp
// DTTCLib.cpp line 153 - Use-after-free detection
info.preconditions.push_back({
    idx, "_lifetime", 0, ProofOp::NE,
    "pointer was freed (use-after-free)", true
});

// DTTCLib.cpp line 272 - Resource tracking
addResourceAcquire("malloc", 1, "@allocated", 1, 0);
```

| Field | Meaning | Used For |
|-------|---------|----------|
| `_lifetime` | Memory is accessible | Use-after-free detection |
| `@allocated` | Memory was allocated | Leak detection |

**The Problem:**
- `_lifetime` uses non-ghost field, `@allocated` uses ghost field
- Checking freed memory uses `_lifetime`, releasing uses `@allocated`
- `free()` sets `@allocated=0` but plugin also invalidates `_lifetime`
- No connection between them!

**Unified Design:**
```cpp
// Single memory state field
enum class FieldID {
    MemoryState,  // Replaces both _lifetime and @allocated
};

enum class MemoryStateValue {
    Unknown = 0,
    Allocated = 1,    // malloc succeeded
    Freed = 2,        // free called
    Invalid = 3,      // dangling/escaped
};

// malloc: result.MemoryState = Allocated
// free: precondition MemoryState == Allocated, postcondition MemoryState = Freed
// Use-after-free: access where MemoryState != Allocated
```

**Impact:** Unified memory tracking, no redundant fields.

---

### A3. The `_has_value` Polymorphism

**Current State:**
`_has_value` used for four different type families:

```cpp
// std::optional (DTTStdLib.cpp line 336)
addPostConst("optional", "optional", 0, "_has_value", 0);

// std::variant (line 352) - WRONG SEMANTIC!
addBoolPost("variant", "valueless_by_exception", "_has_value", 0, 1);

// std::any (line 357)
addPostConst("any", "any", 0, "_has_value", 0);

// std::expected (line 373)
addPostConst("expected", "expected", 0, "_has_value", 1);
```

**The Problem:**
- `optional`: `_has_value=1` means engaged
- `variant`: `_has_value=0` means valueless_by_exception (inverted!)
- `expected`: `_has_value=1` means has value (not error)
- `any`: `_has_value=1` means holds a value

These have **different semantics** shoehorned into one field!

**Unified Design:**
```cpp
enum class FieldID {
    // For std::optional, std::any
    Engaged,          // Contains a value

    // For std::variant
    VariantValid,     // Not valueless_by_exception
    VariantIndex,     // Which alternative (already exists)

    // For std::expected
    HasValue,         // Has value (vs error)
};

// Clear per-type semantics
addPostConst("optional", "optional", 0, FieldID::Engaged, 0);
addPostConst("variant", "variant", 0, FieldID::VariantValid, 1);  // Default is valid
addPostConst("expected", "expected", 0, FieldID::HasValue, 1);
```

**Impact:** Correct semantics, no confusion in diagnostics.

---

### A4. The FILE* Mode Tracking Gap

**Current State:**
FILE* has partial mode tracking:

```cpp
// DTTCLib.cpp line 37-39
info.preconditions.push_back({
    fileArgIdx, "_readable", 0, ProofOp::NE,
    "FILE* not opened for reading"
});

// line 51-53
info.preconditions.push_back({
    fileArgIdx, "_writable", 0, ProofOp::NE,
    "FILE* not opened for writing"
});
```

**The Problem:**
- `_readable` and `_writable` are checked but **never set**!
- `fopen` doesn't establish these fields based on mode string
- No postcondition connects `fopen("r")` to `_readable=1`

**Missing:**
```cpp
// What should happen for fopen:
// fopen(path, "r")  → result._readable=1, result._writable=0
// fopen(path, "w")  → result._readable=0, result._writable=1
// fopen(path, "r+") → result._readable=1, result._writable=1
// fopen(path, "a")  → result._readable=0, result._writable=1
```

**Unified Design:**
```cpp
// Add mode inference to fopen annotation
// This requires runtime string analysis OR compile-time literal detection

// For string literals:
if (modeArg is StringLiteral) {
    StringRef mode = modeArg->getString();
    bool readable = mode.contains('r') || mode.contains('+');
    bool writable = mode.contains('w') || mode.contains('a') || mode.contains('+');
    setField(result, FieldID::Readable, readable ? 1 : 0);
    setField(result, FieldID::Writable, writable ? 1 : 0);
}
```

**Impact:** Makes FILE* mode checking actually work.

---

## Tier B: Missing Postconditions (High Value)

### B1. C Functions Return Values Are Untracked

**Current State:**
Most C functions have no postconditions for their return values:

| Function | Returns | Currently Tracked? |
|----------|---------|-------------------|
| `strlen(s)` | Length of s | ❌ |
| `malloc(n)` | Buffer of n bytes | ❌ (only @allocated) |
| `strdup(s)` | Copy of s | ❌ (only @allocated) |
| `snprintf(buf,n,...)` | Chars written | ❌ |
| `fread(buf,sz,n,f)` | Items read | ❌ |
| `read(fd,buf,n)` | Bytes read | ❌ |

**Compare to C++ (DTTStdLib.cpp line 160):**
```cpp
addReturns("vector", "size", "_size");  // v.size() returns _size
```

**Missing for C:**
```cpp
// strlen: returns string length
// Postcondition: result == arg0._size (for tracked strings)
{
    FreeFunctionInfo info;
    info.resultPostconditions.push_back({FieldID::Value, SymValue::Var("arg0._size")});
    FreeFunctionAnnotations[{"strlen", 1}] = info;
}

// malloc: returns buffer with _size_bytes
// Postcondition: result._size_bytes == arg0
{
    FreeFunctionInfo info;
    info.resultPostconditions.push_back({FieldID::SizeBytes, SymValue::Var("arg0")});
    info.resultPostconditions.push_back({"@allocated", 1});
    info.scopeExitRequires.push_back({"@allocated", ProofOp::EQ, 0});
    FreeFunctionAnnotations[{"malloc", 1}] = info;
}

// strdup: returns allocated copy
// Postcondition: result._size == strlen(arg0), result._size_bytes == strlen(arg0)+1
{
    FreeFunctionInfo info;
    info.resultPostconditions.push_back({FieldID::Size, SymValue::Var("arg0._size")});
    info.resultPostconditions.push_back({FieldID::SizeBytes,
        SymValue::Add(SymValue::Var("arg0._size"), SymValue::Const(1))});
    info.resultPostconditions.push_back({"@allocated", 1});
    FreeFunctionAnnotations[{"strdup", 1}] = info;
}
```

**Impact:** Enables buffer size tracking through C code.

---

### B2. Buffer Size Propagation Gaps

**Current State:**
C++ containers track size through operations:

```cpp
// DTTStdLib.cpp line 194
addSizeIncrement("vector", "push_back", 1);  // _size = old:_size + 1
```

But C buffer operations don't:

| Operation | What Should Happen |
|-----------|-------------------|
| `memcpy(dst, src, n)` | `dst._written_bytes = n` |
| `snprintf(buf, n, fmt, ...)` | `buf._written_bytes = result` |
| `strcat(dst, src)` | `dst._size = old:dst._size + src._size` |
| `realloc(ptr, n)` | `result._size_bytes = n` |

**Unified Design:**
```cpp
// memcpy postcondition: track bytes written
{
    FreeFunctionInfo info;
    info.richPreconditions.push_back(sizeGE(0, "_size_bytes", 2));
    info.richPreconditions.push_back(sizeGE(1, "_size_bytes", 2));
    // NEW: Track what was written
    Postcondition post;
    post.target = PostconditionTarget::Arg;
    post.argIndex = 0;
    post.field = "_initialized_bytes";
    post.expr = SymValue::Var("arg2");
    info.ensures.push_back(post);
    FreeFunctionAnnotations[{"memcpy", 3}] = info;
}
```

**Impact:** Tracks data flow through C functions.

---

### B3. The snprintf Return Value

**snprintf is special:** It returns the number of characters that *would* be written (excluding null), even if truncated.

```cpp
// Current (DTTCLib.cpp line 568-573)
for (unsigned arity = 3; arity <= 8; ++arity) {
    FreeFunctionInfo info;
    info.richPreconditions.push_back(sizeGE(0, "_size_bytes", 1));
    info.richPreconditions.push_back(nonnull(2));
    FreeFunctionAnnotations[{"snprintf", arity}] = info;
}
// NO POSTCONDITION FOR RETURN VALUE!
```

**What's Missing:**
```cpp
// snprintf returns: min(actual_length, n-1) written to buffer
// Postcondition: buffer has result bytes written (if result < n)
// This enables:
//   char buf[100];
//   int n = snprintf(buf, sizeof(buf), "%s", str);
//   // Now buf._initialized_bytes == min(n, 99)
```

---

## Tier C: Registration Pattern Unification (Medium)

### C1. Shared Constraint Factories

**Current State:**
C++ has reusable factories:

```cpp
// DTTStdLib.cpp lines 22-44
auto boundsCheck = []() -> ConstraintExprPtr { ... };
auto nonEmpty = []() -> ConstraintExprPtr { ... };
auto hasValue = []() -> ConstraintExprPtr { ... };
auto nonnull = []() -> ConstraintExprPtr { ... };
```

C has different, non-shared factories:

```cpp
// DTTCLib.cpp lines 498-509
auto sizeGE = [](unsigned argIdx, const char* field, unsigned countArg) { ... };
auto nonnull = [](unsigned argIdx) { ... };  // Different signature!
```

**The Problem:**
- `nonnull()` in C++ takes no args (field on `this`)
- `nonnull(idx)` in C takes arg index
- Can't share constraint factories between C and C++

**Unified Design:**
```cpp
// Shared constraint factory namespace
namespace dtt::constraints {

// Base check: field op value
ConstraintExprPtr fieldCheck(FieldID field, ProofOp op, int64_t value);

// For methods (implicit this)
ConstraintExprPtr thisFieldGT(FieldID field, int64_t value);
ConstraintExprPtr thisFieldNE(FieldID field, int64_t value);

// For functions (explicit arg index)
ConstraintExprPtr argFieldGT(unsigned argIdx, FieldID field, int64_t value);
ConstraintExprPtr argFieldGE(unsigned argIdx, FieldID field, unsigned otherArg);

// Common patterns
ConstraintExprPtr nonnull();                    // this._nonnull != 0
ConstraintExprPtr argNonnull(unsigned idx);     // argN._nonnull != 0
ConstraintExprPtr boundsCheck();                // arg0 < this._size
ConstraintExprPtr bufferFits(unsigned bufArg, unsigned sizeArg);  // argN._size_bytes >= argM

}

// Usage in both libraries:
addPre("vector", "operator[]", 1, constraints::boundsCheck());
addPre("memcpy", 3, constraints::bufferFits(0, 2));
```

**Impact:** Single set of constraint factories, shared across C and C++.

---

### C2. Table-Driven Registration

**Current State:**
Registration is imperative with lots of loops:

```cpp
// DTTStdLib.cpp lines 144-152
for (const char* cls : {"vector", "deque", "list", "forward_list",
                        "set", "multiset", "map", "multimap", ...}) {
    addPostConst(cls, cls, 0, "_size", 0);
    addBoolPost(cls, "empty", "_size", 0, 1);
    addPostConst(cls, "clear", 0, "_size", 0);
}
```

```cpp
// DTTCLib.cpp lines 123-145
static constexpr LifetimeEntry lifetimeChecks[] = {
    {"strcpy", 2, {0, 1}},
    {"strncpy", 3, {0, 1}},
    ...
};
```

**Unified Design:**
```cpp
// Single declarative table for all annotations
struct FunctionSpec {
    const char* name;
    unsigned arity;
    bool isMethod;
    const char* className;  // For methods

    // Preconditions as array of specs
    struct PreSpec { unsigned argIdx; FieldID field; ProofOp op; int64_t value; };
    std::initializer_list<PreSpec> preconditions;

    // Postconditions as array of specs
    struct PostSpec { PostconditionTarget target; unsigned argIdx; FieldID field; /* expr */ };
    std::initializer_list<PostSpec> postconditions;

    // Resource tracking
    struct ResourceSpec { FieldID field; int64_t acquireVal; int64_t releaseVal; };
    std::optional<ResourceSpec> resource;
};

// Single table for everything
constexpr FunctionSpec AllSpecs[] = {
    // C++ containers
    {"operator[]", 1, true, "vector", {{0, Size, LT, Arg0}}, {}, {}},
    {"push_back", 1, true, "vector", {}, {{Result, 0, Size, OldPlus1}}, {}},

    // C functions
    {"malloc", 1, false, nullptr, {}, {{Result, 0, SizeBytes, Arg0}}, {Allocated, 1, 0}},
    {"memcpy", 3, false, nullptr, {{0, SizeBytes, GE, Arg2}, {1, SizeBytes, GE, Arg2}}, {}, {}},
    {"fopen", 2, false, nullptr, {}, {{Result, 0, HandleValid, 1}}, {Open, 1, 0}},
};

// Single registration function
void registerAllAnnotations() {
    for (const auto& spec : AllSpecs) {
        FunctionAnnotation ann = buildAnnotation(spec);
        AllAnnotations[makeKey(spec)] = ann;
    }
}
```

**Impact:** All annotations in one auditable table.

---

## Tier D: Cross-Library Connections (High Value)

### D1. String Literal → C Function Size

**Current State:**
String literals have known size, but C functions don't use it:

```cpp
// In plugin: string literal "hello" has _size=5, _size_bytes=6
const char* s = "hello";

// But strlen(s) returns UNKNOWN because:
// 1. strlen has no postcondition
// 2. Even if it did, s._size isn't connected to return value
```

**What Should Happen:**
```cpp
const char* s = "hello";     // s._size = 5
size_t len = strlen(s);       // len = s._size = 5 (KNOWN!)
char buf[10];
strncpy(buf, s, len);         // Safe: buf._size_bytes(10) >= len(5)
```

**Unified Design:**
```cpp
// strlen postcondition
{
    FreeFunctionInfo info;
    // Return value equals arg0's size
    info.resultExpr = SymValue::Var("arg0._size");
    FreeFunctionAnnotations[{"strlen", 1}] = info;
}
```

**Impact:** String literal sizes flow through strlen, strcpy, etc.

---

### D2. Container → C Function Bridging

**Current State:**
Recent addition bridges container.data() to buffer size:

```cpp
// In DTTPlugin.cpp (recently added)
if (methodName == "data" || methodName == "c_str") {
    // Bridge container _size to buffer _size_bytes
}
```

**But C functions don't leverage this:**
```cpp
std::vector<int> v(100);
memset(v.data(), 0, v.size() * sizeof(int));  // Is this checked?

// v.data()._size_bytes should be 100 * 4 = 400
// memset checks arg0._size_bytes >= arg2
// This SHOULD work, but does it?
```

**What's Missing:**
- The bridging is in plugin code, not annotations
- Should be expressible in annotation system
- Needs proper documentation of what's connected

**Unified Design:**
```cpp
// In type traits, express the bridging relationship
TypeTraits["vector"] = {
    .concepts = {ContainerLike},
    .methods = {
        {"data", {
            .returns = {FieldID::Pointer},
            .bridgesFrom = {
                {FieldID::SizeBytes, "this._size * sizeof(element)"},
                {FieldID::BaseLength, "this._size"},
            }
        }},
    }
};
```

---

### D3. fread/fwrite Size Computation

**Current State:**
```cpp
// fread(buf, size, count, file) reads size*count bytes
// fwrite(buf, size, count, file) writes size*count bytes

// DTTCLib.cpp line 105-107
addFilePreRead("fread", 4, 3);   // Only checks FILE* is readable
addFilePreWrite("fwrite", 4, 3); // Only checks FILE* is writable
```

**What's Missing:**
```cpp
// fread precondition: buf._size_bytes >= size * count (arg1 * arg2)
// fwrite precondition: buf._size_bytes >= size * count

{
    FreeFunctionInfo info;
    // arg0._size_bytes >= arg1 * arg2
    info.richPreconditions.push_back(
        std::make_shared<BinaryExpr>(ConstraintExpr::CmpGE,
            std::make_shared<ArgFieldRefExpr>(0, "_size_bytes"),
            std::make_shared<BinaryExpr>(ConstraintExpr::Mul,
                std::make_shared<ArgRefExpr>(1),
                std::make_shared<ArgRefExpr>(2))));
    // ... plus FILE* checks
    FreeFunctionAnnotations[{"fread", 4}] = info;
}
```

**Impact:** Catches buffer overflows in fread/fwrite.

---

## Tier E: Ghost Field Systematization (Medium)

### E1. Ghost Field Naming Convention

**Current State:**
Inconsistent naming:

| Field | Type | Convention |
|-------|------|------------|
| `@open` | Ghost | Resource state |
| `@allocated` | Ghost | Resource state |
| `@fd` | Ghost | Resource state |
| `_lifetime` | Regular | Memory validity |
| `_readable` | Regular | FILE* mode |
| `_writable` | Regular | FILE* mode |

**The Problem:**
- Why is `_lifetime` not `@lifetime`?
- Why is `_readable` not `@readable`?
- No consistent rule for when to use `@` prefix

**Unified Design:**
```cpp
// Clear convention:
// @ prefix = Resource that requires cleanup (scope-exit checked)
// _ prefix = Property that doesn't require cleanup

// Resource state (requires cleanup)
@allocated    // malloc/free
@open         // fopen/fclose
@fd           // open/close
@mapped       // mmap/munmap

// Properties (no cleanup required)
_valid        // Memory is accessible (replaces _lifetime)
_readable     // FILE* mode
_writable     // FILE* mode
_size         // Element count
```

---

### E2. Resource Hierarchy

**Current State:**
Each resource type has its own ghost field:

```cpp
@open, @allocated, @fd, @dir, @dl, @mapped, @sem, @mq, @shm, @iconv, @regex, @addrinfo
```

12 different fields for the same concept: "acquired resource that must be released."

**Unified Design:**
```cpp
// Single resource state field with type tag
enum class ResourceKind : uint8_t {
    None = 0,
    File,           // fopen/fclose
    Memory,         // malloc/free
    FileDescriptor, // open/close
    Directory,      // opendir/closedir
    DynLib,         // dlopen/dlclose
    MemoryMap,      // mmap/munmap
    Semaphore,      // sem_open/sem_close
    MessageQueue,   // mq_open/mq_close
    SharedMemory,   // shm_open/close
    Iconv,          // iconv_open/iconv_close
    Regex,          // regcomp/regfree
    AddrInfo,       // getaddrinfo/freeaddrinfo
};

// Single ghost field: @resource
// Value encodes: (kind << 1) | acquired_bit

// fopen: result.@resource = (File << 1) | 1
// fclose: precondition @resource == (File << 1) | 1
//         postcondition @resource = (File << 1) | 0
```

**Impact:** Single resource tracking mechanism, unified leak detection.

---

## Summary: What Beautiful Annotations Look Like

After consolidation, annotations become:

1. **Uniform** - Same representation for C and C++
2. **Complete** - Return values tracked, sizes propagated
3. **Connected** - Container sizes flow to C buffer checks
4. **Semantic** - Distinct fields for distinct concepts
5. **Auditable** - Single table, easy to review
6. **Extensible** - Adding new function = adding one table row

### Example: The New `memcpy`

```cpp
// Before: Scattered, incomplete
{
    FreeFunctionInfo info;
    info.richPreconditions.push_back(sizeGE(0, "_size_bytes", 2));
    info.richPreconditions.push_back(sizeGE(1, "_size_bytes", 2));
    FreeFunctionAnnotations[{"memcpy", 3}] = info;
}

// After: Complete, in unified table
FunctionSpec{"memcpy", 3, false, nullptr,
    // Preconditions
    .pre = {
        {0, SizeBytes, GE, Arg{2}},      // dst big enough
        {1, SizeBytes, GE, Arg{2}},      // src big enough
        {0, Valid, NE, 0},                // dst not freed
        {1, Valid, NE, 0},                // src not freed
    },
    // Postconditions
    .post = {
        {Arg{0}, InitializedBytes, Arg{2}},  // dst has n bytes initialized
    },
    // Return value
    .returns = Arg{0},                    // returns dst
}
```

---

## Implementation Priority for Annotations

| Priority | Item | Impact |
|----------|------|--------|
| P0 | Fix `_nonnull` semantic overload | Correctness |
| P0 | Add malloc/strlen/strdup postconditions | Size tracking |
| P0 | Connect fopen mode to `_readable`/`_writable` | FILE* checking works |
| P1 | Unify `_lifetime`/`@allocated` | Single memory model |
| P1 | Add fread/fwrite size checks | Buffer overflow detection |
| P1 | Split `_has_value` per type family | Correct diagnostics |
| P2 | Shared constraint factories | Code reuse |
| P2 | Table-driven registration | Maintainability |
| P2 | Ghost field systematization | Consistency |

---

## The Vision

```
┌─────────────────────────────────────────────────────────────┐
│                    Unified Annotation Table                  │
├─────────────────────────────────────────────────────────────┤
│  C Functions    │  C++ Methods    │  POSIX Functions        │
│  ─────────────  │  ────────────   │  ────────────────       │
│  malloc         │  vector::[]     │  open                   │
│  free           │  optional::*    │  read                   │
│  memcpy         │  string::data   │  mmap                   │
│  strlen         │  any::has_value │  pthread_create         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│               Shared Constraint Evaluation                   │
│  ─────────────────────────────────────────────────────────  │
│  • Single ConstraintExpr type                               │
│  • Single evaluation pipeline                               │
│  • Unified diagnostics                                      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Unified Field Model                       │
│  ─────────────────────────────────────────────────────────  │
│  Properties:  _size, _nonnull, _engaged, _valid             │
│  Resources:   @resource (kind-tagged)                       │
│  Computed:    _size_bytes = _size * element_size            │
└─────────────────────────────────────────────────────────────┘
```

*When the annotations are beautiful, the analysis is powerful.*

---

# Part III: Quantified Constraints - The Unused Power

*DTT has `forall` and `exists`. Nobody uses them.*

---

## The Hidden Infrastructure

DTT already has quantified constraint support:

```cpp
// DTTPlugin.cpp lines 1369-1424
struct ForallExpr : ConstraintExpr {
    std::string boundVar;       // "i"
    ConstraintExprPtr lo;       // Lower bound (inclusive)
    ConstraintExprPtr hi;       // Upper bound (exclusive)
    ConstraintExprPtr body;     // Predicate over boundVar
};

struct ExistsExpr : ConstraintExpr { /* same structure */ };

struct IndexExpr : ConstraintExpr {
    std::string arrayName;      // "_data" or field path
    ConstraintExprPtr index;    // Index expression
};
```

With full Z3 integration:
```cpp
// DTTPlugin.cpp lines 4663-4683
// Build: forall i. (lo <= i < hi) => body
z3::expr range = (boundVar >= lo) && (boundVar < hi);
return z3::forall(boundVar, z3::implies(range, body));
```

And parser support:
```cpp
// DTTPlugin.cpp line 1887
if (match("forall(")) {
    return parseQuantifierBody(/*isForall=*/true);
}
```

**Usage count in annotations: ZERO.**

---

## What Quantifiers Enable

### 1. Array Initialization Guarantees

**Current:** No way to express "memset zeroes the entire buffer"

```cpp
// memset(buf, 0, n) should guarantee:
// forall(i, 0, n, buf[i] == 0)

// This enables:
char buf[100];
memset(buf, 0, sizeof(buf));
// Now: forall(i, 0, 100, buf[i] == 0)
if (buf[50] == 0) { /* provably true! */ }
```

**Annotation:**
```cpp
// memset with value=0 postcondition
{
    FreeFunctionInfo info;
    info.richPreconditions.push_back(sizeGE(0, "_size_bytes", 2));
    // NEW: Quantified postcondition
    // forall(i, 0, arg2, arg0[i] == arg1)
    info.postconditions.push_back({
        PostconditionTarget::Arg, 0, "_data",
        ForallExpr("i",
            LiteralExpr(0),
            ArgRefExpr(2),
            BinaryExpr(CmpEQ, IndexExpr("arg0", VarExpr("i")), ArgRefExpr(1)))
    });
    FreeFunctionAnnotations[{"memset", 3}] = info;
}
```

---

### 2. Sorted Container Invariants

**Current:** No way to express "std::sort produces sorted output"

```cpp
// std::sort(begin, end) should guarantee:
// forall(i, 0, size-1, data[i] <= data[i+1])

std::vector<int> v = {3, 1, 4, 1, 5};
std::sort(v.begin(), v.end());
// Now: forall(i, 0, 4, v[i] <= v[i+1])

// Binary search is now provably correct:
auto it = std::lower_bound(v.begin(), v.end(), 3);  // Requires sorted!
```

**Annotation:**
```cpp
// std::sort postcondition
addPost("vector", "sort", {
    ForallExpr("i",
        LiteralExpr(0),
        BinaryExpr(Sub, FieldRefExpr("_size"), LiteralExpr(1)),
        BinaryExpr(CmpLE,
            IndexExpr("_data", VarExpr("i")),
            IndexExpr("_data", BinaryExpr(Add, VarExpr("i"), LiteralExpr(1)))))
});
```

---

### 3. String Null-Termination

**Current:** No way to express "string is null-terminated"

```cpp
// A null-terminated string of length n:
// exists(i, 0, n+1, str[i] == '\0') && forall(j, 0, i, str[j] != '\0')
// Simplified: str[n] == '\0' && forall(i, 0, n, str[i] != '\0')

const char* s = "hello";  // _size=5
// Guarantee: s[5] == '\0' && forall(i, 0, 5, s[i] != '\0')

strlen(s);  // Can prove terminates!
```

**Annotation:**
```cpp
// String literal postcondition
// forall(i, 0, _size, this[i] != 0) && this[_size] == 0
```

---

### 4. Buffer Copy Semantics

**Current:** `memcpy` precondition exists, but no postcondition about contents

```cpp
// memcpy(dst, src, n) should guarantee:
// forall(i, 0, n, dst[i] == src[i])

char src[10] = "hello";
char dst[10];
memcpy(dst, src, 5);
// Now: forall(i, 0, 5, dst[i] == src[i])
// Therefore: dst[0] == 'h', dst[1] == 'e', ...
```

**Annotation:**
```cpp
// memcpy content transfer postcondition
info.quantifiedPostconditions.push_back(
    ForallExpr("i",
        LiteralExpr(0),
        ArgRefExpr(2),  // n
        BinaryExpr(CmpEQ,
            IndexExpr("arg0", VarExpr("i")),   // dst[i]
            IndexExpr("arg1", VarExpr("i"))))  // src[i]
);
```

---

### 5. Range-Based Validity

**Current:** Can prove `v[i]` safe if `i < v.size()`, but not for ranges

```cpp
// Processing a subrange:
void process(int* arr, size_t start, size_t end, size_t total) {
    // Precondition: forall(i, start, end, i < total)
    // Equivalently: end <= total

    for (size_t i = start; i < end; ++i) {
        arr[i] = 0;  // Safe because i < total
    }
}
```

**Annotation:**
```cpp
// Range-based precondition
addPre("process", 4, {
    BinaryExpr(CmpLE, ArgRefExpr(2), ArgRefExpr(3))  // end <= total
});
// Which implies: forall(i, start, end, i < total)
```

---

### 6. Iterator Validity Ranges

**Current:** Iterators are opaque; no range reasoning

```cpp
// std::for_each(begin, end, f) processes [begin, end)
// Precondition: forall(it, begin, end, *it is dereferenceable)
// Which requires: end - begin <= container.size()

std::vector<int> v(10);
auto it1 = v.begin();
auto it2 = v.begin() + 5;
std::for_each(it1, it2, [](int& x) { x = 0; });
// Safe: 5 <= 10
```

---

### 7. Fill/Generate Patterns

```cpp
// std::fill(begin, end, value)
// Postcondition: forall(i, 0, end-begin, container[i] == value)

std::vector<int> v(100);
std::fill(v.begin(), v.end(), 42);
// Now: forall(i, 0, 100, v[i] == 42)
assert(v[50] == 42);  // Provably true!
```

---

### 8. Partial Initialization Tracking

```cpp
// After partial write:
char buf[100];
memset(buf, 0, 50);
// forall(i, 0, 50, buf[i] == 0)
// but buf[50..99] are uninitialized!

memset(buf + 50, 'x', 50);
// Now: forall(i, 0, 50, buf[i] == 0) && forall(i, 50, 100, buf[i] == 'x')
```

---

## Why It's Unused

1. **No annotations use it** - stdlib/clib annotations stick to simple field checks
2. **No helper factories** - Creating quantified constraints is verbose
3. **No documentation** - Developers don't know it exists
4. **Unclear semantics** - When does Z3 get invoked vs simple checking?

---

## Unified Design: Quantifier Factories

```cpp
namespace dtt::constraints {

// Array element constraint
// forall(i, 0, size, arr[i] op value)
ConstraintExprPtr forallElements(
    FieldID arrayField,
    FieldID sizeField,
    ProofOp op,
    int64_t value
) {
    return std::make_shared<ForallExpr>("i",
        std::make_shared<LiteralExpr>(0),
        std::make_shared<FieldRefExpr>(fieldName(sizeField)),
        std::make_shared<BinaryExpr>(constraintOp(op),
            std::make_shared<IndexExpr>(fieldName(arrayField),
                std::make_shared<VarRefExpr>("i")),
            std::make_shared<LiteralExpr>(value)));
}

// Range copy equality
// forall(i, 0, n, dst[i] == src[i])
ConstraintExprPtr forallCopied(unsigned dstArg, unsigned srcArg, unsigned countArg) {
    return std::make_shared<ForallExpr>("i",
        std::make_shared<LiteralExpr>(0),
        std::make_shared<ArgRefExpr>(countArg),
        std::make_shared<BinaryExpr>(CmpEQ,
            std::make_shared<IndexExpr>("arg" + std::to_string(dstArg),
                std::make_shared<VarRefExpr>("i")),
            std::make_shared<IndexExpr>("arg" + std::to_string(srcArg),
                std::make_shared<VarRefExpr>("i"))));
}

// Sorted range invariant
// forall(i, 0, size-1, arr[i] <= arr[i+1])
ConstraintExprPtr sorted(FieldID arrayField, FieldID sizeField) {
    return std::make_shared<ForallExpr>("i",
        std::make_shared<LiteralExpr>(0),
        std::make_shared<BinaryExpr>(Sub,
            std::make_shared<FieldRefExpr>(fieldName(sizeField)),
            std::make_shared<LiteralExpr>(1)),
        std::make_shared<BinaryExpr>(CmpLE,
            std::make_shared<IndexExpr>(fieldName(arrayField),
                std::make_shared<VarRefExpr>("i")),
            std::make_shared<IndexExpr>(fieldName(arrayField),
                std::make_shared<BinaryExpr>(Add,
                    std::make_shared<VarRefExpr>("i"),
                    std::make_shared<LiteralExpr>(1)))));
}

// Null-terminated string
// this[_size] == 0 && forall(i, 0, _size, this[i] != 0)
ConstraintExprPtr nullTerminated(FieldID dataField, FieldID sizeField);

// Existential: find element
// exists(i, 0, size, arr[i] == value)
ConstraintExprPtr existsElement(FieldID arrayField, FieldID sizeField, int64_t value);

}
```

---

## Annotations Using Quantifiers

```cpp
// memset(buf, val, n): forall(i, 0, n, buf[i] == val)
{"memset", 3, false, nullptr,
    .pre = {{0, SizeBytes, GE, Arg{2}}},
    .quantifiedPost = {forallFilled(0, 1, 2)},  // dst filled with val
}

// memcpy(dst, src, n): forall(i, 0, n, dst[i] == src[i])
{"memcpy", 3, false, nullptr,
    .pre = {{0, SizeBytes, GE, Arg{2}}, {1, SizeBytes, GE, Arg{2}}},
    .quantifiedPost = {forallCopied(0, 1, 2)},
}

// std::sort: establishes sorted invariant
{"sort", 2, true, "vector",
    .post = {},
    .quantifiedPost = {sorted(Data, Size)},
}

// std::fill: forall(i, begin, end, container[i] == value)
{"fill", 3, false, nullptr,  // fill(begin, end, value)
    .quantifiedPost = {forallFilled(/*range*/0, 1, 2)},
}
```

---

## Implementation Priority

| Priority | Item | Impact |
|----------|------|--------|
| P1 | Add quantifier factory functions | Usability |
| P1 | Annotate memset/memcpy with quantified postconditions | Content tracking |
| P1 | Annotate std::fill/std::sort | Algorithm semantics |
| P2 | Track partial initialization ranges | Precise analysis |
| P2 | Iterator range validity | Iterator safety |
| P3 | String null-termination proofs | String safety |

---

## The Complete Picture

```
┌─────────────────────────────────────────────────────────────┐
│                    Constraint Hierarchy                      │
├─────────────────────────────────────────────────────────────┤
│  Simple:     _size >= 10, _nonnull != 0                     │
│  Relational: arg0 < this._size, arg1._size >= arg2          │
│  Quantified: forall(i, 0, n, arr[i] == 0)  ← UNUSED!        │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Evaluation Pipeline                       │
├─────────────────────────────────────────────────────────────┤
│  Simple   → Direct field lookup                             │
│  Relation → Symbolic comparison                             │
│  Quantified → Z3 SMT solver (already implemented!)          │
└─────────────────────────────────────────────────────────────┘
```

The infrastructure is there. The Z3 integration works. The parser handles `forall(i, 0, n, body)`.

**All that's missing: annotations that use it.**

*The most powerful feature is the one nobody knows exists.*
