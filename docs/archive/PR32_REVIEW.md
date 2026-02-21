# PR #32 Review: Declarative `.dtt` Annotation File Format

## Summary

Replaces hardcoded C++ annotation files (DTTStdLib.cpp, DTTCLib.cpp, DTTWinLib.cpp)
with a declarative `.dtt` text format. A line-oriented parser (`DTTAnnotationFile.cpp`)
loads annotations at plugin init. Users can supply custom `.dtt` files via
`--dtt-annotations=path`.

**Verdict: Strong architectural improvement.** The trait system, readability gains,
and user-extensibility are excellent. Several issues below should be fixed before merge.

---

## Blocking Issues

### 1. `DTTAnnotationFile.cpp:297-303` — Duplicate string interning pools

`parseFnRequires`, `parseFnEnsures`, and `parseFnScopeExit` each declare their own
`static llvm::StringMap<const char*> FieldPool`. These are three separate pools that
don't share interned strings, wasting memory and creating inconsistency.

**Fix:** Extract a single `internString()` helper backed by one `static` pool:

```cpp
static const char *internString(llvm::StringRef s) {
    static llvm::StringMap<const char*> Pool;
    auto it = Pool.find(s);
    if (it != Pool.end()) return it->second;
    char *dup = static_cast<char*>(malloc(s.size() + 1));
    memcpy(dup, s.data(), s.size());
    dup[s.size()] = '\0';
    return Pool.insert({s, dup}).first->second;
}
```

Then replace all three `FieldPool` blocks + the `FnNamePool` + `MsgPool` with
calls to `internString()`. Reduces ~60 lines to ~10.

### 2. `DTTAnnotationFile.cpp:80-86` — Silent failure on parenthesized expressions

```cpp
if (s.front() == '(') {
    s = s.drop_front();
    return nullptr;  // silently drops the constraint
}
```

This silently swallows any constraint containing parentheses and drops the
`(` character from the parse stream, potentially corrupting subsequent parsing.
At minimum, emit an error. Better: consume until matching `)` before returning
nullptr.

**Fix:**
```cpp
if (s.front() == '(') {
    // Parenthesized sub-expressions not yet supported
    // Consume to matching close-paren to avoid corrupting parse state
    s = s.drop_front();
    int depth = 1;
    while (!s.empty() && depth > 0) {
        if (s.front() == '(') ++depth;
        if (s.front() == ')') --depth;
        s = s.drop_front();
    }
    return nullptr;
}
```

### 3. `CMakeLists.txt:212` — `DTT_ANNOTATIONS_DIR` is absolute build-time path

```cmake
target_compile_definitions(DTTPlugin PRIVATE
    DTT_ANNOTATIONS_DIR="${CMAKE_SOURCE_DIR}/annotations"
)
```

This bakes the build machine's absolute path into the `.so`. If the plugin is
installed, packaged, or the build directory moves, built-in annotations silently
fail to load.

**Fix:** At runtime, resolve relative to the plugin `.so` location:
```cpp
// In registerBuiltinAnnotations():
Dl_info info;
if (dladdr((void*)registerBuiltinAnnotations, &info)) {
    SmallString<256> dir(info.dli_fname);
    sys::path::remove_filename(dir);
    sys::path::append(dir, "..", "annotations");
    // load from dir
}
```
Or use `DTT_ANNOTATIONS_DIR` as fallback when the runtime path fails.

### 4. `DTTAnnotationFile.cpp:368` — Lambda captures stale reference

```cpp
auto internField = [](llvm::StringRef f) -> const char* {
    auto it = FieldPool.find(f);  // FieldPool is declared static ABOVE the lambda
    ...
};
```

The lambda doesn't capture `FieldPool`, it accesses it by name as a file-scope
static. This works but the lambda is declared inside `parseFnEnsures` while the
`static` pool is also inside `parseFnEnsures` — so the lambda can only be used
within that function. This is confusing; if someone copies the lambda elsewhere
it will reference a different pool. Consolidate into the single `internString()`
helper from issue #1.

---

## Non-Blocking Issues

### 5. `DTTAnnotationFile.cpp` — Silent parse failures in method bodies

When `parseAnnotationLine()` fails to parse a line inside a `type` or `trait`
block, it silently drops the annotation. This makes debugging `.dtt` files
frustrating — typos like `requries` (instead of `requires`) are invisible.

**Suggestion:** Add a fallback `else` branch:
```cpp
void parseAnnotationLine(llvm::StringRef l, MethodInfo &info) {
    if (l.starts_with("requires ")) { ... }
    else if (l.starts_with("ensures_if_true ")) { ... }
    ...
    else {
        // Unknown annotation directive
        llvm::errs() << "DTT: unknown annotation directive: " << l << "\n";
    }
}
```

### 6. `DTTAnnotationFile.cpp:501-508` — `ensures` conflates `=` and `==`

Both `ensures _size = 0` and `ensures _size == 0` are accepted as postconditions.
While convenient, this means a typo like `requires _size = 0` (should be `==`)
would parse differently than intended — but `requires` uses `parseConstraintExpr`
which only accepts `==`, so this is actually safe in practice. Still, consider
being explicit in the format spec: `ensures` uses `=` (assignment), `requires`
uses `==` (check). Reject `==` in `ensures` with a warning.

### 7. `std.dtt:283-290` — `basic_string` ctor missing multi-arg variants

```
type basic_string : IteratorPreserving {
  basic_string(0):
    ensures _size = 0
  # Note: 1-arg ctor (from string literal) has length detection in plugin
```

The 2-arg ctor `string(size_t count, char ch)` should have `ensures _size = arg0`.
The fill constructor is commonly used and the size is known.

### 8. `libc.dtt` — Review needed for completeness vs old DTTCLib.cpp

I'd want to diff the old `DTTCLib.cpp` against `libc.dtt` to verify no
annotations were lost in the translation. This is a mechanical check but
important for correctness.

### 9. `DTTAnnotationFile.h` — Missing from the diff

The header file declares the public API (`registerBuiltinAnnotations`,
`parseAnnotationFile`, `parseAnnotations`) but I can't verify the declarations
match the definitions since the header wasn't readable from HEAD. Ensure it's
consistent.

### 10. Test coverage for the parser

Only two test files (`1710_annotation_file_{pass,fail}.cpp`) test the feature.
Consider adding:
- A malformed `.dtt` file test (verifies error reporting)
- A test with trait inheritance + explicit override (verifies priority)
- A test with `fn` annotations for scope_exit (resource tracking via `.dtt`)

---

## Format Extension Suggestions

After annotating 10 real-world libraries (libwebrtc, libusb, libssh2,
s2n-tls, libarchive, libpng, zlib, libdbus, FFmpeg, libuv), we found
9 expressibility gaps. Full analysis: `docs/DTT_EXPRESSIBILITY_GAPS.md`.

These fall into two categories:

### Parser-side extensions (engine already supports these)

The old hardcoded C++ format (DTTStdLib.cpp, DTTCLib.cpp) could express
these by building `ConstraintExpr` trees directly. The `.dtt` parser is
more restrictive. These are regressions from the format migration.

#### ~~A. `||` / `&&` support in constraint parser~~ DONE (0d39d39)

#### B. `scope_exit` on arguments — `DTTAnnotationFile.cpp:461`

`parseFnScopeExit()` hard-codes `result.` as the only accepted target:
```cpp
if (!s.starts_with("result.")) return false;
```

This blocks zlib, libarchive, and libuv where the resource is initialized
via an argument, not returned:
```
# Fails to parse today:
fn deflateInit(2) {
  ensures arg0.@stream_state = 1
  scope_exit arg0.@stream_state == 0   # rejected — only result.* accepted
}
```

**Fix:** Accept `argN.FIELD` in addition to `result.FIELD`. Extend
`FreeFunctionScopeExitReq` in DTTTypes.h with `bool isResult = true` +
`int argIndex = 0`. ~15 lines parser, ~10 lines engine (check which
target at scope-exit enforcement time).

#### C. Raw argument value bounds — `DTTAnnotationFile.cpp:322`

`parseFnRequires()` requires a `.` after `argN`:
```cpp
size_t dotPos = s.find('.');
if (dotPos == llvm::StringRef::npos) return false;
```

This blocks simple numeric bounds that don't involve ghost fields:
```
# Fails to parse today:
fn png_set_quantize(4) {
  requires arg2 <= 256         # rejected — no .FIELD after argN
}
fn deflateInit2(6) {
  requires arg3 >= 8           # rejected
  requires arg3 <= 15          # rejected
}
```

Affects libpng (CVE-2026-25646), zlib, FFmpeg. The old C++ format could
express this via `BinaryExpr(CmpLE, ArgRefExpr(2), LiteralExpr(256))`.

**Fix:** When no dot is found after `argN`, parse as raw arg constraint.
Add `bool isRawArgConstraint = false` to `FreeFunctionPrecondition`.
~20 lines parser, ~30 lines engine (use arg's tracked value directly
instead of looking up a ghost field).

#### D. Named enum constants — `DTTAnnotationFile.cpp:94-129`

`parseConstraintAtom()` only handles integer literals and field/arg
identifiers. Every external library annotation uses magic numbers:
```
# Current (error-prone, all 10 libraries):
requires @signaling_state != 5
ensures @dc_ready_state = 2

# Desired:
enum SignalingState { STABLE=0, HAVE_LOCAL_OFFER=1, ..., CLOSED=5 }
requires @signaling_state != SignalingState.CLOSED
```

**Fix:** Add top-level `enum` block parsing (like `trait`/`type`/`fn`),
store in `StringMap<DenseMap<StringRef, int64_t>>`, resolve
`EnumName.CONST` to `LiteralExpr` in `parseConstraintAtom()`. ~40 lines
parser, 0 lines engine — pure syntactic sugar resolved at parse time.

---

### Engine-side gaps (neither old C++ nor .dtt can express these)

These require new constraint types or proof strategies. No annotation
syntax change can fix them without corresponding engine work. The old
baked-in format had the same limitations.

#### E. Conditional postconditions (result-dependent)

**Impact:** HIGH — all 10 libraries have fallible allocation.

Many C functions return NULL on failure. The postcondition depends on
the result, but DTT postconditions are unconditional:
```
# Not expressible in either format:
fn s2n_connection_new(1) {
  ensures_if_nonnull result.@conn_state = 1    # success path
  ensures_if_null result.@conn_state = 0        # failure path
}
```

**Workaround:** Set ghost optimistically. DTT's `_nonnull` tracking
partially compensates — if the caller NULL-checks, the ghost is only
accessed on the non-NULL branch.

**Engine work:** New `ensures_if_nonnull`/`ensures_if_return` keywords +
branch-aware postcondition application. Medium difficulty.

#### F. Conditional preconditions (state-dependent rules)

**Impact:** MEDIUM — libdbus, libusb.

Some functions have rules that depend on another ghost field's value:
```
# libdbus: private connections MUST close before unref; shared MUST NOT
fn dbus_connection_unref(1) {
  requires_if arg0.@conn_type == 2 then arg0.@conn_state == 2
}
```

**Workaround:** Use the simpler precondition that catches the most
dangerous case (`requires arg0.@conn_state != 0` catches UAF for both).

**Engine work:** New `requires_if ... then ...` keyword + conditional
precondition evaluation. Medium difficulty.

#### G. Indirect out-parameters (pointer-to-pointer)

**Impact:** MEDIUM — libusb, SQLite, FFmpeg, libpng.

Many C APIs return objects via `T**` out-parameters. DTT can't
dereference pointers in annotations:
```
# libusb_open(dev, &handle) — handle is at *arg1
fn libusb_open(2) {
  ensures (*arg1).@dev_open = 1     # not expressible — can't deref
}
```

**Workaround:** Only works for functions that return the resource as
their return value. True out-params need manual ghost tracking through
the variable that receives the value.

**Engine work:** New `*argN` dereference expression type + tracking
the pointed-to object through the out-parameter. Medium difficulty.

#### H. Ownership transfer / consumption

**Impact:** MEDIUM — FFmpeg, OpenSSL, libusb.

Some functions invalidate an argument (ownership transfer):
```
fn SSL_free(1) {
  consumes arg0    # marks arg0 as dead after call
}
fn av_packet_move_ref(2) {
  consumes arg1              # src emptied
  ensures arg0.@alive = 1   # dst receives ownership
}
```

**Workaround:** `ensures arg0.@alive = 0` on the consuming function.
Functional but manual — the caller triggers an error on subsequent use
since `@alive == 0` violates preconditions.

**Engine work:** New `consumes` keyword + automatic invalidation of
the argument's tracking state. Medium difficulty. The ghost-field
workaround covers most cases, so this is largely syntactic sugar.

#### I. Cross-object constraints (ownership hierarchies)

**Impact:** MEDIUM — 6 of 10 libraries.

DTT tracks each object independently. Can't express parent-child
lifetime relationships:
```
# libssh2: session must outlive its channels
fn libssh2_channel_free(1) {
  requires arg0.@parent_session.@session_state != 0
}

# libuv: all handles must be closed before loop closes
fn uv_loop_close(1) {
  requires ALL_HANDLES_ON(arg0).@handle_state == 4
}
```

**Workaround:** Scope-exit checks catch the common case (parent and
child in same scope). Doesn't cover objects in containers or across
function boundaries.

**Engine work:** Ghost field aliasing (linking one object's ghost to
another object) + multi-hop ghost field traversal. Hard.

#### J. Thread affinity tracking

**Impact:** LOW (compile-time is fundamentally limited here).

```
# libwebrtc: must be called from signaling thread
PeerConnection::Close():
  requires current_thread == signaling_thread
```

"Current thread" is a runtime property. Would need ghost-field-based
thread context tracking (mark blocks as "running on thread X" and check
preconditions). Research territory — runtime tools (TSan,
`RTC_DCHECK_RUN_ON`) are the current solution.

---

## Files to Check

- [ ] `DTTAnnotationFile.cpp:297` — consolidate string pools
- [ ] `DTTAnnotationFile.cpp:80` — fix parenthesized expression handling
- [ ] `CMakeLists.txt:212` — make annotation path relocatable
- [ ] `std.dtt:283` — add basic_string(2) ctor
- [ ] Verify libc.dtt completeness vs old DTTCLib.cpp
- [ ] Add parser error reporting for unknown directives
