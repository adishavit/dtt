// DTTTypes.h - Shared type definitions for DTT plugin
//
// Types used by both the main plugin and stdlib annotations

#ifndef DTT_TYPES_H
#define DTT_TYPES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include <climits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
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
// SYMBOLIC VALUES (for constraint tracking and postconditions)
// ============================================================

struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR, BITAND, MOD, STRING,
                TERNARY, BITOR, BITXOR, SHL, SHR, NEG, BITNOT, WRAP } kind;
    int64_t constVal = 0;  // CONST: the value; WRAP: the width in bits
    bool wrapSigned = false;  // WRAP: into a signed type
    std::string varRef;
    std::string strVal;  // For STRING kind (union active member tracking)
    std::shared_ptr<SymValue> left, right;
    std::shared_ptr<SymValue> cond;  // For TERNARY: cond ? left : right

    static SymValue Const(int64_t v) { SymValue s; s.kind = CONST; s.constVal = v; return s; }
    static SymValue Unknown() { SymValue s; s.kind = UNKNOWN; return s; }
    static SymValue Var(const std::string& ref) { SymValue s; s.kind = VAR; s.varRef = ref; return s; }
    static SymValue String(llvm::StringRef v) { SymValue s; s.kind = STRING; s.strVal = v.str(); return s; }
    bool isString() const { return kind == STRING; }
    const std::string& getStringValue() const { return strVal; }

    // Symbolic values use signed 64-bit arithmetic. Never execute overflowing
    // host arithmetic while analyzing a program: an unrepresentable result is unknown.
    static std::optional<int64_t> checkedBinary(Kind op, int64_t a, int64_t b) {
        int64_t result;
        switch (op) {
        case ADD:
            if (!llvm::AddOverflow(a, b, result)) return result;
            break;
        case SUB:
            if (!llvm::SubOverflow(a, b, result)) return result;
            break;
        case MUL:
            if (!llvm::MulOverflow(a, b, result)) return result;
            break;
        case DIV:
        case MOD:
            if (b != 0 && !(a == INT64_MIN && b == -1))
                return op == DIV ? a / b : a % b;
            break;
        case SHL:
            if (b >= 0 && b < 64 && a >= 0 && a <= (INT64_MAX >> b))
                return a << b;
            break;
        case SHR:
            if (b >= 0 && b < 64) return a >> b;
            break;
        case BITAND: return a & b;
        case BITOR: return a | b;
        case BITXOR: return a ^ b;
        default: break;
        }
        return std::nullopt;
    }

    static SymValue foldBinary(Kind op, int64_t a, int64_t b) {
        auto value = checkedBinary(op, a, b);
        return value ? Const(*value) : Unknown();
    }

    static SymValue Add(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return foldBinary(ADD, a.constVal, b.constVal);
        if (a.kind == CONST && a.constVal == 0) return b;
        if (b.kind == CONST && b.constVal == 0) return a;
        SymValue s; s.kind = ADD;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Sub(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return foldBinary(SUB, a.constVal, b.constVal);
        if (b.kind == CONST && b.constVal == 0) return a;
        SymValue s; s.kind = SUB;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Mul(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return foldBinary(MUL, a.constVal, b.constVal);
        SymValue s; s.kind = MUL;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Div(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return foldBinary(DIV, a.constVal, b.constVal);
        SymValue s; s.kind = DIV;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue BitAnd(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal & b.constVal);
        SymValue s; s.kind = BITAND;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Mod(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return foldBinary(MOD, a.constVal, b.constVal);
        SymValue s; s.kind = MOD;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue BitOr(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal | b.constVal);
        SymValue s; s.kind = BITOR;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue BitXor(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal ^ b.constVal);
        SymValue s; s.kind = BITXOR;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Shl(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return foldBinary(SHL, a.constVal, b.constVal);
        SymValue s; s.kind = SHL;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Shr(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return foldBinary(SHR, a.constVal, b.constVal);
        SymValue s; s.kind = SHR;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Neg(SymValue a) {
        if (a.kind == CONST) return foldBinary(SUB, 0, a.constVal);
        SymValue s; s.kind = NEG;
        s.left = std::make_shared<SymValue>(a);
        return s;
    }

    static SymValue BitNot(SymValue a) {
        if (a.kind == CONST) return Const(~a.constVal);
        SymValue s; s.kind = BITNOT;
        s.left = std::make_shared<SymValue>(a);
        return s;
    }

    // Value a reduced modulo 2^bits into a bits-wide integer type, signed or
    // unsigned: what an integer conversion, unsigned arithmetic or a shift
    // gives when the mathematical value a may not fit the type.
    static SymValue Wrap(SymValue a, unsigned bits, bool isSigned) {
        if (a.kind == UNKNOWN) return a;
        SymValue s; s.kind = WRAP; s.constVal = bits; s.wrapSigned = isSigned;
        s.left = std::make_shared<SymValue>(a);
        if (auto v = s.eval()) return Const(*v);
        return s;
    }

    // v reduced into a bits-wide integer, if the result fits int64_t.
    static std::optional<int64_t> wrapValue(int64_t v, unsigned bits, bool isSigned) {
        if (bits == 0 || bits > 64) return std::nullopt;
        uint64_t mask = bits == 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1;
        uint64_t u = static_cast<uint64_t>(v) & mask;
        if (isSigned && bits < 64 && (u >> (bits - 1)) & 1) u |= ~mask;  // sign-extend
        if (!isSigned && bits == 64 && u > uint64_t(INT64_MAX)) return std::nullopt;
        return static_cast<int64_t>(u);
    }

    static SymValue Ternary(SymValue c, SymValue t, SymValue f) {
        SymValue s; s.kind = TERNARY;
        s.cond = std::make_shared<SymValue>(c);
        s.left = std::make_shared<SymValue>(t);
        s.right = std::make_shared<SymValue>(f);
        return s;
    }

    std::optional<int64_t> eval() const {
        switch (kind) {
            case CONST: return constVal;
            case ADD: case SUB: case MUL: case DIV: case MOD:
            case BITAND: case BITOR: case BITXOR: case SHL: case SHR:
                if (left && right) {
                    auto l = left->eval(), r = right->eval();
                    if (l && r) return checkedBinary(kind, *l, *r);
                }
                return std::nullopt;
            case NEG:
                if (left) {
                    if (auto l = left->eval()) return checkedBinary(SUB, 0, *l);
                }
                return std::nullopt;
            case BITNOT: if (left) { auto l = left->eval(); if (l) return ~(*l); } return std::nullopt;
            case WRAP:
                if (left) {
                    if (auto l = left->eval()) return wrapValue(*l, constVal, wrapSigned);
                }
                return std::nullopt;
            case TERNARY:
                // Can evaluate if condition is known
                if (cond && left && right) {
                    auto c = cond->eval();
                    if (c) {
                        // Condition is known - pick the right branch
                        return (*c != 0) ? left->eval() : right->eval();
                    }
                    // Condition unknown, but if both branches eval to same, we know the result
                    auto l = left->eval(), r = right->eval();
                    if (l && r && *l == *r) return *l;
                }
                return std::nullopt;
            default: return std::nullopt;
        }
    }

    std::string str() const {
        switch (kind) {
            case CONST: return std::to_string(constVal);
            case UNKNOWN: return "?";
            case VAR: return varRef;
            case STRING: return "\"" + strVal + "\"";
            case ADD: return "(" + left->str() + "+" + right->str() + ")";
            case SUB: return "(" + left->str() + "-" + right->str() + ")";
            case MUL: return "(" + left->str() + "*" + right->str() + ")";
            case DIV: return "(" + left->str() + "/" + right->str() + ")";
            case BITAND: return "(" + left->str() + "&" + right->str() + ")";
            case MOD: return "(" + left->str() + "%" + right->str() + ")";
            case BITOR: return "(" + left->str() + "|" + right->str() + ")";
            case BITXOR: return "(" + left->str() + "^" + right->str() + ")";
            case SHL: return "(" + left->str() + "<<" + right->str() + ")";
            case SHR: return "(" + left->str() + ">>" + right->str() + ")";
            case NEG: return "(-" + left->str() + ")";
            case BITNOT: return "(~" + left->str() + ")";
            case WRAP:
                if (left->kind == CONST && !wrapSigned && constVal == 64)  // above INT64_MAX
                    return std::to_string(static_cast<uint64_t>(left->constVal));
                return (wrapSigned ? "wraps" : "wrapu") + std::to_string(constVal) + "(" + left->str() + ")";
            case TERNARY: return "(" + cond->str() + "?" + left->str() + ":" + right->str() + ")";
            default: return "?";
        }
    }

    // Template substitute to work with both std::map and llvm::StringMap
    template<typename MapT>
    SymValue substitute(const MapT& env) const {
        if (kind == VAR) {
            auto it = env.find(varRef);
            if (it != env.end()) {
                if constexpr (std::is_same_v<typename MapT::mapped_type, SymValue>) {
                    return it->second;
                } else {
                    return it->second;
                }
            }
            return *this;
        }
        if (kind == ADD) return Add(left->substitute(env), right->substitute(env));
        if (kind == SUB) return Sub(left->substitute(env), right->substitute(env));
        if (kind == MUL) return Mul(left->substitute(env), right->substitute(env));
        if (kind == DIV) return Div(left->substitute(env), right->substitute(env));
        if (kind == BITAND) return BitAnd(left->substitute(env), right->substitute(env));
        if (kind == MOD) return Mod(left->substitute(env), right->substitute(env));
        if (kind == BITOR) return BitOr(left->substitute(env), right->substitute(env));
        if (kind == BITXOR) return BitXor(left->substitute(env), right->substitute(env));
        if (kind == SHL) return Shl(left->substitute(env), right->substitute(env));
        if (kind == SHR) return Shr(left->substitute(env), right->substitute(env));
        if (kind == NEG) return Neg(left->substitute(env));
        if (kind == BITNOT) return BitNot(left->substitute(env));
        if (kind == WRAP) return Wrap(left->substitute(env), constVal, wrapSigned);
        if (kind == TERNARY) return Ternary(cond->substitute(env), left->substitute(env), right->substitute(env));
        return *this;
    }

    bool equals(const SymValue& other) const {
        if (kind != other.kind) return false;
        if (kind == CONST) return constVal == other.constVal;
        if (kind == VAR) return varRef == other.varRef;
        if (kind == STRING) return strVal == other.strVal;
        if (kind == NEG || kind == BITNOT) return left && other.left && left->equals(*other.left);
        if (kind == WRAP)
            return constVal == other.constVal && wrapSigned == other.wrapSigned && left && other.left &&
                   left->equals(*other.left);
        if (kind == TERNARY)
            return cond && other.cond && left && other.left && right && other.right &&
                   cond->equals(*other.cond) && left->equals(*other.left) && right->equals(*other.right);
        if (kind == ADD || kind == SUB || kind == MUL || kind == DIV ||
            kind == BITAND || kind == MOD || kind == BITOR || kind == BITXOR ||
            kind == SHL || kind == SHR)
            return left && other.left && right && other.right &&
                   left->equals(*other.left) && right->equals(*other.right);
        return false;
    }

    std::optional<int64_t> upperBound() const {
        if (kind == CONST && constVal >= 0) return constVal;
        if (kind == WRAP && !wrapSigned && constVal < 63) return (int64_t(1) << constVal) - 1;
        if (kind == BITAND && right && right->kind == CONST && right->constVal >= 0) return right->constVal;
        if (kind == MOD && right && right->kind == CONST && right->constVal > 0) return right->constVal - 1;
        if (kind == ADD && left && right) {
            auto lb = left->upperBound(), rb = right->upperBound();
            if (lb && rb) return checkedBinary(ADD, *lb, *rb);
        }
        if (kind == MUL && left && right) {
            auto lb = left->upperBound(), rb = right->upperBound();
            if (lb && rb) return checkedBinary(MUL, *lb, *rb);
        }
        return std::nullopt;
    }

    bool isGuaranteedNonNegative() const {
        if (kind == CONST && constVal >= 0) return true;
        if (kind == WRAP && !wrapSigned) return true;
        if (kind == BITAND && right && right->kind == CONST && right->constVal >= 0) return true;
        if (kind == MOD && right && right->kind == CONST && right->constVal > 0) return true;
        if ((kind == ADD || kind == MUL) && left && right)
            return left->isGuaranteedNonNegative() && right->isGuaranteedNonNegative();
        return false;
    }

    std::optional<int64_t> extractConstantOffset() const {
        if (kind == CONST) return constVal;
        if (kind == VAR || kind == UNKNOWN) return 0;
        if (kind == WRAP) return std::nullopt;
        if (kind == ADD || kind == SUB) {
            auto l = left ? left->extractConstantOffset() : std::optional<int64_t>(0);
            auto r = right ? right->extractConstantOffset() : std::optional<int64_t>(0);
            if (l && r) return checkedBinary(kind, *l, *r);
            return std::nullopt;
        }
        return 0;
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
        LogicalAnd, LogicalOr, LogicalNot,

        // Quantifiers
        Forall,         // forall(var, lo, hi, body)
        Exists,         // exists(var, lo, hi, body)
        ArrayIndex      // arr[i] - array element access
    };

    Kind kind;

    ConstraintExpr(Kind k) : kind(k) {}
    virtual ~ConstraintExpr() = default;

    virtual std::string toString() const = 0;

    bool isComparison() const {
        return kind >= CmpGT && kind <= CmpNE;
    }

    bool isLogical() const {
        return kind >= LogicalAnd && kind <= LogicalNot;
    }

    virtual ConstraintExprPtr clone() const = 0;

    virtual std::optional<SymValue> toSymValue() const { return std::nullopt; }
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

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Const(value);
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

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var(fieldName);
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

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("arg" + std::to_string(argIndex));
    }
};

// Argument field reference: arg0._size, arg1._nonnull, etc.
struct ArgFieldRefExpr : ConstraintExpr {
    unsigned argIndex;
    llvm::SmallString<32> fieldName;

    ArgFieldRefExpr(unsigned idx, llvm::StringRef field)
        : ConstraintExpr(ArgFieldRef), argIndex(idx), fieldName(field) {}

    std::string toString() const override {
        llvm::SmallString<48> result;
        result += "arg";
        result += std::to_string(argIndex);
        result += ".";
        result += fieldName;
        return std::string(result);
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<ArgFieldRefExpr>(argIndex, fieldName.str());
    }

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("arg" + std::to_string(argIndex) + "." +
                             std::string(fieldName));
    }
};

// Old value reference: old(_size), old(_capacity)
struct OldRefExpr : ConstraintExpr {
    std::string fieldName;

    OldRefExpr(const std::string& field) : ConstraintExpr(OldRef), fieldName(field) {}

    std::string toString() const override {
        return "old(" + fieldName + ")";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<OldRefExpr>(fieldName);
    }

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("old:" + fieldName);
    }
};

// Result reference for postconditions: _result
struct ResultRefExpr : ConstraintExpr {
    ResultRefExpr() : ConstraintExpr(ResultRef) {}

    std::string toString() const override {
        return "_result";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<ResultRefExpr>();
    }

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("_result");
    }
};

// Binary expression: comparisons and arithmetic
struct BinaryExpr : ConstraintExpr {
    ConstraintExprPtr left, right;

    BinaryExpr(Kind k, ConstraintExprPtr l, ConstraintExprPtr r)
        : ConstraintExpr(k), left(std::move(l)), right(std::move(r)) {}

    std::string toString() const override {
        std::string opStr;
        switch (kind) {
            case Add: opStr = " + "; break;
            case Sub: opStr = " - "; break;
            case Mul: opStr = " * "; break;
            case Div: opStr = " / "; break;
            case Mod: opStr = " % "; break;
            case BitAnd: opStr = " & "; break;
            case CmpGT: opStr = " > "; break;
            case CmpGE: opStr = " >= "; break;
            case CmpLT: opStr = " < "; break;
            case CmpLE: opStr = " <= "; break;
            case CmpEQ: opStr = " == "; break;
            case CmpNE: opStr = " != "; break;
            case LogicalAnd: opStr = " && "; break;
            case LogicalOr: opStr = " || "; break;
            default: opStr = " ? "; break;
        }
        bool needLeftParen = left->isLogical() &&
                             (kind == LogicalAnd || kind == LogicalOr);
        bool needRightParen = right->isLogical() &&
                              (kind == LogicalAnd || kind == LogicalOr);
        std::string result;
        if (needLeftParen) result += "(";
        result += left->toString();
        if (needLeftParen) result += ")";
        result += opStr;
        if (needRightParen) result += "(";
        result += right->toString();
        if (needRightParen) result += ")";
        return result;
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<BinaryExpr>(kind, left->clone(), right->clone());
    }

    std::optional<SymValue> toSymValue() const override {
        if (kind < Add || kind > BitAnd) return std::nullopt;
        auto lhs = left->toSymValue();
        auto rhs = right->toSymValue();
        if (!lhs || !rhs) return std::nullopt;
        switch (kind) {
            case Add: return SymValue::Add(*lhs, *rhs);
            case Sub: return SymValue::Sub(*lhs, *rhs);
            case Mul: return SymValue::Mul(*lhs, *rhs);
            case Div: return SymValue::Div(*lhs, *rhs);
            case Mod: return SymValue::Mod(*lhs, *rhs);
            case BitAnd: return SymValue::BitAnd(*lhs, *rhs);
            default: return std::nullopt;
        }
    }
};

// Unary operation expression (logical not)
struct UnaryExpr : ConstraintExpr {
    ConstraintExprPtr operand;

    UnaryExpr(Kind k, ConstraintExprPtr op)
        : ConstraintExpr(k), operand(std::move(op)) {}

    std::string toString() const override {
        if (kind == LogicalNot)
            return "!" + operand->toString();
        return "?" + operand->toString();
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<UnaryExpr>(kind, operand->clone());
    }
};

// Bounded universal quantifier: forall(var, lo, hi, body)
// Semantics: (lo >= hi) v forall i in [lo,hi). body(i)
struct ForallExpr : ConstraintExpr {
    std::string boundVar;
    ConstraintExprPtr lo, hi, body;

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
};

// Bounded existential quantifier: exists(var, lo, hi, body)
// Semantics: (lo < hi) ^ exists i in [lo,hi). body(i)
struct ExistsExpr : ConstraintExpr {
    std::string boundVar;
    ConstraintExprPtr lo, hi, body;

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

// Array index expression: arr[i] — used inside quantifier bodies
struct IndexExpr : ConstraintExpr {
    std::string arrayName;
    ConstraintExprPtr index;

    IndexExpr(std::string arr, ConstraintExprPtr idx)
        : ConstraintExpr(ArrayIndex),
          arrayName(std::move(arr)), index(std::move(idx)) {}

    std::string toString() const override {
        return arrayName + "[" + index->toString() + "]";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<IndexExpr>(arrayName, index->clone());
    }
};

// ============================================================
// POSTCONDITIONS
// ============================================================

// Conditional guard for postconditions and scope_exit: `when FIELD OP VALUE`
// If present, the postcondition/constraint is only applied when the guard holds.
// e.g., `ensures result.@open = 1 when result._nonnull != 0`
// e.g., `scope_exit result.@open == 0 when result._nonnull != 0`
// Works uniformly across fn blocks, type/trait blocks, and scope_exit lines.
struct PostconditionGuard {
    bool isResult = true;           // true = result.FIELD, false = argN.FIELD
    int argIndex = 0;               // Which argument (when isResult == false)
    const char* field = "";         // Field to check (e.g., "_nonnull")
    ProofOp op = ProofOp::NE;      // Comparison operator
    int64_t value = 0;             // Value to compare against
};

// Target of a postcondition: result or a specific argument
enum class PostconditionTarget { Result, Arg };

struct Postcondition {
    PostconditionTarget target = PostconditionTarget::Result;
    int argIndex = 0;           // Which argument (if target == Arg)
    std::string field;          // Field to set (e.g., "@open", "_size")
    SymValue expr;
    bool usesOld = false;
    std::optional<PostconditionGuard> guard;  // Optional `when` condition
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
// FREE FUNCTION ANNOTATION KEY AND INFO (for C library functions)
// ============================================================

struct FreeFunctionKey {
    const char* functionName;
    unsigned numParams;

    bool operator==(const FreeFunctionKey& other) const {
        return numParams == other.numParams &&
               llvm::StringRef(functionName) == llvm::StringRef(other.functionName);
    }
};

// Single precondition check for a free function argument
struct FreeFunctionPrecondition {
    int argIndex = -1;              // Which argument to check
    const char* field = "";         // Field to check (e.g., "_nonnull", "_readable")
    int64_t forbiddenValue = 0;     // Value that violates precondition
    ProofOp checkOp = ProofOp::NE;  // Typically NE (field != forbiddenValue)
    const char* errorReason = "";   // Human-readable error message
    bool isRawArg = false;          // true = check arg value directly (no field)
};

// Single postcondition to apply after a free function call
struct FreeFunctionPostcondition {
    int argIndex = -1;              // Which argument to modify
    const char* field = "";         // Field to set
    int64_t value = 0;              // Value to set
    std::optional<PostconditionGuard> guard;  // Optional `when` condition
};

// Scope-exit requirement for resource-acquiring functions
// Target is either the result or an argument:
//   fopen:        scope_exit result.@open == 0
//   deflateInit:  scope_exit arg0.@stream_state == 0
struct FreeFunctionScopeExitReq {
    const char* field = "";         // Ghost field (e.g., "@open", "@allocated")
    ProofOp op = ProofOp::EQ;       // Required comparison at scope exit
    int64_t requiredValue = 0;      // Required value at scope exit
    bool isResult = true;           // true = result.FIELD, false = argN.FIELD
    int argIndex = 0;               // Which argument (when isResult == false)
    std::optional<PostconditionGuard> guard;  // Optional `when` condition
};

// Result postcondition (for resource-acquiring functions)
// e.g., fopen ensures result.@open == 1
struct FreeFunctionResultPost {
    const char* field = "";         // Ghost field to set
    int64_t value = 0;              // Value to set
    std::optional<PostconditionGuard> guard;  // Optional `when` condition
};

struct FreeFunctionInfo {
    // Multiple preconditions (all must be satisfied)
    llvm::SmallVector<FreeFunctionPrecondition, 2> preconditions;

    // Multiple postconditions for arguments (all applied after call)
    llvm::SmallVector<FreeFunctionPostcondition, 2> postconditions;

    // Result postconditions (for return value of resource-acquiring functions)
    llvm::SmallVector<FreeFunctionResultPost, 2> resultPostconditions;

    // Scope-exit requirements (for return value - must be satisfied before scope exit)
    llvm::SmallVector<FreeFunctionScopeExitReq, 2> scopeExitRequires;
};

// ============================================================
// TEMPLATE PARAMETER ALIASES (for std::array, std::span, etc.)
// ============================================================

struct TemplateParamAlias {
    int paramIndex;
    std::string propertyName;
    std::optional<int64_t> skipValue;  // Don't set if param equals this value
};

// ============================================================
// INFERRED PRECONDITIONS (for backward inference)
// ============================================================

enum class PreconditionKind { NonNull, SizeGE, Generic };

struct InferredPrecondition {
    std::string path;                        // e.g., "arg0", "arg0->a->b"
    PreconditionKind kind;
    std::string boundStr;                    // For SizeGE: string repr of minimum required size
    std::optional<unsigned> argIndex;        // Which parameter the buffer refers to
    std::optional<unsigned> boundArgIndex;   // Which parameter the bound refers to
    int64_t boundConstant = 0;              // Constant part of bound (for expressions like n+10)

    // Phase 3: Pointer arithmetic offset tracking
    std::optional<unsigned> offsetArgIndex;  // Which parameter the offset refers to (element count)
    int64_t offsetConstant = 0;              // Constant part of offset (in bytes)
    int64_t offsetElemSize = 1;              // Element size to scale offsetArgIndex (bytes per element)
    bool offsetNegated = false;              // True if offset should be subtracted (p = base - off)

    // Generic property constraint (used when kind == Generic)
    // Represents: argN.field op value  (e.g., arg0._nonnull != 0)
    llvm::SmallString<32> field;             // Property field (e.g., "_nonnull", "@open")
    ProofOp constraintOp = ProofOp::NE;     // Comparison operator
    int64_t constraintValue = 0;            // Required value
    const char* errorReason = "";           // Human-readable error message

    bool isGeneric() const { return kind == PreconditionKind::Generic; }
    bool isBufferSize() const { return kind == PreconditionKind::SizeGE; }

    std::string toString() const {
        switch (kind) {
        case PreconditionKind::NonNull:
            return path + " != null";
        case PreconditionKind::SizeGE:
            return path + "._size_bytes >= " + boundStr;
        case PreconditionKind::Generic: {
            llvm::SmallString<64> result(path);
            result += ".";
            result += field;
            switch (constraintOp) {
            case ProofOp::NE: result += " != "; break;
            case ProofOp::EQ: result += " == "; break;
            case ProofOp::GT: result += " > "; break;
            case ProofOp::GE: result += " >= "; break;
            case ProofOp::LT: result += " < "; break;
            case ProofOp::LE: result += " <= "; break;
            }
            result += std::to_string(constraintValue);
            return std::string(result);
        }
        }
        return path;
    }
};

// ============================================================
// INFERRED POSTCONDITIONS (for automatic postcondition inference)
// ============================================================

// Inferred postconditions from method/function implementations.
// Supports:
// - Method postconditions: _size = n → ensures=_size == arg0
// - Out-parameter postconditions: v._size = n → ensures=arg0._size == arg1
// - Return value postconditions: return {._size = n} → ensures=result._size == n
struct InferredPostcondition {
    std::string field;                       // e.g., "_size", "_nonnull"
    SymValue expr;                           // The inferred value expression
    bool usesOld = false;                    // True if expression references old() values
    std::optional<unsigned> exprArgIndex;    // If expr is simple argN reference, which arg
    std::optional<unsigned> targetArgIndex;  // Which arg this postcond applies to (nullopt = this/result)

    std::string toString() const {
        std::string target = targetArgIndex ? "arg" + std::to_string(*targetArgIndex) + "." : "";
        std::string result = target + field + " = " + expr.str();
        if (usesOld) result += " (uses old)";
        return result;
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

// DenseMap support for FreeFunctionKey
template<> struct DenseMapInfo<dtt::FreeFunctionKey> {
    static inline dtt::FreeFunctionKey getEmptyKey() {
        return {reinterpret_cast<const char*>(~uintptr_t(0)), UINT_MAX};
    }
    static inline dtt::FreeFunctionKey getTombstoneKey() {
        return {reinterpret_cast<const char*>(~uintptr_t(1)), UINT_MAX - 1};
    }
    static unsigned getHashValue(const dtt::FreeFunctionKey& k) {
        return llvm::hash_combine(llvm::StringRef(k.functionName), k.numParams);
    }
    static bool isEqual(const dtt::FreeFunctionKey& lhs, const dtt::FreeFunctionKey& rhs) {
        if (lhs.numParams != rhs.numParams) return false;
        // Handle sentinel values
        if (lhs.functionName == getEmptyKey().functionName ||
            lhs.functionName == getTombstoneKey().functionName ||
            rhs.functionName == getEmptyKey().functionName ||
            rhs.functionName == getTombstoneKey().functionName)
            return lhs.functionName == rhs.functionName;
        return llvm::StringRef(lhs.functionName) == llvm::StringRef(rhs.functionName);
    }
};
} // namespace llvm

#endif // DTT_TYPES_H
