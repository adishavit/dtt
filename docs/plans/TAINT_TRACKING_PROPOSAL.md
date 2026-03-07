# DTT Taint Tracking Proposal

## Executive Summary

Add taint tracking to DTT to distinguish untrusted external data from internal values,
enabling more precise vulnerability detection and feeding dtx exploit chain analysis.

## Problem Statement

Current DTT warns on ALL unverified symbolic sizes:

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
4. Detect taint flowing into dangerous sinks (command injection, format strings)
5. Maintain DTT's compile-time integration model
6. Produce taint metadata consumable by dtx (see `EXPLOIT_SCANNER_PLAN.md`)

## Non-Goals

1. Full path-sensitive analysis (too expensive for a plugin)
2. Inter-procedural taint tracking (Phase 1; dtx handles cross-function)
3. Replacing Clang Static Analyzer
4. Taint tracking through complex aliasing chains

## Known Limitations

Documented upfront to set expectations:

1. **Aliasing**: If `*ptr` is tainted, `ptr[1]` may not be tracked as tainted
   if accessed via a different path
2. **Interprocedural**: Taint doesn't flow across function boundaries without
   annotations (dtx adds this in its cross-TU pass)
3. **Field sensitivity**: Struct fields tracked independently; tainting `s.len`
   doesn't taint `s.data`
4. **Indirect calls**: Function pointers not analyzed for taint rules
5. **Templates**: Taint rules match by function name; deeply templated wrappers
   around taint sources may not be recognized

---

## Recommended Approach: Option D (Curated Rules)

Manually curate taint rules, seeded from CSA's GenericTaintChecker but
owned by DTT. CSA is the starting point, not the authority.

### Rationale

1. **Maintainability**: Explicit rules are easier to audit and modify
2. **Simplicity**: No complex build dependencies on CSA internals
3. **DTT-specific**: Rules tuned for DTT's needs (e.g., strlen taints length)
4. **C++ coverage**: CSA is C-centric; DTT must cover C++ idioms
5. **Documented**: Clear provenance, deviations from CSA noted

### Design Options Considered

| Option | Approach | Rejected because |
|--------|----------|-----------------|
| A | Copy CSA rules verbatim | Duplicates maintenance, no C++ coverage |
| B | Link against CSA libraries | Heavy dependency, different state model |
| C | Code-gen from CSA at build time | Fragile, complex build system |
| **D** | **Curated rules, seeded from CSA** | **(Selected)** |

---

## Implementation Status

| Phase | Status | Notes |
|-------|--------|-------|
| **Phase 1: Core Infrastructure** | **Done** | TaintEnv, `.dtt` parser, source/sink/propagator engine |
| **Phase 1: Rule Catalog** | **Done** | 31 sources, 88 propagators, 20 sinks in `libc.dtt`, cross-verified against CSA LLVM 18 |
| **Phase 1b: Z3 Taint Guards** | **Done** | Sound Z3-backed guard analysis replaces heuristic attenuation; handles `&&`, `\|\|`, `!`, `==`, `!=`; rejects tainted-vs-tainted bounds; type-range constraints for unsigned; `sizeof`/ternary/`CallExpr` support in Z3 expressions |
| **Phase 1c: Cast Propagation** | **Done** | Taint survives C-style casts (`(unsigned int)atoi(env)`) and `static_cast`; separate taint-only fallback in `VisitVarDecl` |
| Phase 2: argv tainting | Not started | `main(argc, argv)` auto-taint |
| Phase 3: C++ containers | Not started | `std::string`, `std::vector` taint propagation |
| Phase 4: Cross-TU serialization | Not started | Taint metadata in SARIF/module interface |

Tests: `1800_taint_source_fail`, `1801_taint_propagation_fail`, `1802_taint_guarded_pass`, `1803_taint_sanitizer_pass`, `1805_taint_heartbleed_fix_pass`, `1808_taint_z3_guard_patterns_pass`, `1809_taint_z3_tainted_bound_fail`, `1810_taint_cast_propagation_fail`.

## Implementation Plan

### Phase 1: Core Infrastructure

#### 1.1 Add TaintEnv to DTTVisitor

```cpp
// Taint provenance for better diagnostics and dtx consumption
struct TaintInfo {
    SourceLocation origin;          // Where taint was introduced
    llvm::SmallString<16> sourceFn; // "recv", "scanf", etc. (owned, not StringRef)
    enum RiskLevel : uint8_t {
        Low    = 0,  // File I/O, local system info
        Medium = 1,  // Environment, command line, local sockets
        High   = 2   // Network, direct user input (stdin)
    } risk;
};

class DTTVisitor {
    // Existing
    // ... TypedEnv, GlobalProofs ...

    // NEW: Taint tracking with provenance
    DenseMap<FieldPath, TaintInfo> TaintedPaths;

    // Taint bound clearing uses the existing ProofStore (GlobalProofs)
    // rather than a separate ProvenBounds map. When a guard like
    // `if (len <= sizeof(buf))` is proven, the proof is added to
    // GlobalProofs via addProofTyped(). Sink checks query GlobalProofs
    // via the existing entailment logic to determine if a tainted
    // value has been sufficiently bounded.

    bool isTainted(const FieldPath &Path) const {
        return TaintedPaths.count(Path);
    }

    const TaintInfo *getTaintInfo(const FieldPath &Path) const {
        auto It = TaintedPaths.find(Path);
        return It != TaintedPaths.end() ? &It->second : nullptr;
    }

    void setTainted(const FieldPath &Path, TaintInfo Info) {
        TaintedPaths[Path] = Info;
    }

    void clearTaint(const FieldPath &Path) {
        TaintedPaths.erase(Path);
    }
};
```

**Key Design Decisions**:

1. **`sourceFn` is `SmallString<16>`, not `StringRef`**: Avoids dangling pointer
   risk if name ever comes from a temporary. All current source names fit in 16 bytes.

2. **No separate `ProvenBounds` map**: The existing `ProofStore GlobalProofs` already
   tracks proven constraints per FieldPath with entailment reasoning. When a taint
   guard is proven (e.g., `len <= sizeof(buf)`), add `Proof{"", ProofOp::LE, 64}`
   to `GlobalProofs`. Sink checks query `GlobalProofs` via `canProve()`. This avoids
   a redundant data structure and leverages the already-debugged entailment table.

3. **Taint and proof are orthogonal dimensions**. A value can be:
   - Tainted + unproven → Warning (highest priority)
   - Tainted + proven → OK (guard discharged the risk)
   - Untainted + unproven → Warning (lower priority than tainted)
   - Untainted + proven → OK

#### 1.2 Taint Propagation on Assignment

The most basic propagation mechanism. Any assignment from a tainted value
taints the destination:

```cpp
// In VisitBinaryOperator or VisitDeclStmt:
void DTTVisitor::propagateTaintOnAssign(const FieldPath &Dst,
                                         const FieldPath &Src) {
    if (const TaintInfo *Info = getTaintInfo(Src)) {
        setTainted(Dst, *Info);
    }
}
```

This covers:
```cpp
size_t len;
recv(fd, &len, sizeof(len), 0);  // len tainted
size_t other = len;               // other tainted (assignment propagation)
size_t copy;
copy = other;                     // copy tainted (assignment propagation)
```

Assignment propagation is implicit and always active. It is the foundation
that all other propagation builds on.

#### 1.3 Define Taint Rule Structures

```cpp
// taint_rules.h
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace dtt::taint {

// Argument index constants
constexpr int RetVal = -1;
constexpr int Variadic = -2;  // All variadic args
constexpr int ThisPtr = -3;   // C++ 'this' pointer (for methods)

struct SourceRule {
    StringRef FuncName;
    SmallVector<int, 4> TaintedArgs;  // Which args/retval get tainted
    TaintInfo::RiskLevel Risk;
};

struct PropagatorRule {
    StringRef FuncName;
    SmallVector<int, 4> SrcArgs;   // If any of these tainted...
    SmallVector<int, 4> DstArgs;   // ...these become tainted
};

struct SinkRule {
    StringRef FuncName;
    SmallVector<int, 4> SinkArgs;  // Warn if these are tainted
    StringRef DiagMessage;
    StringRef CWE;
};

struct SanitizerRule {
    StringRef FuncName;
    SmallVector<int, 4> SanitizedArgs;  // These args are cleared of taint
    // RetVal means return value is sanitized version of input
};

// Protocol classification for socket()
enum class ProtocolSafety { Safe, Unsafe, Unknown };
ProtocolSafety classifyProtocol(int AddressFamily);

} // namespace dtt::taint
```

**Rule lookup**: Build `StringMap<const SourceRule*>`, `StringMap<const PropagatorRule*>`,
etc. at initialization for O(1) lookup per call expression. Do NOT linear-scan the
rule arrays on every `VisitCallExpr`.

#### 1.4 Populate Rules

Rules are seeded from CSA's GenericTaintChecker (LLVM 18) but owned by DTT.
Deviations from CSA are marked. DTT-specific additions are marked.

```cpp
// taint_rules.cpp
// Seed source: llvm-project/clang/lib/StaticAnalyzer/Checkers/
//              GenericTaintChecker.cpp (LLVM 18)
// Deviations from CSA marked with [DTT-SPECIFIC]
// Missing from CSA marked with [DTT-ADDITION]

namespace dtt::taint {

// ============================================================
//  TAINT SOURCES
//  Functions that introduce taint from external/untrusted data.
// ============================================================

const SourceRule Sources[] = {

    // --- Program arguments [DTT-ADDITION] ---
    // argv is attacker-controlled in most threat models.
    // Handled specially: in main(argc, argv), all argv elements
    // are tainted. See Phase 2 for implementation.

    // --- stdin / character input ---
    {"getchar",          {RetVal},       High},
    {"getchar_unlocked", {RetVal},       High},
    {"getch",            {RetVal},       High},
    {"wgetch",           {RetVal},       High},
    {"_IO_getc",         {RetVal},       High},

    // --- Line input (always taints buffer, regardless of source) ---
    {"gets",             {0, RetVal},    High},
    {"gets_s",           {0, RetVal},    High},

    // --- Formatted input (taints all output arguments) ---
    {"scanf",            {Variadic},     High},
    {"scanf_s",          {Variadic},     High},
    {"vscanf",           {Variadic},     High},

    // --- Environment ---
    {"getenv",           {RetVal},       Medium},
    {"secure_getenv",    {RetVal},       Medium},

    // --- System info ---
    {"getcwd",           {0, RetVal},    Low},
    {"getwd",            {0, RetVal},    Low},
    {"get_current_dir_name", {RetVal},   Low},
    {"gethostname",      {0},            Low},
    {"getnameinfo",      {2, 4},         Low},   // host and serv buffers
    {"getlogin",         {RetVal},       Low},
    {"getlogin_r",       {0},            Low},
    {"getgroups",        {1},            Low},   // group list buffer
    {"ttyname",          {RetVal},       Low},
    {"ttyname_r",        {1},            Low},

    // --- Symbolic links ---
    {"readlink",         {1, RetVal},    Low},
    {"readlinkat",       {2, RetVal},    Low},

    // --- File I/O (opening files returns tainted handles) ---
    // Note: FILE* taint is a propagation vehicle, not a sink target.
    // The useful taint flows when data is read through the FILE*.
    {"fopen",            {RetVal},       Low},
    {"fdopen",           {RetVal},       Low},
    {"freopen",          {RetVal},       Low},
    {"popen",            {RetVal},       Low},   // [DTT-ADDITION] not in CSA sources

    // --- Network I/O [DTT-SPECIFIC: treated as sources, not just propagators] ---
    // Rationale: recv() on any fd introduces external data into the program.
    // CSA treats recv as a propagator (fd must be pre-tainted). DTT treats
    // network read functions as unconditional taint sources because:
    //   1. The fd often comes from accept() or a parameter (not locally tainted)
    //   2. Any data from recv() is attacker-controlled by definition
    //   3. Requiring pre-tainted fd misses most real-world server code
    {"recv",             {1, RetVal},    High},
    {"recvfrom",         {1, RetVal},    High},
    {"recvmsg",          {1, RetVal},    High},
    {"recvmmsg",         {1, RetVal},    High},  // [DTT-ADDITION]
};

// ============================================================
//  TAINT PROPAGATORS
//  Functions that spread taint from inputs to outputs.
//  Taint flows from SrcArgs to DstArgs only if a SrcArg is tainted.
// ============================================================

const PropagatorRule Propagators[] = {

    // --- File/fd I/O (tainted fd/FILE* taints buffer) ---
    {"read",             {0}, {1, RetVal}},
    {"pread",            {0}, {1, RetVal}},
    {"readv",            {0}, {1, RetVal}},
    {"preadv",           {0}, {1, RetVal}},
    {"fread",            {3}, {0, RetVal}},   // FILE* is arg3
    {"fgetc",            {0}, {RetVal}},
    {"fgets",            {2}, {0, RetVal}},   // FILE* is arg2
    {"fgetln",           {0}, {RetVal}},
    {"fgetws",           {2}, {0, RetVal}},   // FILE* is arg2
    {"getc",             {0}, {RetVal}},
    {"getc_unlocked",    {0}, {RetVal}},
    {"getw",             {0}, {RetVal}},
    {"getline",          {2}, {0, RetVal}},   // FILE* is arg2
    {"getdelim",         {3}, {0, RetVal}},   // FILE* is arg3
    {"fscanf",           {0}, {Variadic}},    // FILE* taints output args
    {"fscanf_s",         {0}, {Variadic}},
    {"sscanf",           {0}, {Variadic}},    // src string taints output args
    {"sscanf_s",         {0}, {Variadic}},
    {"vfscanf",          {0}, {Variadic}},
    {"vsscanf",          {0}, {Variadic}},

    // --- Socket (tainted listen fd -> tainted accepted fd) ---
    {"accept",           {0}, {RetVal}},      // [DTT-ADDITION]

    // --- Memory copy (tainted src -> tainted dst) ---
    {"memcpy",           {1}, {0, RetVal}},
    {"memmove",          {1}, {0, RetVal}},
    {"mempcpy",          {1}, {0, RetVal}},
    {"bcopy",            {0}, {1}},           // bcopy(src, dst, n) -- reversed args
    {"wmemcpy",          {1}, {0, RetVal}},
    {"wmemmove",         {1}, {0, RetVal}},
    // Note: memset does NOT propagate taint (it overwrites with a constant)

    // --- String copy ---
    {"strcpy",           {1}, {0, RetVal}},
    {"strncpy",          {1}, {0, RetVal}},
    {"stpcpy",           {1}, {0, RetVal}},
    {"stpncpy",          {1}, {0, RetVal}},
    {"strlcpy",          {1}, {0, RetVal}},
    {"wcscpy",           {1}, {0, RetVal}},
    {"wcsncpy",          {1}, {0, RetVal}},

    // --- String concat (tainted src taints dst) ---
    {"strcat",           {1}, {0, RetVal}},
    {"strncat",          {1}, {0, RetVal}},
    {"strlcat",          {1}, {0, RetVal}},
    {"wcscat",           {1}, {0, RetVal}},
    {"wcsncat",          {1}, {0, RetVal}},

    // --- String duplication ---
    {"strdup",           {0}, {RetVal}},
    {"strndup",          {0}, {RetVal}},
    {"strdupa",          {0}, {RetVal}},
    {"strndupa",         {0}, {RetVal}},
    {"wcsdup",           {0}, {RetVal}},

    // --- String search (tainted haystack -> tainted result pointer) ---
    {"strstr",           {0}, {RetVal}},
    {"strcasestr",       {0}, {RetVal}},
    {"memchr",           {0}, {RetVal}},
    {"memrchr",          {0}, {RetVal}},
    {"rawmemchr",        {0}, {RetVal}},
    {"strchr",           {0}, {RetVal}},
    {"strrchr",          {0}, {RetVal}},
    {"strchrnul",        {0}, {RetVal}},
    {"index",            {0}, {RetVal}},
    {"rindex",           {0}, {RetVal}},
    {"strpbrk",          {0}, {RetVal}},
    {"memmem",           {0}, {RetVal}},

    // --- String comparison (tainted input -> tainted result) ---
    // Rationale: result reveals information about tainted content.
    // CSA propagates these; DTT follows suit.
    {"strcmp",            {0, 1}, {RetVal}},
    {"strcasecmp",        {0, 1}, {RetVal}},
    {"strncmp",           {0, 1}, {RetVal}},
    {"strncasecmp",       {0, 1}, {RetVal}},
    {"memcmp",            {0, 1}, {RetVal}},
    {"strspn",            {0}, {RetVal}},
    {"strcspn",           {0}, {RetVal}},

    // --- Formatted output (tainted format args -> tainted buffer) ---
    {"sprintf",          {Variadic}, {0}},
    {"snprintf",         {Variadic}, {0}},
    {"vsprintf",         {1}, {0}},
    {"vsnprintf",        {2}, {0}},
    {"__sprintf_chk",    {Variadic}, {0}},
    {"__snprintf_chk",   {Variadic}, {0}},

    // --- Numeric conversion (tainted string -> tainted number) ---
    {"strtol",           {0}, {RetVal}},
    {"strtoll",          {0}, {RetVal}},
    {"strtoul",          {0}, {RetVal}},
    {"strtoull",         {0}, {RetVal}},
    {"strtof",           {0}, {RetVal}},
    {"strtod",           {0}, {RetVal}},
    {"strtold",          {0}, {RetVal}},
    {"atoi",             {0}, {RetVal}},
    {"atol",             {0}, {RetVal}},
    {"atoll",            {0}, {RetVal}},
    {"atof",             {0}, {RetVal}},

    // --- Character classification / conversion ---
    // If input is tainted, output is tainted.
    {"tolower",          {0}, {RetVal}},
    {"toupper",          {0}, {RetVal}},
    {"isalnum",          {0}, {RetVal}},
    {"isalpha",          {0}, {RetVal}},
    {"isascii",          {0}, {RetVal}},
    {"isblank",          {0}, {RetVal}},
    {"iscntrl",          {0}, {RetVal}},
    {"isdigit",          {0}, {RetVal}},
    {"isgraph",          {0}, {RetVal}},
    {"islower",          {0}, {RetVal}},
    {"isprint",          {0}, {RetVal}},
    {"ispunct",          {0}, {RetVal}},
    {"isspace",          {0}, {RetVal}},
    {"isupper",          {0}, {RetVal}},
    {"isxdigit",         {0}, {RetVal}},

    // --- Multibyte/wide character conversion ---
    {"mbtowc",           {1}, {0, RetVal}},
    {"wctomb",           {0}, {RetVal}},
    {"wcwidth",          {0}, {RetVal}},

    // --- Path operations ---
    {"basename",         {0}, {RetVal}},
    {"dirname",          {0}, {RetVal}},
    {"fnmatch",          {0, 1}, {RetVal}},
    {"realpath",         {0}, {1, RetVal}},

    // --- Sorting (tainted array -> tainted array) ---
    {"qsort",            {0}, {0}},           // sorts in place
    {"qsort_r",          {0}, {0}},

    // --- Realloc (preserves taint of original data) ---
    {"realloc",          {0}, {RetVal}},

    // --- [DTT-SPECIFIC] Length operations on tainted buffers ---
    // Rationale: In Heartbleed-style bugs, the LENGTH derived from
    // tainted data is the attack vector, not just the content.
    // CSA does NOT propagate taint through strlen(). DTT does.
    // See "Deviations from CSA" section.
    {"strlen",           {0}, {RetVal}},
    {"wcslen",           {0}, {RetVal}},
    {"strnlen",          {0}, {RetVal}},
    {"wcsnlen",          {0}, {RetVal}},
};

// ============================================================
//  TAINT SINKS
//  Functions where tainted arguments indicate a vulnerability.
//  DTT emits a warning/error when a tainted value reaches a sink
//  argument without being validated.
// ============================================================

const SinkRule Sinks[] = {

    // --- Command injection (CWE-78) ---
    {"system",           {0},
     "tainted value used as shell command",                  "CWE-78"},
    {"popen",            {0},
     "tainted value used as shell command",                  "CWE-78"},
    {"execl",            {0},
     "tainted value used as program path",                   "CWE-78"},
    {"execle",           {0},
     "tainted value used as program path",                   "CWE-78"},
    {"execlp",           {0},
     "tainted value used as program path",                   "CWE-78"},
    {"execv",            {0},
     "tainted value used as program path",                   "CWE-78"},
    {"execve",           {0},
     "tainted value used as program path",                   "CWE-78"},
    {"execvp",           {0},
     "tainted value used as program path",                   "CWE-78"},
    {"execvpe",          {0},
     "tainted value used as program path",                   "CWE-78"},
    {"fexecve",          {0},
     "tainted value used as program fd",                     "CWE-78"},

    // --- Library injection (CWE-426) ---
    {"dlopen",           {0},
     "tainted value used as library path",                   "CWE-426"},

    // --- Format string attacks (CWE-134) ---
    // Only the FORMAT argument is a sink. Data arguments are fine.
    {"printf",           {0},
     "tainted value used as format string",                  "CWE-134"},
    {"fprintf",          {1},
     "tainted value used as format string",                  "CWE-134"},
    {"sprintf",          {1},
     "tainted value used as format string",                  "CWE-134"},
    {"snprintf",         {2},
     "tainted value used as format string",                  "CWE-134"},
    {"syslog",           {1},
     "tainted value used as format string",                  "CWE-134"},
    {"setproctitle",     {0},
     "tainted value used as format string",                  "CWE-134"},

    // --- Path traversal (CWE-22) [DTT-ADDITION] ---
    {"open",             {0},
     "tainted value used as file path",                      "CWE-22"},
    {"fopen",            {0},
     "tainted value used as file path",                      "CWE-22"},
    {"creat",            {0},
     "tainted value used as file path",                      "CWE-22"},
    {"openat",           {1},
     "tainted value used as file path",                      "CWE-22"},

    // --- Allocation size (CWE-190) [DTT-ADDITION] ---
    // Tainted allocation sizes can cause integer overflow -> heap overflow.
    {"malloc",           {0},
     "tainted value used as allocation size",                "CWE-190"},
    {"calloc",           {0, 1},
     "tainted value used as allocation size",                "CWE-190"},
    {"realloc",          {1},
     "tainted value used as allocation size",                "CWE-190"},
    {"alloca",           {0},
     "tainted value used as stack allocation size",          "CWE-190"},

    // --- Buffer operation sizes (CWE-120/CWE-126) [DTT-ADDITION] ---
    // These overlap with DTT's existing bounds checks, but the sink rule
    // provides taint-specific diagnostics with provenance.
    {"memcpy",           {2},
     "tainted value used as copy length",                    "CWE-120"},
    {"memmove",          {2},
     "tainted value used as copy length",                    "CWE-120"},
    {"memset",           {2},
     "tainted value used as fill length",                    "CWE-120"},
    {"strncpy",          {2},
     "tainted value used as copy length",                    "CWE-120"},
    {"strncat",          {2},
     "tainted value used as concat length",                  "CWE-120"},
};

// ============================================================
//  TAINT SANITIZERS
//  Functions that validate/constrain tainted values.
//  After passing through a sanitizer, the output is no longer
//  considered tainted (the risk has been mitigated).
// ============================================================

const SanitizerRule Sanitizers[] = {
    // No built-in sanitizers initially.
    // Users define sanitizers via annotations:
    //   [[clang::annotate("dtt:sanitizes=arg0")]]
    //   size_t validate_length(size_t len);
    //
    // DTT also clears taint via bounds guards (see Phase 2.2).
    //
    // Future: add common validation patterns:
    //   {"strtol_with_errno_check", {RetVal}},
    //   etc.
};

// ============================================================
//  SOCKET PROTOCOL CLASSIFICATION
//  Determines risk level for socket-derived file descriptors.
//  Used by Phase 2 to classify recv/read on known sockets.
// ============================================================

ProtocolSafety classifyProtocol(int AF) {
    switch (AF) {
    // Safe -- local/system protocols, no remote attacker
    case AF_UNIX:
    case AF_LOCAL:    // Same as AF_UNIX on Linux
    case AF_NETLINK:
        return ProtocolSafety::Safe;

    // Unsafe -- network protocols, attacker-controlled data
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

#### 1.5 C++ Standard Library Taint Sources [DTT-ADDITION]

CSA covers C functions only. DTT must handle idiomatic C++:

```cpp
// Handled specially in code, not via the C rule tables above.
// These are detected by matching on CXXMemberCallExpr / CXXOperatorCallExpr.

// --- std::cin and input streams ---
// std::cin >> x         -> x is tainted (High)
// std::getline(cin, s)  -> s is tainted (High)
// stream >> x           -> x is tainted if stream is tainted
// stream.read(buf, n)   -> buf is tainted if stream is tainted
// stream.getline(buf,n) -> buf is tainted if stream is tainted

// --- std::ifstream (file input) ---
// std::ifstream f(path) -> f is tainted (Low, file I/O)
// f >> x                -> x is tainted
// f.read(buf, n)        -> buf is tainted

// --- Detection strategy ---
// In VisitCXXOperatorCallExpr:
//   If operator is >> and LHS is std::istream (or derived):
//     Taint the RHS operand.
//     If LHS is std::cin: risk = High
//     If LHS is std::ifstream: risk = Low
//     Otherwise: propagate LHS taint status
//
// In VisitCallExpr for std::getline:
//   If first arg is tainted istream:
//     Taint the second arg (output string)
```

Implementation:

```cpp
void DTTVisitor::handleCppStreamTaint(CXXOperatorCallExpr *E) {
    if (E->getOperator() != OO_GreaterGreater)
        return;
    if (E->getNumArgs() < 2)
        return;

    Expr *Stream = E->getArg(0);
    Expr *Target = E->getArg(1);

    QualType StreamTy = Stream->getType().getCanonicalType();
    // Check if LHS is std::istream or derived
    if (!isStdIstream(StreamTy))
        return;

    FieldPath TargetPath = getTypedPath(Target);
    if (!TargetPath.isValid())
        return;

    // Determine risk level
    TaintInfo::RiskLevel Risk = TaintInfo::Low;
    if (isStdCin(Stream)) {
        Risk = TaintInfo::High;  // stdin is direct user input
    } else if (const TaintInfo *StreamTaint = getTaintInfo(getTypedPath(Stream))) {
        Risk = StreamTaint->risk;  // Propagate stream's risk level
    }

    setTainted(TargetPath, {E->getOperatorLoc(), "operator>>", Risk});
}
```

#### 1.6 `argv` as Taint Source [DTT-ADDITION]

`main(int argc, char* argv[])` -- argv is attacker-controlled. DTT detects
main() and taints all argv-derived values:

```cpp
void DTTVisitor::handleMainArgv(FunctionDecl *FD) {
    if (!FD->isMain())
        return;
    if (FD->getNumParams() < 2)
        return;

    // argv (second parameter) is tainted
    ParmVarDecl *ArgvParam = FD->getParamDecl(1);
    FieldPath ArgvPath = getPathForDecl(ArgvParam);
    if (ArgvPath.isValid()) {
        setTainted(ArgvPath,
                   {ArgvParam->getBeginLoc(), "argv", TaintInfo::Medium});
    }

    // argc (first parameter) is also tainted -- attacker controls
    // how many arguments are passed
    ParmVarDecl *ArgcParam = FD->getParamDecl(0);
    FieldPath ArgcPath = getPathForDecl(ArgcParam);
    if (ArgcPath.isValid()) {
        setTainted(ArgcPath,
                   {ArgcParam->getBeginLoc(), "argc", TaintInfo::Medium});
    }
}
```

---

### Phase 2: Integration with DTT

#### 2.1 Taint Handling in VisitCallExpr

```cpp
void DTTVisitor::handleTaintInCall(CallExpr *E) {
    const FunctionDecl *Callee = E->getDirectCallee();
    if (!Callee)
        return;

    // Guard: getName() crashes on operators, constructors, etc.
    if (!Callee->getIdentifier())
        return;

    StringRef Fn = Callee->getName();

    // 1. Check taint sources
    if (const SourceRule *Rule = findSourceRule(Fn)) {
        applySourceRule(E, *Rule);
    }

    // 2. Check taint propagation
    if (const PropagatorRule *Rule = findPropagatorRule(Fn)) {
        applyPropagatorRule(E, *Rule);
    }

    // 3. Check taint sinks
    if (const SinkRule *Rule = findSinkRule(Fn)) {
        checkSinkRule(E, *Rule);
    }

    // 4. Check taint sanitizers
    if (const SanitizerRule *Rule = findSanitizerRule(Fn)) {
        applySanitizerRule(E, *Rule);
    }

    // 5. Special case: socket() with protocol analysis
    if (Fn == "socket" && E->getNumArgs() >= 1) {
        handleSocketCall(E);
    }
}
```

**Variadic handling**: `getFirstVariadicArg()` inspects the `FunctionDecl`'s
parameter list. The first variadic arg index equals `Callee->getNumParams()`
(i.e., the first arg beyond the declared non-variadic params). This requires
the callee to have `isVariadic()` returning true.

```cpp
void DTTVisitor::applySourceRule(CallExpr *E, const SourceRule &Rule) {
    TaintInfo Info = {E->getBeginLoc(), Rule.FuncName, Rule.Risk};

    for (int ArgIdx : Rule.TaintedArgs) {
        if (ArgIdx == taint::RetVal) {
            PendingTaintedReturn = Info;
        } else if (ArgIdx == taint::Variadic) {
            unsigned FirstVariadic = E->getDirectCallee()->getNumParams();
            for (unsigned I = FirstVariadic; I < E->getNumArgs(); ++I) {
                if (FieldPath Path = getTypedPath(E->getArg(I));
                    Path.isValid()) {
                    setTainted(Path, Info);
                }
            }
        } else if (ArgIdx >= 0 &&
                   ArgIdx < static_cast<int>(E->getNumArgs())) {
            if (FieldPath Path = getTypedPath(E->getArg(ArgIdx));
                Path.isValid()) {
                setTainted(Path, Info);
            }
        }
    }
}

void DTTVisitor::checkSinkRule(CallExpr *E, const SinkRule &Rule) {
    for (int ArgIdx : Rule.SinkArgs) {
        if (ArgIdx < 0 || ArgIdx >= static_cast<int>(E->getNumArgs()))
            continue;

        FieldPath Path = getTypedPath(E->getArg(ArgIdx));
        if (!Path.isValid())
            continue;

        const TaintInfo *Info = getTaintInfo(Path);
        if (!Info)
            continue;

        // Check if taint has been discharged by a proven bound.
        // Uses existing ProofStore entailment — no separate ProvenBounds map.
        if (hasTaintBoundProven(Path))
            continue;

        // Emit sink violation diagnostic
        emitTaintSinkWarning(E, Path, *Info, Rule);
    }
}
```

**`PendingTaintedReturn` lifecycle**: Set in `applySourceRule` when `RetVal`
is in the tainted args list. Consumed in `VisitDeclStmt` or `VisitBinaryOperator`
when the call result is assigned to a variable. Cleared after consumption.
If the return value is never assigned (statement expression), it is discarded
at end of statement processing.

#### 2.2 Taint Clearing on Bounds Proof

Uses existing `ProofStore` instead of a separate `ProvenBounds` map:

```cpp
void DTTVisitor::handleGuardForTaint(BinaryOperator *Cmp,
                                      bool InTrueBranch) {
    // Pattern: if (len <= bufSize) or if (len < bufSize)
    if (!Cmp->isComparisonOp())
        return;

    Expr *Smaller, *Larger;
    if (Cmp->getOpcode() == BO_LE || Cmp->getOpcode() == BO_LT) {
        Smaller = Cmp->getLHS();
        Larger = Cmp->getRHS();
    } else if (Cmp->getOpcode() == BO_GE || Cmp->getOpcode() == BO_GT) {
        Smaller = Cmp->getRHS();
        Larger = Cmp->getLHS();
    } else {
        return;
    }

    FieldPath SmallerPath = getTypedPath(Smaller);
    if (!SmallerPath.isValid() || !isTainted(SmallerPath))
        return;  // Not tainted, nothing to clear

    if (InTrueBranch) {
        // Inside the guard: the existing proof infrastructure already
        // added this to GlobalProofs via addProofTyped(). Taint remains
        // (for provenance tracking in SARIF output) but sink checks
        // query GlobalProofs and will find the bound proven.
        //
        // No additional data structure needed — the proof is already there.
    }
}

// Sink check queries existing ProofStore:
bool DTTVisitor::hasTaintBoundProven(const FieldPath &Path) const {
    // If GlobalProofs has any upper bound for this path, the taint
    // has been discharged for bounds-related sinks.
    auto Proofs = GlobalProofs.getProofs(Path);
    if (!Proofs) return false;
    for (const auto &P : *Proofs) {
        if (P.op == ProofOp::LE || P.op == ProofOp::LT)
            return true;
    }
    return false;
}
```

#### 2.3 User-Defined Sanitizers via Annotations

```cpp
// Users can declare functions that clear taint:
//
//   [[clang::annotate("dtt:sanitizes=arg0")]]
//   size_t validate_length(size_t len);
//
//   [[clang::annotate("dtt:sanitizes=retval")]]
//   char* sanitize_input(const char* input);
//
// When such a function is called, the specified arguments/return
// are cleared of taint in the taint environment.

void DTTVisitor::applySanitizerRule(CallExpr *E,
                                     const SanitizerRule &Rule) {
    for (int ArgIdx : Rule.SanitizedArgs) {
        if (ArgIdx == taint::RetVal) {
            PendingSanitizedReturn = true;
        } else if (ArgIdx >= 0 &&
                   ArgIdx < static_cast<int>(E->getNumArgs())) {
            FieldPath Path = getTypedPath(E->getArg(ArgIdx));
            if (Path.isValid()) {
                clearTaint(Path);
            }
        }
    }
}
```

#### 2.4 Arithmetic Taint Propagation

```cpp
// Any arithmetic operation (+, -, *, /, %, <<, >>) on a tainted
// value produces a tainted result.
//
//   size_t len;
//   recv(fd, &len, sizeof(len), 0);  // len tainted
//   size_t padded = len + 16;        // padded is also tainted
//   memcpy(buf, src, padded);        // should warn

void DTTVisitor::propagateArithmeticTaint(BinaryOperator *BO) {
    if (!BO->getType()->isIntegerType())
        return;

    if (!BO->isMultiplicativeOp() && !BO->isAdditiveOp() &&
        !BO->isShiftOp() && BO->getOpcode() != BO_Rem)
        return;

    FieldPath LHSPath = getTypedPath(BO->getLHS());
    FieldPath RHSPath = getTypedPath(BO->getRHS());

    const TaintInfo *LHSTaint =
        LHSPath.isValid() ? getTaintInfo(LHSPath) : nullptr;
    const TaintInfo *RHSTaint =
        RHSPath.isValid() ? getTaintInfo(RHSPath) : nullptr;

    if (LHSTaint || RHSTaint) {
        PendingArithmeticTaint = LHSTaint ? *LHSTaint : *RHSTaint;
    }
}
```

#### 2.5 Cast Taint Propagation

```cpp
// Casts preserve taint:
//   int tainted_int = ...;
//   size_t len = (size_t)tainted_int;   // len is tainted
//   char c = (char)tainted_int;         // c is tainted

void DTTVisitor::propagateCastTaint(CastExpr *CE) {
    FieldPath SrcPath = getTypedPath(CE->getSubExpr());
    if (!SrcPath.isValid())
        return;

    if (const TaintInfo *Info = getTaintInfo(SrcPath)) {
        PendingCastTaint = *Info;
    }
}
```

#### 2.6 Modified BoundsResult with Taint Awareness

The existing `BoundsResult` enum (DTTPlugin.cpp:15259) has these variants:

```cpp
enum class BoundsResult {
    Safe,                    // Proven safe
    Overflow,                // Constant overflow detected (Category F)
    UnverifiedConst,         // Constant bounds unverified (Category C/D)
    UnverifiedSymbolic,      // Symbolic bounds unverified (Category C/D)
    UnverifiedUntracked,     // Buffer untracked (Category A/B)
    UnverifiedTainted,       // NEW: Unknown AND value is tainted (highest priority)
    Skip                     // No size info
};
```

Add `UnverifiedTainted` between `UnverifiedUntracked` and `Skip`.
The `checkBufferBounds` lambda gains taint awareness:

```cpp
auto checkBufferBounds = [&](const SymValue& opSize,
                              const SymValue& bufSize,
                              const FieldPath& opSizePath) -> BoundsResult {
    if (bufSize.kind == SymValue::UNKNOWN) {
        if (opSize.kind != SymValue::UNKNOWN)
            return BoundsResult::UnverifiedUntracked;
        return BoundsResult::Skip;
    }

    auto proven = checkBoundsProven(opSize, bufSize);
    if (proven.has_value() && *proven)
        return BoundsResult::Safe;

    bool isTaintedUnbounded = opSizePath.isValid()
                              && isTainted(opSizePath)
                              && !hasTaintBoundProven(opSizePath);

    if (bufSize.kind == SymValue::CONST) {
        if (opSize.kind == SymValue::CONST &&
            opSize.constVal > bufSize.constVal)
            return BoundsResult::Overflow;
        return isTaintedUnbounded ? BoundsResult::UnverifiedTainted
                                  : BoundsResult::UnverifiedConst;
    }

    return isTaintedUnbounded ? BoundsResult::UnverifiedTainted
                              : BoundsResult::UnverifiedSymbolic;
};
```

#### 2.7 Interaction with Existing DTT Diagnostics

When `memcpy(buf, src, tainted_len)` triggers both a regular bounds check
(existing DTT) and a taint sink check (new), the **taint diagnostic takes
priority** and the regular diagnostic is suppressed. This avoids duplicate
warnings for the same issue. The taint diagnostic is strictly more informative
(includes provenance, source function, risk level).

Implementation: In the `BoundsResult` switch, `UnverifiedTainted` emits the
taint-specific diagnostic instead of the regular unverified-bounds diagnostic.

#### 2.8 `std::move` Interaction

When `std::move` transfers a value, taint follows:

```cpp
// In the existing std::move handler:
if (isStdMove(Init)) {
    FieldPath srcPath = getTypedPath(getMovedFromArg(Init));
    FieldPath dstPath = getTypedPath(D);
    // ... existing property transfer ...

    // Transfer taint: destination gets source's taint, source cleared
    if (const TaintInfo *Info = getTaintInfo(srcPath)) {
        setTainted(dstPath, *Info);
        clearTaint(srcPath);
    }
}
```

#### 2.9 Composition with Existing libc.dtt Annotations

recv/scanf/getenv don't currently have libc.dtt annotations that conflict
with taint source rules. `fopen` has ghost-field postconditions (`@open`)
which are orthogonal — ghost fields track resource state, taint tracks
data provenance. Both fire on the same call without conflict:

```
fopen("path", "r")
  → libc.dtt: result.@open = OPEN when result._nonnull != 0
  → taint:    result is tainted (Low risk, FILE* propagation vehicle)
```

No special composition logic needed — the two systems use separate data
stores (`TypedEnv`/ghost fields vs `TaintedPaths`).

#### 2.10 New Diagnostics

```cpp
// Tainted bounds
ModeDependentDiag DiagTaintedBoundsUnverified;
DiagTaintedBoundsUnverified.warn = Diag.getCustomDiagID(
    DiagnosticsEngine::Warning,
    "DTT: cannot verify bounds for tainted value from "
    "untrusted source (CWE-126) [-Wdtt-tainted-bounds]");
DiagTaintedBoundsUnverified.err = Diag.getCustomDiagID(
    DiagnosticsEngine::Error,
    "DTT: cannot verify bounds for tainted value from "
    "untrusted source (CWE-126)");

// Taint sink violations
unsigned DiagTaintSink = Diag.getCustomDiagID(
    DiagnosticsEngine::Warning,
    "DTT: %0 (tainted by '%1' at %2) [-Wdtt-taint-sink]");

// Notes
unsigned NoteTaintSource = Diag.getCustomDiagID(
    DiagnosticsEngine::Note,
    "value was tainted here by call to '%0'");

unsigned NoteProveBounds = Diag.getCustomDiagID(
    DiagnosticsEngine::Note,
    "add bounds check: if (%0 <= buffer_size) { ... }");

unsigned NoteSanitize = Diag.getCustomDiagID(
    DiagnosticsEngine::Note,
    "or validate with a sanitizer function annotated "
    "[[clang::annotate(\"dtt:sanitizes=...\")]]");
```

#### 2.11 SARIF Integration

The existing SARIF infrastructure (DTTSarif.h/cpp) needs two additions:

**1. New SARIF rules** (in DTTSarif.cpp):

```cpp
const SarifRule RuleTaintedBounds = {
    "DTT-tainted-bounds", "Unverified tainted buffer bounds",
    "https://cwe.mitre.org/data/definitions/126.html", "CWE-126"};
const SarifRule RuleTaintSink = {
    "DTT-taint-sink", "Tainted value at dangerous sink",
    "https://cwe.mitre.org/data/definitions/20.html", "CWE-20"};
// CWE for sink varies by sink type; CWE-20 is the umbrella.
// Specific CWE (78, 134, 22, 190, 120, 426) goes in the message text.
```

**2. Taint metadata in SARIF `properties` bag** (for dtx consumption):

Add an optional `TaintMetadata` field to `CapturedResult`:

```cpp
struct CapturedResult {
    const SarifRule *Rule;
    DiagnosticsEngine::Level Level;
    SourceLocation Loc;
    SmallString<256> Message;
    SmallVector<NoteInfo, 2> Notes;

    // NEW: Optional taint provenance (null if not a taint finding)
    struct TaintMeta {
        SmallString<16> sourceFn;   // "recv", "scanf", etc.
        SourceLocation originLoc;   // Where taint was introduced
        uint8_t riskLevel;          // 0=Low, 1=Medium, 2=High
    };
    std::optional<TaintMeta> Taint;
};
```

In `flush()`, when `Taint` is present, emit a `properties` object:

```json
{
  "ruleId": "DTT-tainted-bounds",
  "level": "warning",
  "message": { "text": "..." },
  "locations": [...],
  "properties": {
    "dtt:taint": {
      "sourceFunction": "recv",
      "originFile": "server.cpp",
      "originLine": 42,
      "riskLevel": "high"
    }
  }
}
```

**Registration** (in `HandleTranslationUnit`):
```cpp
GlobalSarifConsumer->registerDiag(DiagTaintedBoundsUnverified.warn,
                                   &dtt::RuleTaintedBounds);
GlobalSarifConsumer->registerDiag(DiagTaintedBoundsUnverified.err,
                                   &dtt::RuleTaintedBounds);
GlobalSarifConsumer->registerDiag(DiagTaintSink,
                                   &dtt::RuleTaintSink);
GlobalSarifConsumer->registerNote(NoteTaintSource);
```

---

### Phase 3: Path-Sensitive Taint Tracking

#### 3.1 Save/Restore Taint State in Branches

Mirrors the existing pattern for `TypedEnv`, `PointerLifetimes`,
`VarToResource`, and `GlobalProofs` (see DTTPlugin.cpp:12077-12124):

```cpp
// In TraverseIfStmt, alongside existing state saves:
auto savedTaint = TaintedPaths;

// ... traverse then/else ...

// At merge points, use same logic as TypedEnv:
// - If then terminates (early return): restore saved taint
// - If else terminates: keep then-taint
// - Neither terminates: merge (union of taint)
```

Merge rule: **union of taint** (tainted in either branch → tainted after).
This is conservative and sound. Proofs are already merged via the existing
`GlobalProofs` save/restore/merge logic.

#### 3.2 Loop Handling

DTT doesn't currently do fixed-point iteration for loops — it traverses
the body once with loop-variable bounds established by the condition.
Taint follows the same model:

- Save `TaintedPaths` before loop body
- Traverse body (may add new tainted paths)
- Taint only grows inside loops (never removed), so one iteration is
  sufficient for soundness — the taint set after one pass is a fixpoint

No special loop handling needed beyond the existing traversal.

#### 3.3 Switch Statements

The existing `VisitSwitchStmt` (DTTPlugin.cpp:12588) handles enum
exhaustiveness checking only. It does not do path-sensitive state
tracking across cases. Taint tracking follows the same simplification:

- Taint state flows through all cases without branching
- Conservative: if any case taints a variable, it remains tainted

This is sound but imprecise. Full switch path-sensitivity can be added
later if needed.

---

### Phase 4: Custom Taint Annotations

Users can extend taint tracking for their own APIs:

```cpp
// Mark a parameter as a taint source
[[clang::annotate("dtt:taints=retval")]]
char* read_user_input();

// Mark a function as a taint propagator
[[clang::annotate("dtt:propagates=arg0->retval")]]
char* transform(const char* input);

// Mark a function as a taint sanitizer
[[clang::annotate("dtt:sanitizes=arg0")]]
size_t validate_length(size_t len);

// Mark a function as a taint sink
[[clang::annotate("dtt:sink=arg0:CWE-78:tainted command")]]
void execute(const char* cmd);
```

This allows projects to annotate their own I/O wrappers, validation
functions, and security-critical APIs without modifying DTT.

**Future**: Taint rules could also be expressed in `.dtt` format for
consistency with DTT's primary annotation mechanism:

```
fn my_read(1) {
  taints result
  risk High
}

fn validate_input(1) {
  sanitizes arg0
}
```

This is deferred — `[[clang::annotate]]` is simpler to implement first.

---

## Deviations from CSA

DTT intentionally differs from Clang Static Analyzer in these cases:

| Rule | CSA behavior | DTT behavior | Rationale |
|------|-------------|-------------|-----------|
| `strlen(tainted)` | Not tainted (metadata) | Tainted | Tainted length is the Heartbleed attack vector |
| `recv()` | Propagator (requires tainted fd) | Source (always taints buffer) | Server fds come from accept()/params, not locally tainted |
| `argv` | Not a taint source | Tainted (Medium risk) | Attacker controls program arguments |
| C++ streams | Not covered | `std::cin >>` is High risk | DTT is a C++ tool |
| Sinks | Only system/exec/popen | Also malloc size, array index, format string, file path | Broader vulnerability coverage |
| Taint sanitizers | Not supported | User-defined via annotations | Real code uses validation functions |

---

## Testing Strategy

### Test Files

**1. taint_source_fail.cpp** -- Direct taint sources produce errors at sinks

```cpp
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>

void test_recv_tainted(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);  // len is tainted
    char buf[64];
    memcpy(buf, "data", len);        // ERROR: tainted copy length (CWE-120)
}

void test_scanf_tainted() {
    int n;
    scanf("%d", &n);                 // n is tainted
    char buf[32];
    memset(buf, 0, n);               // ERROR: tainted fill length (CWE-120)
}

void test_getenv_tainted() {
    char* path = getenv("HOME");     // path is tainted
    fopen(path, "r");                // ERROR: tainted file path (CWE-22)
}

void test_command_injection() {
    char cmd[256];
    scanf("%255s", cmd);             // cmd is tainted
    system(cmd);                     // ERROR: tainted shell command (CWE-78)
}

void test_format_string() {
    char fmt[64];
    scanf("%63s", fmt);              // fmt is tainted
    printf(fmt);                     // ERROR: tainted format string (CWE-134)
}

void test_malloc_size(int fd) {
    size_t sz;
    recv(fd, &sz, sizeof(sz), 0);   // sz is tainted
    malloc(sz);                      // ERROR: tainted allocation size (CWE-190)
}
```

**2. taint_propagation_fail.cpp** -- Taint propagates through operations

```cpp
void test_assignment_propagation(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);
    size_t copy = len;               // copy is tainted (assignment)
    char buf[64];
    memcpy(buf, "data", copy);       // ERROR: tainted
}

void test_arithmetic_propagation(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);
    size_t padded = len + 16;        // padded is tainted
    size_t doubled = len * 2;        // doubled is tainted
    char buf[64];
    memcpy(buf, "data", padded);     // ERROR: tainted
}

void test_strlen_propagation(int fd) {
    char input[100];
    recv(fd, input, sizeof(input), 0);
    size_t len = strlen(input);      // len is tainted (DTT-specific)
    char buf[64];
    memcpy(buf, "x", len);           // ERROR: tainted
}

void test_cast_propagation(int fd) {
    int tainted_int;
    recv(fd, &tainted_int, sizeof(tainted_int), 0);
    size_t len = (size_t)tainted_int; // len is tainted (cast preserves)
    char buf[64];
    memcpy(buf, "data", len);         // ERROR: tainted
}

void test_chained_propagation(int fd) {
    char input[100];
    recv(fd, input, sizeof(input), 0);
    char temp[100];
    memcpy(temp, input, 50);         // temp is tainted
    char final_buf[100];
    strcpy(final_buf, temp);         // final_buf is tainted
    size_t len = strlen(final_buf);  // len is tainted
    char buf[32];
    memcpy(buf, "x", len);           // ERROR: tainted
}
```

**3. taint_guarded_pass.cpp** -- Proper guards clear taint for bounds

```cpp
void test_bounds_guard(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);
    char buf[64];
    if (len <= sizeof(buf)) {
        memcpy(buf, "data", len);    // OK: guard proves len <= 64
    }
}

void test_early_return_guard(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);
    char buf[64];
    if (len > sizeof(buf))
        return;
    memcpy(buf, "data", len);        // OK: early return establishes bound
}
```

**4. taint_argv_fail.cpp** -- argv is tainted

```cpp
int main(int argc, char* argv[]) {
    if (argc > 1) {
        system(argv[1]);             // ERROR: tainted shell command (CWE-78)
    }
    if (argc > 2) {
        fopen(argv[2], "r");         // ERROR: tainted file path (CWE-22)
    }
    return 0;
}
```

**5. taint_cpp_streams_fail.cpp** -- C++ stream input is tainted

```cpp
#include <iostream>
#include <string>
#include <cstdlib>

void test_cin_tainted() {
    std::string cmd;
    std::cin >> cmd;                 // cmd is tainted
    system(cmd.c_str());             // ERROR: tainted shell command (CWE-78)
}

void test_getline_tainted() {
    std::string line;
    std::getline(std::cin, line);    // line is tainted
    system(line.c_str());            // ERROR: tainted shell command
}
```

**6. taint_sanitizer_pass.cpp** -- User-defined sanitizers clear taint

```cpp
[[clang::annotate("dtt:sanitizes=arg0")]]
size_t validate_length(size_t len);

void test_sanitized(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);  // len is tainted
    len = validate_length(len);       // len is sanitized (no longer tainted)
    char buf[64];
    memcpy(buf, "data", len);         // OK: sanitized
}
```

**7. taint_safe_socket_pass.cpp** -- Local sockets at lower risk

```cpp
void test_unix_socket() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    // recv on a known-local socket still introduces taint (it's still
    // external data) but at Low risk level, not High.
    size_t len;
    recv(fd, &len, sizeof(len), 0);
    // Tainted (Low risk), but DTT still warns on unverified bounds.
}
```

**8. taint_internal_pass.cpp** -- Internal code doesn't trigger taint warnings

```cpp
void test_internal_copy() {
    size_t len = compute_length();  // Not tainted (internal function)
    char* buf = (char*)malloc(len);
    memcpy(buf, "data", len);       // OK: not tainted
    // May still produce a regular DTT warning (unverified-symbolic)
    // but NOT a tainted-bounds warning.
    free(buf);
}
```

**9. taint_move_fail.cpp** -- std::move transfers taint

```cpp
#include <string>
#include <cstdlib>

void test_move_tainted() {
    std::string cmd;
    std::cin >> cmd;                  // cmd is tainted
    std::string moved = std::move(cmd); // moved is tainted, cmd cleared
    system(moved.c_str());            // ERROR: tainted shell command
}
```

---

## Open Questions

1. **Interprocedural taint**: If a function receives a tainted parameter,
   should the caller know?
   - Phase 4: `[[clang::annotate("dtt:tainted")]]` on parameters
   - dtx handles this via cross-TU analysis

2. **Taint through pointers**: If `*ptr` is tainted, is `ptr[1]` tainted?
   - Conservative: yes, entire buffer is tainted when any element is

3. **Taint decay**: Should taint clear after N operations?
   - No. Explicit proof (guard or sanitizer) required.

4. **Taint through struct members**: If `s.data` is tainted, is `s.len` tainted?
   - No. Field-independent tracking. Each field tracked separately.

5. **`fopen` dual role**: fopen appears in both Sources (returns tainted FILE*)
   and Sinks (tainted path argument). Is this a conflict?
   - No conflict. They check different arguments.
   - Source: `fopen(...)` -> return value is tainted (the FILE* handle)
   - Sink: `fopen(TAINTED_PATH, ...)` -> arg0 is a tainted file path (CWE-22)
   - Both can fire on the same call if the path is tainted.

6. **recv as source vs propagator**: Why not keep recv as a propagator?
   - See "Deviations from CSA" section. In practice, server fds come from
     `accept()` or function parameters, which aren't pre-tainted in a
     single-function analysis. Making recv a source avoids false negatives.

---

## Maintenance Plan

### Rule Ownership

DTT owns its taint rules. CSA is a reference, not an authority.

1. **Seed**: Rules initially derived from CSA GenericTaintChecker (LLVM 18)
2. **Divergence**: DTT may add, remove, or modify rules as needed
3. **CSA check**: Review CSA changes on major LLVM releases for new functions
4. **Documentation**: Each rule has a comment noting its origin (CSA or DTT-specific)

### Adding New Rules

When adding a taint rule:
1. Determine category (source / propagator / sink / sanitizer)
2. Write a test case FIRST (TDD per CLAUDE.md)
3. Add the rule to the appropriate table
4. Mark as `[DTT-ADDITION]` if not from CSA
5. Run full test suite

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| False positives from aggressive taint | High | Start conservative; sanitizer annotations allow suppression |
| Missing C++ stream detection | Medium | Phased: C functions first, C++ streams in Phase 2 |
| Performance overhead from taint maps | Low | DenseMap is O(1); StringMap index for rule lookup |
| Complex path merging | Medium | Conservative merge (union of taint) follows existing TypedEnv pattern |
| Users don't know about sanitizer annotations | Medium | Emit fix-it hints in diagnostics |

---

## Success Criteria

1. Heartbleed-style tests (`recv` + `memcpy`) produce tainted warnings
2. Command injection tests (`scanf` + `system`) produce sink warnings
3. Internal code (no external input) produces regular warnings only (not tainted)
4. Guarded tainted code passes silently
5. User-defined sanitizer annotations clear taint
6. `argv`-based tests produce tainted warnings
7. C++ stream tests produce tainted warnings
8. No regression in existing DTT tests
9. Performance within 10% of current
10. Taint metadata appears in SARIF output via `properties.dtt:taint` object
11. `std::move` transfers taint correctly

---

## Implementation Phases

| Phase | Scope | Effort |
|-------|-------|--------|
| 1 | Core infrastructure (TaintEnv, TaintInfo, rule structs, StringMap index, assignment propagation, C++ streams, argv) | Medium |
| 2 | Integration: handleTaintInCall, source/propagator/sink application, arithmetic/cast propagation, BoundsResult extension, std::move, diagnostics, SARIF rules+metadata | Medium |
| 3 | Path-sensitive taint (branch save/restore/merge mirroring existing TypedEnv pattern, loop/switch handling) | Medium |
| 4 | Custom annotations (`[[clang::annotate]]`), sanitizer rules, future `.dtt` syntax | Medium |
| 5 | Documentation, CSA sync process, fix-it hints | Low |

---

## References

- Clang Static Analyzer GenericTaintChecker: `llvm-project/clang/lib/StaticAnalyzer/Checkers/GenericTaintChecker.cpp`
- DTT Safety Roadmap Part 9 (Taint Tracking): `docs/DTT_SAFETY_ROADMAP.md` — defines `_tainted`/`_validated` properties
- dtx Exploit Scanner Plan: `docs/plans/EXPLOIT_SCANNER_PLAN.md`
- CWE-78: OS Command Injection
- CWE-120: Buffer Copy without Checking Size of Input
- CWE-126: Buffer Over-read
- CWE-134: Use of Externally-Controlled Format String
- CWE-190: Integer Overflow
- CWE-22: Path Traversal
- CWE-20: Improper Input Validation
- CWE-426: Untrusted Search Path
- CERT C: STR31-C, ENV33-C, FIO30-C
