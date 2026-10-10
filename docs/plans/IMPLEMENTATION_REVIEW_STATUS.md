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

The full analyzer suite completed with the same known-gap failures as the
baseline and no additional failures. The focused review regressions passed.
New analyzer regressions check individual scenarios rather than accepting any
compiler failure from a multi-scenario source file.

## Platform validation still outstanding

The Windows release configuration now selects ClangCL and explicitly uses the
vcpkg Clang driver for tests. CMake preserves a supplied `CLANGXX`. Configuration
checks passed locally; a native Windows build and plugin-load test remain pending.
Native macOS and Windows execution has not been validated in this session.

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
- Migrate legacy negative tests to per-location diagnostic expectations.

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
