# Value Concepts Implementation Guide

**Status:** Implementation sketch for TDD
**Tests:** `examples/1000_*.cpp` - `examples/1006_*.cpp`

This document provides the implementation blueprint for Value Concepts,
following Chris Lattner's design principles: clean, minimal, composable.

## 1. AST Extension

Add to `ConstraintExpr::Kind` enum in `DTTPlugin.cpp:1211`:

```cpp
enum Kind {
    // ... existing kinds ...

    // Quantifiers (new)
    Forall,         // forall(i, lo, hi, body)
    Exists,         // exists(i, lo, hi, body)

    // Aggregates (future)
    Sum, Count, Min, Max
};
```

Add new AST node after `UnaryExpr`:

```cpp
// Bounded universal quantifier: forall(var, lo, hi, body)
// Semantics: (lo >= hi) ∨ ∀i ∈ [lo,hi). body(i)
struct ForallExpr : ConstraintExpr {
    std::string boundVar;       // "i"
    ConstraintExprPtr lo;       // Lower bound (inclusive)
    ConstraintExprPtr hi;       // Upper bound (exclusive)
    ConstraintExprPtr body;     // Predicate over boundVar

    ForallExpr(std::string var, ConstraintExprPtr l, ConstraintExprPtr h,
               ConstraintExprPtr b)
        : ConstraintExpr(Forall), boundVar(std::move(var)),
          lo(std::move(l)), hi(std::move(h)), body(std::move(b)) {}

    std::string toString() const override {
        return "forall(" + boundVar + ", " + lo->toString() + ", " +
               hi->toString() + ", " + body->toString() + ")";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<ForallExpr>(
            boundVar, lo->clone(), hi->clone(), body->clone());
    }

    // Quantifiers don't produce scalar values
    std::optional<SymValue> toSymValue() const override {
        return std::nullopt;
    }
};

// Bounded existential quantifier: exists(var, lo, hi, body)
// Semantics: (lo < hi) ∧ ∃i ∈ [lo,hi). body(i)
struct ExistsExpr : ConstraintExpr {
    std::string boundVar;
    ConstraintExprPtr lo;
    ConstraintExprPtr hi;
    ConstraintExprPtr body;

    ExistsExpr(std::string var, ConstraintExprPtr l, ConstraintExprPtr h,
               ConstraintExprPtr b)
        : ConstraintExpr(Exists), boundVar(std::move(var)),
          lo(std::move(l)), hi(std::move(h)), body(std::move(b)) {}

    std::string toString() const override {
        return "exists(" + boundVar + ", " + lo->toString() + ", " +
               hi->toString() + ", " + body->toString() + ")";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<ExistsExpr>(
            boundVar, lo->clone(), hi->clone(), body->clone());
    }
};

// Array index expression: arr[i]
// Used inside quantifier bodies
struct IndexExpr : ConstraintExpr {
    std::string arrayName;      // "arg0" or field path
    ConstraintExprPtr index;    // Index expression (often just boundVar)

    IndexExpr(std::string arr, ConstraintExprPtr idx)
        : ConstraintExpr(/* new kind */ ArrayIndex),
          arrayName(std::move(arr)), index(std::move(idx)) {}

    std::string toString() const override {
        return arrayName + "[" + index->toString() + "]";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<IndexExpr>(arrayName, index->clone());
    }
};
```

## 2. Parser Extension

Add to `ConstraintExprParser` class around line 1643:

```cpp
// Parse quantifier: forall(var, lo, hi, body) or exists(var, lo, hi, body)
ConstraintExprPtr parseQuantifier() {
    if (match("forall(") || match("exists(")) {
        bool isForall = input[pos - 7] == 'f';  // Check which we matched

        // Parse bound variable
        std::string var = parseIdentifier();
        if (var.empty()) {
            error("expected variable name in quantifier");
            return nullptr;
        }
        expect(',');

        // Parse lower bound
        auto lo = parseExpr();
        expect(',');

        // Parse upper bound
        auto hi = parseExpr();
        expect(',');

        // Parse body (with bound variable in scope)
        boundVars.insert(var);  // Add to scope
        auto body = parseLogicalOr();
        boundVars.erase(var);   // Remove from scope

        expect(')');

        if (isForall) {
            return std::make_shared<ForallExpr>(var, lo, hi, body);
        } else {
            return std::make_shared<ExistsExpr>(var, lo, hi, body);
        }
    }
    return nullptr;
}

// Update parseAtomInternal to try quantifiers first:
ConstraintExprPtr parseAtomInternal() {
    skipWhitespace();

    // Try quantifiers first
    if (auto q = parseQuantifier()) return q;

    // ... rest of existing parseAtomInternal ...
}

// Parse array index: arr[expr]
// Add to parsePostfix or parseAtomInternal:
if (peek() == '[') {
    advance();  // consume '['
    auto index = parseExpr();
    expect(']');
    return std::make_shared<IndexExpr>(name, index);
}
```

## 3. Z3 Translation

Add to `constraintExprToZ3` around line 3617:

```cpp
case ConstraintExpr::Forall: {
    auto* f = static_cast<const ForallExpr*>(expr.get());

    // Create bound variable
    z3::expr boundVar = ctx.int_const(f->boundVar.c_str());

    // Add to variable map for body translation
    auto oldVars = vars;
    vars[f->boundVar] = boundVar;

    // Translate bounds and body
    z3::expr lo = constraintExprToZ3(f->lo, ctx, vars, basePath);
    z3::expr hi = constraintExprToZ3(f->hi, ctx, vars, basePath);
    z3::expr body = constraintExprToZ3(f->body, ctx, vars, basePath);

    // Restore variable map
    vars = oldVars;

    // Build: forall i. (lo <= i < hi) => body
    z3::expr range = (boundVar >= lo) && (boundVar < hi);
    return z3::forall(boundVar, z3::implies(range, body));
}

case ConstraintExpr::Exists: {
    auto* e = static_cast<const ExistsExpr*>(expr.get());

    z3::expr boundVar = ctx.int_const(e->boundVar.c_str());
    auto oldVars = vars;
    vars[e->boundVar] = boundVar;

    z3::expr lo = constraintExprToZ3(e->lo, ctx, vars, basePath);
    z3::expr hi = constraintExprToZ3(e->hi, ctx, vars, basePath);
    z3::expr body = constraintExprToZ3(e->body, ctx, vars, basePath);

    vars = oldVars;

    // Build: exists i. (lo <= i < hi) && body
    z3::expr range = (boundVar >= lo) && (boundVar < hi);
    return z3::exists(boundVar, range && body);
}

case ConstraintExpr::ArrayIndex: {
    auto* idx = static_cast<const IndexExpr*>(expr.get());

    // Get array as Z3 array sort
    z3::expr arr = getOrCreateArray(ctx, vars, idx->arrayName, basePath);
    z3::expr index = constraintExprToZ3(idx->index, ctx, vars, basePath);

    return z3::select(arr, index);
}
```

## 4. Predefined Concept Registry

Add new file or section in `DTTStdLib.cpp`:

```cpp
// Predefined value concepts
// Format: name := expansion
static const std::vector<std::pair<std::string, std::string>> PredefinedConcepts = {
    // Container state
    {"nonempty",    "_size > 0"},
    {"empty",       "_size == 0"},
    {"full",        "_size == _capacity"},
    {"has_space",   "_size < _capacity"},

    // Pointer state
    {"nonnull",     "_nonnull == 1"},
    {"valid",       "_lifetime == 1"},
    {"freed",       "_lifetime == 0"},

    // Ordering (require forall)
    {"sorted",      "forall(i, 0, _size - 1, _data[i] <= _data[i + 1])"},
    {"unique",      "forall(i, 0, _size, forall(j, i + 1, _size, _data[i] != _data[j]))"},

    // Element properties
    {"all_nonnull", "forall(i, 0, _size, _data[i] != 0)"},
    {"all_positive", "forall(i, 0, _size, _data[i] > 0)"},
};

// Parameterized concepts
// These are templates expanded at use site
static const std::vector<std::tuple<std::string, std::vector<std::string>, std::string>>
ParameterizedConcepts = {
    {"all_in_range",    {"lo", "hi"},   "forall(i, 0, _size, lo <= _data[i] && _data[i] < hi)"},
    {"all_less_than",   {"bound"},      "forall(i, 0, _size, _data[i] < bound)"},
    {"size_eq",         {"n"},          "_size == n"},
    {"size_ge",         {"n"},          "_size >= n"},
    {"valid_index",     {"idx"},        "idx >= 0 && idx < _size"},
};

// Expand concept in constraint string
std::string expandConcept(const std::string& constraint) {
    // Simple concepts: just string replace
    std::string result = constraint;
    for (const auto& [name, expansion] : PredefinedConcepts) {
        // Replace whole-word occurrences
        result = replaceWholeWord(result, name, "(" + expansion + ")");
    }

    // Parameterized concepts: parse and substitute
    for (const auto& [name, params, expansion] : ParameterizedConcepts) {
        // Match: name(arg1, arg2, ...)
        // Replace params with actual args in expansion
        result = expandParameterized(result, name, params, expansion);
    }

    return result;
}
```

## 5. User-Defined Concepts

Parse `dtt:define=name := expr` annotations:

```cpp
// In annotation processing:
if (startsWith(annotation, "dtt:define=")) {
    std::string def = annotation.substr(strlen("dtt:define="));
    size_t colonPos = def.find(":=");
    if (colonPos != std::string::npos) {
        std::string name = trim(def.substr(0, colonPos));
        std::string expr = trim(def.substr(colonPos + 2));

        // Register in user concept map
        UserDefinedConcepts[name] = expr;
    }
}
```

## 6. Test Files

| File | Purpose | Expected Behavior |
|------|---------|-------------------|
| `1000_forall_basic_xfail.cpp` | Bounded forall | Should fail: unsafe code not caught |
| `1001_forall_basic_pass.cpp` | Bounded forall (safe) | Should pass when implemented |
| `1002_concept_nonempty_xfail.cpp` | nonempty concept | Should fail: empty vec access |
| `1003_concept_nonempty_pass.cpp` | nonempty concept (safe) | Should pass when implemented |
| `1004_concept_sorted_xfail.cpp` | sorted concept | Should fail: unsorted vec |
| `1005_concept_sorted_pass.cpp` | sorted concept (safe) | Should pass when implemented |
| `1006_concept_define_xfail.cpp` | User-defined concepts | Should fail: composite check |

## 7. TDD Checklist

- [x] Write failing tests (xfail)
- [x] Write passing tests
- [x] Document expected behavior
- [ ] Implement ForallExpr AST node
- [ ] Implement parser extension
- [ ] Implement Z3 translation
- [ ] Implement concept registry
- [ ] Run tests: xfail should become fail
- [ ] Rename xfail to fail
- [ ] Run all tests
- [ ] Commit

## 8. Design Decisions (Lattner-style)

1. **Minimal AST**: Only `ForallExpr` and `ExistsExpr`, no intermediate representations
2. **Reuse existing infrastructure**: Same `ConstraintExprPtr` pattern, same Z3 pipeline
3. **Bounded quantifiers only**: Decidable fragment, no unbounded quantifiers
4. **String-based concept expansion**: Simple, debuggable, matches existing annotation style
5. **No runtime overhead**: All checking at compile time, as always
