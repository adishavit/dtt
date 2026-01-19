# DTT Taint Tracking Proposal (Change 5)

## Executive Summary

Add taint tracking to DTT to distinguish untrusted external data from internal values,
enabling more precise Heartbleed-style vulnerability detection.

## Problem Statement

Current DTT (with Changes 1-3) warns on ALL unverified symbolic sizes:

```cpp
void internal_copy(size_t len) {
    char* buf = malloc(len);
    memcpy(buf, "data", len);  // WARNING: unverified (false positive?)
}

void heartbleed(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);
    char* buf = malloc(len);
    memcpy(buf, record, len);  // WARNING: unverified (TRUE POSITIVE!)
}
```

Both produce the same warning, but the second is far more dangerous.

## Goals

1. Distinguish untrusted (tainted) values from internal values
2. Provide stronger warnings/errors for tainted unverified bounds
3. Reduce false positives on internal code
4. Maintain DTT's compile-time integration model

## Non-Goals

1. Full path-sensitive analysis (too expensive)
2. Inter-procedural taint tracking (Phase 1)
3. Replacing Clang Static Analyzer

## Known Limitations

Documented upfront to set expectations:

1. **Aliasing**: If `*ptr` is tainted, `ptr[1]` may not be tracked as tainted if accessed via different path
2. **Interprocedural**: Taint doesn't flow across function boundaries without annotations
3. **Field sensitivity**: Struct fields tracked independently; tainting `s.len` doesn't taint `s.data`
4. **Indirect calls**: Function pointers not analyzed for taint rules

---

## Design Options

### Option A: DTT-Native Implementation (Copy CSA Data)

Copy taint source/propagator lists from CSA, implement tracking in DTT's existing infrastructure.

**Architecture:**
```
┌─────────────────────────────────────────────────────────┐
│                    DTTPlugin.cpp                        │
├─────────────────────────────────────────────────────────┤
│  Existing: TypedEnv, SymValue, FieldPath, Z3 proofs    │
├─────────────────────────────────────────────────────────┤
│  NEW: TaintEnv (parallel to TypedEnv)                  │
│       TaintRules (copied from CSA)                      │
│       Taint propagation in VisitCallExpr               │
└─────────────────────────────────────────────────────────┘
```

**Pros:**
- Self-contained, no external dependencies
- Uses existing DTT infrastructure
- Compile-time integration preserved
- Simple flow-sensitive model

**Cons:**
- Duplicates CSA's rule maintenance
- Less precise than path-sensitive
- Manual sync with CSA updates

### Option B: Link Against CSA Libraries

Use CSA's taint tracking code directly by linking against Clang libraries.

**Architecture:**
```
┌─────────────────────────────────────────────────────────┐
│                    DTTPlugin.cpp                        │
├─────────────────────────────────────────────────────────┤
│  #include "clang/StaticAnalyzer/Core/..."              │
│  Use GenericTaintChecker's TaintPropagationRule        │
│  Link: libclangStaticAnalyzerCheckers.a                │
└─────────────────────────────────────────────────────────┘
```

**Pros:**
- Automatic sync with CSA updates
- Battle-tested rules
- No duplication

**Cons:**
- Heavy dependency (pulls in CSA infrastructure)
- CSA uses different state model (ProgramState vs TypedEnv)
- May require CSA's path-sensitive engine to work
- Larger binary, longer compile times

### Option C: Code Generation from CSA

Extract CSA rules at build time, generate DTT-compatible code.

**Architecture:**
```
┌──────────────────┐    build-time    ┌─────────────────┐
│ GenericTaint     │ ───────────────► │ taint_rules.inc │
│ Checker.cpp      │    extraction    │ (generated)     │
└──────────────────┘                  └────────┬────────┘
                                               │
                                               ▼
                                      ┌─────────────────┐
                                      │ DTTPlugin.cpp   │
                                      │ #include "..."  │
                                      └─────────────────┘
```

**Pros:**
- Auto-sync with CSA
- No runtime dependency
- Clean separation

**Cons:**
- Complex build system
- Fragile if CSA format changes
- Maintenance burden

### Option D: Hybrid - Minimal CSA Data Import

Manually curate a subset of CSA rules, document source, periodic manual sync.

**Architecture:**
```cpp
// taint_rules.h - manually maintained, sourced from CSA
// Last synced: LLVM 18.1.3, 2024-01-15

namespace dtt::taint {

// Source: GenericTaintChecker.cpp lines 150-200
constexpr std::array<const char*, 15> SourceFunctions = {
    "fopen", "fdopen", "freopen", ...
};

// Source: GenericTaintChecker.cpp lines 250-300
constexpr std::array<PropagatorRule, 20> Propagators = {
    {"recv", {0}, {1, RetVal}},
    ...
};

} // namespace dtt::taint
```

**Pros:**
- Explicit, auditable
- No build complexity
- Curated for DTT's needs
- Easy to understand

**Cons:**
- Manual sync required
- May drift from CSA

---

## Recommended Approach: Option D (Hybrid)

### Rationale

1. **Maintainability**: Explicit rules are easier to audit and modify
2. **Simplicity**: No complex build dependencies
3. **Focused**: Only include rules relevant to DTT's use cases
4. **Documented**: Clear provenance from CSA

### Implementation Plan

#### Phase 1: Core Infrastructure

**1.1 Add TaintEnv to DTTVisitor**

```cpp
// Taint provenance for better diagnostics
struct TaintInfo {
    SourceLocation origin;      // Where taint was introduced
    const char* sourceFn;       // "recv", "scanf", etc.
    enum RiskLevel : uint8_t {
        Low = 0,     // File I/O
        Medium = 1,  // Environment, command line
        High = 2     // Network, user input
    } risk;
};

// Parallel to TypedEnv - tracks taint status per path
class DTTVisitor {
    // Existing
    std::unordered_map<std::string, SymValue> TypedEnv;

    // NEW: Taint tracking with provenance
    std::unordered_map<std::string, TaintInfo> TaintedPaths;

    // NEW: Proven bounds (separate from taint - a value can be tainted but proven safe)
    // Maps path -> maximum proven bound for that path
    std::unordered_map<std::string, SymValue> ProvenBounds;

    bool isTainted(const FieldPath& path) const {
        return TaintedPaths.count(path.toString()) > 0;
    }

    const TaintInfo* getTaintInfo(const FieldPath& path) const {
        auto it = TaintedPaths.find(path.toString());
        return it != TaintedPaths.end() ? &it->second : nullptr;
    }

    void setTainted(const FieldPath& path, TaintInfo info) {
        TaintedPaths[path.toString()] = info;
    }

    void clearTaint(const FieldPath& path) {
        TaintedPaths.erase(path.toString());
    }

    // Record that path has been proven <= bound
    void addProvenBound(const FieldPath& path, const SymValue& bound) {
        ProvenBounds[path.toString()] = bound;
    }

    // Check if path has a proven bound >= required
    bool hasSufficientBound(const FieldPath& path, const SymValue& required) const;
};
```

**Key Design Decision**: Taint and proven bounds are tracked separately. A value can be:
- Tainted + unproven → Warning (highest priority)
- Tainted + proven → OK (guard discharged the risk)
- Untainted + unproven → Warning (lower priority than tainted)
- Untainted + proven → OK

**1.2 Define Taint Rule Structures**

```cpp
// taint_rules.h
namespace dtt::taint {

// Argument index constants
constexpr int RetVal = -1;
constexpr int Variadic = -2;  // All variadic args

struct SourceRule {
    const char* funcName;
    std::initializer_list<int> taintedArgs;  // Which args/retval get tainted
};

struct PropagatorRule {
    const char* funcName;
    std::initializer_list<int> srcArgs;   // If any of these tainted...
    std::initializer_list<int> dstArgs;   // ...these become tainted
};

struct SinkRule {
    const char* funcName;
    std::initializer_list<int> sinkArgs;  // Warn if these are tainted
    const char* diagMessage;
};

// Protocol classification for socket()
enum class ProtocolSafety { Safe, Unsafe, Unknown };
ProtocolSafety classifyProtocol(int addressFamily);

} // namespace dtt::taint
```

**1.3 Populate Rules (Sourced from CSA)**

```cpp
// taint_rules.cpp
// Rules derived from: llvm-project/clang/lib/StaticAnalyzer/Checkers/GenericTaintChecker.cpp
// LLVM version: 18.1.3
// Last sync: 2024-01-15

namespace dtt::taint {

//=== TAINT SOURCES ===
// Functions that introduce taint from external sources

const SourceRule Sources[] = {
    // File I/O - return tainted FILE*/fd
    {"fopen",    {RetVal}},
    {"fdopen",   {RetVal}},
    {"freopen",  {RetVal}},

    // Character input
    {"getchar",  {RetVal}},
    {"getchar_unlocked", {RetVal}},
    {"getch",    {RetVal}},
    {"wgetch",   {RetVal}},
    {"_IO_getc", {RetVal}},

    // Line input - taints buffer AND returns it
    {"gets",     {0, RetVal}},
    {"gets_s",   {0, RetVal}},
    {"fgets",    {0, RetVal}},

    // Formatted input - taints output arguments
    {"scanf",    {Variadic}},  // All args after format
    {"fscanf",   {Variadic}},
    {"sscanf",   {Variadic}},
    {"vscanf",   {Variadic}},
    {"vfscanf",  {Variadic}},
    {"vsscanf",  {Variadic}},

    // Environment
    {"getenv",   {RetVal}},
    {"secure_getenv", {RetVal}},

    // System info
    {"getcwd",   {0, RetVal}},
    {"getwd",    {0, RetVal}},
    {"get_current_dir_name", {RetVal}},
    {"gethostname", {0}},
    {"getlogin", {RetVal}},
    {"getlogin_r", {0}},

    // Link reading
    {"readlink",   {1, RetVal}},
    {"readlinkat", {2, RetVal}},
};

//=== TAINT PROPAGATORS ===
// Functions that spread taint from sources to destinations

const PropagatorRule Propagators[] = {
    // Network I/O - fd taints buffer
    {"recv",     {0}, {1, RetVal}},
    {"recvfrom", {0}, {1, RetVal}},
    {"recvmsg",  {0}, {1, RetVal}},

    // File I/O - fd/FILE* taints buffer
    {"read",     {0}, {1, RetVal}},
    {"pread",    {0}, {1, RetVal}},
    {"readv",    {0}, {1, RetVal}},
    {"preadv",   {0}, {1, RetVal}},
    {"fread",    {3}, {0, RetVal}},  // FILE* is arg3
    {"fgetc",    {0}, {RetVal}},
    {"fgets",    {2}, {0, RetVal}},  // FILE* is arg2
    {"getc",     {0}, {RetVal}},
    {"getline",  {2}, {0, RetVal}},
    {"getdelim", {3}, {0, RetVal}},

    // Memory copy - src taints dst
    {"memcpy",   {1}, {0, RetVal}},
    {"memmove",  {1}, {0, RetVal}},
    {"memset",   {},  {}},  // Does NOT propagate (overwrites)
    {"mempcpy",  {1}, {0, RetVal}},
    {"wmemcpy",  {1}, {0, RetVal}},
    {"wmemmove", {1}, {0, RetVal}},

    // String copy
    {"strcpy",   {1}, {0, RetVal}},
    {"strncpy",  {1}, {0, RetVal}},
    {"stpcpy",   {1}, {0, RetVal}},
    {"stpncpy",  {1}, {0, RetVal}},
    {"wcscpy",   {1}, {0, RetVal}},
    {"wcsncpy",  {1}, {0, RetVal}},

    // String concat - src taints dst
    {"strcat",   {1}, {0, RetVal}},
    {"strncat",  {1}, {0, RetVal}},
    {"wcscat",   {1}, {0, RetVal}},
    {"wcsncat",  {1}, {0, RetVal}},

    // String duplication
    {"strdup",   {0}, {RetVal}},
    {"strndup",  {0}, {RetVal}},
    {"wcsdup",   {0}, {RetVal}},

    // Formatted output - format args taint buffer
    {"sprintf",  {1, 2, 3, 4, 5}, {0}},
    {"snprintf", {2, 3, 4, 5, 6}, {0}},
    {"vsprintf", {1}, {0}},
    {"vsnprintf",{2}, {0}},

    // Conversion - input taints output
    {"strtol",   {0}, {RetVal}},
    {"strtoll",  {0}, {RetVal}},
    {"strtoul",  {0}, {RetVal}},
    {"strtoull", {0}, {RetVal}},
    {"strtof",   {0}, {RetVal}},
    {"strtod",   {0}, {RetVal}},
    {"atoi",     {0}, {RetVal}},
    {"atol",     {0}, {RetVal}},
    {"atoll",    {0}, {RetVal}},
    {"atof",     {0}, {RetVal}},

    // === DTT-SPECIFIC ADDITIONS (deviate from CSA) ===
    // Length operations on tainted buffers propagate taint.
    // Rationale: In Heartbleed-style bugs, the LENGTH derived from
    // tainted data is the attack vector, not just the content.
    // CSA treats strlen() result as metadata (not tainted), but for
    // bounds checking, tainted length is the core risk.
    {"strlen",   {0}, {RetVal}},
    {"wcslen",   {0}, {RetVal}},
    {"strnlen",  {0}, {RetVal}},
    {"wcsnlen",  {0}, {RetVal}},
};

//=== ARITHMETIC TAINT PROPAGATION ===
// Handled specially in code, not via rule table.
// Any arithmetic operation (+, -, *, /, %) on a tainted value
// produces a tainted result:
//   size_t len;
//   recv(fd, &len, sizeof(len), 0);  // len tainted
//   size_t padded = len + 16;        // padded is also tainted
//   memcpy(buf, src, padded);        // should warn
//
// Implementation: In VisitBinaryOperator, if either operand is
// tainted and the result is assigned, mark the result tainted.

//=== SOCKET PROTOCOL CLASSIFICATION ===
// Determines if socket() return value should be tainted

ProtocolSafety classifyProtocol(int af) {
    switch (af) {
    // Safe - local/system protocols
    case AF_UNIX:
    case AF_LOCAL:  // Same as AF_UNIX on Linux
    case AF_NETLINK:
        return ProtocolSafety::Safe;

    // Unsafe - network protocols
    case AF_INET:
    case AF_INET6:
    case AF_BLUETOOTH:
    case AF_PACKET:
    case AF_CAN:
        return ProtocolSafety::Unsafe;

    default:
        return ProtocolSafety::Unknown;  // Conservative: treat as unsafe
    }
}

} // namespace dtt::taint
```

#### Phase 2: Integration with DTT

**2.1 Taint Handling in VisitCallExpr**

```cpp
void DTTVisitor::handleTaintInCall(CallExpr *E) {
    if (!E->getDirectCallee()) return;
    StringRef fn = E->getDirectCallee()->getName();

    // Check for taint sources
    if (auto *rule = findSourceRule(fn)) {
        for (int argIdx : rule->taintedArgs) {
            if (argIdx == taint::RetVal) {
                // Return value will be tainted - handled at assignment
                PendingTaintedReturn = true;
            } else if (argIdx == taint::Variadic) {
                // Taint all variadic arguments
                for (unsigned i = getFirstVariadicArg(fn); i < E->getNumArgs(); i++) {
                    if (FieldPath path = getTypedPath(E->getArg(i)); path.isValid()) {
                        setTainted(path, true);
                    }
                }
            } else if (argIdx >= 0 && argIdx < (int)E->getNumArgs()) {
                if (FieldPath path = getTypedPath(E->getArg(argIdx)); path.isValid()) {
                    setTainted(path, true);
                }
            }
        }
    }

    // Check for taint propagation
    if (auto *rule = findPropagatorRule(fn)) {
        bool srcTainted = false;
        for (int srcIdx : rule->srcArgs) {
            if (srcIdx >= 0 && srcIdx < (int)E->getNumArgs()) {
                if (FieldPath path = getTypedPath(E->getArg(srcIdx)); path.isValid()) {
                    if (isTainted(path)) {
                        srcTainted = true;
                        break;
                    }
                }
            }
        }

        if (srcTainted) {
            for (int dstIdx : rule->dstArgs) {
                if (dstIdx == taint::RetVal) {
                    PendingTaintedReturn = true;
                } else if (dstIdx >= 0 && dstIdx < (int)E->getNumArgs()) {
                    if (FieldPath path = getTypedPath(E->getArg(dstIdx)); path.isValid()) {
                        setTainted(path, true);
                    }
                }
            }
        }
    }

    // Special case: socket() with protocol analysis
    if (fn == "socket" && E->getNumArgs() >= 1) {
        if (Expr::EvalResult R; safeEvaluateAsInt(E->getArg(0), R, Ctx)) {
            int af = R.Val.getInt().getSExtValue();
            if (classifyProtocol(af) != ProtocolSafety::Safe) {
                PendingTaintedReturn = true;
            }
        } else {
            // Unknown protocol - conservative: taint
            PendingTaintedReturn = true;
        }
    }
}
```

**2.2 Taint Clearing on Bounds Proof**

```cpp
void DTTVisitor::handleGuardForTaint(BinaryOperator *Cmp, bool inTrueBranch) {
    // Pattern: if (len <= bufSize) or if (len < bufSize)
    if (Cmp->getOpcode() != BO_LE && Cmp->getOpcode() != BO_LT &&
        Cmp->getOpcode() != BO_GE && Cmp->getOpcode() != BO_GT) {
        return;
    }

    Expr *smaller, *larger;
    if (Cmp->getOpcode() == BO_LE || Cmp->getOpcode() == BO_LT) {
        smaller = Cmp->getLHS();
        larger = Cmp->getRHS();
    } else {
        smaller = Cmp->getRHS();
        larger = Cmp->getLHS();
    }

    FieldPath smallerPath = getTypedPath(smaller);
    if (!smallerPath.isValid() || !isTainted(smallerPath)) {
        return;  // Not tainted, nothing to clear
    }

    // Check if this establishes a bound
    SymValue smallerVal = getFieldTyped(smallerPath, "_value");
    SymValue largerVal = getSizeValue(larger);

    if (inTrueBranch) {
        // Inside the guard: taint is "conditionally cleared"
        // We record that this path has proven bounds
        addProvenBound(smallerPath, largerVal);
        // Taint remains but bounds are proven - no warning needed
    }
}
```

**2.3 Modified Bounds Check with Taint Awareness**

```cpp
// Enhanced BoundsResult
enum class BoundsResult {
    Safe,                    // Proven safe
    Overflow,                // Proven overflow
    UnverifiedConst,         // Unknown, buffer size is constant
    UnverifiedSymbolic,      // Unknown, buffer size is symbolic
    UnverifiedTainted,       // Unknown AND copy size is tainted (highest priority)
    Skip                     // No size info
};

auto checkBufferBoundsWithTaint = [&](const SymValue& opSize, const SymValue& bufSize,
                                       const FieldPath& opSizePath) -> BoundsResult {
    if (bufSize.kind == SymValue::UNKNOWN) return BoundsResult::Skip;

    auto proven = checkBoundsProven(opSize, bufSize);
    if (proven.has_value() && *proven == true) {
        return BoundsResult::Safe;
    }

    // Check if operation size is tainted
    bool opTainted = opSizePath.isValid() && isTainted(opSizePath);

    if (bufSize.kind == SymValue::CONST) {
        if (opSize.kind == SymValue::CONST && opSize.constVal > bufSize.constVal) {
            return BoundsResult::Overflow;
        }
        return opTainted ? BoundsResult::UnverifiedTainted : BoundsResult::UnverifiedConst;
    }

    return opTainted ? BoundsResult::UnverifiedTainted : BoundsResult::UnverifiedSymbolic;
};
```

**2.4 New Diagnostics for Tainted Values**

```cpp
// Declarations
ModeDependentDiag DiagTaintedBoundsUnverified;  // Tainted size, can't verify

// Initialization
DiagTaintedBoundsUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
    "DTT: cannot verify bounds for tainted value from untrusted source (CWE-126) [-Wdtt-tainted-bounds]");
DiagTaintedBoundsUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
    "DTT: cannot verify bounds for tainted value from untrusted source (CWE-126)");

unsigned NoteTaintSource = Diag.getCustomDiagID(DiagnosticsEngine::Note,
    "value was tainted here by call to '%0'");

unsigned NoteProveBounds = Diag.getCustomDiagID(DiagnosticsEngine::Note,
    "add bounds check to clear taint: if (%0 <= buffer_size) { ... }");
```

#### Phase 2.5: Taint-Aware Diagnostics Integration

**Rationale**: Test sources + propagators with flow-insensitive model before adding
path-sensitivity complexity. This phase enables end-to-end testing of taint detection.

**2.5.1 Integrate Taint into mem* Checks**

```cpp
// In memcpy/memmove/memset handling
void DTTVisitor::checkMemOpWithTaint(CallExpr *E, const char* opName,
                                      unsigned dstIdx, unsigned srcIdx, unsigned sizeIdx) {
    Expr* sizeExpr = E->getArg(sizeIdx);
    FieldPath sizePath = getTypedPath(sizeExpr);
    SymValue opSize = getSizeValue(sizeExpr);

    // Check destination
    FieldPath dstPath = getTypedPath(E->getArg(dstIdx));
    SymValue dstSize = getFieldTyped(dstPath, "_size_bytes");
    BoundsResult dstResult = checkBufferBoundsWithTaint(opSize, dstSize, sizePath);

    // Emit appropriate diagnostic based on taint status
    switch (dstResult) {
    case BoundsResult::UnverifiedTainted:
        emitTaintedBoundsWarning(E, sizePath, dstPath);
        break;
    case BoundsResult::UnverifiedSymbolic:
        emitSymbolicBoundsWarning(E, /*isTainted=*/false);
        break;
    // ... other cases
    }
}
```

**2.5.2 Emit Taint Provenance in Diagnostics**

```cpp
void DTTVisitor::emitTaintedBoundsWarning(CallExpr *E,
                                           const FieldPath& sizePath,
                                           const FieldPath& bufPath) {
    // Primary diagnostic
    Diag.Report(E->getBeginLoc(), DiagTaintedBoundsUnverified.get(Mode));

    // Add note showing where taint originated
    if (const TaintInfo* info = getTaintInfo(sizePath)) {
        Diag.Report(info->origin, NoteTaintSource) << info->sourceFn;
    }

    // Suggest fix
    Diag.Report(E->getBeginLoc(), NoteProveBounds) << sizePath.toString();
}
```

**2.5.3 Arithmetic Taint Propagation**

```cpp
bool DTTVisitor::VisitBinaryOperator(BinaryOperator *BO) {
    if (!BO->getType()->isIntegerType()) return true;

    // Check if this is an arithmetic op
    if (BO->isMultiplicativeOp() || BO->isAdditiveOp()) {
        FieldPath lhsPath = getTypedPath(BO->getLHS());
        FieldPath rhsPath = getTypedPath(BO->getRHS());

        // If either operand is tainted, result is tainted
        const TaintInfo* lhsTaint = lhsPath.isValid() ? getTaintInfo(lhsPath) : nullptr;
        const TaintInfo* rhsTaint = rhsPath.isValid() ? getTaintInfo(rhsPath) : nullptr;

        if (lhsTaint || rhsTaint) {
            // Will be propagated when result is assigned
            PendingArithmeticTaint = lhsTaint ? *lhsTaint : *rhsTaint;
        }
    }
    return true;
}
```

#### Phase 3: Path-Sensitive Taint Tracking

**3.1 Save/Restore Taint State in Branches**

```cpp
bool DTTVisitor::TraverseIfStmt(IfStmt *If) {
    // Save taint state before branches
    auto savedTaint = TaintedPaths;

    // Analyze condition for taint-clearing guards
    if (auto *Cmp = dyn_cast<BinaryOperator>(If->getCond())) {
        handleGuardForTaint(Cmp, /*inTrueBranch=*/true);
    }

    TraverseStmt(If->getThen());

    if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
        // Early return - restore taint for continuing path
        TaintedPaths = savedTaint;
    } else if (If->getElse()) {
        auto thenTaint = TaintedPaths;
        TaintedPaths = savedTaint;
        TraverseStmt(If->getElse());
        // Merge: tainted if tainted in EITHER branch (conservative)
        TaintedPaths.insert(thenTaint.begin(), thenTaint.end());
    }

    return true;
}
```

---

## Testing Strategy

### Test Files

**1. taint_source_fail.cpp** - Direct taint sources should warn
```cpp
void test_recv_tainted(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);  // len is tainted
    char buf[64];
    memcpy(buf, "data", len);  // ERROR: tainted unverified
}

void test_scanf_tainted() {
    int n;
    scanf("%d", &n);  // n is tainted
    char buf[32];
    memset(buf, 0, n);  // ERROR: tainted unverified
}
```

**2. taint_propagation_fail.cpp** - Taint should propagate
```cpp
void test_taint_propagates(int fd) {
    char input[100];
    recv(fd, input, sizeof(input), 0);  // input is tainted

    size_t len = strlen(input);  // len is tainted (from tainted input? TBD)
    // OR
    size_t len = *(size_t*)input;  // len is tainted

    char buf[64];
    memcpy(buf, "x", len);  // ERROR: tainted
}
```

**3. taint_guarded_pass.cpp** - Proper guards clear taint
```cpp
void test_taint_cleared(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);  // len is tainted

    char buf[64];
    if (len <= sizeof(buf)) {  // Guard clears taint for this path
        memcpy(buf, "data", len);  // OK: taint cleared by proof
    }
}
```

**4. taint_safe_socket_pass.cpp** - Local sockets not tainted
```cpp
void test_unix_socket_safe() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);  // Safe protocol
    size_t len;
    recv(fd, &len, sizeof(len), 0);  // NOT tainted (local socket)

    char buf[64];
    memcpy(buf, "data", len);  // OK: not tainted (but still unverified-symbolic)
}
```

**5. taint_arithmetic_fail.cpp** - Arithmetic preserves taint
```cpp
void test_arithmetic_taint(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);  // len is tainted

    size_t padded = len + 16;        // padded is ALSO tainted
    size_t doubled = len * 2;        // doubled is ALSO tainted

    char buf[64];
    memcpy(buf, "data", padded);     // ERROR: tainted unverified
    memcpy(buf, "data", doubled);    // ERROR: tainted unverified
}
```

**6. taint_strlen_fail.cpp** - strlen on tainted buffer produces tainted length
```cpp
void test_strlen_taint(int fd) {
    char input[100];
    recv(fd, input, sizeof(input), 0);  // input is tainted

    size_t len = strlen(input);  // len is tainted (DTT-specific, differs from CSA)

    char buf[64];
    memcpy(buf, "x", len);  // ERROR: tainted unverified
}
```

**7. taint_chained_fail.cpp** - Chained propagation
```cpp
void test_chained_propagation(int fd) {
    char input[100];
    recv(fd, input, sizeof(input), 0);  // input is tainted

    char temp[100];
    memcpy(temp, input, 50);  // temp is tainted (propagated from input)

    char final[100];
    strcpy(final, temp);  // final is tainted (propagated from temp)

    size_t len = strlen(final);  // len is tainted
    char buf[32];
    memcpy(buf, "x", len);  // ERROR: tainted unverified
}
```

---

## Open Questions

1. **Interprocedural taint**: If a function receives a tainted parameter, should the caller know?
   - Option: `[[clang::annotate("dtt:tainted")]]` on parameters

2. **Taint through pointers**: If `*ptr` is tainted, is `ptr[1]` tainted?
   - Conservative: yes, entire buffer is tainted

3. **Taint decay**: Should taint clear after N operations?
   - Probably not - explicit proof required

4. **strlen() on tainted buffer**: Is the result tainted?
   - CSA says no (it's the length, not the content)
   - **RESOLVED**: DTT deviates from CSA here - strlen() on tainted buffer produces tainted length.
     For Heartbleed-style bugs, the LENGTH derived from attacker-controlled data IS the attack vector.
     See "DTT-SPECIFIC ADDITIONS" in propagator rules.

5. **Configuration**: Should users be able to add custom taint sources?
   - Phase 2: `--dtt-taint-source=my_func:1,2` or annotations

---

## Maintenance Plan

### Sync with CSA

1. **Frequency**: Check CSA updates with each LLVM release
2. **Process**:
   - Compare GenericTaintChecker.cpp changes
   - Update taint_rules.cpp with new rules
   - Document sync date and LLVM version
3. **Testing**: Run full test suite after sync

### Documentation

- Comment each rule with CSA source line reference
- Document deviations from CSA (if any)
- Maintain CHANGELOG of rule additions/removals

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Rules drift from CSA | Medium | Document sync process, periodic audits |
| False positives from aggressive taint | High | Start conservative, tune based on feedback |
| Performance overhead | Low | Set is O(1), minimal impact |
| Complex path merging | Medium | Conservative merge (union of taint) |

---

## Success Criteria

1. Heartbleed-style tests (recv + memcpy) produce tainted warnings
2. Internal code (no external input) produces regular warnings only
3. Guarded tainted code passes silently
4. No regression in existing tests
5. Performance within 10% of current

---

## Implementation Phases

| Phase | Scope | Effort |
|-------|-------|--------|
| 1 | Core infrastructure (TaintEnv, TaintInfo) + sources | Medium |
| 2 | Propagators + socket analysis + arithmetic propagation | Medium |
| 2.5 | Taint-aware diagnostics integration (test before path-sensitivity) | Medium |
| 3 | Path-sensitive taint tracking (branch save/restore) | Medium-High |
| 4 | Custom taint annotations | Low |
| 5 | Documentation + sync process | Low |

---

## References

- Clang Static Analyzer GenericTaintChecker: `llvm-project/clang/lib/StaticAnalyzer/Checkers/GenericTaintChecker.cpp`
- CWE-126: Buffer Over-read
- CWE-20: Improper Input Validation
- CERT C: STR31-C, ENV33-C
