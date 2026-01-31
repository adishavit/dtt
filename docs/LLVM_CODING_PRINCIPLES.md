# LLVM/Clang Coding Principles

**Read this before writing any code in this codebase.**

This document captures hard-won lessons from Lattner-style code reviews. These principles apply to any LLVM/Clang plugin or tool.

## Core Philosophy

> **Zero allocation in hot paths. Zero copy by default.**

Every `std::string`, `std::vector`, or heap allocation is a code smell in LLVM code. The standard library types are almost never the right choice.

---

## 1. String Handling

### The Hierarchy

| Type | Use Case | Allocates? |
|------|----------|------------|
| `StringRef` | Borrowed/viewed strings | No |
| `SmallString<N>` | Owned strings (small) | No (if ≤N) |
| `Twine` | Building strings for output | No (lazy) |
| `std::string` | **Avoid** | Yes |

### `getName()` vs `getNameAsString()`

```cpp
// WRONG - allocates std::string every call
std::string name = Decl->getNameAsString();
if (name == "foo") ...

// WRONG - crashes on operators, constructors, destructors
if (Decl->getName() == "foo") ...  // Assert failure on operator=

// RIGHT - check for identifier first, then use StringRef
if (Decl->getIdentifier() && Decl->getName() == "foo") ...
```

**Rule:** `getName()` returns `StringRef` (fast) but asserts that the name is a simple identifier. It will crash on:
- Operators (`operator=`, `operator[]`)
- Constructors/destructors
- Conversion functions
- Deduction guides

Always guard with `getIdentifier()` or use `getNameAsString()` if you need to handle all cases.

### String Comparison

```cpp
// WRONG
std::string name = Decl->getNameAsString();
if (name == "vector") ...

// RIGHT
if (Decl->getIdentifier() && Decl->getName() == "vector") ...

// ALSO RIGHT (when you need to handle non-identifiers)
if (Decl->getNameAsString() == "operator=") ...  // Acceptable for rare cases
```

### String Concatenation

```cpp
// WRONG - multiple allocations
std::string key = "_index==" + std::to_string(idx);

// RIGHT - SmallString with append
llvm::SmallString<32> key("_index==");
key += std::to_string(idx);

// ALSO RIGHT - Twine for immediate use (e.g., diagnostics)
Diag << ("expected " + Twine(n) + " arguments").str();
```

### Struct Fields

```cpp
// WRONG
struct Info {
    std::string path;      // Allocates on every construction
    std::string name;
};

// RIGHT
struct Info {
    llvm::SmallString<64> path;   // Stack-allocated for typical sizes
    llvm::SmallString<32> name;
};
```

### Function Parameters

```cpp
// WRONG
void process(const std::string& path) { ... }

// RIGHT
void process(StringRef path) { ... }
```

### Returning Strings

```cpp
// WRONG
std::string getName() { return decl->getNameAsString(); }

// RIGHT - if caller just needs to read it
StringRef getName() { return decl->getName(); }

// RIGHT - if caller needs ownership
void getName(SmallVectorImpl<char>& out) { ... }
```

---

## 2. Container Choices

| Instead of | Use | Why |
|------------|-----|-----|
| `std::vector<T>` | `SmallVector<T, N>` | Stack-allocated for ≤N elements |
| `std::map<K,V>` | `DenseMap<K,V>` | Cache-friendly, no tree overhead |
| `std::set<T>` | `DenseSet<T>` | Cache-friendly |
| `std::unordered_map` | `DenseMap<K,V>` | Better hash table implementation |
| `const std::vector<T>&` | `ArrayRef<T>` | Zero-copy view |
| `const std::string&` | `StringRef` | Zero-copy view |

### SmallVector Sizing

```cpp
// Pick N based on expected common case
SmallVector<Expr*, 4> args;        // Most calls have ≤4 args
SmallVector<char, 128> buffer;     // Most paths ≤128 chars
SmallVector<Decl*, 8> decls;       // Typical block has ≤8 decls
```

### Out Parameters

```cpp
// WRONG - returns allocated vector
std::vector<Decl*> getDecls() { ... }

// RIGHT - caller provides storage
void getDecls(SmallVectorImpl<Decl*>& out) { ... }
```

---

## 3. LLVM Idioms

### Iterating with `enumerate`

```cpp
// WRONG
for (size_t i = 0; i < args.size(); ++i) {
    process(i, args[i]);
}

// RIGHT
for (auto [i, arg] : llvm::enumerate(args)) {
    process(i, arg);
}
```

### String Prefix/Suffix Checking

```cpp
// WRONG
if (name.find("std::") == 0) ...
if (name.rfind("_ptr", name.size()-4) != std::string::npos) ...

// RIGHT
if (name.starts_with("std::")) ...
if (name.ends_with("_ptr")) ...
```

### Casting

```cpp
// WRONG
if (dynamic_cast<VarDecl*>(D)) ...

// RIGHT
if (auto *VD = dyn_cast<VarDecl>(D)) {
    // Use VD here
}

// For known-safe casts
auto *VD = cast<VarDecl>(D);  // Asserts if wrong type
```

### Null Checking with `dyn_cast`

```cpp
// WRONG
Decl *D = getDecl();
if (D && isa<VarDecl>(D)) {
    auto *VD = cast<VarDecl>(D);
    ...
}

// RIGHT
if (auto *VD = dyn_cast_or_null<VarDecl>(getDecl())) {
    ...
}
```

---

## 4. Common Pitfalls

### Pitfall: Forgetting `getIdentifier()` Check

```cpp
// This WILL crash on operator overloads
StringRef name = Method->getName();  // BOOM on operator=

// Safe version
if (Method->getIdentifier()) {
    StringRef name = Method->getName();
    ...
}
```

### Pitfall: Using `.str()` Unnecessarily

```cpp
// WRONG - converts StringRef to std::string
std::string name = TD->getName().str();
if (name == "variant") ...

// RIGHT - use StringRef directly
StringRef name = TD->getName();
if (name == "variant") ...
```

### Pitfall: String Concatenation in Loops

```cpp
// WRONG - O(n²) allocations
std::string result;
for (auto& item : items) {
    result += item.getName().str();
}

// RIGHT
SmallString<256> result;
for (auto& item : items) {
    result += item.getName();
}
```

### Pitfall: Temporary StringRef

```cpp
// WRONG - StringRef points to destroyed temporary
StringRef getName() {
    return (Twine("prefix_") + decl->getName()).str();  // Returns dangling ref!
}

// RIGHT - return owned string
SmallString<64> getName() {
    SmallString<64> result;
    result += "prefix_";
    result += decl->getName();
    return result;
}
```

---

## 5. Quick Reference

### Before Writing Any Function, Ask:

1. **Parameters:** Can I use `StringRef` instead of `const std::string&`?
2. **Parameters:** Can I use `ArrayRef<T>` instead of `const std::vector<T>&`?
3. **Return type:** Am I returning a view (`StringRef`) or owned data (`SmallString`)?
4. **Local variables:** Am I using `SmallVector` and `SmallString`?
5. **Maps/Sets:** Am I using `DenseMap`/`DenseSet`?
6. **getName():** Did I check `getIdentifier()` first?

### Size Guidelines for SmallString/SmallVector

| Content | Suggested Size |
|---------|---------------|
| Variable names | `SmallString<32>` |
| File paths | `SmallString<128>` |
| Error messages | `SmallString<256>` |
| Function arguments | `SmallVector<T, 4>` |
| Statement lists | `SmallVector<T, 8>` |
| Generic small collection | `SmallVector<T, 4>` |

---

## 6. The Lattner Test

Before submitting code, ask: **"Would this survive a Lattner review?"**

Red flags that will be called out:
- [ ] Any `std::string` in a hot path
- [ ] Any `std::vector` or `std::map`
- [ ] Using `getNameAsString()` when `getName()` would work
- [ ] Using `getName()` without `getIdentifier()` check
- [ ] String concatenation with `+`
- [ ] Returning `std::string` from functions
- [ ] Using `.str()` to convert StringRef unnecessarily

If you check any of these boxes, refactor before requesting review.
