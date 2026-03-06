# DTT SARIF Output

> **Last reviewed: 2026-03-06** — Updated to reflect completed implementation.
> SARIF output is fully implemented: `SarifDiagConsumer` wrapper in `src/DTTSarif.{h,cpp}`,
> all diagnostic categories wired up, `--sarif=<path>` plugin argument, partialFingerprints,
> relative paths, CWE tags. See "Implementation Status" section below.

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

## Implementation Status

**Fully implemented.** All phases shipped in a single merge to main (2026-02-23).

### Files

| File | Role |
|------|------|
| `src/DTTSarif.h` | `SarifRule` struct, `SarifDiagConsumer` class, `findRepoRoot()` |
| `src/DTTSarif.cpp` | Rule constants, consumer logic, JSON serialization, repo root discovery |
| `src/DTTPlugin.cpp` | Argument parsing (`--sarif=`), consumer install/teardown, diagnostic registration |

### Architecture: DiagnosticConsumer Wrapper

The plan originally proposed an `emitDiag()` helper that would need changes at
every diagnostic emission site. The actual implementation uses a cleaner approach:
a `SarifDiagConsumer` that wraps Clang's existing `DiagnosticConsumer`.

**Key insight:** DTT diagnostics already flow through `DiagnosticsEngine::Report()`.
By wrapping the consumer, SARIF capture is transparent — zero changes to any
diagnostic emission site in DTTPlugin.cpp.

```
                  ┌──────────────────────┐
 Diag.Report() → │  SarifDiagConsumer    │
                  │  ├─ Forward to orig   │
                  │  ├─ Match DiagRuleMap │
                  │  ├─ Capture result    │
                  │  └─ Group notes       │
                  └──────────────────────┘
                           ↓
                  ┌──────────────────────┐
                  │  Original Consumer   │ (TextDiagnosticPrinter, etc.)
                  └──────────────────────┘
```

How it works:

1. **`registerDiag(Id, Rule)`** — maps a DTT diagnostic ID to a SARIF rule
2. **`registerNote(Id)`** — marks a diagnostic ID as a note (attaches to parent)
3. **`HandleDiagnostic()`** — intercepts every diagnostic:
   - Forwards to the original consumer unchanged
   - If the ID is in `DiagRuleMap`: captures as a `CapturedResult`
   - If it's a note after a captured result: attaches as `relatedLocations`
4. **`flush()`** — writes all captured results as SARIF JSON at end of TU

### Lifecycle in HandleTranslationUnit

```cpp
// 1. Install: wrap the original consumer before analysis passes
SarifConsumer = std::make_unique<dtt::SarifDiagConsumer>(
    OrigDiagClient, SM, SarifOutputPath, Root);
DE.setClient(SarifConsumer.get(), /*ShouldOwnClient=*/false);

// 2. Run all analysis passes (diagnostics flow through SarifDiagConsumer)

// 3. Teardown: restore original consumer, flush SARIF
DE.setClient(OrigDiagClient, OrigDiagOwned);
if (auto EC = SarifConsumer->flush()) { /* warn */ }
```

### All Diagnostics Wired Up

All diagnostic categories are registered — no phased rollout needed:

| Category | Registered Diagnostics | Rule |
|----------|----------------------|------|
| Bounds | `ErrBufferOOB`, `ErrIndexBounds`, `ErrNegativeIndex` | `RuleBoundsOverflow` |
| Null | `ErrNullDeref`, `ErrNullDerefUnk`, `ErrCallSiteNull` | `RuleNullDeref` |
| UAF/double-free | `ErrUseAfterFree`, `ErrDoubleFree` | `RuleUseAfterFree`, `RuleDoubleFree` |
| Uninitialized | `ErrUninitializedPtr/Var/Heap` | `RuleUninitialized` |
| Mismatched dealloc | `ErrMismatchedDealloc` | `RuleMismatchedDealloc` |
| Memory ops | `ErrMemcpy*`, `ErrMemset*`, `ErrStr*`, `ErrSnprintf*` (10 diags) | `RuleMemopOverflow` |
| Pointer arith | `ErrPointerArithOOB`, `ErrNullPtrArith` | `RulePointerArith` |
| Resources | `ErrScopeExitConstraint`, `ErrOverwriteLeak`, `WarnUncheckedResource` | `RuleResourceLeak` |
| Unverified bounds | All `Diag*Unverified` (warn + err variants, 30 IDs) | `RuleUnverifiedBounds` |
| Preconditions | `ErrFail`, `ErrUnk`, `ErrMultiArg`, `ErrMethodReq`, `ErrGenericPrecond` | `RulePrecondition` |
| Unverified precond | `DiagCallSitePreconditionUnverified`, `WarnPrecondViolation` | `RuleUnverifiedPrecondition` |
| Enums | `ErrNotTotal` | `RuleEnumExhaustive` |
| Annotations | `ErrAnnotationConflict`, `WarnInvalidArgIndex`, `DiagArityMismatch` | `RuleAnnotationConflict` |
| Unions | `ErrUnionInactiveMember`, `WarnUnionTypePunning` | `RuleUnionAccess`, `RuleUnionTypePun` |
| Sprintf | `WarnSprintfUnsafe` | `RuleSprintfUnsafe` |
| Tracking | `DiagLostTracking`, `DiagSolverFailed` (warn + err) | `RuleLostTracking`, `RuleSolverFailed` |

All note diagnostics (25+) are registered via `registerNote()` and attach as
`relatedLocations` to their parent result.

### Plugin Argument Parsing

```cpp
// In DTTPlugin::ParseArgs (alongside --dtt-mode= and --dtt-annotations=)
if (StringRef(arg).starts_with("--sarif=") ||
    StringRef(arg).starts_with("-sarif=")) {
    StringRef sPath = StringRef(arg);
    SarifOutputPath = sPath.substr(sPath.find('=') + 1);
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

## Rollout Status

| Phase | Scope | Status |
|-------|-------|--------|
| 1 | `SarifDiagConsumer` class + `--sarif` arg parsing + path handling + flush on TU end | **Done** |
| 2 | Wire P0 diagnostics (memory safety + bounds + null deref) with `partialFingerprints` | **Done** |
| 3 | Wire P1 diagnostics (resource leaks + unverified bounds) | **Done** |
| 4 | Golden file tests + CI validation + schema conformance | Not started |
| 5 | Wire P2 diagnostics (preconditions, enums, annotations) | **Done** |
| 6 | GitHub Code Scanning integration (upload action in CI workflow) | Not started |

Phases 1-3 and 5 shipped in a single merge (2026-02-23). Remaining work:
golden file tests (Phase 4) and CI upload action (Phase 6).

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
