# DTT SARIF Output

> **Last reviewed: 2026-02-23** — Updated for GitHub July 2025 multi-run change,
> added missing diagnostics, partialFingerprints, relative path requirements.

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

### Why not Clang's native SARIF?

Clang has experimental `-fdiagnostics-format=sarif` support, but it only covers
Clang's own diagnostics (parse errors, warnings, static analyzer findings). Plugin
diagnostics emitted via `DiagnosticsEngine::Report()` appear as opaque warning text,
losing DTT-specific rule IDs, CWE mappings, tracked property metadata, and proof
status. DTT needs its own SARIF emitter for structured, machine-readable output.

---

## Interface

```bash
# Existing behavior (unchanged)
clang++ -fplugin=libDTTPlugin.so -fsyntax-only file.cpp

# New: also emit SARIF
clang++ -fplugin=libDTTPlugin.so \
        -Xclang -plugin-arg-dtt -Xclang --sarif=output.sarif \
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
            "properties": { "tags": ["CWE-787"] }
          },
          {
            "id": "DTT-use-after-free",
            "shortDescription": { "text": "Use after free" },
            "helpUri": "https://cwe.mitre.org/data/definitions/416.html",
            "properties": { "tags": ["CWE-416"] }
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
            "artifactLocation": {
              "uri": "src/parser.cpp",
              "uriBaseId": "%SRCROOT%"
            },
            "region": { "startLine": 87, "startColumn": 5 }
          }
        }],
        "partialFingerprints": {
          "primaryLocationLineHash": "a1b2c3d4e5f6"
        },
        "relatedLocations": [
          {
            "id": 1,
            "message": { "text": "variable declared here" },
            "physicalLocation": {
              "artifactLocation": {
                "uri": "src/parser.cpp",
                "uriBaseId": "%SRCROOT%"
              },
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

### Key SARIF Requirements

| Requirement | Why | How |
|-------------|-----|-----|
| **Relative paths** | GitHub Code Scanning requires paths relative to repo root | Strip SourceManager prefix; use `uriBaseId: "%SRCROOT%"` |
| **`partialFingerprints`** | GitHub uses these for alert deduplication across runs; without them, alerts duplicate when code shifts | Hash the source line content at the diagnostic location |
| **CWE in `tags[]`** | GitHub displays security severity based on CWE tags | `"properties": { "tags": ["CWE-787"] }` (not `"cwe": "CWE-787"`) |
| **One run per upload** | GitHub (July 2025) stopped combining multiple runs with same tool+category in a single upload | One SARIF file = one `runs[]` entry |

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
| `DTT-resource-leak` | `ErrScopeExitConstraint`, `ErrOverwriteLeak`, `WarnUncheckedResource` | CWE-401 / CWE-775 |
| `DTT-memop-overflow` | `ErrMemcpyOverflow`, `ErrMemcpyOverread`, `ErrMemsetOverflow`, `ErrMemcmpOverread`, `ErrMemchrOverread`, `ErrStrncpyOverflow`, `ErrSnprintfOverflow`, `ErrStrcpyOverflow`, `ErrStrcatOverflow`, `ErrStrncatOverflow` | CWE-120 / CWE-126 |
| `DTT-pointer-arith` | `ErrPointerArithOOB`, `ErrNullPtrArith` | CWE-823 |
| `DTT-union-access` | `ErrUnionInactiveMember` | CWE-758 |
| `DTT-precondition` | `ErrFail`, `ErrUnk`, `ErrMultiArg`, `ErrMethodReq`, `ErrGenericPrecond` | (project-specific) |
| `DTT-enum-exhaustive` | `ErrNotTotal` | (project-specific) |
| `DTT-annotation-conflict` | `ErrAnnotationConflict`, `WarnInvalidArgIndex`, `DiagArityMismatch` | (project-specific) |

### Unverified Bounds (mode-dependent: warning or error)

| SARIF Rule ID | DTT Diagnostics | CWE |
|---------------|-----------------|-----|
| `DTT-unverified-bounds` | `DiagMemcpyUnverified`, `DiagMemcpySrcUnverified`, `DiagMemcpySymbolicUnverified`, `DiagMemcpySrcSymbolicUnverified`, `DiagMemsetUnverified`, `DiagMemsetSymbolicUnverified`, `DiagMemcmpUnverified`, `DiagMemcmpSymbolicUnverified`, `DiagMemchrUnverified`, `DiagMemchrSymbolicUnverified`, `DiagStrncpyUnverified`, `DiagSnprintfUnverified`, `DiagStrcpyUnverified`, `DiagStrcatUnverified`, `DiagStrncatUnverified` | CWE-120 |
| `DTT-unverified-precondition` | `DiagCallSitePreconditionUnverified`, `WarnPrecondViolation` | (project-specific) |

### Warnings

| SARIF Rule ID | DTT Diagnostics | CWE |
|---------------|-----------------|-----|
| `DTT-lost-tracking` | `DiagLostTracking` | (informational) |
| `DTT-solver-failed` | `DiagSolverFailed` | (informational) |
| `DTT-sprintf-unsafe` | `WarnSprintfUnsafe` | CWE-120 |
| `DTT-union-type-pun` | `WarnUnionTypePunning` | CWE-758 |

### Note Diagnostics (relatedLocations, not independent rules)

Note diagnostics attach to their parent error/warning as `relatedLocations` entries
in the SARIF result. They do not produce independent SARIF results.

| Parent Category | Note Diagnostics |
|----------------|------------------|
| Preconditions | `NoteGuard`, `NoteCase`, `NotePrecondSource`, `NoteGenericPrecondSource`, `NoteCallSitePreconditionSource`, `NoteInferredPrecondition` |
| Null safety | `NoteNullGuard`, `NoteNullProvenance`, `NoteCallSiteGuard` |
| Bounds | `NoteBufferSize`, `NoteBufferGuard`, `NoteAddBoundsCheck`, `NoteAddSymbolicBoundsCheck` |
| Fix suggestions | `NoteUseSnprintf`, `NoteUseStrncpy`, `NoteUseStrncat`, `NoteCheckSrcLen`, `NoteFixItGuard` |
| Memory safety | `NoteFirstFree`, `NoteHeapAllocatedHere` |
| Resources | `NoteScopeExitAcquired` |
| Diagnostics | `NoteLostAt`, `NoteCounterexample`, `NoteUntracked` |
| Unions | `NoteUnionActiveSet` |
| Provenance | `NoteProvenanceEntry`, `NoteEstablisher`, `NoteEstablisherGuard` |

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

**Build:** Add `src/DTTSarif.cpp` to CMakeLists.txt (source files are explicitly listed):

```cmake
add_library(DTTPlugin SHARED
    src/DTTPlugin.cpp
    src/DTTAnnotationFile.cpp
    src/DTTSarif.cpp
)
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
    SmallString<128> OutputPath;
    StringRef RepoRoot;         // For converting absolute→relative paths

public:
    SarifEmitter(const SourceManager &SM, StringRef Path, StringRef Root)
        : SM(SM), OutputPath(Path), RepoRoot(Root) {}

    void addResult(SarifResult R) { Results.push_back(std::move(R)); }

    // Call at end of TU (HandleTranslationUnit)
    std::error_code flush();

    bool empty() const { return Results.empty(); }

private:
    // Convert SourceLocation to repo-relative path
    SmallString<128> getRelativePath(SourceLocation Loc) const;

    // Compute partialFingerprint from source line content
    std::string computeLineHash(SourceLocation Loc) const;

    void emitToolSection(llvm::json::OStream &J) const;
    void emitResultsSection(llvm::json::OStream &J) const;
};

} // namespace dtt
```

### Path Handling

GitHub Code Scanning requires file paths relative to the repository root.
SourceManager provides absolute paths. The emitter strips the repo root prefix:

```cpp
SmallString<128> SarifEmitter::getRelativePath(SourceLocation Loc) const {
    StringRef Filename = SM.getFilename(SM.getExpansionLoc(Loc));
    if (Filename.starts_with(RepoRoot)) {
        Filename = Filename.drop_front(RepoRoot.size());
        if (Filename.starts_with("/"))
            Filename = Filename.drop_front(1);
    }
    return SmallString<128>(Filename);
}
```

The repo root is determined at plugin init time from the main input file's
directory (walk up until `.git/` is found, or use the working directory).

### Partial Fingerprints

GitHub uses `partialFingerprints.primaryLocationLineHash` to deduplicate
alerts across runs. Without this, alerts duplicate when surrounding code
shifts line numbers.

```cpp
std::string SarifEmitter::computeLineHash(SourceLocation Loc) const {
    SourceLocation ExpLoc = SM.getExpansionLoc(Loc);
    unsigned LineNo = SM.getExpansionLineNumber(ExpLoc);
    StringRef Buf = SM.getBufferData(SM.getFileID(ExpLoc));
    // Find the line content
    unsigned LineOffset = SM.getDecomposedExpansionLoc(ExpLoc).second -
                          SM.getColumnNumber(SM.getFileID(ExpLoc), LineOffset) + 1;
    // Hash the trimmed line content (stable across whitespace changes)
    StringRef Line = Buf.substr(LineOffset).split('\n').first.trim();
    return llvm::utohexstr(llvm::xxh3_64bits(Line));
}
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
| P0 | Null deref (`ErrNullDeref`, `ErrNullDerefUnk`, `ErrCallSiteNull`) | 3 | Standard SAST finding |
| P1 | Resource leaks (`ErrScopeExitConstraint`, `ErrOverwriteLeak`, `WarnUncheckedResource`) | 3 | Important for security |
| P1 | Unverified bounds (`Diag*Unverified`) | 15 | High volume, lower certainty |
| P2 | Preconditions, enums, annotations, warnings | ~10 | DTT-specific, less SARIF value |

P0 diagnostics should be wired up first. P1 and P2 can follow in subsequent PRs.

### JSON Serialization

No external JSON library required. Use `llvm::json::OStream` (part of LLVM Support,
guaranteed available in LLVM 18+):

```cpp
std::error_code SarifEmitter::flush() {
    std::error_code EC;
    raw_fd_ostream OS(OutputPath, EC, sys::fs::OF_None);
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

Fits the existing `ParseArgs` pattern (iterates `std::vector<std::string>`,
uses `StringRef.starts_with()` to detect prefixes):

```cpp
// In DTTPlugin::ParseArgs (alongside --dtt-mode= and --dtt-annotations=)
if (Arg.starts_with("--sarif=")) {
    SarifPath = Arg.substr(Arg.find('=') + 1);
    continue;
}
```

### Flush Timing

The emitter flushes in `HandleTranslationUnit` (line ~16189), after all
diagnostics for the current TU have been collected:

```cpp
void HandleTranslationUnit(ASTContext &Ctx) override {
    // ... existing DTT analysis (Pass 1, Pass 2, Pass 2b, transitive) ...

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
| Disk full during write | Partial SARIF file — consumer should validate |

SARIF output must never affect compilation success or failure. If SARIF
writing fails, DTT emits a warning and proceeds normally.

---

## Multi-TU Builds

Build systems compile each TU separately. Each TU produces one SARIF file.

### Recommended: Per-TU files + merge

```bash
# Each TU writes its own SARIF
clang++ -fplugin=... --sarif=.sarif/a.cpp.sarif -fsyntax-only a.cpp
clang++ -fplugin=... --sarif=.sarif/b.cpp.sarif -fsyntax-only b.cpp

# Post-build: merge into single SARIF for upload
# (jq one-liner: combine all runs[] arrays into one document)
jq -s '{
  "$schema": .[0]["$schema"],
  "version": "2.1.0",
  "runs": [.[0].runs[0] | .results = [.[] | .runs[0].results[]] ]
}' .sarif/*.sarif > merged.sarif
```

### GitHub upload

```bash
# Upload merged SARIF
gh api /repos/:owner/:repo/code-scanning/sarifs \
  -f "commit_sha=$(git rev-parse HEAD)" \
  -f "ref=$(git symbolic-ref HEAD)" \
  -f "sarif=$(gzip -c merged.sarif | base64 -w0)"
```

Or use the `github/codeql-action/upload-sarif@v3` action in CI.

### Why not append mode?

The original plan proposed `--sarif-append` to concatenate multiple SARIF
JSON objects into one file. This has two problems:

1. **GitHub (July 2025)** stopped combining multiple SARIF runs with the
   same tool+category in a single upload. Each distinct analysis needs its
   own category or a single merged `runs[]` entry.
2. Multi-document JSON (JSON Lines) is not valid SARIF — consumers that
   validate schema reject it.

Per-TU files with a post-build merge is simpler and universally compatible.

### GitHub upload limits

| Limit | Value |
|-------|-------|
| Max file size (gzip-compressed) | 10 MB |
| Max runs per file | 20 |
| Max results per run | 25,000 |
| Max rules per run | 25,000 |
| Displayed results (by severity) | Top 5,000 |

DTT is well under these limits for any realistic project.

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
| Paths are relative (no `/`) | Script checks no `artifactLocation.uri` starts with `/` |
| `partialFingerprints` present | Script checks each result has `primaryLocationLineHash` |
| CWE tags present | Script checks `properties.tags` on rules |
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
| 1 | `SarifEmitter` class + `--sarif` arg parsing + path handling + flush on TU end | Infrastructure, no findings yet |
| 2 | Wire P0 diagnostics (memory safety + bounds + null deref) with `partialFingerprints` | SARIF file with findings for `_fail` tests |
| 3 | Wire P1 diagnostics (resource leaks + unverified bounds) | Full coverage of security-relevant findings |
| 4 | Golden file tests + CI validation + schema conformance | Regression protection |
| 5 | Wire P2 diagnostics (preconditions, enums, annotations) | Complete coverage |
| 6 | GitHub Code Scanning integration (upload action in CI workflow) | PR inline annotations |

Phases 1-2 are the MVP. Phases 3-6 can follow in separate PRs.

---

## Compatibility with Refactoring Plan

The refactoring plan (`docs/plans/REFACTOR_LARGE_FILES.md`) proposes splitting
DTTPlugin.cpp into 7 files. SARIF files (`DTTSarif.h`, `DTTSarif.cpp`) are
independent — they hook into diagnostic emission via the `SarifOut` pointer,
orthogonal to where visitor/infra logic lives. Post-refactor, SARIF flush
stays in the new `DTTPlugin.cpp` (~200 lines, consumer + registration).

---

## Dependency Graph

```
SARIF Output (this plan)
   ├── No dependency on taint tracking
   ├── No dependency on dtx
   ├── No dependency on refactoring plan
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
- [GitHub: Stop combining SARIF runs (July 2025)](https://github.blog/changelog/2025-07-21-code-scanning-will-stop-combining-multiple-sarif-runs-uploaded-in-the-same-sarif-file/)
- [Clang SARIF limitations](https://discourse.llvm.org/t/rfc-emit-sarif-diagnostics-via-fdiagnostics-format-sarif/57850) — plugin diagnostics not covered
- [LLVM json::OStream](https://llvm.org/doxygen/classllvm_1_1json_1_1OStream.html)
- dtx Exploit Scanner: `docs/plans/EXPLOIT_SCANNER_PLAN.md` (Step 0 references this plan)
- DTT Taint Tracking: `docs/plans/TAINT_TRACKING_PROPOSAL.md` (future taint metadata in SARIF)
