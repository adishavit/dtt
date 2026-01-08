# DTT Philosophical Design Concerns

**Status:** UNFINISHED DELIBERATION
**Purpose:** Handoff document for continuing discussion in fresh session

---

## The Core Question

Should DTT adopt a **"Sound/Gatekeeper"** philosophy or an **"Additive Safety/Bug Finder"** philosophy?

| Philosophy | Behavior | Trade-off |
|------------|----------|-----------|
| **Gatekeeper** | Reject code DTT cannot prove safe | Sound but incomplete; may have false positives |
| **Additive Safety** | Only reject code DTT can prove unsafe | May have false negatives; never worse than baseline |

---

## Context: What DTT Is

- A Clang plugin adding compile-time dependent type checking to C++
- Tracks properties (e.g., vector sizes) and proves bounds checks at compile time
- Uses Z3 SMT solver for complex constraint reasoning
- **Baseline:** Without DTT, all C++ code compiles (standard clang behavior)
- DTT has inherent theoretical (Rice's theorem), practical, and quality-of-implementation limits

---

## The Contrarian Argument (User's Position)

> "DTT is (and may always be) a partial solution. Without it, C++ compilers may compile code which is unsound or UB. Partial DTT can improve SOME of these cases. But DTT knows it has limits. In these cases why not just drop back to what a non-DTT compiler would do anyway?"

Key insight: **It is NOT about not having a proof, but about not being able to prove something because it is beyond the scope of DTT at that point.**

### User's Strong Objection to Gatekeeper Approach

> "I don't want to make the user write extra code that will not provably fix a bug - just to get rid of a DTT compiler error. Compiler errors are not an annoyance - they are a blocker."

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

// USER'S PREFERRED: Just pass it through
// - DTT can't prove it's wrong
// - The guard doesn't fix a bug, it satisfies the tool
// - Without DTT, this code compiles fine
```

---

## Evidence Gathered: How Other Systems Handle This

### Dependent Type Systems

| System | Approach | Escape Hatch |
|--------|----------|--------------|
| **Idris/Agda/Coq** | Sound within scope; conservative totality checking | `partial` keyword opts out of totality |
| **Liquid Haskell** | Sound (with lazy evaluation caveats) | `assume` keyword asserts unproven types |

Idris documentation states: "The totality check is, necessarily, conservative" due to undecidability of the halting problem.

### Language-Level Safety

| System | Approach | Escape Hatch |
|--------|----------|--------------|
| **Rust borrow checker** | Sound for safe code | `unsafe` blocks for operations beyond the model |

Rust philosophy: "`unsafe` exists because static analysis is inherently conservative." But `unsafe` doesn't turn off the borrow checker - it adds new capabilities. Safe code remains fully checked.

### Industrial Static Analyzers

| Tool | Philosophy | Trade-off |
|------|------------|-----------|
| **Facebook Infer** | Explicitly unsound; bug-finder | Low FPs; 1000+ bugs/month at Facebook |
| **Coverity/Klocwork** | Sacrifices soundness for low FP rate | May skip difficult functions entirely |
| **PolySpace** | Sound; exhaustive | Many FPs; requires manual intervention at scale |

Facebook Infer's pivot: "They gave up on the ideal of proving absolute race freedom. Instead, they established a 'completeness' theorem saying that... the analyzer reports only true positives."

### Key Insight from Research

From SIGPLAN: "Sound but incomplete → Catch every possible error, even if it means many false positives. Complete but unsound → Report only guaranteed issues, even if some real ones slip by."

Coverity's experience: Tools that block too much get disabled. A tool catching 50% of bugs that developers actually use beats a tool catching 95% that gets turned off.

---

## The Six Categories of "Unknown"

Critical distinction identified during deliberation:

| Category | Example | What DTT Knows | Current Behavior |
|----------|---------|----------------|------------------|
| **A. Beyond Scope** | `ptr[i]` (raw pointer) | DTT doesn't model this type | Silent (correct) |
| **B. Never Tracked** | `std::vector` (no DTT wrapper) | User didn't opt into DTT | Silent (correct) |
| **C. Tracked, Then Lost** | `dtt::vector v; unknown(v); v[0]` | DTT was tracking, lost visibility | Error (disputed) |
| **D. Tracked, Undecidable** | `v[n]` where `n` symbolic, no guard | DTT tracking, can't resolve | Error (disputed) |
| **E. Tracked, Provably Safe** | `v[0]` where `_size=5` | DTT proves safe | Silent (correct) |
| **F. Tracked, Provably Unsafe** | `v[10]` where `_size=5` | DTT proves bug exists | Error (correct) |

### The Semantic Difference

**A & B: "Not my job"**
- DTT never claimed to analyze this
- Silent pass is obviously correct
- No user expectation violated

**C: "I was watching, then I blinked"**
- User opted into DTT tracking (used `dtt::vector`)
- DTT was providing coverage
- Something broke the tracking (unknown function, complex flow)
- This is a **gap in coverage** - user might want to know

**D: "I'm watching, but I can't tell"**
- DTT is actively tracking
- Has symbolic values, constraints, everything
- But the math doesn't resolve (Z3 timeout, insufficient constraints)
- This is a **verification limit** - different from "not tracking"

### The Concern With Pure Additive Safety

If C and D are silent:
```cpp
dtt::vector<int> v;
v.push_back(1);
v.push_back(2);  // _size = 2

process(v);      // Unknown function - tracking lost

v[0] = 10;       // Silent? User thinks DTT is checking this!
v[5] = 50;       // Silent? This is actually a bug DTT can't see!
```

Risk: **False confidence.** User chose `dtt::vector` expecting coverage, but after `process(v)`, DTT is blind and user doesn't know.

---

## Technical Feasibility: Warnings and Strict Modes

**Confirmed possible.** Clang's diagnostic engine supports severity levels:

```cpp
// Error (blocks compilation)
Diag.getCustomDiagID(DiagnosticsEngine::Error, "...");

// Warning (doesn't block by default)
Diag.getCustomDiagID(DiagnosticsEngine::Warning, "...");

// Remark (even softer)
Diag.getCustomDiagID(DiagnosticsEngine::Remark, "...");
```

User controls via standard clang flags:
- `-Werror` → promote all warnings to errors
- `-Wno-*` → suppress specific warnings
- Plugin could accept `--dtt-strict` argument

---

## Proposed Tiered Model (Under Consideration)

```
                    SILENT          WARNING         ERROR
                    (baseline)      (inform)        (block)
                         │              │              │
A. Beyond scope     ────●              │              │
B. Never tracked    ────●              │              │
C. Lost tracking    ────?──────────────?──────────────?  ← UNDECIDED
D. Undecidable      ────?──────────────?──────────────?  ← UNDECIDED
E. Provably safe    ────●              │              │
F. Provably unsafe  ────┼──────────────┼──────────────●
```

### Option 1: Pure Additive Safety
| Category | Behavior |
|----------|----------|
| A, B, C, D, E | Silent |
| F | Error |

Pros: Never blocks valid code; never forces busywork
Cons: Risk of false confidence; user doesn't know when coverage is lost

### Option 2: Informative Additive Safety
| Category | Default | Strict Mode |
|----------|---------|-------------|
| A, B | Silent | Silent |
| C, D | Warning | Error |
| E | Silent | Silent |
| F | Error | Error |

Pros: User informed of coverage gaps; can choose to act
Cons: Warnings might still feel like noise to some users

### Option 3: Configurable
Let users choose their philosophy:
- `--dtt-mode=strict` → C, D are errors
- `--dtt-mode=warn` → C, D are warnings (default?)
- `--dtt-mode=additive` → C, D are silent

---

## Open Questions (To Be Resolved)

1. **Should users know when DTT's coverage has gaps?**
   - Yes → Warnings for C & D
   - No → Silent for C & D (pure additive)

2. **Is "false confidence" a real concern?**
   - If users understand DTT has limits (via docs), maybe not
   - If users assume `dtt::vector` = fully verified, it is

3. **Should there be an explicit escape hatch?**
   - `[[clang::annotate("dtt:trust")]]` to silence warnings/errors
   - Explicit acknowledgment vs implicit pass-through

4. **What about the "seam" problem?**
   - Bugs often live at boundaries between tracked and untracked code
   - Silent pass-through might miss exactly these cases

5. **How to distinguish C from D programmatically?**
   - C: DTT had tracking, then an operation invalidated it
   - D: DTT has tracking, but constraint solving failed
   - Implementation may need to track these differently

---

## The Fundamental Tension

Two valid philosophies with different failure modes:

| Philosophy | Failure Mode | Who Pays |
|------------|--------------|----------|
| **Gatekeeper** | False positives; user writes busywork code | Developer friction |
| **Additive Safety** | False negatives; bugs slip through | End users (security) |

The user's position: DTT should never cause the first failure mode. The second is acceptable because it's no worse than not having DTT at all.

Counter-position: If users opt into DTT (by using `dtt::vector`), they expect coverage. Silent gaps may be worse than no tool because they create false confidence.

---

## Key Quotes to Remember

User:
> "I don't want to make the user write extra code that will not provably fix a bug - just to get rid of a DTT compiler error."

> "It is NOT about not having a proof, but about not being able to prove something because it is beyond the scope of DTT at that point."

Facebook Infer team:
> "They gave up on the ideal of proving absolute race freedom... a critical factor for practical adoption."

Rust philosophy:
> "`unsafe` exists because static analysis is inherently conservative."

---

## Next Steps

1. Decide on treatment of categories C and D (the core question)
2. Decide if warnings are acceptable or if pure silence is required
3. Decide if escape hatch annotation is needed
4. Consider if different categories need different diagnostic IDs (for `-Wno-*` control)
5. Update DTTPlugin.cpp to implement chosen philosophy
6. Document the philosophy clearly in README/GUIDE

---

## References

- [Idris Totality & Dependent Types](https://vladris.com/blog/2017/07/20/idris-totality-dependent-types-proofs.html)
- [SIGPLAN: What Does It Mean for Analysis to Be Sound?](https://blog.sigplan.org/2019/08/07/what-does-it-mean-for-a-program-analysis-to-be-sound/)
- [Scaling Static Analyses at Facebook](https://cacm.acm.org/research/scaling-static-analyses-at-facebook/)
- [Liquid Haskell Tutorial](https://ucsd-progsys.github.io/liquidhaskell-tutorial/book.pdf)
- [Rust Soundness Pledge](https://raphlinus.github.io/rust/2020/01/18/soundness-pledge.html)
- [You Can't Turn Off the Borrow Checker](https://steveklabnik.com/writing/you-can-t-turn-off-the-borrow-checker-in-rust/)
- [Comparative Study of Static Analysis Tools](http://www.promptedu.se/promptwp/wp-content/uploads/2015/01/Comparative-Study-ENTCS-08.pdf)
