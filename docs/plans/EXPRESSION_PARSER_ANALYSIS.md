# Expression Parser Analysis: Auto-Postcondition Inference

## Current State

The auto-postcondition inference uses **pattern matching** for specific expression forms:

```cpp
// Current patterns recognized:
_size = 0;           // Constant → ensures=_size == 0
_size = n;           // Argument → ensures=_size == arg0
_size = _size + 1;   // Increment → ensures=_size == old(_size) + 1
_size++;             // Unary → ensures=_size == old(_size) + 1
_size += n;          // Compound → ensures=_size == old(_size) + arg0
```

### What Works

| Pattern | Status | Tests |
|---------|--------|-------|
| Constant assignment | ✅ | 1000, 1002 |
| Argument assignment | ✅ | 1001 |
| Increment/decrement expr | ✅ | 1003, 1004 |
| Unary ++/-- | ✅ | 1005 |
| Compound +=/-= | ✅ | 1006 |
| Out-parameters | ✅ | 1007, 1008 |
| Multi-field | ✅ | 1009 |
| Early return | ✅ | 1012 |
| This aliasing | ✅ | 1017 |

### What Has Issues (Tracking Loss → False Positives)

| Pattern | Issue | Impact |
|---------|-------|--------|
| Ternary `? :` | Tracking lost | False positives on safe code |
| Conditionals | Path not tracked | False positives on safe code |
| Chained calls | Intermediate state lost | False positives |
| Complex arithmetic | Not fully parsed | Tracking lost |
| Loops | No analysis | Tracking lost |
| Field-to-field | Not recognized | Tracking lost |
| Return values | Not tracked | False positives |

---

## Unified Expression Parser Proposal

### The Problem with Pattern Matching

Current approach adds a case for each pattern:

```cpp
// Pseudo-code of current approach
if (isConstant(rhs)) {
    postcond = SymValue::Const(getValue(rhs));
} else if (isParameter(rhs)) {
    postcond = SymValue::Var("arg" + getParamIndex(rhs));
} else if (isBinaryOp(rhs, BinaryOperatorKind::BO_Add)) {
    auto lhs = parseExpr(getLHS(rhs));
    auto rhs = parseExpr(getRHS(rhs));
    if (lhs.isOldField() && rhs.isConst()) {
        postcond = SymValue::Add(lhs, rhs);
    }
} else {
    postcond = SymValue::Unknown();  // TRACKING LOST!
}
```

**Problem:** Each unrecognized pattern falls through to `Unknown()`, causing tracking loss.

### The Solution: Recursive Expression Parser

Build a complete symbolic expression tree for ANY expression:

```cpp
SymValue parseExpr(const Expr* E) {
    if (auto* IL = dyn_cast<IntegerLiteral>(E)) {
        return SymValue::Const(IL->getValue().getSExtValue());
    }

    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        if (auto* PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
            return SymValue::Var("arg" + std::to_string(getParamIndex(PVD)));
        }
        if (isThisField(DRE)) {
            return SymValue::Var("old(" + getFieldPath(DRE) + ")");
        }
        return SymValue::Var(DRE->getDecl()->getName());
    }

    if (auto* BO = dyn_cast<BinaryOperator>(E)) {
        auto lhs = parseExpr(BO->getLHS());
        auto rhs = parseExpr(BO->getRHS());
        switch (BO->getOpcode()) {
            case BO_Add: return SymValue::Add(lhs, rhs);
            case BO_Sub: return SymValue::Sub(lhs, rhs);
            case BO_Mul: return SymValue::Mul(lhs, rhs);
            case BO_Div: return SymValue::Div(lhs, rhs);
            case BO_Rem: return SymValue::Mod(lhs, rhs);
            case BO_And: return SymValue::BitAnd(lhs, rhs);
            // ... all operators
        }
    }

    if (auto* CO = dyn_cast<ConditionalOperator>(E)) {
        auto cond = parseExpr(CO->getCond());
        auto thenE = parseExpr(CO->getTrueExpr());
        auto elseE = parseExpr(CO->getFalseExpr());
        return SymValue::Ternary(cond, thenE, elseE);  // NEW!
    }

    if (auto* ME = dyn_cast<MemberExpr>(E)) {
        auto base = parseExpr(ME->getBase());
        return SymValue::Member(base, ME->getMemberDecl()->getName());
    }

    // Recursively handle all expression types...
    return SymValue::Unknown();  // Only truly unparseable exprs
}
```

### Benefits

1. **DRY**: Single parser handles all expressions
2. **Complete**: Never loses tracking for parseable expressions
3. **Extensible**: Add new SymValue kinds, not new pattern cases
4. **Z3-Ready**: Symbolic tree can be directly converted to Z3 expressions

### New SymValue Kinds Needed

```cpp
enum Kind {
    // Existing
    CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR, BITAND, MOD, STRING,

    // New for complete parsing
    TERNARY,      // cond ? a : b
    MEMBER,       // base.field
    DEREF,        // *ptr
    ARRAY_SUB,    // arr[idx]
    CALL,         // fn(args...)
    CAST,         // (type)expr
    COMPARE,      // a < b, a == b, etc.
    LOGICAL,      // a && b, a || b, !a
};
```

### Implementation Phases

**Phase 1: Core Arithmetic (Low Effort)**
- All binary operators (+, -, *, /, %, &, |, ^, <<, >>)
- Unary operators (-, ~, !)
- **Benefit:** Eliminates tracking loss for arithmetic expressions

**Phase 2: Ternary & Comparisons (Medium Effort)**
- Ternary operator with proper symbolic representation
- Comparison operators (produces boolean symbolic value)
- **Benefit:** Path-sensitive postconditions become possible

**Phase 3: Member Access & Pointers (Medium Effort)**
- Nested member access (`a.b.c`)
- Pointer dereference (`*p`)
- Array subscript (`a[i]`)
- **Benefit:** Eliminates false positives for complex access patterns

**Phase 4: Calls & Returns (High Effort)**
- Function call symbolic representation
- Return value tracking
- **Benefit:** Complete interprocedural analysis

---

## Comparison: Pattern Matching vs Expression Parser

| Aspect | Pattern Matching | Expression Parser |
|--------|-----------------|-------------------|
| Lines of Code | ~100 per pattern | ~200 total |
| New Pattern | Add case + tests | Usually automatic |
| Tracking Loss | Common | Rare |
| False Positives | Many | Few |
| Maintenance | High | Low |
| Elegance | ❌ | ✅ |

---

## Recommendation

**Adopt the unified expression parser approach.**

The current system already has `SymValue` with arithmetic operations. Extending it to handle all expressions is a natural evolution that will:

1. Eliminate false positives from tracking loss
2. Enable ternary/conditional postconditions
3. Simplify codebase (fewer special cases)
4. Prepare for full Z3 integration

### Starting Point

The existing `SymValue` in `DTTTypes.h` is a good foundation:

```cpp
struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR, BITAND, MOD, STRING };
    // ... existing implementation
};
```

Add new kinds incrementally as each phase is implemented.

---

## Test Matrix After Expression Parser

| Test | Current | After Parser |
|------|---------|--------------|
| 1010_conditional | FP on OK | Should pass |
| 1011_ternary | FP on OK | Should pass |
| 1013_chained | FP on OK | Should pass |
| 1014_arithmetic | FP on OK | Should pass |
| 1015_nested_field | FP on OK | Should pass |
| 1016_pointer_deref | Null check | Should pass |
| 1018_return_value | FP on OK | Phase 4 |
| 1019_loop | FP on OK | Needs widening |
| 1020_field_to_field | FP on OK | Should pass |

**FP = False Positive**
