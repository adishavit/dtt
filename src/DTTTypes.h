// DTTTypes.h - Shared type definitions for DTT plugin
//
// Types used by both the main plugin and stdlib annotations

#ifndef DTT_TYPES_H
#define DTT_TYPES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
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
                TERNARY, BITOR, BITXOR, SHL, SHR, NEG, BITNOT } kind;
    int64_t constVal = 0;
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

    static SymValue Add(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal + b.constVal);
        if (a.kind == CONST && a.constVal == 0) return b;
        if (b.kind == CONST && b.constVal == 0) return a;
        SymValue s; s.kind = ADD;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Sub(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal - b.constVal);
        if (b.kind == CONST && b.constVal == 0) return a;
        SymValue s; s.kind = SUB;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Mul(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) return Const(a.constVal * b.constVal);
        SymValue s; s.kind = MUL;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Div(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST && b.constVal != 0) return Const(a.constVal / b.constVal);
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
        if (a.kind == CONST && b.kind == CONST && b.constVal != 0) return Const(a.constVal % b.constVal);
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
        if (a.kind == CONST && b.kind == CONST && b.constVal >= 0 && b.constVal < 64)
            return Const(a.constVal << b.constVal);
        SymValue s; s.kind = SHL;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Shr(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST && b.constVal >= 0 && b.constVal < 64)
            return Const(a.constVal >> b.constVal);
        SymValue s; s.kind = SHR;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    static SymValue Neg(SymValue a) {
        if (a.kind == CONST) return Const(-a.constVal);
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
            case ADD: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l + *r; } return std::nullopt;
            case SUB: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l - *r; } return std::nullopt;
            case MUL: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l * *r; } return std::nullopt;
            case DIV: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r && *r != 0) return *l / *r; } return std::nullopt;
            case BITAND: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l & *r; } return std::nullopt;
            case MOD: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r && *r != 0) return *l % *r; } return std::nullopt;
            case BITOR: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l | *r; } return std::nullopt;
            case BITXOR: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l ^ *r; } return std::nullopt;
            case SHL: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r && *r >= 0 && *r < 64) return *l << *r; } return std::nullopt;
            case SHR: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r && *r >= 0 && *r < 64) return *l >> *r; } return std::nullopt;
            case NEG: if (left) { auto l = left->eval(); if (l) return -(*l); } return std::nullopt;
            case BITNOT: if (left) { auto l = left->eval(); if (l) return ~(*l); } return std::nullopt;
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
        if (kind == TERNARY) return Ternary(cond->substitute(env), left->substitute(env), right->substitute(env));
        return *this;
    }

    bool equals(const SymValue& other) const {
        if (kind != other.kind) return false;
        if (kind == CONST) return constVal == other.constVal;
        if (kind == VAR) return varRef == other.varRef;
        if (kind == STRING) return strVal == other.strVal;
        if (kind == NEG || kind == BITNOT) return left && other.left && left->equals(*other.left);
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
        if (kind == BITAND && right && right->kind == CONST && right->constVal >= 0) return right->constVal;
        if (kind == MOD && right && right->kind == CONST && right->constVal > 0) return right->constVal - 1;
        return std::nullopt;
    }

    bool isGuaranteedNonNegative() const {
        if (kind == CONST && constVal >= 0) return true;
        if (kind == BITAND && right && right->kind == CONST && right->constVal >= 0) return true;
        if (kind == MOD && right && right->kind == CONST && right->constVal > 0) return true;
        return false;
    }

    int64_t extractConstantOffset() const {
        if (kind == CONST) return constVal;
        if (kind == VAR || kind == UNKNOWN) return 0;
        if (kind == ADD) {
            if (left && left->kind == CONST) return left->constVal + (right ? right->extractConstantOffset() : 0);
            if (right && right->kind == CONST) return right->constVal + (left ? left->extractConstantOffset() : 0);
            return (left ? left->extractConstantOffset() : 0) + (right ? right->extractConstantOffset() : 0);
        }
        if (kind == SUB) {
            if (right && right->kind == CONST) return (left ? left->extractConstantOffset() : 0) - right->constVal;
            return (left ? left->extractConstantOffset() : 0) - (right ? right->extractConstantOffset() : 0);
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
    bool checkLifetime = false;     // If true, check pointer lifetime (use-after-free)
};

// Single postcondition to apply after a free function call
struct FreeFunctionPostcondition {
    int argIndex = -1;              // Which argument to modify
    const char* field = "";         // Field to set
    int64_t value = 0;              // Value to set
};

struct FreeFunctionInfo {
    // Multiple preconditions (all must be satisfied)
    llvm::SmallVector<FreeFunctionPrecondition, 2> preconditions;

    // Multiple postconditions (all applied after call)
    llvm::SmallVector<FreeFunctionPostcondition, 2> postconditions;
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

enum class PreconditionKind { NonNull, SizeGE };

struct InferredPrecondition {
    std::string path;                        // e.g., "arg0", "arg0->a->b"
    PreconditionKind kind;
    std::string boundStr;                    // For SizeGE: string repr of minimum required size
    std::optional<unsigned> argIndex;        // Which parameter the buffer refers to
    std::optional<unsigned> boundArgIndex;   // Which parameter the bound refers to
    int64_t boundConstant = 0;               // Constant part of bound (for expressions like n+10)

    // Phase 3: Pointer arithmetic offset tracking
    std::optional<unsigned> offsetArgIndex;  // Which parameter the offset refers to (element count)
    int64_t offsetConstant = 0;              // Constant part of offset (in bytes)
    int64_t offsetElemSize = 1;              // Element size to scale offsetArgIndex (bytes per element)
    bool offsetNegated = false;              // True if offset should be subtracted (p = base - off)

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
