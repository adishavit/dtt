# Implementation review status

Updated: 2026-10-10. Baseline: `2b06b5c`.

## Current scope

Finish command-line analyzer readiness for the Core C++ presentation. Talk content
and its move to a private repository belong to a separate session. Editor work is
deferred and does not block command-line readiness.

Implementation defects are distinct from additional analysis capabilities.
The existing known-gap tests remain; detecting every program bug is not the goal
of this repair pass.

## Implemented and verified locally

- Preserve conditional method guards; join continuing branches conservatively;
  compare symbolic expressions and union discriminants structurally.
- Preserve nullable/empty states through smart-pointer reset and copy/move
  assignment. Keep `and_then` success unknown without a callback proof.
- Check symbolic arithmetic for overflow and exceptional division; report
  explicit annotation-file errors as compiler errors.
- Correct `fflush(nullptr)` and `shm_open` contracts. Remove the incorrect claim
  that `pipe` returns an owned file descriptor; output-descriptor tracking is
  still unsupported.
- Fix SARIF script invocation, configured compiler selection, stdin artifacts,
  and repository-relative paths. Root discovery no longer assigns a borrowed
  substring into its own buffer; assertion-enabled regression coverage checks
  this even with a Release plugin build. Source archives without Git metadata
  use paths relative to the compilation directory.
- Embed STL/libc/Win32 contracts in the plugin and install their source files
  and optional external-library annotations. A relocated standalone plugin and
  a temporary installation both retain STL/libc checking.
- Honor manually requested release tags and publish the checksum file consumed
  by installers. Release shell steps were exercised locally; no release was made.
- Make the ghost-state failure example establish its initial state explicitly.
  It now demonstrates a proven violation in inform, quiet, and enforce modes.

The previous repair baseline included the original known gaps and the registered
range-for bounds gap. The negative-fixture audit below exposes additional existing
gaps rather than treating unrelated diagnostics as successful detections.
New analyzer regressions check individual scenarios rather than accepting any
compiler failure from a multi-scenario source file. Regular main-suite `_fail`
fixtures now require intended per-location DTT errors and reject unrelated
compiler errors. Known-gap fixtures retain their exit-code oracle; their results
are reported separately from passing regression checks.

Follow-up repairs:

- Add opt-in Linux runtime bundling with plugin-relative Z3 loading. Installation,
  relocation, actual Clang loader tracing, archive contents and compile probes
  were checked. The release definition includes the archive; no release was run.
- Repair missing headers, an invalid pointer-to-int cast and a misplaced template
  attribute in legacy negative fixtures exposed by the stronger test oracle.
- Reclassify safe range iteration as `_pass`, and safe variant-switch examples
  as `_xpass`. Add an explicit range-for overrun `_xfail` rather than claiming
  iteration bounds are implemented.
- Reconcile roadmap claims about switch proofs, ternaries, range-for and iterator
  invalidation. Correct heuristic dates and remove stale test-count/coverage claims.
- Reproduce the README example and replace its output with current default-mode
  diagnostics. Constructor/guard notes are present; post-move access is reported
  as unknown size, not a proved zero-sized vector.

The talk session's unknown-state findings were reproduced without changing
unknown-state reporting policy: untracked reference-container fields are
intentionally silent. Postconditions now require successful call-entry precondition proof.
Ghost fields have no implicit zero initialization. See the reporting limits in
`GUIDE.md` and the reproductions in `examples/review/talk_findings.cpp`.

Initialization repairs now import integer members from Clang's evaluation of
complete user-record initialization. This respects aggregate arguments and
constexpr constructor overrides; runtime-dependent values are not inferred.
Direct `std::vector` initializer-list construction records element count after
constructor contracts, including defaulted allocator arguments. The safe `993`
fixture is now `_pass`. Isolated regressions check the intended errors in quiet
mode as well as safe accesses, overrides, copy/move and both standard libraries.
Direct vector move construction into a by-value call argument now invalidates
source tracking before applying the callee's postconditions. Bare casts, copies,
reference binding and unevaluated expressions preserve tracking. The isolated
move fixture covers free, member, operator and indirect calls; the `993b` fixture
is now `_fail` with per-location diagnostic assertions. Nested argument-call
ordering is not repaired by this change.

Explicit method argument-versus-constant contracts now resolve tracked values and
check supported integer expressions against existing solver facts. Unknown results
use existing C/D diagnostics rather than silently skipping checks or claiming a
proven violation. Runtime scalar assignments discard stale values and guards.
Loop initialization precedes the new bound proofs, and increments are analyzed
after the body so they cannot erase entry bounds or create premature equations.
Regressions include defaults, guarded chains, unrelated unknown calls, signed
remainder and overwritten values. The simple untracked-container A/B diagnostic
policy remains unchanged.

Call-state recovery checks declared and inferred preconditions before accepting
postconditions. An unproved or violated requirement blocks result, argument,
receiver and boolean guarantees. Recovery preserves the existing annotation
frame: failed queries without declared mutations retain receiver and argument
facts. Declared mutations and mutable objects without a contract still lose
state, including aliases and pending guarded guarantees; constness does not
protect an explicitly declared state change. Pointer identity is preserved when
only its pointee state can change. Recovery is independent of diagnostic mode.
The call-entry outcome is retained so guarantees cannot prove their own requirements.
Direct return-annotation guards use the same gate. Unknown guard operands no longer
become a fabricated zero bound; scalar return facts cannot survive loop invalidation.
Recovery regressions assert the complete diagnostic set for errors followed by
valid code, repeated independent errors, guarded queries and argument side
effects. They also retain checks that failed mutators cannot promise success or
preserve stale state. This corrects the overly broad invalidation introduced in
`0eddc6b` without changing the annotation frame convention.

Regular and external-library negative fixtures now require per-location
diagnostic assertions. External fixtures establish valid acquisition/setup
conditions before checking the intended misuse, and compiler errors from missing
headers cannot satisfy the runner.
Legacy fixtures were audited against intended error sites in both standard
libraries. Safe-only false-positive fixtures were reclassified as `_xpass`,
missed bugs were separated as `_xfail`, and unsupported scenarios in mixed
fixtures are explicitly marked `KNOWN GAP`. Those comments are documentation,
not passing assertions. Capability gaps exposed by the stronger oracle remain
separate from the test-runner repair.
The final local full suite, including both standard libraries and external
libraries, matched the documented gap set exactly. Focused recovery and negative
oracle regressions passed; there were no unexpected failures.

The SARIF follow-up was also verified in a clean source archive with the default
build configuration (no `CMAKE_BUILD_TYPE`). Both the SARIF tests and the small
regression harness passed without Git metadata. Minimal constructor-initialized
ghost-state and explicitly contracted vector-parameter examples were checked
against a clean `15cc7b6` build and the repaired plugin; they are retained in
`examples/review/talk_contracts.cpp`.

## Platform validation still outstanding

The Windows release configuration now selects ClangCL and explicitly uses the
vcpkg Clang driver for tests. CMake preserves a supplied `CLANGXX`. Configuration
checks passed locally; a native Windows build and plugin-load test remain pending.
Native macOS and Windows execution has not been validated in this session.
Windows host inspection found CMake/MSVC Build Tools but no Clang/LLVM development
toolchain. No macOS host is available. These are validation blockers, not passing
checks. See [Distribution and platform validation](../DISTRIBUTION.md).
GitHub CI repair is active. The scheduled Linux failure at `2b06b5c` was traced
to CTest directly executing the non-executable SARIF runner. The repair invokes
Bash explicitly, selects the configured compiler and fixes assertion-enabled
SARIF path discovery. These fixes must reach GitHub's default branch before the
scheduled run includes them; local suite results alone cannot establish hosted
CI status. The existing strict regression gate and informational known-gap run
remain in place.

## Deferred editor work

VS Code regressions cover process cancellation, stale/failed results, project
arguments and working directory, staged downloads and checksums, compiler
selection, plugin-load checks, and LLVM asset selection. A real local
Clang/plugin setup probe also passed. CLion timeout behavior was checked against
the cached SDK. Earlier Vim command quoting and Neovim diagnostic parsing fixes
have focused regressions.

LLVM filename/extraction changes have also been applied to the other editor
installers, but their validation is unfinished. Do not treat them as release-ready.
The published archive names and top-level directories were inspected directly.

Still deferred:

- Analyze unsaved document contents across editors.
- Extend compiler/plugin and Z3 runtime validation beyond VS Code.
- Complete installer and editor validation on their native platforms.

## Reproduction and handoff

Use the build commands in `CLAUDE.md`. Run the full suite in the background:

```sh
ctest --test-dir build --output-on-failure -j8 --output-junit verification.xml > build/verification.log 2>&1 &
```

Targeted analyzer checks: `ctest --test-dir build -R '^review_' --output-on-failure`.
VS Code checks: `npm --prefix ide-plugins/vscode-dtt test`.
Known-gap failures are expected; compare their names with the baseline rather
than describing the suite as entirely green.

Verified CLI, release-workflow, and editor fixes are committed in separate
groups. Unvalidated installer changes remain in the working tree for later
review. Talk and blog corrections also remain uncommitted for the session
handling the private talk repository. No release or push was performed.
