# DTT SARIF Output

## What This Adds

A `--sarif=<path>` plugin argument that writes structured findings in
[SARIF 2.1.0](https://docs.oasis-open.org/sarif/sarif/v2.1.0/sarif-v2.1.0.html)
format alongside normal Clang diagnostics.

**No behavior change to existing diagnostics.** SARIF is purely additive.

## Why

DTT currently emits Clang diagnostics (errors/warnings to stderr). This is fine
for developers in a terminal but useless for tooling.

SARIF consumers that work out of the box:
- GitHub Code Scanning (findings appear inline on PRs)
- VS Code (SARIF Viewer extension)
- CLion / IntelliJ (SARIF support)
- Azure DevOps
- Any CI system with SARIF upload
- dtx exploit scanner (consumes DTT findings programmatically)

---

## Interface

```bash
# Existing behavior (unchanged)
clang++ -fplugin=libDTTPlugin.so -fsyntax-only file.cpp

# New: also emit SARIF
clang++ -fplugin=libDTTPlugin.so \
        -Xclang -plugin-arg-dtt -Xclang --sarif=output.sarif \
        -fsyntax-only file.cpp

# Append to existing SARIF (multi-TU builds)
clang++ -fplugin=libDTTPlugin.so \
        -Xclang -plugin-arg-dtt -Xclang --sarif=output.sarif \
        -Xclang -plugin-arg-dtt -Xclang --sarif-append \
        -fsyntax-only file.cpp
```

When `--sarif` is not specified, no SARIF file is written and no overhead
is added to the compilation.

---

## SARIF Schema (per DTT finding)

```json
{
  "$schema": "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/main/sarif-2.1/schema/sarif-schema-2.1.0.json",
  "version": "2.1.0",
  "runs": [{
    "tool": {
      "driver": {
        "name": "DTT",
        "version": "1.0.0",
        "informationUri": "https://github.com/adishavit/dtt",
        "rules": [
          {
            "id": "DTT-bounds-overflow",
            "shortDescription": { "text": "Out-of-bounds access" },
            "helpUri": "https://cwe.mitre.org/data/definitions/787.html",
            "properties": { "cwe": "CWE-787" }
          },
          {
            "id": "DTT-use-after-free",
            "shortDescription": { "text": "Use after free" },
            "helpUri": "https://cwe.mitre.org/data/definitions/416.html",
            "properties": { "cwe": "CWE-416" }
          }
        ]
      }
    },
    "results": [
      {
        "ruleId": "DTT-bounds-overflow",
        "level": "error",
        "message": {
          "text": "index 10 out of bounds: requires index < _size (10)"
        },
        "locations": [{
          "physicalLocation": {
            "artifactLocation": { "uri": "src/parser.cpp" },
            "region": { "startLine": 87, "startColumn": 5 }
          }
        }],
        "relatedLocations": [
          {
            "id": 1,
            "message": { "text": "variable declared here" },
            "physicalLocation": {
              "artifactLocation": { "uri": "src/parser.cpp" },
              "region": { "startLine": 80 }
            }
          }
        ],
        "properties": {
          "dttDiagnostic": "ErrBufferOOB",
          "trackedProperty": "_size",
          "trackedValue": "10",
          "accessIndex": "10",
          "proofStatus": "proven_unsafe"
        }
      }
    ]
  }]
}
```

---

## DTT Diagnostic to SARIF Rule Mapping

Every DTT diagnostic maps to a SARIF rule ID and CWE. Diagnostics are grouped
by their SARIF rule — multiple DTT diagnostics can map to the same rule.

### Proven Unsafe (always errors)

| SARIF Rule ID | DTT Diagnostics | CWE |
|---------------|-----------------|-----|
| `DTT-bounds-overflow` | `ErrBufferOOB`, `ErrIndexBounds`, `ErrNegativeIndex` | CWE-787 / CWE-125 |
| `DTT-null-deref` | `ErrNullDeref`, `ErrNullDerefUnk`, `ErrCallSiteNull` | CWE-476 |
| `DTT-use-after-free` | `ErrUseAfterFree` | CWE-416 |
| `DTT-double-free` | `ErrDoubleFree` | CWE-415 |
| `DTT-uninitialized` | `ErrUninitializedPtr`, `ErrUninitializedVar`, `ErrUninitializedHeap` | CWE-457 |
| `DTT-mismatched-dealloc` | `ErrMismatchedDealloc` | CWE-762 |
| `DTT-resource-leak` | `ErrScopeExitConstraint`, `ErrOverwriteLeak` | CWE-401 / CWE-775 |
| `DTT-memop-overflow` | `ErrMemcpyOverflow`, `ErrMemcpyOverread`, `ErrMemsetOverflow`, `ErrMemcmpOverread`, `ErrMemchrOverread`, `ErrStrncpyOverflow`, `ErrSnprintfOverflow`, `ErrStrcpyOverflow`, `ErrStrcatOverflow`, `ErrStrncatOverflow` | CWE-120 / CWE-126 |
| `DTT-pointer-arith` | `ErrPointerArithOOB`, `ErrNullPtrArith` | CWE-823 |
| `DTT-union-access` | `ErrUnionInactiveMember` | CWE-758 |
| `DTT-precondition` | `ErrFail`, `ErrUnk`, `ErrMultiArg`, `ErrMethodReq` | (project-specific) |
| `DTT-enum-exhaustive` | `ErrNotTotal` | (project-specific) |
| `DTT-annotation-conflict` | `ErrAnnotationConflict` | (project-specific) |

### Unverified Bounds (mode-dependent: warning or error)

| SARIF Rule ID | DTT Diagnostics | CWE |
|---------------|-----------------|-----|
| `DTT-unverified-bounds` | `DiagMemcpyUnverified`, `DiagMemcpySrcUnverified`, `DiagMemcpySymbolicUnverified`, `DiagMemcpySrcSymbolicUnverified`, `DiagMemsetUnverified`, `DiagMemsetSymbolicUnverified`, `DiagMemcmpUnverified`, `DiagMemcmpSymbolicUnverified`, `DiagMemchrUnverified`, `DiagMemchrSymbolicUnverified`, `DiagStrncpyUnverified`, `DiagSnprintfUnverified`, `DiagStrcpyUnverified`, `DiagStrcatUnverified`, `DiagStrncatUnverified` | CWE-120 |
| `DTT-unverified-precondition` | `DiagCallSitePreconditionUnverified` | (project-specific) |

### Warnings

| SARIF Rule ID | DTT Diagnostics | CWE |
|---------------|-----------------|-----|
| `DTT-lost-tracking` | `DiagLostTracking` | (informational) |
| `DTT-solver-failed` | `DiagSolverFailed` | (informational) |
| `DTT-sprintf-unsafe` | `WarnSprintfUnsafe` | CWE-120 |
| `DTT-union-type-pun` | `WarnUnionTypePunning` | CWE-758 |

### Future (taint tracking)

| SARIF Rule ID | DTT Diagnostics | CWE |
|---------------|-----------------|-----|
| `DTT-tainted-bounds` | (not yet implemented) | CWE-20 / CWE-126 |

When taint tracking is added (see `TAINT_TRACKING_PROPOSAL.md`), SARIF results
will include taint metadata in `properties`:

```json
"properties": {
  "taint": {
    "isTainted": true,
    "sourceFunction": "recv",
    "originFile": "src/server.cpp",
    "originLine": 42,
    "riskLevel": "high",
    "propagationChain": ["recv", "memcpy", "strtoul"]
  }
}
```

---

## Implementation

### New Files

```
src/DTTSarif.h     -- SarifResult struct, SarifEmitter class
src/DTTSarif.cpp   -- JSON serialization, file output
```

### SarifEmitter Class

```cpp
// src/DTTSarif.h
#include "llvm/Support/JSON.h"
#include "clang/Basic/SourceLocation.h"

namespace dtt {

struct SarifResult {
    StringRef RuleId;           // "DTT-bounds-overflow"
    StringRef DiagName;         // "ErrBufferOOB"
    DiagnosticsEngine::Level Level;
    SourceLocation Loc;
    std::string Message;
    SmallVector<std::pair<SourceLocation, std::string>, 2> RelatedLocs;
    // DTT-specific properties
    StringRef TrackedProperty;  // "_size", "_null", etc.
    std::string TrackedValue;
    std::string ProofStatus;    // "proven_unsafe", "unverified", "unknown"
};

class SarifEmitter {
    SmallVector<SarifResult, 16> Results;
    const SourceManager &SM;
    StringRef OutputPath;
    bool AppendMode;

public:
    SarifEmitter(const SourceManager &SM, StringRef Path, bool Append)
        : SM(SM), OutputPath(Path), AppendMode(Append) {}

    void addResult(SarifResult R) { Results.push_back(std::move(R)); }

    // Call at end of TU (HandleTranslationUnit)
    std::error_code flush();

    bool empty() const { return Results.empty(); }
};

} // namespace dtt
```

### Hook Points

DTT's diagnostic emission follows a pattern: compute violation, then
call `Diag.Report(Loc, DiagId) << args...`. The SARIF emitter hooks
in parallel at each emission point.

**Strategy:** Add a helper that wraps both the Clang diagnostic and the
SARIF result in one call:

```cpp
void DTTVisitor::emitDiag(SourceLocation Loc, unsigned DiagId,
                          StringRef RuleId, StringRef DiagName,
                          StringRef TrackedProp, StringRef TrackedVal,
                          StringRef ProofStatus) {
    // Existing Clang diagnostic (unchanged)
    Diag.Report(Loc, DiagId);

    // SARIF (only if emitter is active)
    if (SarifOut) {
        SarifOut->addResult({RuleId, DiagName, /*Level=*/Error,
                             Loc, /*Message filled by caller*/"",
                             /*RelatedLocs=*/{},
                             TrackedProp, TrackedVal.str(),
                             ProofStatus.str()});
    }
}
```

For the initial implementation, wrap the highest-value diagnostics first:

| Priority | Diagnostic Group | Count | Rationale |
|----------|-----------------|-------|-----------|
| P0 | Memory safety (`ErrUseAfterFree`, `ErrDoubleFree`, `ErrUninitializedPtr/Var/Heap`, `ErrMismatchedDealloc`) | 6 | Highest security value |
| P0 | Bounds violations (`ErrBufferOOB`, `ErrMemcpy*`, `ErrStr*`) | 14 | Most common findings |
| P0 | Null deref (`ErrNullDeref`, `ErrNullDerefUnk`) | 2 | Standard SAST finding |
| P1 | Resource leaks (`ErrScopeExitConstraint`, `ErrOverwriteLeak`) | 2 | Important for security |
| P1 | Unverified bounds (`Diag*Unverified`) | 15 | High volume, lower certainty |
| P2 | Preconditions, enums, annotations, warnings | ~8 | DTT-specific, less SARIF value |

P0 diagnostics should be wired up first. P1 and P2 can follow in subsequent PRs.

### JSON Serialization

No external JSON library required. Use `llvm::json::OStream` (part of LLVM Support):

```cpp
std::error_code SarifEmitter::flush() {
    std::error_code EC;
    raw_fd_ostream OS(OutputPath, EC,
                      AppendMode ? sys::fs::OF_Append : sys::fs::OF_None);
    if (EC) return EC;

    json::OStream J(OS, /*IndentSize=*/2);
    J.objectBegin();
    J.attribute("$schema", "https://raw.githubusercontent.com/oasis-tcs/"
                "sarif-spec/main/sarif-2.1/schema/sarif-schema-2.1.0.json");
    J.attribute("version", "2.1.0");
    J.attributeBegin("runs");
    J.arrayBegin();
    J.objectBegin();
    emitToolSection(J);
    emitResultsSection(J);
    J.objectEnd();
    J.arrayEnd();
    J.attributeEnd();
    J.objectEnd();
    return std::error_code();
}
```

### Plugin Argument Parsing

```cpp
// In DTTPlugin.cpp plugin argument handling
bool HandlePluginArg(StringRef Arg, StringRef Value) {
    if (Arg.starts_with("--sarif=")) {
        SarifPath = Arg.drop_front(8);  // Extract path
        return true;
    }
    if (Arg == "--sarif-append") {
        SarifAppend = true;
        return true;
    }
    // ... existing args ...
}
```

### Flush Timing

The emitter flushes in `HandleTranslationUnit`, after all diagnostics
for the current TU have been collected:

```cpp
void HandleTranslationUnit(ASTContext &Ctx) override {
    // ... existing DTT analysis ...

    if (SarifOut && !SarifOut->empty()) {
        if (auto EC = SarifOut->flush()) {
            // Emit a Clang warning, do not abort compilation
            unsigned WarnId = Ctx.getDiagnostics().getCustomDiagID(
                DiagnosticsEngine::Warning,
                "DTT: failed to write SARIF output to '%0': %1");
            Ctx.getDiagnostics().Report(WarnId)
                << SarifPath << EC.message();
        }
    }
}
```

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| `--sarif` path is unwritable | Emit Clang warning, continue compilation normally |
| `--sarif` path is a directory | Emit Clang warning (OS returns EISDIR) |
| Plugin crashes mid-TU | No SARIF written (flush never called) — same as no `--sarif` |
| Zero findings in TU | Write valid SARIF with empty `results` array |
| `--sarif-append` on nonexistent file | Create new file (append to empty) |
| Disk full during write | Partial SARIF file — consumer should validate |

SARIF output must never affect compilation success or failure. If SARIF
writing fails, DTT emits a warning and proceeds normally.

---

## Multi-TU Builds

Build systems compile each TU separately. Two approaches:

### Option A: Append mode (recommended for simplicity)

```bash
# In build system: each TU appends to the same file
clang++ -fplugin=... --sarif=output.sarif --sarif-append -fsyntax-only a.cpp
clang++ -fplugin=... --sarif=output.sarif --sarif-append -fsyntax-only b.cpp
```

This produces a file with multiple SARIF JSON objects. Consumers that
support JSON Lines or multi-document SARIF can process it directly.
A post-build merge step combines them into a single valid SARIF:

```bash
dtx --merge-sarif output.sarif -o merged.sarif
```

### Option B: Per-TU files

```bash
clang++ -fplugin=... --sarif=output/a.sarif -fsyntax-only a.cpp
clang++ -fplugin=... --sarif=output/b.sarif -fsyntax-only b.cpp
```

Consumers process each file independently. GitHub Code Scanning accepts
multiple SARIF uploads.

---

## Testing Strategy

### Unit Tests (SARIF structure validation)

Each test compiles a `.cpp` file with `--sarif=<tmpfile>`, then validates
the output JSON structure.

```bash
# Test: bounds overflow produces valid SARIF
clang++-18 -fplugin=./build/libDTTPlugin.so \
           -Xclang -plugin-arg-dtt -Xclang --sarif=/tmp/test.sarif \
           -std=c++20 -fsyntax-only examples/10_basic_fail.cpp 2>/dev/null || true
python3 tests/validate_sarif.py /tmp/test.sarif \
    --expect-rule DTT-bounds-overflow \
    --expect-min-results 1
```

### Golden File Tests

For key test files, store expected SARIF output and diff against actual:

```
tests/sarif/
├── 10_basic_fail.expected.sarif
├── 30_null_deref_fail.expected.sarif
├── 1360_posix_resource_fail.expected.sarif
└── validate_sarif.py
```

Golden files store only the `results` array (not the full envelope) to avoid
brittleness from version strings or schema URLs changing.

### What to Validate

| Check | How |
|-------|-----|
| Valid JSON | `python3 -m json.tool` |
| SARIF 2.1.0 schema conformance | `sarif-validator` npm package or Python `jsonschema` |
| Rule IDs match table above | Script checks `ruleId` against known set |
| Locations have valid line/column | Script checks > 0 |
| CWE properties present | Script checks `properties.cwe` on rules |
| GitHub Code Scanning accepts it | Upload via `gh api` to a test repo |

### Integration with Existing Test Suite

Add a CMake option to run SARIF validation alongside existing tests:

```cmake
option(DTT_TEST_SARIF "Validate SARIF output in tests" OFF)

if(DTT_TEST_SARIF)
    # For each _fail test, also validate SARIF output
    foreach(test_source ${FAIL_TESTS})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_test(NAME sarif_${test_name}
            COMMAND ${CMAKE_SOURCE_DIR}/tests/sarif/run_sarif_test.sh
                    ${test_source} ${CMAKE_BINARY_DIR}/libDTTPlugin.so)
    endforeach()
endif()
```

---

## Rollout Plan

| Phase | Scope | Deliverable |
|-------|-------|-------------|
| 1 | `SarifEmitter` class + `--sarif` arg parsing + flush on TU end | Infrastructure, no findings yet |
| 2 | Wire P0 diagnostics (memory safety + bounds + null deref) | SARIF file with findings for `_fail` tests |
| 3 | Wire P1 diagnostics (resource leaks + unverified bounds) | Full coverage of security-relevant findings |
| 4 | Golden file tests + CI validation | Regression protection |
| 5 | Wire P2 diagnostics (preconditions, enums, annotations) | Complete coverage |
| 6 | GitHub Code Scanning integration (upload action) | PR inline annotations |

Phases 1-2 are the MVP. Phases 3-6 can follow in separate PRs.

---

## Dependency Graph

```
SARIF Output (this plan)
   ├── No dependency on taint tracking
   ├── No dependency on dtx
   └── Consumed by:
       ├── GitHub Code Scanning
       ├── VS Code / CLion SARIF viewers
       ├── CI gating scripts
       └── dtx exploit scanner (docs/plans/EXPLOIT_SCANNER_PLAN.md)
```

---

## References

- [SARIF 2.1.0 Specification (OASIS)](https://docs.oasis-open.org/sarif/sarif/v2.1.0/sarif-v2.1.0.html)
- [GitHub Code Scanning SARIF Upload](https://docs.github.com/en/code-security/code-scanning/integrating-with-code-scanning/sarif-support-for-code-scanning)
- [LLVM json::OStream](https://llvm.org/doxygen/classllvm_1_1json_1_1OStream.html)
- dtx Exploit Scanner: `docs/plans/EXPLOIT_SCANNER_PLAN.md` (Step 0 references this plan)
- DTT Taint Tracking: `docs/plans/TAINT_TRACKING_PROPOSAL.md` (future taint metadata in SARIF)
