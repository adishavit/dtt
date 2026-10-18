# Plan: A Second Analysis Engine (DTT2)

**Status:** proposal, for discussion. Nothing here is implemented.
**Assumes:** PR #43 (soundness of the current engine) is merged first and is the
baseline the new engine has to beat.

## Why

PR #43 and the reviews of it found many defects in the current engine. Most of
them are not independent bugs. They are consequences of how the engine is built.
It walks the AST with `RecursiveASTVisitor` and re-derives control flow,
evaluation order and memory identity by hand, and it stores what it knows in maps
keyed by strings such as `"s.n"` or `"p'1"`.

| Defect family found in PR #43 | Root cause in the current design |
|---|---|
| Requirements and call facts checked before the arguments ran (R1, G02) | `Visit*` runs before an expression's children; evaluation order is re-imposed by hand |
| `return`/`goto` inside a `switch` not ending a path, the source of every false zlib finding on `main` that disappeared | Control flow re-derived per statement kind |
| Exceptions: snapshots before arguments, inner handlers swallowing unmatched throws, receivers half-forgotten (R2, R3) | No exceptional edges in the representation; each call kind patched separately |
| Destructors missing on jumps, unwinding and temporaries (G04) | No implicit destructors in the representation |
| Dead branches and impossible handlers analyzed (G22) | No reachability from the representation |
| Shadowing, references, globals and method contracts colliding; solver symbol collisions (G06–G08, step 3) | Identity is a string |
| Integer width, signedness and conversions added late (G09–G13) | Values are mathematical integers with wrapping bolted on |
| "Uninitialized on some paths" and lifetimes lost at joins (G14, G18–G20) | Properties share one map with ad-hoc join rules |
| Three mechanisms for "is this argument non-null at the call"; two encodings and three checkers for inferred requirements; four assignment/copy paths | Interprocedural facts computed in passes that do not see each other; callers may be analyzed before callees |
| Quiet-mode leaks and severity inconsistencies | The verdict and its severity decided at each diagnostic site |

Fixing these one at a time in the current engine works, and PR #43 did, but each
fix tends to expose a neighbor in the same mechanism. A design that gets these
right by construction is cheaper than continuing to patch them.

## Goals and non-goals

**Goals**
- A verdict DTT reports as proven is a proof under the stated assumptions. A
  property DTT cannot establish is reported as "cannot verify", never silently
  accepted.
- The same contract language (`.dtt` files and `dtt:` annotations), properties
  (`_size`, `_nonnull`, ghost fields such as `@open`) and diagnostic categories as
  today.
- Every assumption that suppresses a finding is a named option, and every
  diagnostic that depends on one says so.

**Non-goals**
- Whole-program analysis, concurrency, or taint. These stay on the roadmap.
- Proving arbitrary C++ correct. DTT proves the properties it tracks, at the
  places it checks them.
- A bug-finder that gives up on paths it cannot afford. This is why DTT2 is not
  built on the Clang Static Analyzer, which bounds paths and inlining by design.

## Decision needed before starting

**What DTT is for.** If DTT is a research prototype or a demonstration
bug-finder, continuing to fix the current engine with probe-driven reviews is
enough, and this plan is not worth its cost. The plan pays off if "proven safe"
is meant to be trusted on real C and C++ code.

**The heuristics.** `docs/HEURISTICS.md` #3–#11 are recorded but not approved.
DTT2 implements whatever is decided. For example, whether #6 survives decides
whether there is any caller-to-callee nullness inference at all. Proposed
starting point:

| # | Assumption | Proposal |
|---|---|---|
| 3 | Signed arithmetic does not overflow | Keep, as an option; enforce mode may report possible overflow |
| 4 | Library functions do not write the program's globals | Keep |
| 5 | A callee does not retain a pointer it is passed | Restrict: check the body when visible; a `.dtt` clause marks functions that retain |
| 6 | A function's callers are the calls in this file | Restrict to internal linkage with no address taken |
| 7 | Elements of a checked pointer array are non-null | Remove |
| 8 | A positive length makes a same-named pointer non-null | Remove |
| 9 | `_`-prefixed fields are non-negative | Restrict to DTT's own tracked properties |
| 10 | Variables in call-site size checks are non-negative | Remove |
| 11 | Annotated functions write only what their `ensures` say | Restrict to functions with an explicit write-set clause |

## Architecture

DTT2 is built in layers, each testable on its own.

1. **Control flow: Clang's `CFG`.** It provides evaluation order, short-circuit
   operators, implicit destructors, temporaries, `goto`, `switch` and
   reachability. Exception edges come from the CFG where Clang provides them; the
   rest are added in one place (a `try`'s handlers and the enclosing handler for
   unmatched throws).
2. **Fixpoint: one worklist solver** over the CFG, with join, widening and
   narrowing defined per property. Whether to use Clang's `FlowSensitive`
   dataflow framework or a small solver of our own is decided by the spike
   below.
3. **Memory: abstract locations, not strings.** A location is a declaration plus
   a path of fields, base subobjects and elements. Heap allocations are locations
   too, identified by their allocation site and context. Aliasing is explicit: a
   reference names its referent's location, and a pointer's value is a set of
   locations, possibly unknown. Contracts are keyed by the canonical declaration,
   or by Clang's USR when crossing translation units. Solver symbols are derived
   from locations, so they cannot collide.
4. **Values: one lattice per property.**
   - Nullness.
   - Initialization, including "uninitialized on some path".
   - Allocation lifetime, attached to the allocation, not to the pointer variable.
   - Integers as fixed-width bit-vectors with signedness, following C's
     conversion rules.
   - Container sizes.
   - Resource typestate (ghost fields).

   Each lattice defines its own join, so a join cannot lose one property while
   keeping another.
5. **Constraints: one representation.** `.dtt` contracts, source annotations and
   inferred contracts all compile to the same constraint form over locations and
   `argN`. One checker evaluates a constraint at a program point and returns a
   verdict: `Proven`, `Violated`, or `Unknown(reason)`.
6. **Interprocedural: callee-first summaries.** Functions are analyzed in
   post-order of the call graph's strongly connected components, iterating within
   a component. A summary holds, per exit (including throwing):
   - the requirements on arguments;
   - what the function may write;
   - the facts it establishes;
   - what it frees or retains.

   A call site applies the callee's summary at call entry, after its arguments
   are evaluated. Indirect and virtual calls get a conservative summary from the
   callable's type.
7. **Policy and diagnostics.** Assumptions are named options. The single
   function mapping a verdict to a diagnostic is the only place the mode
   (inform/enforce/quiet) matters. SARIF output and the existing diagnostic
   texts carry over.

File layout follows the size principles of `REFACTOR_LARGE_FILES.md` (files of
1,500–4,000 lines, one core header, task routing in CLAUDE.md), applied to the
new engine under `src/dtt2/`. That plan's split of the current `DTTPlugin.cpp` is
not worth doing if the current engine is replaced.

## Where the work happens

The work happens in the same repository, as a second plugin target
(`libDTT2Plugin.so`) built alongside the current one. It is merged to `main` in
small pull requests and is not the default until it qualifies (below).

- **Not a new repository.** The test suite, the `.dtt` annotations, CI and the
  real-code harness are the asset both engines need. One build producing both
  plugins makes comparing them trivial.
- **Not one long-lived branch.** A branch that diverges for months cannot be
  reviewed or merged. Each layer is a reviewable change to an engine that is not
  yet on.
- **Shared code.** Both engines share the `.dtt` parser (`DTTAnnotationFile`),
  the SARIF writer (`DTTSarif`) and the annotation files. The current engine
  receives fixes only for real regressions while DTT2 is built.

## Order of work

Each step has an exit criterion. A step does not start until the previous one
meets its criterion.

### 0. Ground truth and the comparison harness

This is useful even if DTT2 is never built.
- Check in the probe corpus from the PR #43 audit as labeled fixtures (safe,
  unsafe, policy), with a runner that classifies each one as diagnosed at the
  target, silent, or diagnosed elsewhere, in enforce and quiet mode.
- For every unsafe fixture that can run, confirm the label by executing it under
  AddressSanitizer and UndefinedBehaviorSanitizer.
- Add a differential runner. It compares two plugins on the suite, the probe
  corpus, and real code (zlib first), and lists every target that became silent.

**Exit:** the runner reproduces the PR #43 audit's classification for the current
engine, and runs in CI.

### 1. Heuristic decisions

Record the decisions on #3–#11 in `docs/HEURISTICS.md`.

**Exit:** each is approved, restricted, or removed.

### 2. Spike: about a week, then decide

Build a nullness-only prototype on the CFG, twice: once on the `FlowSensitive`
framework and once on a small worklist solver. Compare them on:
- the nullness fixtures;
- exception and destructor handling;
- API stability across Clang releases (the framework has changed between
  releases);
- how naturally the location model fits.

**Exit:** a written decision, and the prototype discarded or kept.

### 3. Core: CFG, solver and locations

The worklist solver, the location model, and an empty property set.

**Exit:** the core runs on the whole suite without crashing. It is checked on its
own fixtures for evaluation order, exceptions, destructors, unreachable code and
aliasing.

### 4. Properties, one at a time

The order is chosen so that early properties exercise the core most:
1. Nullness.
2. Initialization.
3. Allocation lifetime.
4. Integers as bit-vectors.
5. Container sizes.
6. Resources and ghost fields.

**Exit, per property:** on that property's fixtures and probes, DTT2 is at least
as good as the current engine. No unsafe target becomes silent, and no safe
target gains a proven violation. Every remaining difference is explained.

### 5. Constraints, checker and diagnostics

The single constraint form, the single checker, verdicts, and the
mode-to-severity mapping. Assumptions become options.

**Exit:** the full suite runs on DTT2 with the existing expected-diagnostic
markers. Any marker change is justified in its pull request.

### 6. Interprocedural summaries

Callee-first analysis with one summary per function. This replaces caller-derived
nullness, deferred dereference checks, generic requirement propagation and
transitive buffer requirements.

**Exit:** the interprocedural fixtures pass, and zlib has no newly silent
diagnostic that the differential runner cannot explain.

### 7. Cut-over

DTT2 becomes the default plugin. The current engine stays available for one
release, then is deleted.

**Exit:** the qualification criteria below hold on `main`.

## Qualification for becoming the default

- The full suite passes, with libstdc++ and libc++.
- On the labeled corpus, no unsafe target that the current engine diagnoses is
  silent under DTT2.
- No safe target that the current engine leaves silent gets a proven violation
  under DTT2.
- On real code, every diagnostic the current engine reports and DTT2 does not is
  accounted for line by line, as was done for zlib in PR #43.
- Analysis time on the suite and on zlib is no worse than the current engine by
  more than an agreed factor.

## Risks

- **Second-system effect.** The current engine encodes many decisions about
  `.dtt` semantics and library behavior that are not written down. Mitigation:
  the tests are behavioral and carry over unchanged; step 4's parity criterion
  forces each decision to be re-made explicitly or kept.
- **Cost.** Reaching parity is weeks to months of work, not days. The spike in
  step 2 is the first point at which a better estimate is possible.
- **Framework churn.** If the `FlowSensitive` framework is chosen, its API may
  change between Clang releases. The spike weighs this against owning a solver.
- **Path explosion avoided, precision lost.** A dataflow engine merges paths, so
  some facts the current engine keeps by accident of traversal will become
  "cannot verify". Relational facts between branches, for example correlated
  conditions, need explicit design (path-sensitive refinement at joins, or
  solver-backed disjunctions) rather than being assumed.
- **Two engines at once.** Fixes to the current engine during the build split
  attention. Policy: fix only regressions there.

## Open questions

- What DTT is for. This is the decision above, and the plan depends on it.
- Whether cross-translation-unit summaries are in scope for the first cut-over,
  or remain a follow-up.
- Whether the probe corpus should become a public benchmark, with the sanitizer
  confirmations, independent of DTT.
