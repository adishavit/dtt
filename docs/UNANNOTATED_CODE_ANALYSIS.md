# DTT Analysis: Unannotated C++ Codebases

## Executive Summary

When DTT runs on unannotated codebases, it produces many "nullability unknown" warnings. This analysis examines these findings to understand their sources, determine which are likely true positives vs false positives, and propose mitigations.

**Critical insight:** We cannot assume all findings are false positives. Many may represent genuine null pointer vulnerabilities that exist in real-world code.

---

## Validation Dataset: tinyexr

**Project:** tinyexr - Header-only C++ EXR image library
**Size:** ~11,000 lines
**DTT Findings:** 462 total

| Finding Type | Count | % |
|--------------|-------|---|
| Null pointer (unknown nullability) | 460 | 99.6% |
| Buffer index bounds | 2 | 0.4% |

---

## Finding Source Analysis

### Where Do "Nullability Unknown" Findings Come From?

We traced the 460 null pointer findings to their sources:

| Variable | Count | Source Type |
|----------|-------|-------------|
| `exr_header` | 143 | Function parameter |
| `info` | 63 | Function parameter |
| `exr_image` | 29 | Function parameter |
| `exr_headers[?]` | 27 | Array element from param |
| `px`, `py` | 40 | Derived from parameter |
| `p01`, `p10`, `p11` | 48 | Derived from parameter |
| `image`, `level_image` | 36 | Function parameter |
| Other | 74 | Mixed |

**Pattern:** ~95% trace back to function parameters (directly or via assignment chains).

### Example: Parameter Derivation Chain

```cpp
static void wav2Encode(
    unsigned short *in,  // Parameter - nullability unknown
    int nx, int ox, int ny, int oy,
    unsigned short mx)
{
    unsigned short *py = in;      // Derived from param
    unsigned short *px = py;      // Derived from py
    unsigned short *p01 = px + ox1;  // Derived from px

    wenc14(*px, *p01, ...);  // DTT warns: px nullability unknown
}
```

---

## The Key Question: True Positives or False Positives?

### Arguments That These Are FALSE POSITIVES

1. **Internal functions assume valid input**
   - Static/internal helper functions trust their callers
   - Adding null checks everywhere would bloat code
   - The "contract" is implicit: caller ensures valid pointers

2. **API documentation**
   - Many C APIs document "behavior undefined if ptr is NULL"
   - This is a valid design choice, not a bug

3. **Performance-critical code**
   - tinyexr is image processing code
   - Null checks in inner loops have measurable cost

### Arguments That These Are TRUE POSITIVES

1. **Defense in depth**
   - Even internal functions can receive bad data
   - A bug in one caller propagates to all callees
   - Null checks are cheap compared to crashes

2. **Real CVEs exist**
   - CVE-2020-18768: tinyexr null pointer dereference
   - CVE-2022-34300: tinyexr heap buffer overflow
   - The library HAS had null pointer bugs

3. **Caller mistakes happen**
   ```cpp
   EXRHeader* header = LoadHeader(file);  // Returns NULL on error
   ConvertHeader(header, info, ...);      // Oops - didn't check!
   ```

4. **Error handling gaps**
   - Many functions return error codes but callers ignore them
   - The "successful" path assumes all pointers valid
   - Error paths may pass NULL inadvertently

### Verdict: MIXED

Without auditing each case individually, we cannot determine the true FP rate. However:

- **Public API functions:** Findings likely TRUE POSITIVES (callers may pass NULL)
- **Internal static helpers:** Findings likely FALSE POSITIVES (trusted callers)
- **Error handling paths:** Findings likely TRUE POSITIVES (common bug source)

---

## Categorization Framework

We propose categorizing findings by **confidence level**:

### Category A: High Confidence True Positives
- Public API functions (exported, documented)
- Functions called from error handling paths
- Functions that check some params but not others (inconsistent)

### Category B: Medium Confidence (Needs Review)
- Internal functions called from multiple sites
- Functions in complex control flow
- Params that could be NULL in edge cases

### Category C: Low Confidence (Likely False Positives)
- Private static helper functions
- Params derived from other non-null sources
- Performance-critical inner loops with trusted callers

---

## Proposed Mitigations

### Tier 1: Improve Signal-to-Noise (High Priority)

#### 1.1 Function Visibility Heuristics

Trust parameters in `static` functions more than exported functions:

```cpp
// High confidence - public API, should warn
TINYEXR_API int LoadEXR(float **out, int *width, EXRHeader *header);

// Lower confidence - internal, caller likely validates
static bool ConvertHeader(EXRHeader *exr_header, const HeaderInfo &info);
```

**Implementation:** Reduce severity for static/internal function parameters.

#### 1.2 Null Check Pattern Recognition

If a function checks SOME parameters, findings on unchecked params are higher confidence:

```cpp
void process(Data* a, Data* b, Data* c) {
    if (!a) return;  // 'a' is checked
    if (!b) return;  // 'b' is checked
    // 'c' NOT checked - higher confidence this is a bug!
    c->value = 42;
}
```

**Implementation:** Boost confidence when sibling params are null-checked.

#### 1.3 Derive-Chain Collapsing

Multiple warnings from the same derivation chain should collapse:

```cpp
int* p = param;      // Source
int* q = p;          // Derived
int* r = q;          // Derived
*r = 42;             // Warn once, not for p, q, r separately
```

**Implementation:** Track derivation, report at root source only.

### Tier 2: User Control (Medium Priority)

#### 2.1 Trust Annotations

Allow users to mark trusted boundaries:

```cpp
// Trust all params in this function
[[clang::annotate("dtt:trust_params")]]
static void internalHelper(Data* a, Data* b);

// Trust specific param
[[clang::annotate("dtt:assumes=arg0 != nullptr")]]
void process(Data* trusted, Data* untrusted);
```

#### 2.2 Trust Regions

Pragma-based trust for legacy code:

```cpp
#pragma dtt trust push
// All null findings suppressed in this region
void legacyFunction(Data* p) {
    p->value = 42;  // No warning
}
#pragma dtt trust pop
```

#### 2.3 Severity Modes

```bash
# Default: warn on all unknown nullability
clang++ -fplugin=DTTPlugin.so file.cpp

# Strict: only warn on public APIs
clang++ -fplugin=DTTPlugin.so -Xclang -plugin-arg-dtt -Xclang --null-check=public file.cpp

# Paranoid: error on all unknown nullability
clang++ -fplugin=DTTPlugin.so -Xclang -plugin-arg-dtt -Xclang --null-check=strict file.cpp
```

### Tier 3: Analysis Improvements (Lower Priority)

#### 3.1 Basic Null Check Recognition

Recognize common guard patterns:

```cpp
if (ptr == nullptr) return ERROR;
// After this: ptr proven non-null ✓ (already implemented)

if (!ptr) return ERROR;
// Same pattern ✓ (already implemented)

assert(ptr != nullptr);
// Should also prove non-null (not yet implemented)
```

#### 3.2 Output Parameter Recognition

Parameters used as outputs are less likely null:

```cpp
void getData(int* out_value) {
    *out_value = 42;  // 'out_value' is output param
}
// Lower confidence - caller wants to receive data, unlikely to pass NULL
```

#### 3.3 Allocation Return Tracking ✓ IMPLEMENTED

Track common allocation patterns:

```cpp
EXRHeader* h = (EXRHeader*)malloc(sizeof(EXRHeader));
// h could be NULL (malloc can fail)
h->field = 0;  // HIGH confidence true positive!

EXRHeader* h2 = new EXRHeader();
// h2 is non-null (new throws on failure) ✓ (already implemented)
```

**Status:** DTT now tracks malloc/calloc/realloc provenance and shows where allocation occurred:
```
error: potential null pointer dereference of 'h' (nullability unknown)
note: 'h' may be NULL because: return value from malloc() may be NULL (allocation can fail)
note: add guard: if (h != nullptr) { ... }
```

---

## Diagnostic Improvements

### Current Output (with Provenance Tracking) ✓ IMPLEMENTED
```
error: DTT: potential null pointer dereference of 'exr_header' (nullability unknown)
note: 'exr_header' may be NULL because: parameter 'exr_header' may be NULL (callers could pass NULL)
note: add guard: if (exr_header != nullptr) { ... }
```

For malloc-derived pointers:
```
error: DTT: potential null pointer dereference of 'ptr' (nullability unknown)
note: 'ptr' may be NULL because: return value from malloc() may be NULL (allocation can fail)
note: add guard: if (ptr != nullptr) { ... }
```

### Proposed Future Enhanced Output
```
warning: DTT: potential null pointer dereference of 'exr_header' (nullability unknown)
  --> tinyexr.h:6160:3
   |
   | static bool ConvertHeader(EXRHeader *exr_header, ...) {
   |                           ^^^^^^^^^ parameter declared here
   |
   |   exr_header->pixel_aspect_ratio = info.pixel_aspect_ratio;
   |   ^~~~~~~~~~ dereference here
   |
note: confidence: MEDIUM (internal function, but unchecked parameter)
note: to suppress: add [[clang::annotate("dtt:assumes=arg0 != nullptr")]]
note: to fix: add null check before dereference
```

### Statistics Report

Add `--dtt-stats` flag to output:

```
DTT Null Pointer Analysis Summary
=================================
Total findings: 460

By confidence:
  HIGH   (public API):     12 (2.6%)
  MEDIUM (internal):      203 (44.1%)
  LOW    (derived/helper): 245 (53.3%)

By source:
  Function parameters:    412 (89.6%)
  Return values:           31 (6.7%)
  Member access:           17 (3.7%)

Top functions by finding count:
  ConvertHeader:           47
  DecodeChunk:            38
  wav2Encode:             32
  ...
```

---

## Implementation Roadmap

### Phase 1: Quick Wins (1-2 weeks)
- [ ] Add `--dtt-stats` for finding categorization
- [ ] Implement derive-chain collapsing (reduce noise)
- [ ] Add confidence level to diagnostic output

### Phase 2: Heuristics (2-3 weeks)
- [ ] Static function parameter trust heuristic
- [ ] Sibling null-check detection (boost confidence)
- [ ] `assert()` recognition as proof

### Phase 3: User Control (2-3 weeks)
- [ ] `dtt:trust_params` annotation
- [ ] `dtt:assumes=` precondition annotation
- [ ] Trust region pragmas
- [ ] `--null-check=public|all|strict` mode

### Phase 4: Advanced (Future)
- [ ] Output parameter recognition
- [ ] Cross-function null check propagation
- [ ] Integration with existing static analyzers' annotations

---

## Recommendations

### For DTT Development

1. **Don't assume FPs** - Many findings may be real bugs
2. **Add confidence levels** - Help users triage findings
3. **Provide escape hatches** - Trust annotations for intentional patterns
4. **Improve diagnostics** - Show derivation chains, suggest fixes

### For Users Running DTT on Unannotated Code

1. **Start with public APIs** - These findings are highest confidence
2. **Review error paths** - Common source of real null bugs
3. **Use quiet mode initially** - `--dtt-mode=quiet` to see only proven bugs
4. **Annotate incrementally** - Add `dtt:ensures` to critical functions first
5. **Don't blanket-suppress** - Each finding deserves review

### For Codebase Maintainers

1. **Add null checks to public APIs** - Defense in depth
2. **Document null contracts** - Even if just comments
3. **Consider DTT annotations** - Zero runtime cost, compile-time safety
4. **Review allocation error handling** - malloc/realloc CAN return NULL

---

## Conclusion

DTT's "nullability unknown" findings on unannotated code are a **mixed bag** of true and false positives. The solution is not to suppress them blindly, but to:

1. **Categorize by confidence** - Help users prioritize
2. **Provide user control** - Trust annotations for intentional patterns
3. **Improve diagnostics** - Make triage easier
4. **Recognize that some ARE bugs** - DTT may be finding real vulnerabilities

The goal is making DTT useful for unannotated codebases while maintaining its core value proposition: **catching real bugs at compile time**.
