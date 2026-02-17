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

## Format Extension Suggestions (for future PRs)

These are NOT bugs — they're ideas for extending the `.dtt` format based on
analyzing 10+ external libraries that would benefit from annotations:

### A. `||` / `&&` support in constraint parser

The plugin's internal constraint engine (`DTTPlugin.cpp:2039-2058`) fully
supports `LogicalOr`, `LogicalAnd`, and `LogicalNot` with Z3 integration.
But the `.dtt` parser (`parseConstraintExpr`) only handles simple
`atom OP atom` comparisons — no compound expressions.

This is the #1 gap blocking state machine annotations for external
libraries. Every library with >2 states needs disjunctive preconditions:
```
requires @state == 1 || @state == 3    # .dtt parser can't parse this
```

**Fix:** Add `parseLogicalAnd()` / `parseLogicalOr()` to the `.dtt` parser,
mirroring the existing plugin parser pattern.

### B. State enum declarations

Many libraries have multi-valued state machines (not just 0/1).
The format should support named constants:

```
enum SignalingState {
  STABLE = 0
  HAVE_LOCAL_OFFER = 1
  HAVE_REMOTE_OFFER = 2
  CLOSED = 5
}
```

Then use in annotations:
```
requires @signaling_state == SignalingState.HAVE_LOCAL_OFFER
```

### B. Conditional postconditions (result-dependent)

Many functions have "returns non-null on success, null on failure":

```
fn sqlite3_prepare_v2(5) {
  ensures_if_nonnull result.@stmt_state = 1
  ensures_if_null result.@stmt_state = 0
}
```

### C. Ownership transfer annotations

```
fn SSL_free(1) {
  requires arg0.@ssl_state != 5 "already freed"
  consumes arg0   # marks arg0 as invalidated after call
}
```

### D. Cross-object constraints

```
fn leveldb_iter_destroy(1) {
  requires arg0.@parent_db.@db_open == 1 "database closed before iterator"
}
```

---

## Files to Check

- [ ] `DTTAnnotationFile.cpp:297` — consolidate string pools
- [ ] `DTTAnnotationFile.cpp:80` — fix parenthesized expression handling
- [ ] `CMakeLists.txt:212` — make annotation path relocatable
- [ ] `std.dtt:283` — add basic_string(2) ctor
- [ ] Verify libc.dtt completeness vs old DTTCLib.cpp
- [ ] Add parser error reporting for unknown directives
