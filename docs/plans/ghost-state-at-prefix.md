# Ghost State: `@` Prefix for Compile-Time-Only Properties

## Status: IMPLEMENTED ✓

**Implementation History:**
1. Initial test with basic `ensures` passed — appeared to work out of the box
2. Deep review with `ensures_if_true` revealed 5 bugs
3. TDD cycle: write test → confirm fail → fix → confirm pass
4. All tests now pass

**Code changes required:** 5 locations in DTTPlugin.cpp (see below)

## Summary

**Ghost state** — tracked properties that exist only in the compiler's symbolic analysis, not as runtime fields. These properties use an `@` prefix (e.g., `@encrypted`, `@validated`) which is syntactically invalid in C++ identifiers, making them impossible to declare or tamper with in user code.

## Motivation

### The Problem with `_` Prefix Properties

Current DTT examples use underscore-prefixed fields like `_encrypted`:

```cpp
struct SecureData {
    bool _encrypted = false;    // Runtime field
    size_t _key_id = 0;         // Runtime field

    [[clang::annotate("dtt:ensures=_encrypted == 1")]]
    void encrypt(Key& key);
};
```

**Issues:**
1. **Tamperable** — User code can write `obj._encrypted = true;` to bypass the state machine
2. **Memory overhead** — Fields consume runtime memory even though they exist purely for verification
3. **Mixed concerns** — Verification metadata is tangled with actual data

### The Solution: Ghost State

Properties prefixed with `@` exist only in the compiler's symbolic tracking:

```cpp
struct SecureData {
    std::vector<char> payload;  // Only real data

    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt(Key& key);

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send();
};
```

**Benefits:**
1. **Unforgeable** — `obj.@encrypted = true;` is a syntax error (@ invalid in C++ identifiers)
2. **Zero memory** — No runtime storage
3. **Pure specification** — Clean separation between data and verification metadata

## Design Decision: Why `@`?

| Character | Pros | Cons |
|-----------|------|------|
| **`@`** | Familiar (Java/C# annotations), clean syntax | None |
| `$` | Common in shells | Some compilers accept as extension (non-standard but allowed) |
| `#` | Clearly metadata | Looks like preprocessor directive |
| `%` | Unusual | Looks like printf format specifier |

**Decision:** Use `@` — it's intuitive, unambiguous, and guaranteed invalid in all C++ standards.

## How It Works

DTT already tracks properties symbolically based on annotations. The actual mechanism doesn't require fields to exist at runtime — it just needs consistent property names across annotations.

### Current Flow (with `_` prefix)
1. Parser sees `dtt:ensures=_size == 0`
2. DTT tracks `_size` symbolically for the object
3. When `_size` is referenced in `requires`, DTT checks against tracked value
4. The `_size` field MAY exist at runtime (and can be tampered with)

### New Flow (with `@` prefix)
1. Parser sees `dtt:ensures=@encrypted == 1`
2. DTT tracks `@encrypted` symbolically for the object
3. When `@encrypted` is referenced in `requires`, DTT checks against tracked value
4. The `@encrypted` field CANNOT exist at runtime (@ is invalid syntax)

**Key insight:** The implementation change is minimal — just extend the parser to accept `@` as a valid property prefix alongside `_`.

## Implementation Plan

### Prerequisites

Follow TDD workflow from CLAUDE.md:
1. Write test FIRST
2. Run test — confirm FAILS
3. Implement feature
4. Run test — confirm PASSES

### Step 1: Write Tests

**`examples/493_ghost_state_pass.cpp`** — Correct usage:
```cpp
// Ghost state: @encrypted exists only in compiler tracking
struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt();

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send();
};

int main() {
    SecureData s;
    s.encrypt();  // Sets @encrypted = 1
    s.send();     // OK: @encrypted == 1
}
```

**`examples/494_ghost_state_fail.cpp`** — Violation detected:
```cpp
struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt();

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send();
};

int main() {
    SecureData s;
    s.send();     // ERROR: @encrypted is 0 (uninitialized), requires 1
}
```

**`examples/496_ghost_state_multi_pass.cpp`** — Multiple ghost properties:
```cpp
struct Channel {
    [[clang::annotate("dtt:ensures=@state == 1")]]
    void connect();

    [[clang::annotate("dtt:requires=@state == 1"),
      clang::annotate("dtt:ensures=@state == 2")]]
    void authenticate();

    [[clang::annotate("dtt:requires=@state == 2")]]
    void send(const char* data);
};

int main() {
    Channel ch;
    ch.connect();
    ch.authenticate();
    ch.send("hello");  // OK: @state == 2
}
```

**`examples/499_ghost_state_key_binding_fail.cpp`** — Key ID tracking:
```cpp
struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 1"),
      clang::annotate("dtt:ensures=@key_id == arg0")]]
    void encrypt(size_t key_id);

    [[clang::annotate("dtt:requires=@encrypted == 1"),
      clang::annotate("dtt:requires=@key_id == arg0")]]
    void decrypt(size_t key_id);
};

int main() {
    SecureData s;
    s.encrypt(1);
    s.decrypt(2);  // ERROR: @key_id is 1, not 2
}
```

### Step 2: Bugs Found via TDD

Initial testing with basic `ensures` worked. But `ensures_if_true` test **failed**:
```
error: DTT: SecureData::send requires @encrypted == 1, but s.@encrypted = ?
```

Root cause: Multiple locations prepended `_` to property names, turning `@encrypted` into `_@encrypted`.

### Step 3: Fixes Applied (5 locations)

| Line | Function | Bug | Fix |
|------|----------|-----|-----|
| 612 | `parseSimpleConstraint` | `arg0 < @prop` not recognized | Added `\|\| rhsStr[0] == '@'` |
| 771 | `parseBoolPostcondition` | `@encrypted` → `_@encrypted` | Added `\|\| lhs[0] == '@'` |
| 1169 | VarBoundProof normalization | Same prepend bug | Added `\|\| fieldName[0] != '@'` |
| 1597 | Expression constraint subst | Same prepend bug | Added @ check in substitution |
| 2054 | Post-call substitution | Same prepend bug | Added @ check in substitution |

**Note:** The plan originally suggested creating an `isPropertyPrefix()` helper, but inline checks were used instead for minimal diff.

### Step 4: Verify All Tests Pass

```bash
cmake --build build
ctest --test-dir build -R ghost_state
ctest --test-dir build -LE xfail  # Full regression
```

## Example: Before and After

### Before (tamperable)
```cpp
struct SecureData {
    bool _encrypted = false;  // Can be modified!

    [[clang::annotate("dtt:ensures=_encrypted == 1")]]
    void encrypt();
};

// Bypass:
SecureData s;
s._encrypted = true;  // Compiles! Bypasses state machine
s.send();             // DTT thinks it's encrypted
```

### After (unforgeable)
```cpp
struct SecureData {
    // No @encrypted field — it's ghost state

    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt();
};

// Cannot bypass:
SecureData s;
s.@encrypted = true;  // SYNTAX ERROR: @ invalid in identifier
```

## Testing Checklist

- [x] `493_ghost_state_pass.cpp` — Basic ghost state works
- [x] `494_ghost_state_fail.cpp` — Violations detected
- [x] `495_ghost_state_bool_postcond_pass.cpp` — `ensures_if_true` with @ prefix
- [x] `496_ghost_state_multi_pass.cpp` — Multiple ghost properties
- [x] `497_ghost_state_multi_fail.cpp` — State machine violation detected
- [x] `498_ghost_state_key_binding_pass.cpp` — `arg0 == @key_id` binding (correct key)
- [x] `499_ghost_state_key_binding_fail.cpp` — `arg0 == @key_id` binding (wrong key)
- [x] `500_ghost_state_mixed_pass.cpp` — `_size` + `@validated` in same struct
- [x] Existing `_` prefix tests still pass
- [x] **Full regression: all tests pass**

## Future Considerations

1. **Error messages** — Consider special messaging for ghost properties:
   ```
   error: DTT: requires @encrypted == 1, but @encrypted is 0 (ghost state)
   ```

2. **Documentation** — Update GUIDE.md and BLOG.md with ghost state examples

3. **Naming convention** — Consider recommending `@` for state-machine properties, `_` for properties that mirror runtime fields (like `_size`)

## References

- CLAUDE.md — TDD workflow requirements
- Similar concepts in other systems:
  - SPARK Ada: `Ghost` aspect
  - Dafny: `ghost` keyword
  - Frama-C: `ghost` variables
  - Liquid Haskell: Refinement types (erased at runtime)
