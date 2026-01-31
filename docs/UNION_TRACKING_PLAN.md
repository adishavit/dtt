# Implementation Plan: Union Active Member Tracking

## Executive Summary

This plan extends DTT to track the **active member** of C/C++ unions, enabling detection of:
- Reading an inactive union member (undefined behavior in C++)
- Type confusion vulnerabilities (CWE-843)
- Memory corruption from misinterpreted union data

Union type confusion is a significant source of CVEs, particularly in:
- Legacy C codebases
- Protocol parsers and deserializers
- Windows COM (10,000+ unions per OS release)
- Embedded systems with memory constraints

---

## Motivation: CVE Impact

### Research Findings

| Source | Finding |
|--------|---------|
| USENIX Security 2023 | 19 memory-corrupting type confusions found in Windows COM, 4 confirmed CVEs |
| MITRE CWE | CWE-843 (Type Confusion) in Top 25 Most Dangerous Weaknesses (2024) |
| RunSafe Security | ~70% of compiled code vulnerabilities are memory safety related |
| Chrome/V8 2024 | Multiple CISA KEV entries for type confusion (CVE-2024-5274, CVE-2024-4947, CVE-2024-7971) |

### Why Unions Matter

Unlike `std::variant` (modern C++), raw unions are prevalent in:
- **C codebases** - no `std::variant` available
- **Protocol handlers** - parsing network/file formats with tagged unions
- **Memory-constrained systems** - unions save space vs structs
- **Legacy Windows code** - COM uses unions extensively

---

## Scope: What This WILL and WILL NOT Support

### WILL Support

| Feature | Description | Example | Priority |
|---------|-------------|---------|----------|
| **Active member tracking** | Track which member was last written | `u.i = 42` → `_union_active = "i"` | P0 |
| **Inactive member read (C++)** | Error on reading non-active member | `u.i = 42; x = u.f;` | P0 |
| **Inactive member read (C)** | Warning on reading non-active member | Same, but warning not error | P0 |
| **Initialization tracking** | First member active after declaration | `union U u;` → `_union_active = first` | P0 |
| **Designated initializers** | Track from `{.member = val}` | `union U u = {.f = 3.14}` | P1 |
| **Assignment tracking** | Update active on member assignment | `u.s = "hello"` | P0 |
| **Copy semantics** | Propagate active member on copy | `u2 = u1` | P1 |
| **Pointer-to-member write** | Track `int *p = &u.i; *p = 42;` | Activates `u.i` | P2 |
| **Guard extraction** | Proof from discriminant checks | `if (tag == INT) { u.i; }` | P2 |
| **Anonymous unions** | Track members of anonymous unions | `struct { union { int i; float f; }; }` | P1 |
| **Common initial sequence** | Allow access to common prefix (C++) | `union { struct {int t;} a, b; }` | P2 |

### WILL NOT Support (Out of Scope)

| Feature | Reason | Workaround |
|---------|--------|------------|
| **memcpy/memset to union** | Cannot determine which member | Conservative: mark unknown |
| **Union in array** | `union U arr[10]` element tracking | Track array, not elements |
| **Cross-function tracking** | Union passed to/from functions | Annotations on function boundaries |
| **Pointer arithmetic** | `*(int*)((char*)&u + offset)` | Too complex to track statically |
| **Bit-fields in unions** | Complex layout rules | Ignore bit-field specifics |
| **Volatile unions** | Hardware-mapped unions | Assume any member valid |
| **Union of unions** | Nested union tracking | Track outer union only |
| **reinterpret_cast overlay** | `reinterpret_cast<U*>(buffer)` | Existing buffer tracking handles this |

### C vs C++ Mode

**Key semantic difference:**

| Behavior | C (permissive) | C++ (strict) |
|----------|----------------|--------------|
| Read inactive member | Implementation-defined | Undefined behavior |
| Type punning via union | Widely supported | UB (except char/byte) |
| DTT response | **Warning** | **Error** |

Detection of language mode:
```cpp
// In DTTPlugin.cpp
bool isCPlusPlusMode() {
    return Ctx.getLangOpts().CPlusPlus;
}
```

### Exceptions to Strict C++ Mode

Even in C++ mode, some accesses are legal:

1. **Common initial sequence**: If two struct members share a common initial sequence, accessing either is valid
2. **char/unsigned char/std::byte access**: Always allowed (for serialization)
3. **Inactive member address**: Taking `&u.member` is valid even if inactive

---

## Technical Design

### New Property: `_union_active`

Track the active member as a string (member name):

```cpp
union Data { int i; float f; char c[4]; };
Data u;
u.i = 42;      // _union_active = "i"
u.f = 3.14;    // _union_active = "f"
float x = u.f; // OK: reading active member
int y = u.i;   // ERROR (C++), WARNING (C): reading inactive member
```

### Property Values

| Value | Meaning |
|-------|---------|
| `"member_name"` | That member is active |
| `""` (empty) | Unknown/uninitialized |
| `"*"` | Any member could be active (after memcpy, etc.) |

### Detection Points

#### 1. Union Declaration with Initializer

```cpp
// VarDecl with union type
union U { int i; float f; } u = {42};  // _union_active = "i" (first member)
union U u2 = {.f = 3.14};              // _union_active = "f" (designated)
```

**Implementation location:** `VisitVarDecl`

```cpp
if (auto *RD = VD->getType()->getAsRecordDecl()) {
    if (RD->isUnion()) {
        if (auto *Init = VD->getInit()) {
            StringRef activeMember = getInitializedUnionMember(Init, RD);
            setFieldTyped(varPath, "_union_active", SymValue::String(activeMember));
        } else {
            // Uninitialized - first member is "active" (indeterminate value)
            if (auto *FirstField = *RD->field_begin()) {
                setFieldTyped(varPath, "_union_active",
                             SymValue::String(FirstField->getName()));
            }
        }
    }
}
```

#### 2. Member Assignment (Write)

```cpp
// BinaryOperator with MemberExpr LHS
u.f = 3.14;  // _union_active = "f"
```

**Implementation location:** `VisitBinaryOperator` (existing MemberExpr handling at line ~6869)

```cpp
if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
    QualType baseType = Mem->getBase()->getType()->getPointeeOrArrayElementType();
    if (auto *RD = baseType->getAsRecordDecl()) {
        if (RD->isUnion()) {
            StringRef memberName = Mem->getMemberDecl()->getName();
            FieldPath basePath = getTypedPath(Mem->getBase());
            setFieldTyped(basePath, "_union_active", SymValue::String(memberName));
        }
    }
    // ... existing member assignment handling
}
```

#### 3. Member Read (Access Check)

```cpp
// MemberExpr on RHS or in expression context
int x = u.i;  // Check: _union_active == "i"
foo(u.f);     // Check: _union_active == "f"
```

**Implementation location:** `VisitMemberExpr` or expression visitor

```cpp
bool VisitMemberExpr(MemberExpr *ME) {
    Expr *Base = ME->getBase()->IgnoreParenImpCasts();
    QualType baseType = Base->getType()->getPointeeOrArrayElementType();

    if (auto *RD = baseType->getAsRecordDecl()) {
        if (RD->isUnion() && !isLValueContext(ME)) {  // Reading, not writing
            FieldPath basePath = getTypedPath(Base);
            SymValue active = getFieldTyped(basePath, "_union_active");
            StringRef accessedMember = ME->getMemberDecl()->getName();

            if (active.isString()) {
                StringRef activeMember = active.getStringValue();
                if (!activeMember.empty() && activeMember != accessedMember) {
                    // Check for exceptions
                    if (!isCharAccess(ME) && !isCommonInitialSequence(RD, activeMember, accessedMember)) {
                        if (Ctx.getLangOpts().CPlusPlus) {
                            reportError(ME->getBeginLoc(),
                                "reading inactive union member '%0' (active: '%1')",
                                accessedMember, activeMember);
                        } else {
                            reportWarning(ME->getBeginLoc(),
                                "reading inactive union member '%0' (active: '%1') - type punning",
                                accessedMember, activeMember);
                        }
                    }
                }
            }
        }
    }
    return true;
}
```

#### 4. Pointer-to-Member Write (P2)

```cpp
int *p = &u.i;
*p = 42;  // Should set _union_active = "i"
```

**Implementation:** Track that `p` points to `u.i`, then on `*p = ...` update `u._union_active`.

This requires extending pointer provenance tracking:
```cpp
// When taking address of union member
if (auto *UO = dyn_cast<UnaryOperator>(Init)) {
    if (UO->getOpcode() == UO_AddrOf) {
        if (auto *ME = dyn_cast<MemberExpr>(UO->getSubExpr())) {
            // Track: p._points_to_union = "u"
            // Track: p._points_to_member = "i"
        }
    }
}
```

#### 5. Copy/Move Assignment

```cpp
union U u1, u2;
u1.i = 42;
u2 = u1;  // u2._union_active = u1._union_active
```

**Implementation:** In assignment handling, propagate `_union_active`:

```cpp
if (isUnionType(lhsType)) {
    SymValue srcActive = getFieldTyped(rhsPath, "_union_active");
    setFieldTyped(lhsPath, "_union_active", srcActive);
}
```

#### 6. Guard Extraction (Tagged Unions)

Common pattern in C:
```cpp
struct TaggedUnion {
    int tag;
    union { int i; float f; } data;
};

void process(TaggedUnion *t) {
    if (t->tag == TAG_INT) {
        int x = t->data.i;  // Should be allowed - guarded
    }
}
```

**Implementation:** Extract proof from discriminant comparison:

```cpp
// In guard extraction (VisitIfStmt)
// Pattern: obj.tag == CONSTANT  or  obj->tag == CONSTANT
// Creates proof: obj.data._union_active == member_for_tag
```

This requires a way to associate tag values with union members. Options:
1. **Naming convention:** `TAG_INT` → member `i`, `TAG_FLOAT` → member `f`
2. **Annotation:** User provides mapping
3. **Heuristic:** Same prefix/suffix matching

For Phase 1, skip this - mark as future enhancement.

---

## Helper Functions

### isUnionType

```cpp
bool isUnionType(QualType T) {
    T = T.getCanonicalType();
    if (T->isPointerType() || T->isReferenceType())
        T = T->getPointeeType();
    if (auto *RD = T->getAsRecordDecl())
        return RD->isUnion();
    return false;
}
```

### getUnionRecordDecl

```cpp
const RecordDecl *getUnionRecordDecl(QualType T) {
    T = T.getCanonicalType();
    if (T->isPointerType() || T->isReferenceType())
        T = T->getPointeeType();
    if (auto *RD = T->getAsRecordDecl())
        if (RD->isUnion())
            return RD;
    return nullptr;
}
```

### isCharAccess

```cpp
bool isCharAccess(MemberExpr *ME) {
    QualType memberType = ME->getMemberDecl()->getType().getCanonicalType();
    if (memberType->isCharType())
        return true;
    if (auto *AT = dyn_cast<ArrayType>(memberType))
        if (AT->getElementType()->isCharType())
            return true;
    // Also check for unsigned char and std::byte
    return false;
}
```

### isCommonInitialSequence

```cpp
bool isCommonInitialSequence(const RecordDecl *Union,
                              StringRef Member1, StringRef Member2) {
    // Find both members
    const FieldDecl *F1 = nullptr, *F2 = nullptr;
    for (auto *Field : Union->fields()) {
        if (Field->getName() == Member1) F1 = Field;
        if (Field->getName() == Member2) F2 = Field;
    }
    if (!F1 || !F2) return false;

    // Check if both are structs with common initial sequence
    auto *R1 = F1->getType()->getAsRecordDecl();
    auto *R2 = F2->getType()->getAsRecordDecl();
    if (!R1 || !R2) return false;

    // Compare field types in order
    auto I1 = R1->field_begin(), I2 = R2->field_begin();
    while (I1 != R1->field_end() && I2 != R2->field_end()) {
        if (!Ctx.hasSameType(I1->getType(), I2->getType()))
            break;
        ++I1; ++I2;
    }
    // At least one common field means common initial sequence exists
    return I1 != R1->field_begin();
}
```

### isLValueContext

Determine if MemberExpr is being written to (LHS of assignment):

```cpp
bool isLValueContext(MemberExpr *ME) {
    // Walk up parents to find if this is LHS of assignment
    // This requires parent map or context tracking
    // Simplified: check if parent is BinaryOperator with this as LHS
    // Full implementation needs ParentMapContext
}
```

---

## Diagnostics

### Error Messages (C++ Mode)

```
error: reading inactive union member 'f' when 'i' is active
  --> src/parser.cpp:42:15
   |
42 |     float x = u.f;
   |               ^~~
   |
note: union 'u' was last written to member 'i' here
  --> src/parser.cpp:40:5
   |
40 |     u.i = 42;
   |     ^~~~~~~~
```

### Warning Messages (C Mode)

```
warning: reading inactive union member 'f' when 'i' is active (type punning)
  --> src/parser.c:42:15
   |
42 |     float x = u.f;
   |               ^~~
   |
note: this is implementation-defined behavior in C
note: union 'u' was last written to member 'i' here
  --> src/parser.c:40:5
   |
40 |     u.i = 42;
   |     ^~~~~~~~
```

### Diagnostic IDs

```cpp
// In DTTPlugin.cpp diagnostic setup
unsigned DiagUnionInactiveMemberError = DE.getCustomDiagID(
    DiagnosticsEngine::Error,
    "reading inactive union member '%0' when '%1' is active");

unsigned DiagUnionInactiveMemberWarning = DE.getCustomDiagID(
    DiagnosticsEngine::Warning,
    "reading inactive union member '%0' when '%1' is active (type punning)");

unsigned DiagUnionInactiveMemberNote = DE.getCustomDiagID(
    DiagnosticsEngine::Note,
    "union was last written to member '%0' here");
```

---

## Implementation Phases

### Phase 1: Core Tracking (P0)

**Goal:** Basic active member tracking and violation detection

1. Add `_union_active` property infrastructure
2. Track active member on declaration with initializer
3. Track active member on member assignment
4. Check active member on member read
5. Emit error (C++) or warning (C) on inactive read
6. Handle uninitialized unions (first member)

**Tests:**
- `840_union_active_pass.cpp` - Correct access patterns
- `841_union_active_fail.cpp` - Inactive member read (C++ mode)
- `842_union_active_warn.c` - Inactive member read (C mode, warning)

**Estimated scope:** ~150 lines of code

### Phase 2: Enhanced Tracking (P1)

**Goal:** Cover more initialization and copy patterns

7. Designated initializer tracking (`{.member = val}`)
8. Copy/move assignment propagation
9. Anonymous union support
10. Compound literal unions

**Tests:**
- `843_union_designated_pass.cpp`
- `844_union_copy_pass.cpp`
- `845_union_anonymous_pass.cpp`

**Estimated scope:** ~100 lines of code

### Phase 3: Pointer Aliasing (P2)

**Goal:** Track writes through pointers to union members

11. Track pointer-to-member provenance
12. Update active member on pointer dereference write
13. Handle address-of union member

**Tests:**
- `846_union_pointer_pass.cpp`
- `847_union_pointer_fail.cpp`

**Estimated scope:** ~150 lines of code

### Phase 4: Guard Extraction (P2)

**Goal:** Extract proofs from tagged union patterns

14. Detect tag field comparisons
15. Associate tag values with union members
16. Create proofs for guarded access

**Tests:**
- `848_union_tagged_pass.cpp`
- `849_union_tagged_fail.cpp`

**Estimated scope:** ~200 lines of code (complex)

---

## Test Cases

### 840_union_active_pass.cpp (C++ Mode - Safe Patterns)

```cpp
// Basic correct access
union U { int i; float f; };

void test_basic() {
    U u;
    u.i = 42;
    int x = u.i;  // OK - reading active member

    u.f = 3.14f;
    float y = u.f;  // OK - reading newly active member
}

// Char access always allowed
void test_char_access() {
    U u;
    u.i = 0x12345678;
    char *bytes = reinterpret_cast<char*>(&u);
    char c = bytes[0];  // OK - char access allowed
}

// Taking address of inactive member is OK
void test_address() {
    U u;
    u.i = 42;
    float *pf = &u.f;  // OK - just taking address
    // *pf = 3.14f;    // Would activate u.f
}

// Common initial sequence
union V {
    struct { int tag; int data; } a;
    struct { int tag; float data; } b;
};

void test_common_initial() {
    V v;
    v.a = {1, 42};
    int tag = v.b.tag;  // OK - common initial sequence
}
```

### 841_union_active_fail.cpp (C++ Mode - Unsafe Patterns)

```cpp
union U { int i; float f; };

void test_inactive_read() {
    U u;
    u.i = 42;
    float x = u.f;  // ERROR: reading inactive member 'f' when 'i' is active
}

void test_uninitialized_wrong_member() {
    U u;            // First member 'i' is active (indeterminate)
    float x = u.f;  // ERROR: reading inactive member 'f' when 'i' is active
}

void test_after_reassign() {
    U u;
    u.f = 3.14f;    // 'f' is now active
    u.i = 42;       // 'i' is now active
    float x = u.f;  // ERROR: reading inactive member 'f' when 'i' is active
}
```

### 842_union_active_warn.c (C Mode - Warnings)

```c
union U { int i; float f; };

void test_type_punning(void) {
    union U u;
    u.i = 0x40490FDB;
    float pi = u.f;  // WARNING: type punning (implementation-defined in C)
    // This is a common pattern to interpret int bits as float
}
```

### 843_union_designated_pass.cpp

```cpp
union U { int i; float f; char c; };

void test_designated() {
    U u1 = {.f = 3.14f};
    float x = u1.f;  // OK - 'f' is active via designated init

    U u2 = {.i = 42};
    int y = u2.i;    // OK - 'i' is active via designated init
}
```

### 846_union_pointer_pass.cpp

```cpp
union U { int i; float f; };

void test_pointer_write() {
    U u;
    int *p = &u.i;
    *p = 42;         // Activates 'i'
    int x = u.i;     // OK - 'i' is active
}

void test_pointer_activate() {
    U u;
    u.f = 3.14f;     // 'f' is active
    int *p = &u.i;
    *p = 42;         // Now 'i' is active
    int x = u.i;     // OK - 'i' is active
}
```

### 847_union_pointer_fail.cpp

```cpp
union U { int i; float f; };

void test_pointer_wrong_read() {
    U u;
    int *p = &u.i;
    *p = 42;         // Activates 'i'
    float x = u.f;   // ERROR: reading inactive 'f' when 'i' is active
}
```

### 848_union_tagged_pass.cpp

```cpp
enum Tag { TAG_INT, TAG_FLOAT };

struct Tagged {
    Tag tag;
    union { int i; float f; } data;
};

void test_tagged(Tagged *t) {
    if (t->tag == TAG_INT) {
        int x = t->data.i;  // OK - guarded by tag check
    } else if (t->tag == TAG_FLOAT) {
        float y = t->data.f;  // OK - guarded by tag check
    }
}

void test_switch(Tagged *t) {
    switch (t->tag) {
    case TAG_INT:
        int x = t->data.i;  // OK - guarded by switch case
        break;
    case TAG_FLOAT:
        float y = t->data.f;  // OK - guarded by switch case
        break;
    }
}
```

---

## Edge Cases

### Anonymous Unions in Structs

```cpp
struct S {
    int tag;
    union {          // Anonymous union
        int i;
        float f;
    };
};

void test(S *s) {
    s->i = 42;       // Track: s._union_active = "i"
    int x = s->i;    // OK
    float y = s->f;  // ERROR
}
```

**Handling:** Anonymous unions don't have a named path component. Track as `s._anon_union_0._union_active` or flatten to `s._union_active`.

### Nested Unions

```cpp
union Outer {
    union { int i; float f; } inner;
    double d;
};
```

**Handling:** Track outer union only. Inner union access implies outer is accessing `inner` member.

### Union of Arrays

```cpp
union U {
    int arr_i[4];
    float arr_f[4];
};

void test() {
    U u;
    u.arr_i[0] = 42;  // Activates entire 'arr_i'
    int x = u.arr_i[1];  // OK - arr_i is active
    float y = u.arr_f[0];  // ERROR - arr_f is inactive
}
```

**Handling:** Member name is `arr_i` or `arr_f`, regardless of index.

### Placement New in Union (C++)

```cpp
union U {
    int i;
    std::string s;  // Non-trivial
};

void test() {
    U u;
    new (&u.s) std::string("hello");  // Activates 's'
    u.s.size();  // OK
    // Must explicitly destroy: u.s.~basic_string();
}
```

**Handling:** Detect placement new, update `_union_active` accordingly.

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| False positives on valid type punning (C) | User frustration | Warning not error in C mode |
| Missing pointer aliasing | False negatives | Phase 3 addresses this |
| Complex control flow | Imprecise tracking | Conservative: allow if unknown |
| Anonymous unions | Tracking complexity | Flatten to parent struct |
| Performance overhead | Slow compilation | Only track union types |
| memcpy/memset | Can't determine member | Mark as unknown (allow all) |

---

## Success Criteria

1. **Zero false positives** on correct C++ union access
2. **Warnings** on C type punning (configurable)
3. **Catch** common CVE patterns (inactive member read leading to type confusion)
4. **Performance** < 5% overhead (unions are relatively rare)
5. **Cross-platform** works on libstdc++, libc++, MSVC

---

## Future Extensions

- `std::expected<T, E>` tracking (C++23) - similar to variant
- User-defined discriminated unions via annotation
- Tag-to-member mapping via configuration
- Integration with Clang Static Analyzer's union handling
- Cross-function union state tracking via annotations

---

## Appendix: Related Work

### Clang Static Analyzer

CSA has some union handling but focuses on uninitialized values, not active member tracking.

### GCC -Wstrict-aliasing

Warns about type punning but not specific to unions.

### COMFUSION (USENIX 2023)

Binary-level union type confusion detection for Windows COM. DTT operates at source level with more precision.

### type++ (NDSS 2025)

Runtime type tracking for C++. DTT is compile-time static analysis.
