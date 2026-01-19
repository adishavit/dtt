// DTTTypes.h - Shared type definitions for DTT plugin
//
// Types used by both the main plugin and stdlib annotations

#ifndef DTT_TYPES_H
#define DTT_TYPES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include <climits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dtt {

// ============================================================
// PROOF OPERATIONS
// ============================================================

enum class ProofOp { GT, GE, LT, LE, EQ, NE };

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

struct ConstraintExpr;
using ConstraintExprPtr = std::shared_ptr<ConstraintExpr>;

// ============================================================
// SYMBOLIC VALUES (simplified for stdlib annotations)
// ============================================================

struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR, BITAND, MOD } kind;
    int64_t constVal = 0;
    std::string varRef;
    std::shared_ptr<SymValue> left, right;

    static SymValue Const(int64_t v) { SymValue s; s.kind = CONST; s.constVal = v; return s; }
    static SymValue Unknown() { SymValue s; s.kind = UNKNOWN; return s; }
    static SymValue Var(const std::string& ref) { SymValue s; s.kind = VAR; s.varRef = ref; return s; }

    static SymValue Add(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal + b.constVal);
        SymValue s; s.kind = ADD;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Sub(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal - b.constVal);
        SymValue s; s.kind = SUB;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    std::optional<int64_t> eval() const {
        switch (kind) {
            case CONST: return constVal;
            case ADD: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l + *r; } return std::nullopt;
            case SUB: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l - *r; } return std::nullopt;
            default: return std::nullopt;
        }
    }

    std::string str() const {
        switch (kind) {
            case CONST: return std::to_string(constVal);
            case UNKNOWN: return "?";
            case VAR: return varRef;
            case ADD: return "(" + left->str() + "+" + right->str() + ")";
            case SUB: return "(" + left->str() + "-" + right->str() + ")";
            default: return "?";
        }
    }

    SymValue substitute(const std::map<std::string, SymValue>& env) const {
        if (kind == VAR) { auto it = env.find(varRef); return it != env.end() ? it->second : *this; }
        if (kind == ADD) return Add(left->substitute(env), right->substitute(env));
        if (kind == SUB) return Sub(left->substitute(env), right->substitute(env));
        return *this;
    }
};

// ============================================================
// CONSTRAINT EXPRESSION AST
// ============================================================

struct ConstraintExpr {
    enum Kind {
        // Values
        Literal,        // Constant integer: 0, 1, 42
        FieldRef,       // Reference to a field: _size, _nonnull
        ArgRef,         // Reference to an argument: arg0, arg1
        ArgFieldRef,    // Reference to argument's field: arg0._size
        OldRef,         // Old value: old(_size)
        ResultRef,      // Return value: _result

        // Arithmetic operations
        Add, Sub, Mul, Div, Mod, BitAnd,

        // Comparison operations (return bool)
        CmpGT, CmpGE, CmpLT, CmpLE, CmpEQ, CmpNE,

        // Logical operations
        LogicalAnd, LogicalOr, LogicalNot
    };

    Kind kind;

    ConstraintExpr(Kind k) : kind(k) {}
    virtual ~ConstraintExpr() = default;

    virtual std::string toString() const = 0;

    bool isComparison() const {
        return kind >= CmpGT && kind <= CmpNE;
    }

    virtual ConstraintExprPtr clone() const = 0;
};

// Literal constant
struct LiteralExpr : ConstraintExpr {
    int64_t value;

    LiteralExpr(int64_t v) : ConstraintExpr(Literal), value(v) {}

    std::string toString() const override {
        return std::to_string(value);
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<LiteralExpr>(value);
    }
};

// Field reference: _size, _nonnull, etc.
struct FieldRefExpr : ConstraintExpr {
    std::string fieldName;

    FieldRefExpr(const std::string& name) : ConstraintExpr(FieldRef), fieldName(name) {}

    std::string toString() const override {
        return fieldName;
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<FieldRefExpr>(fieldName);
    }
};

// Argument reference: arg0, arg1, etc.
struct ArgRefExpr : ConstraintExpr {
    unsigned argIndex;

    ArgRefExpr(unsigned idx) : ConstraintExpr(ArgRef), argIndex(idx) {}

    std::string toString() const override {
        return "arg" + std::to_string(argIndex);
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<ArgRefExpr>(argIndex);
    }
};

// Binary expression: comparisons and arithmetic
struct BinaryExpr : ConstraintExpr {
    ConstraintExprPtr left, right;

    BinaryExpr(Kind k, ConstraintExprPtr l, ConstraintExprPtr r)
        : ConstraintExpr(k), left(l), right(r) {}

    std::string toString() const override {
        std::string opStr;
        switch (kind) {
            case CmpLT: opStr = " < "; break;
            case CmpLE: opStr = " <= "; break;
            case CmpGT: opStr = " > "; break;
            case CmpGE: opStr = " >= "; break;
            case CmpEQ: opStr = " == "; break;
            case CmpNE: opStr = " != "; break;
            case Add: opStr = " + "; break;
            case Sub: opStr = " - "; break;
            default: opStr = " ? "; break;
        }
        return left->toString() + opStr + right->toString();
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<BinaryExpr>(kind, left->clone(), right->clone());
    }
};

// ============================================================
// POSTCONDITIONS
// ============================================================

struct Postcondition {
    std::string resultField;
    SymValue expr;
    bool usesOld = false;
};

struct BoolPostcondition {
    std::string argRef;
    std::string field;
    ProofOp op;
    int64_t rhs;
    bool onTrue;
};

// ============================================================
// METHOD ANNOTATION KEY AND INFO
// ============================================================

struct MethodKey {
    std::string className;
    std::string methodName;
    unsigned numParams;

    bool operator==(const MethodKey& other) const {
        return numParams == other.numParams &&
               className == other.className &&
               methodName == other.methodName;
    }
};

struct MethodInfo {
    std::vector<ConstraintExprPtr> preconditions;
    std::vector<Postcondition> ensures;
    std::string returns;
    std::vector<BoolPostcondition> boolPostconds;
};

// ============================================================
// INFERRED PRECONDITIONS (for backward inference)
// ============================================================

enum class PreconditionKind { NonNull, SizeGE };

struct InferredPrecondition {
    std::string path;                        // e.g., "arg0", "arg0->a->b"
    PreconditionKind kind;
    std::string boundStr;                    // For SizeGE: string repr of minimum required size
    std::optional<unsigned> argIndex;        // Which parameter the buffer refers to
    std::optional<unsigned> boundArgIndex;   // Which parameter the bound refers to
    int64_t boundConstant = 0;               // Constant part of bound (for expressions like n+10)

    std::string toString() const {
        switch (kind) {
        case PreconditionKind::NonNull:
            return path + " != null";
        case PreconditionKind::SizeGE:
            return path + "._size_bytes >= " + boundStr;
        }
        return path;
    }
};

} // namespace dtt

// DenseMap support for MethodKey
namespace llvm {
template<> struct DenseMapInfo<dtt::MethodKey> {
    static inline dtt::MethodKey getEmptyKey() {
        return {"", "", UINT_MAX};
    }
    static inline dtt::MethodKey getTombstoneKey() {
        return {"", "", UINT_MAX - 1};
    }
    static unsigned getHashValue(const dtt::MethodKey& k) {
        return llvm::hash_combine(k.className, k.methodName, k.numParams);
    }
    static bool isEqual(const dtt::MethodKey& lhs, const dtt::MethodKey& rhs) {
        return lhs == rhs;
    }
};
} // namespace llvm

#endif // DTT_TYPES_H
