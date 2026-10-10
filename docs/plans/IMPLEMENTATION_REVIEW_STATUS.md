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
  and repository-relative paths.
- Embed STL/libc/Win32 contracts in the plugin and install their source files
  and optional external-library annotations. A relocated standalone plugin and
  a temporary installation both retain STL/libc checking.
- Honor manually requested release tags and publish the checksum file consumed
  by installers. Release shell steps were exercised locally; no release was made.
- Make the ghost-state failure example establish its initial state explicitly.
  It now demonstrates a proven violation in inform, quiet, and enforce modes.

The full analyzer suite completed with the original known-gap failures plus
the newly registered range-for bounds and by-value move gaps. Previously passing
checks have no unresolved new failures. The focused review regressions passed.
New analyzer regressions check individual scenarios rather than accepting any
compiler failure from a multi-scenario source file. Regular main-suite `_fail`
fixtures now require a normal DTT error and reject unrelated compiler errors.
Selected smart-pointer cases assert diagnostics at specific source locations.
Most legacy fixtures remain unmarked; external-library and known-gap fixtures
retain their old exit-code oracle.

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
intentionally silent; the argument-versus-constant path skips unknown arguments;
method postconditions are applied without requiring successful precondition proof.
Ghost fields have no implicit zero initialization. See the reporting limits in `GUIDE.md` and the reproductions in
`examples/review/talk_findings.cpp`.

Initialization repairs now import integer members from Clang's evaluation of
complete user-record initialization. This respects aggregate arguments and
constexpr constructor overrides; runtime-dependent values are not inferred.
Direct `std::vector` initializer-list construction records element count after
constructor contracts, including defaulted allocator arguments. The safe `993`
fixture is now `_pass`. Isolated regressions check the intended errors in quiet
mode as well as safe accesses, overrides, copy/move and both standard libraries.
The regression pass also exposed a separate existing gap: moving a vector into a
by-value function argument retains its source size. It is recorded in
`993b_vector_move_argument_xfail.cpp`; named-destination move construction is
covered by the passing tests. No unknown-parameter policy or postcondition gating
change is included.

## Platform validation still outstanding

The Windows release configuration now selects ClangCL and explicitly uses the
vcpkg Clang driver for tests. CMake preserves a supplied `CLANGXX`. Configuration
checks passed locally; a native Windows build and plugin-load test remain pending.
Native macOS and Windows execution has not been validated in this session.
Windows host inspection found CMake/MSVC Build Tools but no Clang/LLVM development
toolchain. No macOS host is available. These are validation blockers, not passing
checks. See [Distribution and platform validation](../DISTRIBUTION.md).
GitHub CI remains deferred at the user's direction.

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
- Continue migrating legacy negative tests to per-location diagnostic expectations.

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
