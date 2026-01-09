# DTT Philosophical Design Concerns

**Status:** RESOLVED - Implementation Complete
**Resolution Date:** January 2026
**Implementation:** v27 - Informative Additive Safety

---

## Resolution Summary

DTT now implements **"Informative Additive Safety"** (Option 2 from the original deliberation):

> *"DTT is a bug-finder by default: it blocks only proven violations and annotation errors, and reports coverage gaps and proof failures as configurable diagnostics so teams can choose their strictness."*

### The Seven Categories

| Category | Description | Default Behavior | Mode-Independent? |
|----------|-------------|------------------|-------------------|
| **A** | Beyond Scope (raw pointers, etc.) | Silent | Yes |
| **B** | Never Tracked (no DTT wrapper) | Silent | Yes |
| **C** | Tracked, Then Lost | **Warning** | No (configurable) |
| **D** | Tracked, Undecidable | **Warning** | No (configurable) |
| **E** | Provably Safe | Silent | Yes |
| **F** | Provably Unsafe | **Error** | Yes |
| **G** | Annotation Conflict | **Error** | Yes |

### The Three Modes

```
--dtt-mode=inform   (default)  C, D → Warning; F, G → Error
--dtt-mode=enforce             C, D, F, G → Error
--dtt-mode=quiet               C, D → Silent; F, G → Error
```

---

## Key Design Decisions

### 1. Deferred Warning at First Unsafe Use

For Category C (lost tracking), DTT does NOT warn at the point of loss. Instead:

1. `unknown(v)` → DTT internally marks `v._size` as `UnknownLost` with provenance
2. No diagnostic emitted yet
3. First unsafe use (e.g., `v.front()`) → **Warning** with note pointing back to loss site
4. Subsequent uses → Silent (warn-once per variable per scope)

**Rationale:** Warning at loss is speculative noise. Warning at first unsafe use is precise.

### 2. Category G: Annotation Errors Are Always Errors

Conflicting annotations (e.g., `ensures=_size == 0` and `ensures=_size == 10` on the same method) are **always errors** regardless of mode. These represent bugs in the safety specifications themselves.

### 3. Standard Clang Integration

Users control behavior via standard flags:
- `-Werror=dtt-lost-tracking` → Promote C to error
- `-Werror=dtt-solver-failed` → Promote D to error
- `-Wno-dtt-lost-tracking` → Silence C
- `-Wno-dtt-solver-failed` → Silence D

---

## Original Deliberation (Preserved for Context)

### The Core Question

Should DTT adopt a **"Sound/Gatekeeper"** philosophy or an **"Additive Safety/Bug Finder"** philosophy?

| Philosophy | Behavior | Trade-off |
|------------|----------|-----------|
| **Gatekeeper** | Reject code DTT cannot prove safe | Sound but incomplete; may have false positives |
| **Additive Safety** | Only reject code DTT can prove unsafe | May have false negatives; never worse than baseline |

### The User's Strong Objection to Gatekeeper

> "I don't want to make the user write extra code that will not provably fix a bug - just to get rid of a DTT compiler error."

The anti-pattern being rejected:
```cpp
dtt::vector<int> v;
v.resize(n);
unknown_function(v);  // DTT loses tracking

// GATEKEEPER: Error - "v._size unknown"
// User forced to write:
if (v.size() > 0) {  // Busywork! Doesn't fix a bug!
    v.front();       // Just satisfies the tool
}

// RESOLVED BEHAVIOR (inform mode):
// Warning at v.front() with note pointing to unknown_function()
// Code compiles, user is informed of the gap
```

### Evidence That Supported the Decision

**Facebook Infer:** "They gave up on the ideal of proving absolute race freedom... a critical factor for practical adoption."

**Coverity:** "Tools that block too much get disabled. A tool catching 50% of bugs that developers actually use beats a tool catching 95% that gets turned off."

**Rust:** "`unsafe` exists because static analysis is inherently conservative."

---

## Implementation Details

### Tracking State Machine

```cpp
enum class TrackingState {
    Known,         // Category E if safe, F if unsafe
    UnknownLost,   // Category C - had tracking, lost it
    UnknownSolver, // Category D - has constraints, solver failed
    Untracked      // Category A/B - never in scope
};
```

### Provenance Recording

When tracking is lost (Category C), DTT records:
- **Location:** Source location where tracking was lost
- **Reason:** Description (e.g., "call to unannotated function 'process()'")

This provenance is emitted as a note when the warning fires at first unsafe use.

### Diagnostic IDs

```
-Wdtt-lost-tracking    Category C warnings
-Wdtt-solver-failed    Category D warnings
```

---

## Test Coverage

| Test | Category | Expected |
|------|----------|----------|
| `300_annotation_conflict_ensures_fail.cpp` | G | Error (conflicting ensures) |
| `301_annotation_conflict_bool_fail.cpp` | G | Error (conflicting bool postconditions) |
| `303_lost_tracking_inform_pass.cpp` | C | Warning, compiles |
| `304_lost_tracking_after_restore_pass.cpp` | C | Pass (tracking restored) |
| `305_proven_unsafe_still_fail.cpp` | F | Error (provably unsafe) |
| `306_solver_undecidable_pass.cpp` | D | Warning, compiles |
| `307_warn_once_pass.cpp` | C | Single warning for multiple uses |

---

## References

- [Idris Totality & Dependent Types](https://vladris.com/blog/2017/07/20/idris-totality-dependent-types-proofs.html)
- [SIGPLAN: What Does It Mean for Analysis to Be Sound?](https://blog.sigplan.org/2019/08/07/what-does-it-mean-for-a-program-analysis-to-be-sound/)
- [Scaling Static Analyses at Facebook](https://cacm.acm.org/research/scaling-static-analyses-at-facebook/)
- [Liquid Haskell Tutorial](https://ucsd-progsys.github.io/liquidhaskell-tutorial/book.pdf)
- [Rust Soundness Pledge](https://raphlinus.github.io/rust/2020/01/18/soundness-pledge.html)
- [You Can't Turn Off the Borrow Checker](https://steveklabnik.com/writing/you-can-t-turn-off-the-borrow-checker-in-rust/)
