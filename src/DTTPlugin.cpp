// DTT Plugin — Dependent Type Tracking for C++

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include <map>
#include <string>
#include <optional>
#include <vector>
#include <set>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <array>
#include <memory>
#include <cerrno>
#include <z3++.h>

using namespace clang;

namespace {

ASTContext *GlobalCtx = nullptr;

// ============================================================
// DTT MODE CONFIGURATION
// ============================================================
// The three modes control how DTT handles Category C (lost tracking)
// and Category D (undecidable constraints):
//   - inform (default): C & D are warnings; F & G are errors
//   - enforce: C, D, F, G are all errors
//   - quiet: C & D are silent; F & G are errors
enum class DTTMode {
    Inform,   // Default: warnings for C & D
    Enforce,  // Strict: errors for C & D
    Quiet     // Additive: silent for C & D
};

DTTMode GlobalMode = DTTMode::Inform;

// Helper for mode-dependent diagnostics (Category C & D)
// Groups warning and error variants together with clean selection logic
struct ModeDependentDiag {
    unsigned warn = 0;  // Used in inform mode
    unsigned err = 0;   // Used in enforce mode

    unsigned get(DTTMode mode) const {
        return (mode == DTTMode::Enforce) ? err : warn;
    }

    bool shouldEmit(DTTMode mode) const {
        return mode != DTTMode::Quiet;
    }
};

// ============================================================
// TRACKING STATE (Categories A-G)
// ============================================================
// Each tracked field can be in one of these states:
//   Known         - We have exact value or tight constraints (Category E if safe)
//   UnknownLost   - Category C: Had tracking, lost due to unknown call/escape
//   UnknownSolver - Category D: Has constraints, solver couldn't resolve
//   Untracked     - Category A/B: Never in scope or user didn't opt in
enum class TrackingState {
    Known,
    UnknownLost,
    UnknownSolver,
    Untracked
};

// ============================================================
// POINTER LIFETIME TRACKING
// ============================================================
// Tracks whether heap-allocated memory is still valid or has been freed.
// Enables detection of use-after-free and double-free bugs.

enum class PointerLifetime {
    Valid,      // Pointer to live memory
    Freed,      // Memory has been freed - any use is UAF
    Unknown     // Lifetime not tracked
};

// Allocation source for mismatched dealloc detection
enum class AllocSource {
    Malloc,       // malloc, calloc, realloc
    New,          // new T
    NewArray,     // new T[]
    AlignedAlloc, // aligned_alloc, posix_memalign
    Placement,    // placement new (no dealloc needed)
    Unknown
};

struct PointerLifetimeState {
    PointerLifetime lifetime = PointerLifetime::Unknown;
    AllocSource allocSource = AllocSource::Unknown;
    SourceLocation allocLocation;    // Where allocated
    SourceLocation freeLocation;     // Where freed (if Freed)
};

// Map from pointer path to lifetime state
std::map<std::string, PointerLifetimeState> PointerLifetimes;

// Forward declarations for FieldState (defined after SymValue)
struct LossProvenance;
struct FieldState;

// Forward declaration for safe expression evaluation
bool safeEvaluateAsInt(const Expr *E, Expr::EvalResult &R, ASTContext &Ctx);

// ============================================================
// TEMPLATE PARAMETER EXTRACTION
// ============================================================

// Alias table: maps (qualified class name, param index) -> semantic property name
// Also supports skip values (e.g., std::span with dynamic_extent = -1)
struct TemplateParamAlias {
    int paramIndex;
    std::string propertyName;
    std::optional<int64_t> skipValue;  // Don't set if param equals this value
};

// Known template aliases for std:: types
std::map<std::string, std::vector<TemplateParamAlias>> TemplateAliases = {
    {"std::array",  {{1, "_size", std::nullopt}}},
    {"std::span",   {{1, "_size", static_cast<int64_t>(-1)}}},  // skip dynamic_extent
    {"std::bitset", {{0, "_size", std::nullopt}}},
};

std::string getExprPath(const Expr *E);

std::string getExprPathFromBase(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        return getExprPath(ME->getBase());
    }
    if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        return getExprPath(MCE->getImplicitObjectArgument());
    }
    return getExprPath(E);
}

std::string getExprPath(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    if (auto *DRE = dyn_cast<DeclRefExpr>(E))
        return DRE->getDecl()->getNameAsString();
    // Handle 'this' pointer for method body analysis
    if (isa<CXXThisExpr>(E))
        return "this";
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        std::string base = getExprPath(ME->getBase());
        if (base.empty()) return "";
        return base + "." + ME->getMemberDecl()->getNameAsString();
    }
    if (auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        std::string base = getExprPath(ASE->getBase());
        if (base.empty()) return "";
        if (GlobalCtx) {
            Expr::EvalResult R;
            if (safeEvaluateAsInt(ASE->getIdx(), R, *GlobalCtx)) {
                const llvm::APSInt &Val = R.Val.getInt();
                if (Val.isRepresentableByInt64()) {
                    int64_t idx = Val.getExtValue();
                    return base + "[" + std::to_string(idx) + "]";
                }
            }
        }
        return base + "[?]";
    }
    // Handle chain assignment (v3 = v2 = v1) — result of v2 = v1 is v2
    if (auto *OpCall = dyn_cast<CXXOperatorCallExpr>(E)) {
        if (OpCall->getOperator() == OO_Equal && OpCall->getNumArgs() >= 1) {
            return getExprPath(OpCall->getArg(0));
        }
    }
    // Handle C-style assignment: (local = ptr) — result is local
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->getOpcode() == BO_Assign) {
            return getExprPath(BO->getLHS());
        }
    }
    // Handle address-of: &v -> return "v" (for pointer argument invalidation)
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
        if (UO->getOpcode() == UO_AddrOf) {
            return getExprPath(UO->getSubExpr());
        }
    }
    return "";
}

// Helper to safely get int64_t from APSInt
std::optional<int64_t> safeGetInt64(const llvm::APSInt& Val) {
    if (Val.isRepresentableByInt64()) {
        return Val.getExtValue();
    }
    return std::nullopt;
}

// Helper to safely parse int64_t from string (no exceptions)
std::optional<int64_t> safeStoll(const std::string& s) {
    if (s.empty()) return std::nullopt;
    const char* str = s.c_str();
    char* end = nullptr;
    errno = 0;
    long long val = std::strtoll(str, &end, 10);
    if (errno == ERANGE || end == str || *end != '\0') return std::nullopt;
    return static_cast<int64_t>(val);
}

// Helper to safely get type size in chars.
// Returns std::nullopt for dependent types (template params) or incomplete types.
// This prevents crashes when calling ASTContext::getTypeSizeInChars on such types.
std::optional<int64_t> safeGetTypeSizeInChars(ASTContext &Ctx, QualType T) {
    if (T.isNull()) return std::nullopt;
    const Type *Ty = T.getTypePtr();
    if (!Ty) return std::nullopt;
    if (Ty->isDependentType()) return std::nullopt;
    if (Ty->isIncompleteType()) return std::nullopt;
    return Ctx.getTypeSizeInChars(T).getQuantity();
}

// Helper to safely evaluate an expression as an integer constant.
// Returns false for expressions involving dependent types, which can crash
// EvaluateAsInt internally when getTypeInfo is called on incomplete types.
bool safeEvaluateAsInt(const Expr *E, Expr::EvalResult &R, ASTContext &Ctx) {
    if (!E) return false;
    QualType T = E->getType();
    if (T.isNull()) return false;
    if (T->isDependentType()) return false;
    // Check if the expression involves instantiation-dependent constructs
    if (E->isInstantiationDependent()) return false;
    if (E->isValueDependent()) return false;
    return E->EvaluateAsInt(R, Ctx);
}

std::string getBasePath(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    if (auto *ME = dyn_cast<MemberExpr>(E)) return getExprPath(ME->getBase());
    return "";
}

std::string getFieldName(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    if (auto *ME = dyn_cast<MemberExpr>(E)) return ME->getMemberDecl()->getNameAsString();
    return "";
}

bool hasRuntimeIndex(const std::string& path) { return path.find("[?]") != std::string::npos; }

std::string getArrayBase(const std::string& path) {
    auto pos = path.find('[');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
}

// ============================================================
// NULLPTR INFERENCE HELPERS
// ============================================================

// Infer nullability from an expression
// Returns: 0 = known null, 1 = known non-null, nullopt = unknown
std::optional<int64_t> inferNonnullFromExpr(Expr *E) {
    if (!E) return std::nullopt;
    E = E->IgnoreParenImpCasts();

    // nullptr literal or GNU __null
    if (isa<CXXNullPtrLiteralExpr>(E) || isa<GNUNullExpr>(E)) {
        return 0;
    }

    // Integer literal: 0 is null, non-zero is non-null
    if (auto *IL = dyn_cast<IntegerLiteral>(E)) {
        return (IL->getValue() == 0) ? 0 : 1;
    }

    // new T() - check for nothrow variant first
    if (auto *NewE = dyn_cast<CXXNewExpr>(E)) {
        // Check for nothrow variant: new (std::nothrow) T()
        for (unsigned i = 0; i < NewE->getNumPlacementArgs(); ++i) {
            Expr *PlacementArg = NewE->getPlacementArg(i)->IgnoreParenImpCasts();
            if (auto *DRE = dyn_cast<DeclRefExpr>(PlacementArg)) {
                std::string name = DRE->getDecl()->getNameAsString();
                if (name == "nothrow" || name == "nothrow_t") {
                    // nothrow new can return nullptr on allocation failure
                    return std::nullopt;
                }
            }
        }
        // Regular new throws on failure, guaranteed non-null on success
        return 1;
    }

    // &x - address-of always produces valid pointer
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
        if (UO->getOpcode() == UO_AddrOf) {
            return 1;
        }
    }

    // String literal - always non-null (points to static storage)
    if (isa<StringLiteral>(E)) {
        return 1;
    }

    // Array DeclRefExpr - arrays decay to non-null pointer (address of first element)
    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        if (DRE->getType()->isArrayType()) {
            return 1;
        }
    }

    // Compound literal - always non-null (temporary storage)
    if (isa<CompoundLiteralExpr>(E)) {
        return 1;
    }

    // Ternary operator: cond ? a : b
    // Result is non-null if BOTH branches are non-null
    if (auto *CO = dyn_cast<ConditionalOperator>(E)) {
        auto trueVal = inferNonnullFromExpr(CO->getTrueExpr());
        auto falseVal = inferNonnullFromExpr(CO->getFalseExpr());

        // Both branches known non-null → result is non-null
        if (trueVal.has_value() && *trueVal == 1 &&
            falseVal.has_value() && *falseVal == 1) {
            return 1;
        }
        // Either branch known null → result could be null
        if ((trueVal.has_value() && *trueVal == 0) ||
            (falseVal.has_value() && *falseVal == 0)) {
            return std::nullopt;
        }
        // Otherwise unknown
        return std::nullopt;
    }

    // Unknown expression - caller must handle (e.g., DeclRefExpr for copying)
    return std::nullopt;
}

// Check if a CXXNewExpr is true placement new (not nothrow new)
// Placement new constructs in pre-allocated memory and should NOT be deleted
bool isPlacementNew(const CXXNewExpr *NewE) {
    if (!NewE || NewE->getNumPlacementArgs() == 0) {
        return false;
    }
    // Check if it's nothrow new (which still allocates)
    for (unsigned i = 0; i < NewE->getNumPlacementArgs(); ++i) {
        const Expr *PlacementArg = NewE->getPlacementArg(i)->IgnoreParenImpCasts();
        if (auto *DRE = dyn_cast<DeclRefExpr>(PlacementArg)) {
            std::string name = DRE->getDecl()->getNameAsString();
            if (name == "nothrow" || name == "nothrow_t") {
                return false;  // nothrow new is not placement new
            }
        }
    }
    // Has placement args that are not nothrow - true placement new
    return true;
}

// ============================================================
// SYMBOLIC VALUES
// ============================================================

// Overflow-safe arithmetic helpers (return nullopt on overflow)
std::optional<int64_t> safeAdd(int64_t a, int64_t b) {
    int64_t result;
    if (__builtin_add_overflow(a, b, &result)) return std::nullopt;
    return result;
}

std::optional<int64_t> safeSub(int64_t a, int64_t b) {
    int64_t result;
    if (__builtin_sub_overflow(a, b, &result)) return std::nullopt;
    return result;
}

std::optional<int64_t> safeMul(int64_t a, int64_t b) {
    int64_t result;
    if (__builtin_mul_overflow(a, b, &result)) return std::nullopt;
    return result;
}

std::optional<int64_t> safeDiv(int64_t a, int64_t b) {
    if (b == 0) return std::nullopt;
    // INT64_MIN / -1 overflows
    if (a == INT64_MIN && b == -1) return std::nullopt;
    return a / b;
}

struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR, BITAND, MOD } kind;
    int64_t constVal = 0;
    std::string varRef;
    std::shared_ptr<SymValue> left, right;

    static SymValue Const(int64_t v) { SymValue s; s.kind = CONST; s.constVal = v; return s; }
    static SymValue Unknown() { SymValue s; s.kind = UNKNOWN; return s; }
    static SymValue Var(const std::string& ref) { SymValue s; s.kind = VAR; s.varRef = ref; return s; }

    static SymValue Add(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) {
            if (auto r = safeAdd(a.constVal, b.constVal)) return Const(*r);
            // Overflow: keep as symbolic expression, let Z3 handle it
        }
        SymValue s; s.kind = ADD;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }
    static SymValue Sub(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) {
            if (auto r = safeSub(a.constVal, b.constVal)) return Const(*r);
        }
        SymValue s; s.kind = SUB;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }
    static SymValue Mul(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) {
            if (auto r = safeMul(a.constVal, b.constVal)) return Const(*r);
        }
        SymValue s; s.kind = MUL;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }
    static SymValue Div(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) {
            if (auto r = safeDiv(a.constVal, b.constVal)) return Const(*r);
        }
        SymValue s; s.kind = DIV;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }
    static SymValue BitAnd(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) {
            return Const(a.constVal & b.constVal);
        }
        SymValue s; s.kind = BITAND;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }
    static SymValue Mod(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST && b.constVal != 0) {
            return Const(a.constVal % b.constVal);
        }
        SymValue s; s.kind = MOD;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }

    std::optional<int64_t> eval() const {
        switch (kind) {
            case CONST: return constVal;
            case ADD: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return safeAdd(*l, *r); } return std::nullopt;
            case SUB: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return safeSub(*l, *r); } return std::nullopt;
            case MUL: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return safeMul(*l, *r); } return std::nullopt;
            case DIV: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return safeDiv(*l, *r); } return std::nullopt;
            case BITAND: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l & *r; } return std::nullopt;
            case MOD: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r && *r != 0) return *l % *r; } return std::nullopt;
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
            case MUL: return "(" + left->str() + "*" + right->str() + ")";
            case DIV: return "(" + left->str() + "/" + right->str() + ")";
            case BITAND: return "(" + left->str() + "&" + right->str() + ")";
            case MOD: return "(" + left->str() + "%" + right->str() + ")";
            default: return "?";
        }
    }
    
    SymValue substitute(const std::map<std::string, SymValue>& env) const {
        if (kind == VAR) { auto it = env.find(varRef); return it != env.end() ? it->second : *this; }
        if (kind == ADD) return Add(left->substitute(env), right->substitute(env));
        if (kind == SUB) return Sub(left->substitute(env), right->substitute(env));
        if (kind == MUL) return Mul(left->substitute(env), right->substitute(env));
        if (kind == DIV) return Div(left->substitute(env), right->substitute(env));
        if (kind == BITAND) return BitAnd(left->substitute(env), right->substitute(env));
        if (kind == MOD) return Mod(left->substitute(env), right->substitute(env));
        return *this;
    }

    bool equals(const SymValue& other) const {
        if (kind != other.kind) return false;
        if (kind == CONST) return constVal == other.constVal;
        if (kind == VAR) return varRef == other.varRef;
        if (kind == ADD || kind == SUB || kind == MUL || kind == DIV || kind == BITAND || kind == MOD)
            return left && other.left && right && other.right &&
                   left->equals(*other.left) && right->equals(*other.right);
        return false;
    }

    // Get the guaranteed upper bound from bitmask/modulo patterns
    // BITAND: x & N where N >= 0: result is in [0, N]
    // MOD: x % N where N > 0: result for unsigned is in [0, N-1]
    std::optional<int64_t> upperBound() const {
        if (kind == CONST && constVal >= 0) {
            return constVal;
        }
        if (kind == BITAND && right && right->kind == CONST && right->constVal >= 0) {
            return right->constVal;
        }
        if (kind == MOD && right && right->kind == CONST && right->constVal > 0) {
            return right->constVal - 1;
        }
        return std::nullopt;
    }

    // Check if this value is guaranteed non-negative from pattern analysis
    bool isGuaranteedNonNegative() const {
        if (kind == CONST && constVal >= 0) return true;
        if (kind == BITAND && right && right->kind == CONST && right->constVal >= 0) return true;
        if (kind == MOD && right && right->kind == CONST && right->constVal > 0) return true;
        return false;
    }
};

// ============================================================
// FIELD STATE WITH TRACKING PROVENANCE
// ============================================================

// Provenance for lost tracking (Category C) - where and why tracking was lost
struct LossProvenance {
    SourceLocation location;
    std::string reason;  // e.g., "unknown call to process()"
};

// Provenance for null pointers - tracks why a pointer's nullability is unknown
// This helps users understand the root cause when a null dereference error is reported
struct NullProvenance {
    SourceLocation location;     // Where the pointer was declared/assigned
    std::string reason;          // e.g., "parameter may be NULL (caller could pass NULL)"
};

// Map from pointer path to its null provenance (for enhanced diagnostics)
std::map<std::string, NullProvenance> NullProvenanceMap;

// Extended field state with tracking provenance
struct FieldState {
    SymValue value;
    TrackingState state = TrackingState::Untracked;
    std::optional<LossProvenance> lossInfo;  // Set when state becomes UnknownLost
    bool warnedInScope = false;              // Warn-once flag per scope

    // Convenience constructors
    static FieldState Known(SymValue v) {
        FieldState fs;
        fs.value = v;
        fs.state = TrackingState::Known;
        return fs;
    }

    static FieldState Lost(SourceLocation loc, const std::string& reason) {
        FieldState fs;
        fs.value = SymValue::Unknown();
        fs.state = TrackingState::UnknownLost;
        fs.lossInfo = LossProvenance{loc, reason};
        return fs;
    }

    static FieldState SolverFailed() {
        FieldState fs;
        fs.value = SymValue::Unknown();
        fs.state = TrackingState::UnknownSolver;
        return fs;
    }
};

// Map from "varPath.field" to extended state
std::map<std::string, FieldState> FieldStates;

// Track which (varPath, field) combinations have already warned in current scope
std::set<std::pair<std::string, std::string>> WarnedLostTracking;

// ============================================================
// CONSTRAINTS
// ============================================================

struct Constraint {
    enum Op { GT, GE, LT, LE, EQ, NE, AND, OR } op;
    std::string field;
    int64_t rhs = 0;
    bool isMultiArg = false;
    std::string lhsArg, rhsArg, rhsField;
    std::shared_ptr<Constraint> left, right;
    // For arg0 < _size constraints
    bool isArgVsField = false;
    int argIdx = 0;
    std::string rhsFieldName;  // The _size part
    // For arbitrary expression constraints like "arg0 + arg1 < _size"
    bool isExprConstraint = false;
    SymValue lhsExpr, rhsExpr;
    
    std::string str() const {
        if (op == AND) return "(" + left->str() + " && " + right->str() + ")";
        if (op == OR) return "(" + left->str() + " || " + right->str() + ")";
        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
        if (isExprConstraint) return lhsExpr.str() + " " + ops[op] + " " + rhsExpr.str();
        if (isMultiArg) return lhsArg + "." + field + " " + ops[op] + " " + rhsArg + "." + rhsField;
        if (isArgVsField) {
            if (rhsFieldName.empty() && !field.empty()) {
                // argN._field > constant format
                return "arg" + std::to_string(argIdx) + "." + field + " " + ops[op] + " " + std::to_string(rhs);
            }
            return "arg" + std::to_string(argIdx) + " " + ops[op] + " " + rhsFieldName;
        }
        return field + " " + ops[op] + " " + std::to_string(rhs);
    }
    
};

auto trim = [](std::string& str) {
    while (!str.empty() && str.front() == ' ') str = str.substr(1);
    while (!str.empty() && str.back() == ' ') str.pop_back();
};

// ============================================================
// EXPRESSION PARSER (for arbitrary arithmetic constraints)
// ============================================================

// Forward declaration
SymValue parseExprAddSub(const std::string& s, size_t& pos);

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool isAlnum(char c) { return isDigit(c) || isAlpha(c); }

void skipSpaces(const std::string& s, size_t& pos) {
    while (pos < s.size() && s[pos] == ' ') pos++;
}

// Parse a single term: number, argN, _field, old(_field)
SymValue parseExprAtom(const std::string& s, size_t& pos) {
    skipSpaces(s, pos);
    if (pos >= s.size()) return SymValue::Unknown();
    
    // Parenthesized expression
    if (s[pos] == '(') {
        pos++;  // skip '('
        SymValue inner = parseExprAddSub(s, pos);
        skipSpaces(s, pos);
        if (pos < s.size() && s[pos] == ')') pos++;  // skip ')'
        return inner;
    }
    
    // Number (possibly negative)
    if (isDigit(s[pos]) || (s[pos] == '-' && pos + 1 < s.size() && isDigit(s[pos + 1]))) {
        size_t start = pos;
        if (s[pos] == '-') pos++;
        while (pos < s.size() && isDigit(s[pos])) pos++;
        auto val = safeStoll(s.substr(start, pos - start));
        return val ? SymValue::Const(*val) : SymValue::Unknown();
    }
    
    // old(_field)
    if (s.substr(pos, 4) == "old(") {
        pos += 4;
        size_t start = pos;
        while (pos < s.size() && s[pos] != ')') pos++;
        std::string field = s.substr(start, pos - start);
        trim(field);
        if (pos < s.size()) pos++;  // skip ')'
        return SymValue::Var("old:" + field);
    }
    
    // Identifier: argN, _field, or just a name
    if (isAlpha(s[pos])) {
        size_t start = pos;
        while (pos < s.size() && (isAlnum(s[pos]) || s[pos] == '.')) pos++;
        std::string name = s.substr(start, pos - start);
        return SymValue::Var(name);
    }
    
    return SymValue::Unknown();
}

// Parse multiplication and division
SymValue parseExprMulDiv(const std::string& s, size_t& pos) {
    SymValue left = parseExprAtom(s, pos);
    
    while (true) {
        skipSpaces(s, pos);
        if (pos >= s.size()) break;
        if (s[pos] == '*') {
            pos++;
            SymValue right = parseExprAtom(s, pos);
            left = SymValue::Mul(left, right);
        } else if (s[pos] == '/') {
            pos++;
            SymValue right = parseExprAtom(s, pos);
            left = SymValue::Div(left, right);
        } else {
            break;
        }
    }
    return left;
}

// Parse addition/subtraction
SymValue parseExprAddSub(const std::string& s, size_t& pos) {
    SymValue left = parseExprMulDiv(s, pos);
    
    while (true) {
        skipSpaces(s, pos);
        if (pos >= s.size()) break;
        if (s[pos] == '+') {
            pos++;
            SymValue right = parseExprMulDiv(s, pos);
            left = SymValue::Add(left, right);
        } else if (s[pos] == '-') {
            pos++;
            SymValue right = parseExprMulDiv(s, pos);
            left = SymValue::Sub(left, right);
        } else {
            break;
        }
    }
    return left;
}

SymValue parseExpr(const std::string& s) {
    size_t pos = 0;
    return parseExprAddSub(s, pos);
}

// Check if string contains arithmetic operators (outside of old())
bool hasArithmeticOps(const std::string& s) {
    int parenDepth = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '(') parenDepth++;
        else if (s[i] == ')') parenDepth--;
        else if (parenDepth == 0) {
            if (s[i] == '+' || s[i] == '*' || s[i] == '/') return true;
            // '-' is subtraction if preceded by alphanumeric, ')', or '_' (skip spaces)
            // Otherwise it's negation (e.g., -5)
            if (s[i] == '-' && i > 0) {
                // Find previous non-space character
                size_t j = i - 1;
                while (j > 0 && std::isspace(s[j])) j--;
                char prev = s[j];
                if (std::isalnum(prev) || prev == ')' || prev == '_') return true;
            }
        }
    }
    return false;
}

// ============================================================
// CONSTRAINT PARSING
// ============================================================

// Translate parameter names to argN format using function declaration
// e.g., "buf._size >= n" -> "arg0._size >= arg1" for process(int* buf, size_t n)
std::string translateParamNames(const std::string& s, const FunctionDecl* FD) {
    if (!FD) return s;

    std::string result = s;
    for (unsigned i = 0; i < FD->getNumParams(); i++) {
        std::string paramName = FD->getParamDecl(i)->getNameAsString();
        if (paramName.empty()) continue;

        std::string argN = "arg" + std::to_string(i);

        // Replace param._field with argN._field
        size_t pos = 0;
        while ((pos = result.find(paramName, pos)) != std::string::npos) {
            // Check this is a complete token (not part of a longer word)
            bool startOK = (pos == 0 || !std::isalnum(result[pos - 1]));
            bool endOK = (pos + paramName.size() >= result.size() ||
                          (!std::isalnum(result[pos + paramName.size()]) &&
                           result[pos + paramName.size()] != '_'));

            if (startOK && endOK) {
                result = result.substr(0, pos) + argN + result.substr(pos + paramName.size());
                pos += argN.size();
            } else {
                pos++;
            }
        }
    }
    return result;
}

std::optional<Constraint> parseSimpleConstraint(const std::string& s) {
    Constraint c;
    size_t pos; int opLen = 1;
    
    // First find the comparison operator
    size_t opPos = std::string::npos;
    Constraint::Op foundOp;
    if ((opPos = s.find(">=")) != std::string::npos) { foundOp = Constraint::GE; opLen = 2; }
    else if ((opPos = s.find("<=")) != std::string::npos) { foundOp = Constraint::LE; opLen = 2; }
    else if ((opPos = s.find("==")) != std::string::npos) { foundOp = Constraint::EQ; opLen = 2; }
    else if ((opPos = s.find("!=")) != std::string::npos) { foundOp = Constraint::NE; opLen = 2; }
    else if ((opPos = s.find(">")) != std::string::npos) { foundOp = Constraint::GT; opLen = 1; }
    else if ((opPos = s.find("<")) != std::string::npos) { foundOp = Constraint::LT; opLen = 1; }
    else return std::nullopt;
    
    std::string lhsStr = s.substr(0, opPos);
    std::string rhsStr = s.substr(opPos + opLen);
    trim(lhsStr); trim(rhsStr);
    
    // Check for arithmetic expressions (e.g., "arg0 + arg1 < _size")
    if (hasArithmeticOps(lhsStr) || hasArithmeticOps(rhsStr) ||
        (lhsStr.find("arg") != std::string::npos && lhsStr.find('+') != std::string::npos)) {
        c.op = foundOp;
        c.isExprConstraint = true;
        c.lhsExpr = parseExpr(lhsStr);
        c.rhsExpr = parseExpr(rhsStr);
        return c;
    }
    
    // Existing arg handling
    if (lhsStr.find("arg") == 0) {
        c.op = foundOp;
        
        if (rhsStr.find("arg") == 0 && rhsStr.find('.') != std::string::npos) {
            c.isMultiArg = true;
            auto lhsDot = lhsStr.find('.');
            if (lhsDot != std::string::npos) { c.lhsArg = lhsStr.substr(0, lhsDot); c.field = lhsStr.substr(lhsDot + 1); }
            auto rhsDot = rhsStr.find('.');
            if (rhsDot != std::string::npos) { c.rhsArg = rhsStr.substr(0, rhsDot); c.rhsField = rhsStr.substr(rhsDot + 1); }
            return c;
        }

        // Handle "arg0._size >= arg1" format (parameter field vs parameter value)
        if (lhsStr.find('.') != std::string::npos && rhsStr.find("arg") == 0 && rhsStr.find('.') == std::string::npos) {
            c.isExprConstraint = true;
            c.lhsExpr = parseExpr(lhsStr);
            c.rhsExpr = parseExpr(rhsStr);
            return c;
        }

        // Handle "arg0 < _size" or "arg0 < @property" format (argument vs this-field)
        if (!rhsStr.empty() && (rhsStr[0] == '_' || rhsStr[0] == '@')) {
            c.isArgVsField = true;
            c.argIdx = std::atoi(lhsStr.c_str() + 3);  // Extract N from "argN"
            c.rhsFieldName = rhsStr;
            return c;
        }

        // Handle "argN._field > constant" format (argument's field vs constant)
        // e.g., "arg1._size > 0" means check arg1's _size property against 0
        auto lhsDot = lhsStr.find('.');
        if (lhsDot != std::string::npos) {
            std::string argPart = lhsStr.substr(0, lhsDot);  // "arg1"
            std::string fieldPart = lhsStr.substr(lhsDot + 1);  // "_size"
            auto rhsVal = safeStoll(rhsStr);
            if (rhsVal) {
                c.isArgVsField = true;
                c.argIdx = std::atoi(argPart.c_str() + 3);  // Extract N from "argN"
                c.field = fieldPart;  // The field to check on that argument
                c.rhs = *rhsVal;
                c.rhsFieldName = "";  // Not comparing against another field
                return c;
            }
        }
    }
    
    // Handle nullptr constraints: "arg0 != nullptr", "ptr != nullptr", "_result != nullptr"
    if (rhsStr == "nullptr" || rhsStr == "NULL" || rhsStr == "0") {
        // Check if LHS looks like a pointer constraint
        // Patterns: "arg0", "arg0.field", "ptr", "_result"
        bool isNullptrConstraint = (rhsStr == "nullptr" || rhsStr == "NULL");

        if (isNullptrConstraint || (rhsStr == "0" && (foundOp == Constraint::NE || foundOp == Constraint::EQ))) {
            c.op = foundOp;
            c.rhs = 0;

            // Handle "arg0 != nullptr" - check arg0._nonnull != 0
            // This will be checked against the argument's _nonnull property
            if (lhsStr.find("arg") == 0 && lhsStr.find('.') == std::string::npos) {
                // Mark this as a special nullptr constraint on an argument
                // The field to check is _nonnull on the argument
                c.field = "_nonnull";
                // Store which argument index this applies to
                c.isArgVsField = true;
                c.argIdx = std::atoi(lhsStr.c_str() + 3);
                c.rhsFieldName = "0";  // Marker for null/zero constraint
                return c;
            }

            // Handle "_result != nullptr" for return value tracking
            if (lhsStr == "_result") {
                c.field = "_nonnull";
                return c;
            }

            // Handle simple pointer variable: "ptr != nullptr"
            c.field = "_nonnull";
            return c;
        }
    }

    // Simple constraint: field op constant
    c.op = foundOp;
    c.field = lhsStr;
    auto rhsVal = safeStoll(rhsStr);
    if (!rhsVal) return std::nullopt;
    c.rhs = *rhsVal;
    return c;
}

std::optional<Constraint> parseConstraint(const std::string& s) {
    size_t andPos = s.find("&&");
    if (andPos != std::string::npos) {
        auto left = parseConstraint(s.substr(0, andPos));
        auto right = parseConstraint(s.substr(andPos + 2));
        if (left && right) {
            Constraint c; c.op = Constraint::AND;
            c.left = std::make_shared<Constraint>(*left);
            c.right = std::make_shared<Constraint>(*right);
            return c;
        }
    }
    size_t orPos = s.find("||");
    if (orPos != std::string::npos) {
        auto left = parseConstraint(s.substr(0, orPos));
        auto right = parseConstraint(s.substr(orPos + 2));
        if (left && right) {
            Constraint c; c.op = Constraint::OR;
            c.left = std::make_shared<Constraint>(*left);
            c.right = std::make_shared<Constraint>(*right);
            return c;
        }
    }
    return parseSimpleConstraint(s);
}

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
    Constraint::Op op;
    int64_t rhs;
    bool onTrue;
};

std::optional<std::string> parseOld(const std::string& s) {
    if (s.rfind("old(", 0) != 0) return std::nullopt;
    if (s.back() != ')') return std::nullopt;
    return s.substr(4, s.size() - 5);
}

std::optional<Postcondition> parsePostcondition(const std::string& s, bool forMethod = false) {
    auto eqPos = s.find("==");
    if (eqPos == std::string::npos) eqPos = s.find("=");
    if (eqPos == std::string::npos) return std::nullopt;
    
    int skip = (s.size() > eqPos + 1 && s[eqPos + 1] == '=') ? 2 : 1;
    std::string lhs = s.substr(0, eqPos);
    std::string rhs = s.substr(eqPos + skip);
    trim(lhs); trim(rhs);
    
    Postcondition p;
    
    // For methods: "_size" means this->_size, stored as just "_size"
    // For functions: "result.field" 
    if (forMethod) {
        if (lhs.rfind("result.", 0) == 0) p.resultField = lhs.substr(7);
        else if (!lhs.empty() && lhs[0] == '_') p.resultField = lhs;  // _size -> this->_size
        else p.resultField = lhs;
    } else {
        if (lhs.rfind("result.", 0) == 0) p.resultField = lhs.substr(7);
        else return std::nullopt;
    }
    
    // Check for old() references  
    if (rhs.find("old(") != std::string::npos) {
        p.usesOld = true;
    }
    
    // Use expression parser for all RHS expressions
    // This handles: arg0, arg0._size, arg0._size + arg1._size, 5, etc.
    p.expr = parseExpr(rhs);
    return p;
}

std::optional<BoolPostcondition> parseBoolPostcondition(const std::string& s, bool onTrue) {
    BoolPostcondition bp;
    bp.onTrue = onTrue;
    
    size_t opPos = std::string::npos;
    int opLen = 1;
    
    if ((opPos = s.find("==")) != std::string::npos) { bp.op = Constraint::EQ; opLen = 2; }
    else if ((opPos = s.find("!=")) != std::string::npos) { bp.op = Constraint::NE; opLen = 2; }
    else if ((opPos = s.find(">=")) != std::string::npos) { bp.op = Constraint::GE; opLen = 2; }
    else if ((opPos = s.find("<=")) != std::string::npos) { bp.op = Constraint::LE; opLen = 2; }
    else if ((opPos = s.find(">")) != std::string::npos) { bp.op = Constraint::GT; opLen = 1; }
    else if ((opPos = s.find("<")) != std::string::npos) { bp.op = Constraint::LT; opLen = 1; }
    else return std::nullopt;
    
    std::string lhs = s.substr(0, opPos);
    std::string rhs = s.substr(opPos + opLen);
    trim(lhs); trim(rhs);
    
    auto dotPos = lhs.find('.');
    if (dotPos != std::string::npos) {
        bp.argRef = lhs.substr(0, dotPos);
        bp.field = lhs.substr(dotPos + 1);
    } else if (lhs[0] == '_' || lhs[0] == '@') {
        // Explicit property name (underscore or ghost state)
        bp.argRef = "this";
        bp.field = lhs;
    } else {
        // Legacy: bare name like "size" becomes "_size"
        bp.argRef = "this";
        bp.field = "_" + lhs;
    }
    auto rhsVal = safeStoll(rhs);
    if (!rhsVal) return std::nullopt;
    bp.rhs = *rhsVal;
    return bp;
}

// ============================================================
// ENVIRONMENT
// ============================================================

std::map<std::string, SymValue> Env;
std::map<std::string, std::vector<Postcondition>> FnPostconditions;
std::map<std::string, std::vector<BoolPostcondition>> FnBoolPostconditions;
std::set<std::string> TotalFunctions;
// Track pointer aliases: alias -> source (e.g., "alias" -> "ptr" when alias = ptr)
std::map<std::string, std::string> PointerAliases;

// ============================================================
// FUNCTION GHOST STATE (@ prefix)
// ============================================================
// Two-pass bidirectional analysis for automatic function nullability inference:
// - Forward: Call sites → Parameter nullability (@paramN_nonnull)
// - Backward: Return statements → Return nullability (@returns_nonnull)

// Call site information collected in Pass 1
struct CallSiteInfo {
    const CallExpr* callExpr;
    const FunctionDecl* callerFunc;
    SourceLocation loc;
};

// Return statement with guard information (for conditional nullability)
struct GuardedReturn {
    const ReturnStmt* stmt;
    // If this return is guarded by a null check on a parameter, record which one
    // e.g., "if (!param) return nullptr;" -> guardedByParamIdx = index of param
    std::optional<unsigned> guardedByParamNullCheck;  // Returns nullptr if param IS null
    bool returnsNullptr = false;  // True if this return statement returns nullptr
};

// Per-function analysis data
struct FunctionAnalysisData {
    const FunctionDecl* decl = nullptr;

    // Forward analysis: call sites where this function is called
    std::vector<CallSiteInfo> callSites;

    // Backward analysis: return statements in this function
    std::vector<GuardedReturn> returnStmts;

    // Variable nullability tracking (Gap 1)
    // Maps variable name -> nonnull status within this function
    std::map<std::string, std::optional<bool>> varNullability;

    // Computed results (populated between passes)
    bool analyzed = false;
    bool returnsComputed = false;  // Track if returns were computed (for chaining)
    std::map<unsigned, std::optional<bool>> paramNonnull;  // paramIdx -> nonnull?
    std::optional<bool> returnsNonnull;                     // all returns nonnull?

    // Call-site analysis: track which params are dereferenced (for static functions)
    std::set<unsigned> paramDereferenced;  // param indices that are dereferenced in body
};

// Storage: qualified function name -> analysis data
std::map<std::string, FunctionAnalysisData> FunctionAnalysis;

// Helper to get function key (qualified name for uniqueness)
std::string getFunctionKey(const FunctionDecl* F) {
    if (!F) return "";
    return F->getQualifiedNameAsString();
}

// Helper to check if function has internal linkage (all call sites known)
// Static functions, anonymous namespace functions, and private static methods
bool hasInternalLinkage(const FunctionDecl* F) {
    if (!F) return false;
    // Explicit 'static' storage class
    if (F->getStorageClass() == SC_Static) return true;
    // Functions in anonymous namespace have internal linkage
    if (F->isInAnonymousNamespace()) return true;
    return false;
}

// Helper: get parameter index if path refers to a function parameter, else -1
int getParamIndex(const FunctionDecl* F, const std::string& path) {
    if (!F) return -1;
    for (unsigned i = 0; i < F->getNumParams(); i++) {
        if (F->getParamDecl(i)->getNameAsString() == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Method annotations storage
struct MethodInfo {
    std::vector<Constraint> preconditions;  // C++20: 'requires' is a reserved keyword
    std::vector<Postcondition> ensures;
    std::string returns;  // dtt:returns=_size
    std::vector<BoolPostcondition> boolPostconds;
};
std::map<std::string, MethodInfo> MethodAnnotations;  // Key: "ClassName::methodName"

// Register synthetic annotations for std:: types that we can't annotate directly
void registerSyntheticAnnotations() {
    // Helper to create "arg0 < _size" constraint
    auto makeBoundsConstraint = []() {
        Constraint c;
        c.op = Constraint::LT;
        c.isArgVsField = true;
        c.argIdx = 0;
        c.rhsFieldName = "_size";
        return c;
    };
    
    // std::array<T, N>::operator[](size_t)
    {
        MethodInfo info;
        info.preconditions.push_back(makeBoundsConstraint());
        MethodAnnotations["array::operator[]/1"] = info;
    }
    
    // std::span<T, N>::operator[](size_t)
    {
        MethodInfo info;
        info.preconditions.push_back(makeBoundsConstraint());
        MethodAnnotations["span::operator[]/1"] = info;
    }
    
    // std::bitset<N>::operator[](size_t)
    {
        MethodInfo info;
        info.preconditions.push_back(makeBoundsConstraint());
        MethodAnnotations["bitset::operator[]/1"] = info;
    }
}

// Variable bounds proofs
// Key: "varName:objPath:_size" -> Op (LT means var < obj._size)
struct VarBoundProof {
    std::string varName;
    std::string objPath;
    std::string field;
    Constraint::Op op;
};
std::vector<VarBoundProof> VarBoundProofs;

void addVarBoundProof(const std::string& var, const std::string& obj, const std::string& field, Constraint::Op op) {
    VarBoundProofs.push_back({var, obj, field, op});
}

void removeVarBoundProof(const std::string& var, const std::string& obj, const std::string& field) {
    VarBoundProofs.erase(
        std::remove_if(VarBoundProofs.begin(), VarBoundProofs.end(),
            [&](const VarBoundProof& p) { return p.varName == var && p.objPath == obj && p.field == field; }),
        VarBoundProofs.end());
}

bool hasVarBoundProof(const std::string& var, const std::string& obj, const std::string& field, Constraint::Op op) {
    for (const auto& p : VarBoundProofs) {
        if (p.varName == var && p.objPath == obj && p.field == field) {
            // Check if existing proof implies the needed one
            if (p.op == op) return true;
            if (p.op == Constraint::LT && op == Constraint::LE) return true;  // < implies <=
        }
    }
    return false;
}

// Clear all per-function analysis state (called at function entry)
void clearPerFunctionState() {
    Env.clear();
    VarBoundProofs.clear();
    FieldStates.clear();
    WarnedLostTracking.clear();
    NullProvenanceMap.clear();
    PointerLifetimes.clear();
}

void setField(const std::string& basePath, const std::string& field, SymValue v) {
    std::string key = basePath + "." + field;
    Env[key] = v;

    // Update FieldStates to mark this field as Known (restores tracking)
    FieldStates[key] = FieldState::Known(v);
    // Clear any "already warned" flag since tracking is restored
    WarnedLostTracking.erase({basePath, field});

    // Invalidate VarBoundProofs that reference this field
    // e.g., if v._size changes, "i < v._size" proofs are no longer valid
    VarBoundProofs.erase(
        std::remove_if(VarBoundProofs.begin(), VarBoundProofs.end(),
            [&](const VarBoundProof& p) {
                return p.objPath == basePath && p.field == field;
            }),
        VarBoundProofs.end());
}

SymValue getField(const std::string& basePath, const std::string& field) {
    std::string key = basePath + "." + field;
    auto it = Env.find(key);
    return (it != Env.end()) ? it->second : SymValue::Unknown();
}

// Get the tracking state for a field (for Category C diagnostics)
TrackingState getFieldTrackingState(const std::string& basePath, const std::string& field) {
    std::string key = basePath + "." + field;
    auto it = FieldStates.find(key);
    if (it != FieldStates.end()) {
        return it->second.state;
    }
    // Check if field exists in Env but not in FieldStates (legacy)
    if (Env.find(key) != Env.end()) {
        return TrackingState::Known;
    }
    return TrackingState::Untracked;
}

// Get the loss provenance for a field (for Category C note)
std::optional<LossProvenance> getFieldLossProvenance(const std::string& basePath, const std::string& field) {
    std::string key = basePath + "." + field;
    auto it = FieldStates.find(key);
    if (it != FieldStates.end() && it->second.state == TrackingState::UnknownLost) {
        return it->second.lossInfo;
    }
    return std::nullopt;
}

// Check if we've already warned about lost tracking for this field (warn-once per scope)
bool hasWarnedLostTracking(const std::string& basePath, const std::string& field) {
    return WarnedLostTracking.count({basePath, field}) > 0;
}

// Mark that we've warned about lost tracking for this field
void markWarnedLostTracking(const std::string& basePath, const std::string& field) {
    WarnedLostTracking.insert({basePath, field});
}

void addProof(const std::string& basePath, const std::string& proofKey) {
    Env[basePath + "._proof_" + proofKey] = SymValue::Const(1);
}

void invalidateProofs(const std::string& basePath, const std::string& field) {
    std::vector<std::string> toRemove;
    for (auto& kv : Env)
        if (kv.first.rfind(basePath + "._proof_" + field, 0) == 0)
            toRemove.push_back(kv.first);
    for (auto& k : toRemove) Env.erase(k);
}

// Check if there are any proofs about a field (even without a concrete value)
bool hasAnyProofs(const std::string& basePath, const std::string& field) {
    std::string proofPrefix = basePath + "._proof_" + field;
    for (const auto& kv : Env) {
        if (kv.first.rfind(proofPrefix, 0) == 0) return true;
    }
    return false;
}

// Invalidate all nullability information for a pointer
// This clears the _nonnull value and associated proofs
void invalidateNullability(const std::string& ptrPath) {
    if (ptrPath.empty()) return;
    Env.erase(ptrPath + "._nonnull");
    Env.erase(ptrPath + "._proof__nonnull!=0");
    Env.erase(ptrPath + "._proof__nonnull==0");
}

void invalidateArrayElements(const std::string& arrayBase, const std::string& field) {
    std::vector<std::string> toRemove;
    std::string pattern = arrayBase + "[";
    for (auto& kv : Env) {
        if (kv.first.rfind(pattern, 0) == 0) {
            if (kv.first.find("." + field) != std::string::npos ||
                kv.first.find("._proof_" + field) != std::string::npos) {
                toRemove.push_back(kv.first);
            }
        }
    }
    for (auto& k : toRemove) Env.erase(k);
}

// Invalidate all tracked properties for a given variable path with provenance
// Called when variable is passed by non-const reference to function without postcondition
// When provenance is provided, tracking state becomes UnknownLost (Category C)
void invalidatePropertiesWithProvenance(const std::string& path, SourceLocation loc, const std::string& reason) {
    if (path.empty()) return;

    // Find all Env entries that start with "path." or equal "path"
    std::vector<std::string> toUpdate;
    for (const auto& kv : Env) {
        if (kv.first == path || kv.first.rfind(path + ".", 0) == 0) {
            toUpdate.push_back(kv.first);
        }
    }

    // Mark each field as UnknownLost with provenance (Category C)
    for (const auto& key : toUpdate) {
        // Only record provenance for actual fields (not proofs)
        if (key.find("._proof_") == std::string::npos) {
            FieldStates[key] = FieldState::Lost(loc, reason);
        }
        Env.erase(key);
    }

    // Also remove any VarBoundProofs involving this path
    VarBoundProofs.erase(
        std::remove_if(VarBoundProofs.begin(), VarBoundProofs.end(),
            [&](const VarBoundProof& p) {
                return p.objPath == path || p.varName == path;
            }),
        VarBoundProofs.end());
}

// Backward-compatible version without provenance
void invalidateProperties(const std::string& path) {
    if (path.empty()) return;

    // Find and remove all Env entries that start with "path." or equal "path"
    std::vector<std::string> toRemove;
    for (const auto& kv : Env) {
        if (kv.first == path || kv.first.rfind(path + ".", 0) == 0) {
            toRemove.push_back(kv.first);
        }
    }
    for (const auto& key : toRemove) {
        Env.erase(key);
        // Also clear from FieldStates since we don't have provenance
        FieldStates.erase(key);
    }

    // Also remove any VarBoundProofs involving this path
    VarBoundProofs.erase(
        std::remove_if(VarBoundProofs.begin(), VarBoundProofs.end(),
            [&](const VarBoundProof& p) {
                return p.objPath == path || p.varName == path;
            }),
        VarBoundProofs.end());
}

// Check if a function has ANY DTT annotations (requires, ensures, etc.)
// If a function has any annotations, assume the user knows what it does - don't invalidate
bool hasDTTAnnotations(FunctionDecl* FD) {
    if (!FD) return false;

    // Check for any dtt: annotations on the function
    for (auto *A : FD->attrs()) {
        if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
            if (Ann->getAnnotation().str().rfind("dtt:", 0) == 0) {
                return true;
            }
        }
    }

    return false;
}

// Check if a function has a postcondition that sets properties on argIdx
bool hasPostconditionForArg(FunctionDecl* FD, unsigned argIdx) {
    if (!FD) return false;
    std::string fn = FD->getNameAsString();

    // Check FnPostconditions for this function
    auto it = FnPostconditions.find(fn);
    if (it != FnPostconditions.end() && !it->second.empty()) {
        // For void functions, postconditions use "result.xxx" format but apply to arg0
        // So any postcondition on a void function means arg0 is handled
        if (argIdx == 0 && FD->getReturnType()->isVoidType()) {
            return true;
        }

        // Check for argN.xxx style postconditions (future-proof)
        for (const auto& post : it->second) {
            std::string argPrefix = "arg" + std::to_string(argIdx);
            if (post.resultField == argPrefix || post.resultField.rfind(argPrefix + ".", 0) == 0) {
                return true;
            }
        }
    }

    return false;
}

// Check if a parameter type can be used to modify the argument variable itself
// Returns true for references (T&, T&&) which can rebind/modify the argument.
// Returns false for pointers (T*) - see shouldInvalidateForPointerParam for that case.
bool isModifiableRef(QualType paramType) {
    // Check for non-const lvalue reference
    if (paramType->isLValueReferenceType()) {
        QualType pointee = paramType->getPointeeType();
        return !pointee.isConstQualified();
    }

    // Check for non-const rvalue reference (T&&)
    // Rvalue refs can modify the argument (e.g., move semantics, sink parameters)
    if (paramType->isRValueReferenceType()) {
        QualType pointee = paramType->getPointeeType();
        return !pointee.isConstQualified();
    }

    // Pointers (T*) are passed by value - the pointer variable itself can't change.
    // However, the pointed-to object CAN be modified. This is handled separately
    // by shouldInvalidateForPointerParam which checks if the argument is &obj.

    // Pass by value - doesn't modify original
    return false;
}

// Check if a pointer parameter should trigger invalidation based on the argument.
// For f(T* ptr):
// - If argument is &obj (address-of): invalidate obj (function can modify *ptr)
// - If argument is a pointer variable p: DON'T invalidate p (p is copied by value)
//
// This distinction matters because:
// - Passing &obj gives the function access to modify obj's fields via ptr->field
// - Passing p by value means the function gets a COPY of the pointer; local p unchanged
// - Memory lifetime (delete/free) is a separate concern (dangling, not nullability)
bool shouldInvalidateForPointerParam(QualType paramType, Expr* Arg) {
    if (!paramType->isPointerType()) return false;

    // Check if pointee is const - const T* can't modify the object
    QualType pointee = paramType->getPointeeType();
    if (pointee.isConstQualified()) return false;

    // Check if argument is address-of expression (&obj)
    // In this case, the function can modify obj via ptr->field = x
    if (auto* UO = dyn_cast<UnaryOperator>(Arg)) {
        if (UO->getOpcode() == UO_AddrOf) {
            return true;  // &obj passed to T* - invalidate obj
        }
    }

    // Argument is a pointer variable (or other expression) - don't invalidate
    // The pointer is passed by value; the local variable can't change
    return false;
}

// Convert a simple constraint to a proof key string
// For constraint "_size > 0" returns "_size>0"
std::string constraintToProofKey(const Constraint& c) {
    if (c.op == Constraint::AND || c.op == Constraint::OR) {
        // Compound constraints not supported as single proof keys
        return "";
    }
    const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
    return c.field + ops[c.op] + std::to_string(c.rhs);
}

// Inject a requires constraint as an assumption inside the function body
// This implements "contract propagation" - preconditions become assumptions
void injectRequiresAsAssumption(FunctionDecl *F, const Constraint& c) {
    // Handle compound constraints recursively
    if (c.op == Constraint::AND) {
        if (c.left) injectRequiresAsAssumption(F, *c.left);
        if (c.right) injectRequiresAsAssumption(F, *c.right);
        return;
    }
    if (c.op == Constraint::OR) {
        // OR constraints require case-split reasoning, which is not yet implemented.
        // To properly handle (A || B), we'd need to:
        //   1. Track disjunctive states (exponential blowup with N disjuncts)
        //   2. Implement case analysis: "in branch where A holds, X is satisfied"
        //   3. Merge results from different branches
        //
        // Current behavior: OR constraints are skipped during propagation.
        // Call-site checking still works (caller must prove the disjunction).
        // Inside the function body, neither disjunct is available as a proof.
        // See examples/201_xfail_or_constraint_prop.cpp for documentation.
        return;
    }

    // Determine the base path for constraint resolution.
    // This identifies which object the constraint's fields refer to:
    //   - Instance methods: fields like _size refer to 'this' object
    //   - Free functions/static methods: fields refer to a parameter (typically arg0)
    std::string basePath;

    if (auto *M = dyn_cast<CXXMethodDecl>(F)) {
        if (!M->isStatic()) {
            // Instance method: constraints reference 'this' object.
            // Inside the method body, member accesses resolve against 'this'.
            basePath = "this";
        }
        // Static methods fall through to parameter-based handling below,
        // since they have no implicit 'this' object.
    }

    // For free functions and static methods, constraints reference parameters.
    // Default to the first parameter as the base object.
    if (basePath.empty() && F->getNumParams() > 0) {
        basePath = F->getParamDecl(0)->getNameAsString();
    }

    if (basePath.empty()) {
        // No base path available - can't propagate constraints.
        // This happens for parameterless static methods or functions.
        return;
    }

    // Handle different constraint types
    if (c.isArgVsField) {
        // arg0 < _size type constraint - need special handling
        // This compares a parameter value to an object field
        // For methods: _size refers to this._size (basePath = "this")
        // For free functions: _size refers to arg1._size (the object parameter)
        if (c.argIdx < (int)F->getNumParams()) {
            std::string argName = F->getParamDecl(c.argIdx)->getNameAsString();
            std::string objPath = basePath;
            // For free functions, _size is on the second parameter (arg1), not arg0
            if (!isa<CXXMethodDecl>(F) && F->getNumParams() > 1) {
                objPath = F->getParamDecl(1)->getNameAsString();
            }
            // Normalize field name to ensure underscore prefix for consistency.
            // VarBoundProof lookups in hasVarBoundProof() expect "_fieldname" format.
            // The parsing code should always include the underscore, but we normalize
            // here to be defensive against future changes to constraint construction.
            std::string fieldName = c.rhsFieldName;
            if (!fieldName.empty() && fieldName[0] != '_' && fieldName[0] != '@') {
                fieldName = "_" + fieldName;
            }
            VarBoundProofs.push_back({argName, objPath, fieldName, c.op});
        }
        return;
    }

    if (c.isMultiArg || c.isExprConstraint) {
        // Complex constraints - would need more sophisticated tracking
        // For now, skip these (they still work via call-site checking)
        return;
    }

    // Simple constraint: _field op value
    std::string proofKey = constraintToProofKey(c);
    if (!proofKey.empty()) {
        addProof(basePath, proofKey);
    }
}

void copyProperties(const std::string& toPath, const std::string& fromPath) {
    std::vector<std::pair<std::string, SymValue>> toCopy;
    for (auto& kv : Env) if (kv.first.rfind(fromPath + ".", 0) == 0) {
        std::string suffix = kv.first.substr(fromPath.size());
        toCopy.push_back({toPath + suffix, kv.second});
    }
    for (auto& p : toCopy) Env[p.first] = p.second;
}

// Clear all properties for a path (for assignment LHS)
void clearProperties(const std::string& path) {
    std::vector<std::string> toRemove;
    for (auto& kv : Env) {
        if (kv.first.rfind(path + ".", 0) == 0) {
            toRemove.push_back(kv.first);
        }
    }
    for (auto& k : toRemove) Env.erase(k);
}

// Exchange properties between two paths (for swap)
void swapProperties(const std::string& path1, const std::string& path2) {
    // Collect properties from both paths
    std::map<std::string, SymValue> props1, props2;
    
    for (auto& kv : Env) {
        if (kv.first.rfind(path1 + ".", 0) == 0) {
            std::string suffix = kv.first.substr(path1.size());
            props1[suffix] = kv.second;
        }
        if (kv.first.rfind(path2 + ".", 0) == 0) {
            std::string suffix = kv.first.substr(path2.size());
            props2[suffix] = kv.second;
        }
    }
    
    // Clear both
    clearProperties(path1);
    clearProperties(path2);
    
    // Swap: path1 gets path2's props, path2 gets path1's props
    for (auto& kv : props2) Env[path1 + kv.first] = kv.second;
    for (auto& kv : props1) Env[path2 + kv.first] = kv.second;
}

// Extract template parameters from a type and set as properties
void extractTemplateParams(const std::string& varPath, QualType type) {
    // Get the canonical type and strip qualifiers
    type = type.getCanonicalType();

    // Check if this is a template specialization
    const Type* T = type.getTypePtr();
    if (!T) return;

    const CXXRecordDecl* RD = T->getAsCXXRecordDecl();
    if (!RD) return;

    auto* Spec = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!Spec) return;

    const TemplateArgumentList& Args = Spec->getTemplateArgs();

    // Get qualified class name for alias lookup
    // Handle nested/inline namespaces (e.g., std::__1::span -> std::span)
    std::string qualName;
    std::string className = Spec->getNameAsString();
    bool inStd = false;
    
    // Walk up the DeclContext chain to find if we're in std::
    const DeclContext* DC = Spec->getDeclContext();
    while (DC) {
        if (auto* NS = dyn_cast<NamespaceDecl>(DC)) {
            if (NS->getNameAsString() == "std") {
                inStd = true;
                break;
            }
        }
        DC = DC->getParent();
    }
    
    if (inStd) {
        qualName = "std::" + className;
    } else if (auto* NS = dyn_cast<NamespaceDecl>(Spec->getDeclContext())) {
        qualName = NS->getNameAsString() + "::" + className;
    } else {
        qualName = className;
    }

    // Extract all integral template parameters as _tparam0, _tparam1, etc.
    for (unsigned i = 0; i < Args.size(); i++) {
        const TemplateArgument& Arg = Args[i];
        if (Arg.getKind() == TemplateArgument::Integral) {
            llvm::APSInt Val = Arg.getAsIntegral();
            if (Val.isRepresentableByInt64()) {
                int64_t value = Val.getExtValue();
                std::string paramName = "_tparam" + std::to_string(i);
                setField(varPath, paramName, SymValue::Const(value));
            }
        }
    }

    // Apply semantic aliases from the alias table
    auto it = TemplateAliases.find(qualName);
    if (it != TemplateAliases.end()) {
        for (const auto& alias : it->second) {
            if (alias.paramIndex < (int)Args.size()) {
                const TemplateArgument& Arg = Args[alias.paramIndex];
                if (Arg.getKind() == TemplateArgument::Integral) {
                    llvm::APSInt Val = Arg.getAsIntegral();
                    if (Val.isRepresentableByInt64()) {
                        int64_t value = Val.getExtValue();
                        // Skip if value matches skipValue (e.g., dynamic_extent for span)
                        if (alias.skipValue && *alias.skipValue == value) continue;
                        setField(varPath, alias.propertyName, SymValue::Const(value));
                    }
                }
            }
        }
    }
}

// Forward declaration for exprToSymValue
class DTTVisitor;
SymValue exprToSymValueImpl(const Expr *E, ASTContext &Ctx);

// Convert an expression to a SymValue for symbolic tracking
SymValue exprToSymValueImpl(const Expr *E, ASTContext &Ctx) {
    if (!E) return SymValue::Unknown();
    E = E->IgnoreParenImpCasts();
    
    // Constant integer
    Expr::EvalResult R;
    if (safeEvaluateAsInt(E, R, const_cast<ASTContext&>(Ctx))) {
        const llvm::APSInt &Val = R.Val.getInt();
        // Check if value is representable as int64_t
        if (Val.isRepresentableByInt64()) {
            return SymValue::Const(Val.getExtValue());
        }
        // Value too large, treat as unknown
        return SymValue::Unknown();
    }
    
    // Variable reference
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        std::string varName = DRE->getDecl()->getNameAsString();
        // Check if we have a tracked value for this variable
        auto it = Env.find(varName);
        if (it != Env.end()) return it->second;
        return SymValue::Var(varName);
    }
    
    // Member expression: a.len, v._size, etc.
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        std::string path = getExprPath(E);
        if (!path.empty() && !hasRuntimeIndex(path)) {
            // Check if we have a tracked value for this field
            auto it = Env.find(path);
            if (it != Env.end()) return it->second;
            return SymValue::Var(path);
        }
    }
    
    // Binary operator (+, -, *, /, &, %)
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        SymValue left = exprToSymValueImpl(BO->getLHS(), Ctx);
        SymValue right = exprToSymValueImpl(BO->getRHS(), Ctx);
        switch (BO->getOpcode()) {
            case BO_Add: return SymValue::Add(left, right);
            case BO_Sub: return SymValue::Sub(left, right);
            case BO_Mul: return SymValue::Mul(left, right);
            case BO_Div: return SymValue::Div(left, right);
            case BO_And: return SymValue::BitAnd(left, right);
            case BO_Rem: return SymValue::Mod(left, right);
            default: break;
        }
    }

    // Unary operator: ++i, i++, --i, i--
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
        SymValue operand = exprToSymValueImpl(UO->getSubExpr(), Ctx);
        switch (UO->getOpcode()) {
            case UO_PostInc:  // i++ - value is i (before increment)
            case UO_PostDec:  // i-- - value is i (before decrement)
                return operand;
            case UO_PreInc:   // ++i - value is i+1 (after increment)
                return SymValue::Add(operand, SymValue::Const(1));
            case UO_PreDec:   // --i - value is i-1 (after decrement)
                return SymValue::Sub(operand, SymValue::Const(1));
            default: break;
        }
    }

    // Method call: v.size() -> lookup return value
    if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        auto *Method = MCE->getMethodDecl();
        if (Method && Method->getParent()) {
            std::string className = Method->getParent()->getNameAsString();
            std::string methodName = Method->getNameAsString();
            std::string key = className + "::" + methodName + "/" + std::to_string(MCE->getNumArgs());
            
            auto it = MethodAnnotations.find(key);
            if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
                // Method returns a field value (e.g., size() returns _size)
                std::string objPath = getExprPath(MCE->getImplicitObjectArgument());
                if (!objPath.empty()) {
                    SymValue fieldVal = getField(objPath, it->second.returns);
                    return fieldVal;
                }
            }
        }
    }
    
    return SymValue::Unknown();
}

// ============================================================
// Z3 SMT SOLVER (C++ API)
// ============================================================

std::string sanitizeForSMT(const std::string& s) {
    std::string result = s;
    for (char& c : result) if (c == '.' || c == '[' || c == ']' || c == ':') c = '_';
    return result;
}

// Helper: get or create a Z3 integer variable by name
z3::expr getOrCreateVar(z3::context& ctx, std::map<std::string, z3::expr>& vars, const std::string& name) {
    auto it = vars.find(name);
    if (it != vars.end()) return it->second;
    z3::expr var = ctx.int_const(name.c_str());
    vars.emplace(name, var);
    return var;
}

// Convert SymValue to Z3 expression
z3::expr symValueToZ3(const SymValue& sv, z3::context& ctx,
                      std::map<std::string, z3::expr>& vars, const std::string& varPrefix) {
    switch (sv.kind) {
        case SymValue::CONST:
            return ctx.int_val(sv.constVal);
        case SymValue::UNKNOWN:
            return getOrCreateVar(ctx, vars, varPrefix);
        case SymValue::VAR:
            return getOrCreateVar(ctx, vars, sanitizeForSMT(sv.varRef));
        case SymValue::ADD:
            return symValueToZ3(*sv.left, ctx, vars, varPrefix) +
                   symValueToZ3(*sv.right, ctx, vars, varPrefix);
        case SymValue::SUB:
            return symValueToZ3(*sv.left, ctx, vars, varPrefix) -
                   symValueToZ3(*sv.right, ctx, vars, varPrefix);
        case SymValue::MUL:
            return symValueToZ3(*sv.left, ctx, vars, varPrefix) *
                   symValueToZ3(*sv.right, ctx, vars, varPrefix);
        case SymValue::DIV:
            return symValueToZ3(*sv.left, ctx, vars, varPrefix) /
                   symValueToZ3(*sv.right, ctx, vars, varPrefix);
        default:
            return ctx.int_val(0);
    }
}

// Convert Constraint to Z3 expression
z3::expr constraintToZ3(const Constraint& c, z3::context& ctx,
                        std::map<std::string, z3::expr>& vars, const std::string& varName) {
    if (c.op == Constraint::AND)
        return constraintToZ3(*c.left, ctx, vars, varName) &&
               constraintToZ3(*c.right, ctx, vars, varName);
    if (c.op == Constraint::OR)
        return constraintToZ3(*c.left, ctx, vars, varName) ||
               constraintToZ3(*c.right, ctx, vars, varName);

    z3::expr lhs = c.isExprConstraint
        ? symValueToZ3(c.lhsExpr, ctx, vars, varName)
        : getOrCreateVar(ctx, vars, varName + "_" + c.field);
    z3::expr rhs = c.isExprConstraint
        ? symValueToZ3(c.rhsExpr, ctx, vars, varName)
        : ctx.int_val(c.rhs);

    switch (c.op) {
        case Constraint::GT: return lhs > rhs;
        case Constraint::GE: return lhs >= rhs;
        case Constraint::LT: return lhs < rhs;
        case Constraint::LE: return lhs <= rhs;
        case Constraint::EQ: return lhs == rhs;
        case Constraint::NE: return lhs != rhs;
        default: return ctx.bool_val(true);
    }
}

std::optional<bool> checkWithZ3(const std::string& basePath, const Constraint& c) {
    try {
        z3::context ctx;
        z3::solver solver(ctx);

        // Set timeout (1 second = 1000ms)
        z3::params p(ctx);
        p.set("timeout", 1000u);
        solver.set(p);

        std::map<std::string, z3::expr> vars;
        std::string varName = sanitizeForSMT(basePath);

        // Add environment constraints (known values)
        for (const auto& kv : Env) {
            if (kv.first.find("._proof_") != std::string::npos) continue;
            std::string v = sanitizeForSMT(kv.first);

            if (kv.second.kind == SymValue::CONST) {
                solver.add(getOrCreateVar(ctx, vars, v) == ctx.int_val(kv.second.constVal));
            } else if (kv.second.kind == SymValue::VAR) {
                std::string rhsVar = sanitizeForSMT(kv.second.varRef);
                solver.add(getOrCreateVar(ctx, vars, v) == getOrCreateVar(ctx, vars, rhsVar));
            } else if (kv.second.kind == SymValue::ADD || kv.second.kind == SymValue::SUB ||
                       kv.second.kind == SymValue::MUL || kv.second.kind == SymValue::DIV) {
                solver.add(getOrCreateVar(ctx, vars, v) == symValueToZ3(kv.second, ctx, vars, v));
            }
        }

        // Add proof constraints (guards from if-statements, etc.)
        for (const auto& kv : Env) {
            size_t proofPos = kv.first.find("._proof_");
            if (proofPos == std::string::npos) continue;
            std::string base = sanitizeForSMT(kv.first.substr(0, proofPos));
            std::string proofKey = kv.first.substr(proofPos + 8);
            auto parsed = parseSimpleConstraint(proofKey);
            if (parsed) {
                solver.add(constraintToZ3(*parsed, ctx, vars, base));
            }
        }

        // Add negated goal: if unsat, the original constraint must be true
        solver.add(!constraintToZ3(c, ctx, vars, varName));

        auto result = solver.check();
        if (result == z3::unsat) return true;   // Constraint proven (no counterexample)
        if (result == z3::sat) return false;    // Counterexample exists
        return std::nullopt;                     // Unknown (timeout or complex)

    } catch (const z3::exception& e) {
        // Z3 error - fall back to unknown
        return std::nullopt;
    }
}

// Check buffer bounds using Z3: prove that idx >= minIdx && idx < size
// Returns: true = proven safe, false = proven OOB, nullopt = unknown
std::optional<bool> checkBufferBoundsWithZ3(const SymValue& idx, const SymValue& size,
                                             int64_t minIdx, const std::string& ptrPath,
                                             bool isUnsignedIndex = false) {
    try {
        z3::context ctx;
        z3::solver solver(ctx);

        z3::params p(ctx);
        p.set("timeout", 1000u);
        solver.set(p);

        std::map<std::string, z3::expr> vars;

        // Add environment constraints (known values and symbolic relationships)
        for (const auto& kv : Env) {
            if (kv.first.find("._proof_") != std::string::npos) continue;
            std::string v = sanitizeForSMT(kv.first);

            if (kv.second.kind == SymValue::CONST) {
                solver.add(getOrCreateVar(ctx, vars, v) == ctx.int_val(kv.second.constVal));
            } else if (kv.second.kind == SymValue::VAR) {
                std::string rhsVar = sanitizeForSMT(kv.second.varRef);
                solver.add(getOrCreateVar(ctx, vars, v) == getOrCreateVar(ctx, vars, rhsVar));
            } else if (kv.second.kind == SymValue::ADD || kv.second.kind == SymValue::SUB ||
                       kv.second.kind == SymValue::MUL || kv.second.kind == SymValue::DIV) {
                solver.add(getOrCreateVar(ctx, vars, v) == symValueToZ3(kv.second, ctx, vars, v));
            }
        }

        // Add proof constraints from guards (if-statements, loop bounds)
        for (const auto& kv : Env) {
            size_t proofPos = kv.first.find("._proof_");
            if (proofPos == std::string::npos) continue;
            std::string base = sanitizeForSMT(kv.first.substr(0, proofPos));
            std::string proofKey = kv.first.substr(proofPos + 8);
            auto parsed = parseSimpleConstraint(proofKey);
            if (parsed) {
                // Special handling for _value proofs: n._proof__value>0 means n > 0
                // The _value field is a marker for the variable itself
                if (parsed->field == "_value") {
                    z3::expr varExpr = getOrCreateVar(ctx, vars, base);
                    z3::expr rhsExpr = ctx.int_val(parsed->rhs);
                    switch (parsed->op) {
                        case Constraint::GT: solver.add(varExpr > rhsExpr); break;
                        case Constraint::GE: solver.add(varExpr >= rhsExpr); break;
                        case Constraint::LT: solver.add(varExpr < rhsExpr); break;
                        case Constraint::LE: solver.add(varExpr <= rhsExpr); break;
                        case Constraint::EQ: solver.add(varExpr == rhsExpr); break;
                        case Constraint::NE: solver.add(varExpr != rhsExpr); break;
                        default: break;
                    }
                } else {
                    solver.add(constraintToZ3(*parsed, ctx, vars, base));
                }
            }
        }

        // Add VarBoundProofs (loop invariants like i < n)
        for (const auto& vbp : VarBoundProofs) {
            // When objPath is empty, field is a plain variable name (e.g., i < n)
            // When objPath is non-empty, it's a field access (e.g., i < obj._size)
            z3::expr varExpr = getOrCreateVar(ctx, vars, sanitizeForSMT(vbp.varName));
            z3::expr boundExpr = vbp.objPath.empty()
                ? getOrCreateVar(ctx, vars, sanitizeForSMT(vbp.field))
                : getOrCreateVar(ctx, vars, sanitizeForSMT(vbp.objPath + "." + vbp.field));

            switch (vbp.op) {
                case Constraint::LT: solver.add(varExpr < boundExpr); break;
                case Constraint::LE: solver.add(varExpr <= boundExpr); break;
                case Constraint::GT: solver.add(varExpr > boundExpr); break;
                case Constraint::GE: solver.add(varExpr >= boundExpr); break;
                case Constraint::EQ: solver.add(varExpr == boundExpr); break;
                case Constraint::NE: solver.add(varExpr != boundExpr); break;
                default: break;
            }
        }

        // Convert idx and size to Z3 expressions
        z3::expr z3Idx = symValueToZ3(idx, ctx, vars, "idx");
        z3::expr z3Size = symValueToZ3(size, ctx, vars, "size");

        // For unsigned index types, add non-negativity constraint
        // Z3 treats integers as signed, so without this constraint it could find
        // counterexamples like idx=-1 which satisfies "idx < 8" but fails "idx >= 0"
        if (isUnsignedIndex) {
            solver.add(z3Idx >= 0);
        }

        // Goal: idx >= minIdx && idx < size
        z3::expr boundsCheck = (z3Idx >= ctx.int_val(minIdx)) && (z3Idx < z3Size);

        // Try to prove bounds are satisfied (negate and check unsat)
        solver.add(!boundsCheck);

        auto result = solver.check();
        if (result == z3::unsat) return true;   // Proven safe
        if (result == z3::sat) return false;    // Proven OOB (counterexample exists)
        return std::nullopt;                     // Unknown

    } catch (const z3::exception& e) {
        return std::nullopt;
    }
}

// ============================================================
// CONSTRAINT CHECKING
// ============================================================

bool checkOp(Constraint::Op op, int64_t lhs, int64_t rhs) {
    switch (op) {
        case Constraint::GT: return lhs > rhs;
        case Constraint::GE: return lhs >= rhs;
        case Constraint::LT: return lhs < rhs;
        case Constraint::LE: return lhs <= rhs;
        case Constraint::EQ: return lhs == rhs;
        case Constraint::NE: return lhs != rhs;
        default: return false;
    }
}

bool proofImplies(const std::string& proofKey, const Constraint& c) {
    auto parsed = parseSimpleConstraint(proofKey);
    if (!parsed || parsed->field != c.field) return false;
    int64_t pVal = parsed->rhs, cVal = c.rhs;
    if (parsed->op == Constraint::GT) { if ((c.op == Constraint::GT || c.op == Constraint::GE) && pVal >= cVal) return true; }
    if (parsed->op == Constraint::GE) { if (c.op == Constraint::GE && pVal >= cVal) return true; if (c.op == Constraint::GT && pVal > cVal) return true; }
    if (parsed->op == Constraint::LT) { if ((c.op == Constraint::LT || c.op == Constraint::LE) && pVal <= cVal) return true; }
    if (parsed->op == Constraint::LE) { if (c.op == Constraint::LE && pVal <= cVal) return true; if (c.op == Constraint::LT && pVal < cVal) return true; }
    if (parsed->op == Constraint::EQ) {
        if (c.op == Constraint::EQ && pVal == cVal) return true;
        if (c.op == Constraint::GE && pVal >= cVal) return true;
        if (c.op == Constraint::LE && pVal <= cVal) return true;
        if (c.op == Constraint::GT && pVal > cVal) return true;
        if (c.op == Constraint::LT && pVal < cVal) return true;
    }
    return false;
}

std::optional<bool> checkMultiArgConstraint(const Constraint& c, const std::vector<std::string>& argPaths) {
    if (!c.isMultiArg) return std::nullopt;
    int lhsIdx = std::atoi(c.lhsArg.c_str() + 3);
    int rhsIdx = std::atoi(c.rhsArg.c_str() + 3);
    if (lhsIdx >= (int)argPaths.size() || rhsIdx >= (int)argPaths.size()) return std::nullopt;
    SymValue lhsVal = getField(argPaths[lhsIdx], c.field);
    SymValue rhsVal = getField(argPaths[rhsIdx], c.rhsField);
    auto lhsEval = lhsVal.eval();
    auto rhsEval = rhsVal.eval();
    if (lhsEval && rhsEval) return checkOp(c.op, *lhsEval, *rhsEval);
    if (c.op == Constraint::EQ && lhsVal.equals(rhsVal)) return true;
    return std::nullopt;
}

std::optional<bool> checkConstraintSimple(const std::string& basePath, const Constraint& c) {
    if (c.op == Constraint::AND) {
        auto l = checkConstraintSimple(basePath, *c.left);
        auto r = checkConstraintSimple(basePath, *c.right);
        if (l && r) return *l && *r;
        if ((l && !*l) || (r && !*r)) return false;
        return std::nullopt;
    }
    if (c.op == Constraint::OR) {
        auto l = checkConstraintSimple(basePath, *c.left);
        auto r = checkConstraintSimple(basePath, *c.right);
        if (l && r) return *l || *r;
        if ((l && *l) || (r && *r)) return true;
        return std::nullopt;
    }
    
    // Check direct proofs
    std::string prefix = basePath + "._proof_";
    for (auto& kv : Env) if (kv.first.rfind(prefix, 0) == 0) {
        std::string proofKey = kv.first.substr(prefix.size());
        if (proofImplies(proofKey, c)) return true;
    }
    
    // Handle expression constraints
    if (c.isExprConstraint) {
        // Build substitution map from environment
        std::map<std::string, SymValue> subst;
        for (auto& kv : Env) {
            if (kv.first.find("._proof_") != std::string::npos) continue;
            // Handle basePath._field -> _field mapping (and basePath.@field -> @field for ghost state)
            if (kv.first.rfind(basePath + ".", 0) == 0) {
                std::string field = kv.first.substr(basePath.size() + 1);
                // If field already starts with _ or @ (ghost state), use as-is
                if (!field.empty() && (field[0] == '_' || field[0] == '@')) {
                    subst[field] = kv.second;
                } else {
                    subst["_" + field] = kv.second;
                }
            }
            subst[kv.first] = kv.second;
        }
        
        // Substitute and evaluate
        SymValue lhsEval = c.lhsExpr.substitute(subst);
        SymValue rhsEval = c.rhsExpr.substitute(subst);
        auto lhs = lhsEval.eval();
        auto rhs = rhsEval.eval();
        if (lhs && rhs) return checkOp(c.op, *lhs, *rhs);
        return std::nullopt;  // Can't evaluate statically, let Z3 handle it
    }
    
    auto val = getField(basePath, c.field);
    auto v = val.eval();
    
    // Transitive proof support
    // If field value is a variable (e.g., _size == n), check proofs on that variable
    if (!v && val.kind == SymValue::VAR) {
        std::string varName = val.varRef;
        // Check if there's a proof on the variable itself
        for (auto& kv : Env) {
            size_t proofPos = kv.first.find("._proof_");
            if (proofPos != std::string::npos) {
                std::string proofBase = kv.first.substr(0, proofPos);
                if (proofBase == varName) {
                    std::string proofKey = kv.first.substr(proofPos + 8);
                    // Parse the proof to get the value constraint
                    // e.g., "_value>0" means the variable is > 0
                    auto parsed = parseSimpleConstraint(proofKey);
                    if (parsed && (parsed->field == "_value" || parsed->field == c.field)) {
                        // Create a new constraint checking the same op/rhs as original
                        // but using the proof's values
                        if (proofImplies(proofKey, c) ||
                            (parsed->field == "_value" && parsed->op == c.op && parsed->rhs >= c.rhs &&
                             (c.op == Constraint::GT || c.op == Constraint::GE))) {
                            return true;
                        }
                        // Special case: proof is _value>0 and constraint is field>0
                        if (parsed->field == "_value" && parsed->rhs == c.rhs) {
                            if ((parsed->op == Constraint::GT && c.op == Constraint::GT) ||
                                (parsed->op == Constraint::GT && c.op == Constraint::GE) ||
                                (parsed->op == Constraint::GE && c.op == Constraint::GE)) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    // Check _value proofs on the basePath itself (for scalar arg constraints like arg0 >= N)
    // When checking needsAtLeastOne(i) with requires=arg0>=1, basePath="i", c.field="arg0"
    // We need to find i._proof__value>=1 and recognize it proves the constraint
    if (!v && (c.field == "arg0" || c.field.find("arg") == 0)) {
        for (auto& kv : Env) {
            if (kv.first.rfind(basePath + "._proof__value", 0) == 0) {
                std::string proofKey = kv.first.substr(basePath.size() + 8);  // skip "._proof_"
                auto parsed = parseSimpleConstraint(proofKey);
                if (parsed && parsed->field == "_value") {
                    // Check if proof implies constraint (considering both ops)
                    int64_t pVal = parsed->rhs, cVal = c.rhs;
                    bool implied = false;
                    // GE proof implications
                    if (parsed->op == Constraint::GE) {
                        if (c.op == Constraint::GE && pVal >= cVal) implied = true;
                        if (c.op == Constraint::GT && pVal > cVal) implied = true;
                    }
                    // GT proof implications
                    if (parsed->op == Constraint::GT) {
                        if ((c.op == Constraint::GT || c.op == Constraint::GE) && pVal >= cVal) implied = true;
                    }
                    // LT proof implications
                    if (parsed->op == Constraint::LT) {
                        if (c.op == Constraint::LT && pVal <= cVal) implied = true;
                        if (c.op == Constraint::LE && pVal <= cVal) implied = true;
                    }
                    // LE proof implications
                    if (parsed->op == Constraint::LE) {
                        if (c.op == Constraint::LE && pVal <= cVal) implied = true;
                        if (c.op == Constraint::LT && pVal < cVal) implied = true;
                    }
                    if (implied) return true;
                }
            }
        }
    }

    if (!v) return std::nullopt;
    return checkOp(c.op, *v, c.rhs);
}

std::optional<bool> checkConstraint(const std::string& basePath, const Constraint& c, bool useZ3 = true) {
    auto simple = checkConstraintSimple(basePath, c);
    if (simple.has_value()) return simple;
    if (useZ3) return checkWithZ3(basePath, c);
    return std::nullopt;
}

// ============================================================
// PASS 1: Analysis Collector
// ============================================================
// Lightweight visitor that collects call sites and return statements
// for function ghost state inference without performing full analysis.

class AnalysisCollector : public RecursiveASTVisitor<AnalysisCollector> {
    ASTContext& Context;
    const FunctionDecl* CurrentFunc = nullptr;
    // Track current if-condition for guarded return detection
    const Expr* CurrentIfCondition = nullptr;
    bool InThenBranch = false;
    bool InElseBranch = false;

public:
    AnalysisCollector(ASTContext& Ctx) : Context(Ctx) {}

    bool TraverseFunctionDecl(FunctionDecl* F) {
        if (!F || !F->hasBody()) return true;

        CurrentFunc = F;
        std::string key = getFunctionKey(F);
        FunctionAnalysis[key].decl = F;

        RecursiveASTVisitor::TraverseFunctionDecl(F);

        CurrentFunc = nullptr;
        return true;
    }

    // Handle CXXMethodDecl via TraverseFunctionDecl
    bool TraverseCXXMethodDecl(CXXMethodDecl* M) {
        return TraverseFunctionDecl(M);
    }

    // Gap 1: Track variable nullability on initialization
    bool VisitVarDecl(VarDecl* VD) {
        if (!CurrentFunc) return true;
        if (!VD->hasInit()) return true;
        if (!VD->getType()->isPointerType()) return true;

        std::string key = getFunctionKey(CurrentFunc);
        std::string varName = VD->getNameAsString();

        Expr* init = VD->getInit()->IgnoreParenImpCasts();
        auto nonnull = inferNonnullFromExpr(init);

        if (nonnull.has_value()) {
            FunctionAnalysis[key].varNullability[varName] = (*nonnull == 1);
        }
        // Also check if init is a call to a function (will be resolved via chaining)
        else if (isa<CallExpr>(init)) {
            // Mark as "pending call" - will be resolved during chaining
            // For now, store the callee key so we can look it up later
            if (auto* CE = dyn_cast<CallExpr>(init)) {
                if (auto* callee = CE->getDirectCallee()) {
                    // Store a special marker indicating this var depends on a call
                    // We'll use a convention: store nullopt now, resolve in compute phase
                    FunctionAnalysis[key].varNullability[varName] = std::nullopt;
                }
            }
        }

        return true;
    }

    // Gap 3: Track if-statements for guarded return detection
    bool TraverseIfStmt(IfStmt* IS) {
        if (!IS) return true;

        const Expr* oldCond = CurrentIfCondition;
        bool oldThen = InThenBranch;
        bool oldElse = InElseBranch;

        CurrentIfCondition = IS->getCond();

        // Traverse then branch
        InThenBranch = true;
        InElseBranch = false;
        if (IS->getThen()) {
            TraverseStmt(IS->getThen());
        }

        // Traverse else branch
        InThenBranch = false;
        InElseBranch = true;
        if (IS->getElse()) {
            TraverseStmt(IS->getElse());
        }

        // Restore state
        CurrentIfCondition = oldCond;
        InThenBranch = oldThen;
        InElseBranch = oldElse;

        return true;
    }

    // Helper: Check if expression is a null check on a parameter
    // Returns the parameter index if it's "!param" or "param == nullptr"
    std::optional<unsigned> getParamNullCheckIndex(const Expr* E) {
        if (!E || !CurrentFunc) return std::nullopt;
        E = E->IgnoreParenImpCasts();

        // Check for "!param" (unary not)
        if (auto* UO = dyn_cast<UnaryOperator>(E)) {
            if (UO->getOpcode() == UO_LNot) {
                if (auto* DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParenImpCasts())) {
                    if (auto* PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
                        // Find parameter index
                        for (unsigned i = 0; i < CurrentFunc->getNumParams(); i++) {
                            if (CurrentFunc->getParamDecl(i) == PVD) {
                                return i;
                            }
                        }
                    }
                }
            }
        }

        // Check for "param == nullptr" or "nullptr == param"
        if (auto* BO = dyn_cast<BinaryOperator>(E)) {
            if (BO->getOpcode() == BO_EQ) {
                const Expr* lhs = BO->getLHS()->IgnoreParenImpCasts();
                const Expr* rhs = BO->getRHS()->IgnoreParenImpCasts();

                // Check if one side is nullptr and the other is a param
                auto checkSide = [&](const Expr* paramSide, const Expr* nullSide) -> std::optional<unsigned> {
                    if (isa<CXXNullPtrLiteralExpr>(nullSide) || isa<GNUNullExpr>(nullSide) ||
                        (isa<IntegerLiteral>(nullSide) && cast<IntegerLiteral>(nullSide)->getValue() == 0)) {
                        if (auto* DRE = dyn_cast<DeclRefExpr>(paramSide)) {
                            if (auto* PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
                                for (unsigned i = 0; i < CurrentFunc->getNumParams(); i++) {
                                    if (CurrentFunc->getParamDecl(i) == PVD) {
                                        return i;
                                    }
                                }
                            }
                        }
                    }
                    return std::nullopt;
                };

                if (auto idx = checkSide(lhs, rhs)) return idx;
                if (auto idx = checkSide(rhs, lhs)) return idx;
            }
        }

        return std::nullopt;
    }

    // Collect return statements (for backward/return analysis)
    bool VisitReturnStmt(ReturnStmt* RS) {
        if (!CurrentFunc) return true;

        const Expr* retVal = RS->getRetValue();
        if (!retVal) return true;  // void return

        // Only track pointer returns
        if (!retVal->getType()->isPointerType()) return true;

        std::string key = getFunctionKey(CurrentFunc);

        GuardedReturn gr;
        gr.stmt = RS;

        // Check if this returns nullptr
        auto nonnull = inferNonnullFromExpr(const_cast<Expr*>(retVal->IgnoreParenImpCasts()));
        gr.returnsNullptr = nonnull.has_value() && *nonnull == 0;

        // Gap 3: Check if this return is guarded by a null check
        // If we're in "if (!param) return nullptr;" pattern, record it
        if (gr.returnsNullptr && CurrentIfCondition && InThenBranch) {
            gr.guardedByParamNullCheck = getParamNullCheckIndex(CurrentIfCondition);
        }

        FunctionAnalysis[key].returnStmts.push_back(gr);
        return true;
    }

    // Collect call sites (for forward/parameter analysis)
    bool VisitCallExpr(CallExpr* CE) {
        const FunctionDecl* callee = CE->getDirectCallee();
        if (!callee) return true;  // Indirect call - skip

        std::string calleeKey = getFunctionKey(callee);

        CallSiteInfo info;
        info.callExpr = CE;
        info.callerFunc = CurrentFunc;
        info.loc = CE->getBeginLoc();

        FunctionAnalysis[calleeKey].callSites.push_back(info);
        return true;
    }

    // Handle CXXMemberCallExpr (method calls)
    bool VisitCXXMemberCallExpr(CXXMemberCallExpr* MCE) {
        // CXXMemberCallExpr is a subclass of CallExpr, but we want to ensure
        // we capture the callee properly
        const FunctionDecl* callee = MCE->getMethodDecl();
        if (!callee) return true;

        std::string calleeKey = getFunctionKey(callee);

        CallSiteInfo info;
        info.callExpr = MCE;
        info.callerFunc = CurrentFunc;
        info.loc = MCE->getBeginLoc();

        FunctionAnalysis[calleeKey].callSites.push_back(info);
        return true;
    }
};

// ============================================================
// COMPUTE FUNCTION GHOST STATES
// ============================================================
// After Pass 1, compute @paramN_nonnull and @returns_nonnull for each function
// Uses multi-pass computation:
//   Pass A: Compute @returns_nonnull (simple, ignoring guarded returns)
//   Pass B: Compute @paramN_nonnull (using variable tracking + return chaining)
//   Pass C: Re-compute @returns_nonnull for guarded returns (using param info)

// Helper: Check if argument at call site is non-null
// Uses: direct inference, variable tracking (Gap 1), return chaining (Gap 2)
std::optional<bool> isCallArgNonnull(const Expr* arg, const CallSiteInfo& site) {
    arg = arg->IgnoreParenImpCasts();

    // Try direct inference first (address-of, new, literals)
    auto direct = inferNonnullFromExpr(const_cast<Expr*>(arg));
    if (direct.has_value()) {
        return *direct == 1;
    }

    // Gap 1: Check variable tracking in caller's function
    if (auto* DRE = dyn_cast<DeclRefExpr>(arg)) {
        std::string varName = DRE->getDecl()->getNameAsString();
        std::string callerKey = getFunctionKey(site.callerFunc);

        auto callerIt = FunctionAnalysis.find(callerKey);
        if (callerIt != FunctionAnalysis.end()) {
            auto varIt = callerIt->second.varNullability.find(varName);
            if (varIt != callerIt->second.varNullability.end() && varIt->second.has_value()) {
                return varIt->second.value();
            }
        }
    }

    // Gap 2: Check return value chaining (if arg is a call, check callee's @returns_nonnull)
    if (auto* CE = dyn_cast<CallExpr>(arg)) {
        if (auto* callee = CE->getDirectCallee()) {
            std::string calleeKey = getFunctionKey(callee);
            auto calleeIt = FunctionAnalysis.find(calleeKey);
            if (calleeIt != FunctionAnalysis.end() &&
                calleeIt->second.returnsComputed &&
                calleeIt->second.returnsNonnull.value_or(false)) {
                return true;
            }
        }
    }

    return std::nullopt;  // Unknown
}

void computeFunctionGhostStates() {
    // ================================================================
    // PASS A: Compute @returns_nonnull for all functions (simple)
    // ================================================================
    // This must run first so Gap 2 (return chaining) can use the results
    for (auto& [funcKey, data] : FunctionAnalysis) {
        const FunctionDecl* F = data.decl;
        if (!F) continue;

        if (!F->getReturnType()->isPointerType()) {
            data.returnsNonnull = std::nullopt;
            data.returnsComputed = true;
            continue;
        }

        if (data.returnStmts.empty()) {
            data.returnsNonnull = std::nullopt;
            data.returnsComputed = true;
            continue;
        }

        bool allReturnsNonnull = true;
        for (const GuardedReturn& gr : data.returnStmts) {
            const Expr* retVal = gr.stmt->getRetValue();
            if (!retVal) {
                allReturnsNonnull = false;
                break;
            }

            auto nonnull = inferNonnullFromExpr(const_cast<Expr*>(retVal->IgnoreParenImpCasts()));
            if (!nonnull.has_value() || *nonnull == 0) {
                allReturnsNonnull = false;
                break;
            }
        }

        data.returnsNonnull = allReturnsNonnull ? std::optional(true) : std::nullopt;
        data.returnsComputed = true;
    }

    // ================================================================
    // PASS B: Compute @paramN_nonnull for all functions
    // ================================================================
    // Uses variable tracking (Gap 1) and return chaining (Gap 2)
    for (auto& [funcKey, data] : FunctionAnalysis) {
        const FunctionDecl* F = data.decl;
        if (!F) continue;

        unsigned numParams = F->getNumParams();
        for (unsigned p = 0; p < numParams; p++) {
            const ParmVarDecl* param = F->getParamDecl(p);
            if (!param->getType()->isPointerType()) continue;

            if (data.callSites.empty()) {
                data.paramNonnull[p] = std::nullopt;
                continue;
            }

            bool allNonnull = true;
            for (const auto& site : data.callSites) {
                if (p >= site.callExpr->getNumArgs()) {
                    allNonnull = false;
                    break;
                }

                const Expr* arg = site.callExpr->getArg(p);
                auto nonnull = isCallArgNonnull(arg, site);

                if (!nonnull.has_value() || !nonnull.value()) {
                    allNonnull = false;
                    break;
                }
            }

            data.paramNonnull[p] = allNonnull ? std::optional(true) : std::nullopt;
        }

        data.analyzed = true;
    }

    // ================================================================
    // PASS C: Re-compute @returns_nonnull for guarded returns (Gap 3)
    // ================================================================
    // If a nullptr return is guarded by "if (!param)" and all callers
    // pass non-null for that param, the nullptr path is unreachable.
    // Also: if return value is a parameter that all callers pass non-null,
    // the return value is non-null.
    for (auto& [funcKey, data] : FunctionAnalysis) {
        // Skip if already proven non-null
        if (data.returnsNonnull.value_or(false)) continue;

        const FunctionDecl* F = data.decl;
        if (!F || !F->getReturnType()->isPointerType()) continue;
        if (data.returnStmts.empty()) continue;

        bool allReturnsNonnull = true;
        for (const GuardedReturn& gr : data.returnStmts) {
            const Expr* retVal = gr.stmt->getRetValue();
            if (!retVal) {
                allReturnsNonnull = false;
                break;
            }

            const Expr* retExpr = retVal->IgnoreParenImpCasts();
            auto nonnull = inferNonnullFromExpr(const_cast<Expr*>(retExpr));

            if (nonnull.has_value() && *nonnull == 1) {
                // This return is non-null (new, address-of, etc.), OK
                continue;
            }

            if (nonnull.has_value() && *nonnull == 0) {
                // This returns nullptr - check if it's guarded
                if (gr.guardedByParamNullCheck.has_value()) {
                    unsigned guardParam = gr.guardedByParamNullCheck.value();
                    // Check if all callers pass non-null for this param
                    auto paramIt = data.paramNonnull.find(guardParam);
                    if (paramIt != data.paramNonnull.end() &&
                        paramIt->second.value_or(false)) {
                        // All callers pass non-null, so this nullptr return is unreachable
                        continue;
                    }
                }
                // Not guarded or guard param is not always non-null
                allReturnsNonnull = false;
                break;
            }

            // Unknown nullability from direct inference - check if it's a parameter
            // If the return value is a parameter and all callers pass non-null, it's non-null
            if (auto* DRE = dyn_cast<DeclRefExpr>(retExpr)) {
                if (auto* PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
                    // Find parameter index
                    bool foundNonnull = false;
                    for (unsigned i = 0; i < F->getNumParams(); i++) {
                        if (F->getParamDecl(i) == PVD) {
                            auto paramIt = data.paramNonnull.find(i);
                            if (paramIt != data.paramNonnull.end() &&
                                paramIt->second.value_or(false)) {
                                // All callers pass non-null for this param, so return is non-null
                                foundNonnull = true;
                            }
                            break;
                        }
                    }
                    if (foundNonnull) continue;
                }
            }

            // Unknown nullability and not a proven-nonnull parameter
            allReturnsNonnull = false;
            break;
        }

        if (allReturnsNonnull) {
            data.returnsNonnull = true;
        }
    }
}

// ============================================================
// PROPAGATE PARAM DEREFERENCED (TRANSITIVE CLOSURE)
// ============================================================
// If static function F calls static function G with param p mapped to G's param q,
// and G.paramDereferenced contains q, then F.paramDereferenced should contain p.
// This computes the transitive closure so call-site analysis works through chains.

void propagateParamDereferenced() {
    bool changed = true;
    int iterations = 0;
    const int maxIterations = 100;  // Prevent infinite loops in case of cycles

    while (changed && iterations < maxIterations) {
        changed = false;
        iterations++;

        for (auto& [funcKey, data] : FunctionAnalysis) {
            const FunctionDecl* F = data.decl;
            if (!F) continue;
            if (!hasInternalLinkage(F)) continue;

            // Look at calls FROM this function to other static functions
            // We need to find call sites where F is the caller
            for (auto& [calleeKey, calleeData] : FunctionAnalysis) {
                const FunctionDecl* callee = calleeData.decl;
                if (!callee) continue;
                if (!hasInternalLinkage(callee)) continue;
                if (calleeData.paramDereferenced.empty()) continue;

                // Find call sites from F to callee
                for (const CallSiteInfo& site : calleeData.callSites) {
                    if (site.callerFunc != F) continue;

                    // For each dereferenced param in callee
                    for (unsigned calleeParamIdx : calleeData.paramDereferenced) {
                        if (calleeParamIdx >= site.callExpr->getNumArgs()) continue;

                        const Expr* arg = site.callExpr->getArg(calleeParamIdx)->IgnoreParenImpCasts();
                        if (auto* DRE = dyn_cast<DeclRefExpr>(arg)) {
                            if (auto* PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
                                // arg is a parameter of F - find its index
                                for (unsigned callerParamIdx = 0; callerParamIdx < F->getNumParams(); callerParamIdx++) {
                                    if (F->getParamDecl(callerParamIdx) == PVD) {
                                        // F's param callerParamIdx flows to callee's dereferenced param
                                        if (data.paramDereferenced.find(callerParamIdx) == data.paramDereferenced.end()) {
                                            data.paramDereferenced.insert(callerParamIdx);
                                            changed = true;
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ============================================================
// CALL-SITE WARNING EMISSION
// ============================================================
// After Pass 2, emit warnings at call sites for static functions
// where a potentially-null argument is passed to a dereferenced parameter.

void emitCallSiteWarnings(ASTContext& Ctx) {
    DiagnosticsEngine& Diag = Ctx.getDiagnostics();

    // Create diagnostic ID for call-site null warnings
    unsigned ErrCallSiteNull = Diag.getCustomDiagID(DiagnosticsEngine::Error,
        "DTT: passing potentially-null '%0' to parameter '%1' which is dereferenced in '%2'");
    unsigned NoteCallSiteGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note,
        "add guard: if (%0 != nullptr) { %1(%0); }");

    for (auto& [funcKey, data] : FunctionAnalysis) {
        const FunctionDecl* F = data.decl;
        if (!F) continue;

        // Only process functions with internal linkage that have dereferenced params
        if (!hasInternalLinkage(F)) continue;
        if (data.paramDereferenced.empty()) continue;

        // For each call site, check arguments for dereferenced params
        for (const CallSiteInfo& site : data.callSites) {
            // Skip call sites in static functions where argument is a simple param passthrough
            // This propagates warnings up the call chain to the root caller
            if (site.callerFunc && hasInternalLinkage(site.callerFunc)) {
                // If the caller is also a static function being analyzed,
                // the warning will propagate up to its callers
                continue;
            }

            for (unsigned p : data.paramDereferenced) {
                if (p >= site.callExpr->getNumArgs()) continue;

                const Expr* arg = site.callExpr->getArg(p);
                auto nonnull = isCallArgNonnull(arg, site);

                // If argument is not proven non-null, warn at call site
                if (!nonnull.has_value() || !nonnull.value()) {
                    const ParmVarDecl* param = F->getParamDecl(p);
                    std::string argStr = "";

                    // Try to get argument name
                    const Expr* argExpr = arg->IgnoreParenImpCasts();
                    if (auto* DRE = dyn_cast<DeclRefExpr>(argExpr)) {
                        argStr = DRE->getDecl()->getNameAsString();
                    } else {
                        llvm::raw_string_ostream OS(argStr);
                        arg->printPretty(OS, nullptr, Ctx.getPrintingPolicy());
                    }

                    Diag.Report(site.loc, ErrCallSiteNull)
                        << argStr
                        << param->getNameAsString()
                        << F->getNameAsString();
                    Diag.Report(site.loc, NoteCallSiteGuard)
                        << argStr
                        << F->getNameAsString();
                }
            }
        }
    }
}

// ============================================================
// VISITOR
// ============================================================

class DTTVisitor : public RecursiveASTVisitor<DTTVisitor> {
    ASTContext &Ctx;
    DiagnosticsEngine &Diag;
    // Category F diagnostics (always errors - proven unsafe)
    unsigned ErrFail, ErrUnk, NoteGuard, ErrMultiArg, ErrNotTotal, NoteCase, ErrMethodReq, ErrIndexBounds;
    unsigned ErrNullDeref, ErrNullDerefUnk, NoteNullGuard, NoteNullProvenance;
    // Buffer pointer tracking diagnostics
    unsigned ErrBufferOOB;      // Array subscript out of bounds on pointer
    unsigned NoteBufferSize;    // Note showing buffer size info
    unsigned NoteBufferGuard;   // Note suggesting bounds guard
    unsigned ErrPointerArithOOB; // Pointer arithmetic creates invalid pointer
    unsigned ErrNullPtrArith;    // Pointer arithmetic on null pointer
    // Category C & D diagnostics (mode-dependent severity)
    ModeDependentDiag DiagLostTracking;   // Category C: tracking was lost
    ModeDependentDiag DiagSolverFailed;   // Category D: solver couldn't resolve
    unsigned NoteLostAt;                   // Note pointing to where tracking was lost
    // Category G diagnostics (always errors - annotation conflicts)
    unsigned ErrAnnotationConflict;
    // Lifetime tracking diagnostics (Category H - memory safety)
    unsigned ErrDoubleFree;         // Pointer was already freed
    unsigned NoteFirstFree;         // Location of first free
    unsigned ErrMismatchedDealloc;  // malloc/delete or new/free mismatch
    unsigned ErrUseAfterFree;       // Pointer was freed before use
    unsigned ErrUninitializedPtr;   // Using uninitialized pointer is UB
    FunctionDecl *CurrentFunctionDecl = nullptr;
    std::string CurrentLoopVar;  // Track loop variable for pointer increment in loops

    // Check if a location is in a system header
    bool isInSystemHeader(SourceLocation Loc) {
        if (Loc.isInvalid()) return false;
        return Ctx.getSourceManager().isInSystemHeader(Loc);
    }

public:
    DTTVisitor(ASTContext &C) : Ctx(C), Diag(C.getDiagnostics()) {
        GlobalCtx = &C;
        ErrFail = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: %0 requires %1, but %2.%3 = %4");
        ErrUnk = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: %0 requires %1, but %2.%3 is unknown");
        NoteGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note, "add guard: if (%0.%1 > 0) { ... }");
        ErrMultiArg = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: %0 requires %1, but %2.%3 (%4) != %5.%6 (%7)");
        ErrNotTotal = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: switch on enum is not total — missing case: %0");
        NoteCase = Diag.getCustomDiagID(DiagnosticsEngine::Note, "add: case %0:");
        ErrMethodReq = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: %0::%1 requires %2, but %3.%4 = %5");
        ErrIndexBounds = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: %0::%1 index %2 out of bounds (requires < %3, but %4.%5 = %6)");
        // nullptr tracking diagnostics (Category F)
        ErrNullDeref = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: potential null pointer dereference of '%0' (known to be null)");
        ErrNullDerefUnk = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: potential null pointer dereference of '%0' (nullability unknown)");
        NoteNullGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note, "add guard: if (%0 != nullptr) { ... }");
        NoteNullProvenance = Diag.getCustomDiagID(DiagnosticsEngine::Note, "'%0' may be NULL because: %1");
        // Buffer pointer tracking diagnostics
        ErrBufferOOB = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: buffer index '%0' may be out of bounds (buffer size is %1)");
        NoteBufferSize = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "pointer '%0' at offset %1 into %2-element buffer");
        NoteBufferGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "add guard: if (%0 < %1) { ... }");
        ErrPointerArithOOB = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: pointer arithmetic creates out-of-bounds pointer (offset %0 into %1-element buffer)");
        ErrNullPtrArith = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: pointer arithmetic on null pointer is undefined behavior");
        // Category C: Lost tracking (mode-dependent)
        DiagLostTracking.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify %0 for '%1' (tracking was lost) [-Wdtt-lost-tracking]");
        DiagLostTracking.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify %0 for '%1' (tracking was lost)");
        NoteLostAt = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "tracking was lost here: %0");
        // Category D: Solver failure (mode-dependent)
        DiagSolverFailed.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify %0 for '%1' (solver %2) [-Wdtt-solver-failed]");
        DiagSolverFailed.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify %0 for '%1' (solver %2)");
        // Category G: Annotation conflicts (always error)
        ErrAnnotationConflict = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: conflicting annotations for %0::%1 — %2 conflicts with %3");
        // Category H: Lifetime errors (always error - memory safety)
        ErrDoubleFree = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: double-free of '%0' (pointer was already freed)");
        NoteFirstFree = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "first freed here");
        ErrMismatchedDealloc = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: mismatched deallocation of '%0' (allocated with %1, freed with %2)");
        ErrUseAfterFree = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: use-after-free of '%0' (pointer was freed)");
        ErrUninitializedPtr = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: use of uninitialized pointer '%0' is undefined behavior");
    }
    
    // Skip traversing into system headers (std::span internals, etc.)
    bool TraverseDecl(Decl *D) {
        if (!D) return true;
        if (isInSystemHeader(D->getLocation())) return true;  // Skip system headers
        return RecursiveASTVisitor<DTTVisitor>::TraverseDecl(D);
    }

    // Skip lambda bodies - they execute at call time, not definition time
    // Bug #1 fix: Lambda body should not affect outer scope until called
    bool TraverseLambdaExpr(LambdaExpr *LE) {
        // We intentionally do NOT call RecursiveASTVisitor::TraverseLambdaExpr
        // because that would descend into the lambda body and process assignments
        // as if they happened at lambda definition time.
        return true;
    }

    // Check if function is free() for deallocation detection
    bool isDeallocationFunction(const FunctionDecl *FD) {
        if (!FD) return false;
        std::string name = FD->getNameAsString();
        return name == "free";
    }

    // Convert AllocSource to human-readable string
    std::string allocSourceToString(AllocSource src) {
        switch (src) {
            case AllocSource::Malloc: return "malloc/calloc/realloc";
            case AllocSource::New: return "new";
            case AllocSource::NewArray: return "new[]";
            case AllocSource::AlignedAlloc: return "aligned_alloc";
            case AllocSource::Placement: return "placement new";
            case AllocSource::Unknown: return "unknown";
        }
        return "unknown";
    }

    // Check for annotation conflicts (Category G)
    // Returns true if conflicts found, false otherwise
    bool checkAnnotationConflicts(CXXMethodDecl *M, const std::string& className,
                                   const std::string& methodName, const MethodInfo& info) {
        // Check for conflicting ensures (same field set to different values)
        std::map<std::string, std::vector<std::string>> fieldEnsures;
        for (const auto& post : info.ensures) {
            std::string fieldKey = post.resultField;
            // Use expr.str() to get string representation of the postcondition value
            std::string valueStr = post.expr.str();
            if (post.usesOld) {
                valueStr = "old(...) expression: " + valueStr;
            }
            fieldEnsures[fieldKey].push_back(valueStr);
        }

        bool hasConflict = false;
        for (const auto& kv : fieldEnsures) {
            if (kv.second.size() > 1) {
                // Check if values are different
                std::set<std::string> uniqueValues(kv.second.begin(), kv.second.end());
                if (uniqueValues.size() > 1) {
                    std::string first = kv.second[0];
                    std::string second = kv.second[1];
                    Diag.Report(M->getLocation(), ErrAnnotationConflict)
                        << className << methodName
                        << ("ensures=" + kv.first + " == " + first)
                        << ("ensures=" + kv.first + " == " + second);
                    hasConflict = true;
                }
            }
        }

        // Check for conflicting boolean postconditions
        // (e.g., ensures_if_true=_size == 0 and ensures_if_true=_size == 10)
        std::map<std::pair<std::string, bool>, std::vector<std::string>> boolEnsures;
        for (const auto& bp : info.boolPostconds) {
            auto key = std::make_pair(bp.field, bp.onTrue);
            std::string condStr = bp.onTrue ? "ensures_if_true" : "ensures_if_false";
            const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
            std::string valueStr = condStr + "=" + bp.field + " " + ops[bp.op] + " " + std::to_string(bp.rhs);
            boolEnsures[key].push_back(valueStr);
        }

        for (const auto& kv : boolEnsures) {
            if (kv.second.size() > 1) {
                std::set<std::string> uniqueValues(kv.second.begin(), kv.second.end());
                if (uniqueValues.size() > 1) {
                    Diag.Report(M->getLocation(), ErrAnnotationConflict)
                        << className << methodName
                        << kv.second[0] << kv.second[1];
                    hasConflict = true;
                }
            }
        }

        return hasConflict;
    }

    // Collect method annotations
    bool VisitCXXMethodDecl(CXXMethodDecl *M) {
        if (!M->getParent()) return true;
        std::string className = M->getParent()->getNameAsString();
        std::string methodName = M->getNameAsString();
        // Strip template parameters for constructors
        auto anglePos = methodName.find('<');
        if (anglePos != std::string::npos) methodName = methodName.substr(0, anglePos);
        // Include parameter count to distinguish overloads
        std::string key = className + "::" + methodName + "/" + std::to_string(M->getNumParams());

        MethodInfo info;
        for (auto *A : M->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:requires=", 0) == 0) {
                    auto c = parseConstraint(s.substr(13));
                    if (c) info.preconditions.push_back(*c);
                }
                if (s.rfind("dtt:ensures=", 0) == 0) {
                    auto p = parsePostcondition(s.substr(12), true);
                    if (p) info.ensures.push_back(*p);
                }
                if (s.rfind("dtt:returns=", 0) == 0) {
                    info.returns = s.substr(12);
                }
                if (s.rfind("dtt:ensures_if_true=", 0) == 0) {
                    auto bp = parseBoolPostcondition(s.substr(20), true);
                    if (bp) info.boolPostconds.push_back(*bp);
                }
                if (s.rfind("dtt:ensures_if_false=", 0) == 0) {
                    auto bp = parseBoolPostcondition(s.substr(21), false);
                    if (bp) info.boolPostconds.push_back(*bp);
                }
            }
        }

        // Category G: Check for annotation conflicts (always error, mode-independent)
        if (!info.preconditions.empty() || !info.ensures.empty() || !info.returns.empty() || !info.boolPostconds.empty()) {
            checkAnnotationConflicts(M, className, methodName, info);
            MethodAnnotations[key] = info;
        }
        return true;
    }

    // Handle CXXMethodDecl specially - for some reason TraverseFunctionDecl isn't called for methods
    bool TraverseCXXMethodDecl(CXXMethodDecl *M) {
        // First, call VisitCXXMethodDecl to populate MethodAnnotations
        if (!VisitCXXMethodDecl(M)) return false;
        // Then call TraverseFunctionDecl for body analysis and contract propagation
        return TraverseFunctionDecl(M);
    }

    bool TraverseFunctionDecl(FunctionDecl *F) {
        CurrentFunctionDecl = F;
        std::string fn = F->getNameAsString();
        
        for (auto *A : F->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:ensures=", 0) == 0) {
                    auto post = parsePostcondition(s.substr(12));
                    if (post) FnPostconditions[fn].push_back(*post);
                }
                if (s.rfind("dtt:ensures_if_true=", 0) == 0) {
                    auto bp = parseBoolPostcondition(s.substr(20), true);
                    if (bp) FnBoolPostconditions[fn].push_back(*bp);
                }
                if (s.rfind("dtt:ensures_if_false=", 0) == 0) {
                    auto bp = parseBoolPostcondition(s.substr(21), false);
                    if (bp) FnBoolPostconditions[fn].push_back(*bp);
                }
                if (s == "dtt:total") TotalFunctions.insert(fn);
            }
        }

        // Clear per-function state when entering a new function body
        // This prevents variable names from one function polluting another function's analysis
        if (F->hasBody() && F->isThisDeclarationADefinition()) {
            clearPerFunctionState();

            // Contract propagation — inject dtt:requires as assumptions inside function body
            // This is sound because the caller MUST prove the requires at the call site.
            // If the caller cannot prove it, the call is rejected. Therefore, inside the
            // function body, we can safely assume the precondition holds.
            for (auto *A : F->attrs()) {
                if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                    auto s = Ann->getAnnotation().str();
                    if (s.rfind("dtt:requires=", 0) == 0) {
                        auto c = parseConstraint(s.substr(13));
                        if (c) {
                            injectRequiresAsAssumption(F, *c);
                        }
                    }
                }
            }

            // For methods, also check MethodAnnotations
            if (auto *M = dyn_cast<CXXMethodDecl>(F)) {
                if (M->getParent()) {
                    std::string className = M->getParent()->getNameAsString();
                    std::string methodName = M->getNameAsString();
                    auto anglePos = methodName.find('<');
                    if (anglePos != std::string::npos) methodName = methodName.substr(0, anglePos);
                    std::string key = className + "::" + methodName + "/" + std::to_string(M->getNumParams());

                    auto it = MethodAnnotations.find(key);
                    if (it != MethodAnnotations.end()) {
                        for (const auto& req : it->second.preconditions) {
                            injectRequiresAsAssumption(F, req);
                        }
                    }
                }
            }

            // Inject @paramN_nonnull from function ghost state (computed in Pass 1)
            // If all callers pass non-null for a parameter, mark it as non-null inside the function
            // Otherwise, track provenance to explain why nullability is unknown
            std::string funcKey = getFunctionKey(F);
            auto funcIt = FunctionAnalysis.find(funcKey);
            for (unsigned p = 0; p < F->getNumParams(); p++) {
                const ParmVarDecl* param = F->getParamDecl(p);
                if (!param->getType()->isPointerType()) continue;

                std::string paramName = param->getNameAsString();
                bool isProvenNonnull = false;

                if (funcIt != FunctionAnalysis.end()) {
                    auto paramIt = funcIt->second.paramNonnull.find(p);
                    if (paramIt != funcIt->second.paramNonnull.end() &&
                        paramIt->second.value_or(false)) {
                        // All callers pass non-null for this param - set _nonnull = 1
                        setField(paramName, "_nonnull", SymValue::Const(1));
                        isProvenNonnull = true;
                    }
                }

                // Track provenance for unproven pointer parameters
                if (!isProvenNonnull) {
                    NullProvenanceMap[paramName] = NullProvenance{
                        param->getLocation(),
                        "parameter '" + paramName + "' may be NULL (callers could pass NULL)"
                    };
                }
            }
        }

        bool result = RecursiveASTVisitor::TraverseFunctionDecl(F);
        CurrentFunctionDecl = nullptr;
        return result;
    }

    std::map<std::string, SymValue> captureOldValues(CallExpr *Call) {
        std::map<std::string, SymValue> oldValues;
        for (unsigned i = 0; i < Call->getNumArgs(); i++) {
            auto *Arg = Call->getArg(i)->IgnoreParenImpCasts();
            std::string argPath = getExprPath(Arg);
            if (argPath.empty() || hasRuntimeIndex(argPath)) continue;
            std::string argPrefix = "arg" + std::to_string(i) + ".";
            for (auto& kv : Env) {
                if (kv.first.rfind(argPath + ".", 0) == 0 && kv.first.find("._proof_") == std::string::npos) {
                    std::string field = kv.first.substr(argPath.size() + 1);
                    oldValues[argPrefix + field] = kv.second;
                }
            }
        }
        return oldValues;
    }
    
    // Capture old values for method calls (this-based)
    std::map<std::string, SymValue> captureOldValuesMethod(const std::string& objPath) {
        std::map<std::string, SymValue> oldValues;
        for (auto& kv : Env) {
            if (kv.first.rfind(objPath + ".", 0) == 0 && kv.first.find("._proof_") == std::string::npos) {
                std::string field = kv.first.substr(objPath.size() + 1);
                oldValues[field] = kv.second;
            }
        }
        return oldValues;
    }

    void applyPostconditions(const std::string& resultPath, CallExpr *Call, 
                             const std::map<std::string, SymValue>& oldValues) {
        auto *Callee = Call->getDirectCallee();
        if (!Callee) return;
        std::string fn = Callee->getNameAsString();
        auto it = FnPostconditions.find(fn);
        if (it == FnPostconditions.end()) return;
        
        std::map<std::string, SymValue> subst;
        for (unsigned i = 0; i < Call->getNumArgs(); i++) {
            auto *Arg = Call->getArg(i)->IgnoreParenImpCasts();
            std::string argPath = getExprPath(Arg);
            if (argPath.empty() || hasRuntimeIndex(argPath)) continue;
            std::string argPrefix = "arg" + std::to_string(i) + ".";
            for (auto& kv : Env) if (kv.first.rfind(argPath + ".", 0) == 0 && kv.first.find("._proof_") == std::string::npos) {
                std::string field = kv.first.substr(argPath.size() + 1);
                subst[argPrefix + field] = kv.second;
            }
        }
        for (auto& kv : oldValues) subst["old:" + kv.first] = kv.second;
        
        for (const auto& post : it->second) {
            SymValue result = post.expr.substitute(subst);
            setField(resultPath, post.resultField, result);
        }
    }
    
    void applyVoidPostconditions(CallExpr *Call, const std::map<std::string, SymValue>& oldValues) {
        auto *Callee = Call->getDirectCallee();
        if (!Callee) return;
        std::string fn = Callee->getNameAsString();
        auto it = FnPostconditions.find(fn);
        if (it == FnPostconditions.end()) return;
        
        for (const auto& post : it->second) {
            std::map<std::string, SymValue> subst;
            for (auto& kv : oldValues) subst["old:" + kv.first] = kv.second;
            SymValue result = post.expr.substitute(subst);
            
            if (Call->getNumArgs() > 0) {
                auto *Arg = Call->getArg(0)->IgnoreParenImpCasts();
                std::string argPath = getExprPath(Arg);
                if (!argPath.empty() && !hasRuntimeIndex(argPath)) {
                    setField(argPath, post.resultField, result);
                }
            }
        }
    }
    
    // Apply method postconditions
    void applyMethodPostconditions(const std::string& objPath, const MethodInfo& info,
                                   const std::map<std::string, SymValue>& oldValues,
                                   const std::map<std::string, SymValue>& argValues = {}) {
        std::map<std::string, SymValue> subst;
        
        // Substitute old(_field) with captured values
        for (auto& kv : oldValues) {
            subst["old:" + kv.first] = kv.second;  // old(_size) -> oldValues["_size"]
        }
        
        // Substitute arg0, arg1, etc. with argument values
        for (auto& kv : argValues) {
            subst[kv.first] = kv.second;  // arg0 -> value
        }
        
        // Current values
        for (auto& kv : Env) {
            if (kv.first.rfind(objPath + ".", 0) == 0 && kv.first.find("._proof_") == std::string::npos) {
                std::string field = kv.first.substr(objPath.size() + 1);
                // If field already starts with _ or @ (ghost state), use as-is
                if (!field.empty() && (field[0] == '_' || field[0] == '@')) {
                    subst[field] = kv.second;
                } else {
                    subst["_" + field] = kv.second;
                }
            }
        }
        
        for (const auto& post : info.ensures) {
            SymValue result = post.expr.substitute(subst);
            setField(objPath, post.resultField, result);
        }
    }

    // Track buffer properties for pointers derived from arrays
    void trackBufferPointerInit(const std::string& var, QualType varType, Expr* Init) {
        if (!varType->isPointerType()) return;

        QualType dstPointee = varType->getPointeeType();
        bool dstIsVoid = dstPointee->isVoidType();

        // Helper to set buffer properties
        auto setBufferProps = [&](int64_t baseLen, SymValue offset) {
            setField(var, "_base_length", SymValue::Const(baseLen));
            setField(var, "_offset", offset);
            // Compute and store _size for constraint checking
            SymValue size = SymValue::Sub(SymValue::Const(baseLen), offset);
            setField(var, "_size", size);
        };

        // Helper to set void* properties (bytes only)
        auto setVoidBufferProps = [&](int64_t sizeBytes) {
            setField(var, "_size_bytes", SymValue::Const(sizeBytes));
        };

        // Case 1: Array decay - int* p = arr; or void* v = arr;
        if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
            QualType srcType = DRE->getType();
            if (const auto *CAT = Ctx.getAsConstantArrayType(srcType)) {
                int64_t count = CAT->getSize().getZExtValue();
                QualType srcElem = CAT->getElementType();
                auto maybeSrcElemSize = safeGetTypeSizeInChars(Ctx, srcElem);
                if (!maybeSrcElemSize) return;  // Can't determine size for dependent/incomplete types
                int64_t srcElemSize = *maybeSrcElemSize;
                int64_t totalBytes = count * srcElemSize;

                if (dstIsVoid) {
                    // void* v = arr: store _size_bytes only
                    setVoidBufferProps(totalBytes);
                    setField(var, "_nonnull", SymValue::Const(1));
                    return;
                }
                // Typed pointer: store element count
                auto maybeDstElemSize = safeGetTypeSizeInChars(Ctx, dstPointee);
                if (!maybeDstElemSize) return;  // Can't determine size for dependent/incomplete types
                int64_t dstElemSize = *maybeDstElemSize;
                int64_t dstCount = (dstElemSize > 0) ? totalBytes / dstElemSize : count;
                setBufferProps(dstCount, SymValue::Const(0));
                setField(var, "_nonnull", SymValue::Const(1));
                return;
            }
            // Copying from another tracked pointer
            std::string srcPath = DRE->getDecl()->getNameAsString();
            QualType srcPtrType = srcType;
            bool srcIsVoid = srcPtrType->isPointerType() &&
                             srcPtrType->getPointeeType()->isVoidType();

            // Source is void*: has _size_bytes
            SymValue srcSizeBytes = getField(srcPath, "_size_bytes");
            if (srcIsVoid && srcSizeBytes.kind != SymValue::UNKNOWN) {
                if (dstIsVoid) {
                    // void* to void*: copy _size_bytes
                    setField(var, "_size_bytes", srcSizeBytes);
                } else {
                    // void* to typed*: convert bytes to elements
                    auto maybeDstElemSize = safeGetTypeSizeInChars(Ctx, dstPointee);
                    if (maybeDstElemSize && srcSizeBytes.kind == SymValue::CONST && *maybeDstElemSize > 0) {
                        int64_t dstCount = srcSizeBytes.constVal / *maybeDstElemSize;
                        setBufferProps(dstCount, SymValue::Const(0));
                    }
                }
                setField(var, "_nonnull", getField(srcPath, "_nonnull"));
                return;
            }

            // Source is typed*: has _base_length/_offset
            SymValue srcBaseLen = getField(srcPath, "_base_length");
            if (srcBaseLen.kind != SymValue::UNKNOWN) {
                if (dstIsVoid) {
                    // typed* to void*: convert elements to bytes
                    SymValue srcOffset = getField(srcPath, "_offset");
                    QualType srcPointee = srcPtrType->getPointeeType();
                    auto maybeSrcElemSize = safeGetTypeSizeInChars(Ctx, srcPointee);
                    if (maybeSrcElemSize && srcBaseLen.kind == SymValue::CONST) {
                        int64_t srcSize = srcBaseLen.constVal;
                        if (srcOffset.kind == SymValue::CONST) {
                            srcSize -= srcOffset.constVal;
                        }
                        setVoidBufferProps(srcSize * (*maybeSrcElemSize));
                    }
                } else {
                    // typed* to typed*: copy properties
                    setField(var, "_base_length", srcBaseLen);
                    setField(var, "_offset", getField(srcPath, "_offset"));
                }
                setField(var, "_nonnull", getField(srcPath, "_nonnull"));
            }
            return;
        }

        // Case 2: Multi-dim array row - int* p = arr2d[i]; or int* p = arr3d[i][j];
        // Works for 2D, 3D, or any N-dimensional array where result is 1D array
        if (auto *ASE = dyn_cast<ArraySubscriptExpr>(Init)) {
            // Check if the subscript result is a 1D array (T[N]) that decays to T*
            QualType resultType = ASE->getType();
            if (const auto *CAT = Ctx.getAsConstantArrayType(resultType)) {
                // Result is T[N], which decays to T* with N elements
                int64_t rowSize = CAT->getSize().getZExtValue();
                setBufferProps(rowSize, SymValue::Const(0));
                setField(var, "_nonnull", SymValue::Const(1));
                return;
            }
        }

        // Case 3: Struct member array - int* p = s.data; (data is T[N])
        if (auto *ME = dyn_cast<MemberExpr>(Init)) {
            QualType memberType = ME->getMemberDecl()->getType();
            if (const auto *CAT = Ctx.getAsConstantArrayType(memberType)) {
                int64_t count = CAT->getSize().getZExtValue();
                QualType elemType = CAT->getElementType();
                auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
                if (!maybeElemSize) return;  // Can't determine size for dependent/incomplete types
                int64_t elemSize = *maybeElemSize;
                int64_t totalBytes = count * elemSize;

                if (dstIsVoid) {
                    setVoidBufferProps(totalBytes);
                } else {
                    auto maybeDstElemSize = safeGetTypeSizeInChars(Ctx, dstPointee);
                    if (!maybeDstElemSize) return;  // Can't determine size for dependent/incomplete types
                    int64_t dstElemSize = *maybeDstElemSize;
                    int64_t dstCount = (dstElemSize > 0) ? totalBytes / dstElemSize : count;
                    setBufferProps(dstCount, SymValue::Const(0));
                }
                setField(var, "_nonnull", SymValue::Const(1));
                return;
            }
        }

        // Case 4: Address-of expressions
        if (auto *UO = dyn_cast<UnaryOperator>(Init)) {
            if (UO->getOpcode() == UO_AddrOf) {
                Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();

                // Case 4a: Address-of array element - int* p = &arr[k];
                if (auto *ASE = dyn_cast<ArraySubscriptExpr>(Sub)) {
                    Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
                    if (auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
                        QualType arrType = DRE->getType();
                        if (const auto *CAT = Ctx.getAsConstantArrayType(arrType)) {
                            int64_t count = CAT->getSize().getZExtValue();
                            SymValue idx = exprToSymValueImpl(ASE->getIdx(), Ctx);
                            setBufferProps(count, idx);
                            setField(var, "_nonnull", SymValue::Const(1));
                            return;
                        }
                    }
                }

                // Case 4b: Address-of variable - int* p = &a; or int* p = &arr;
                if (auto *DRE = dyn_cast<DeclRefExpr>(Sub)) {
                    QualType srcVarType = DRE->getType();
                    auto maybeDstElemSize = safeGetTypeSizeInChars(Ctx, dstPointee);
                    if (!maybeDstElemSize) return;  // Can't determine size for dependent/incomplete types
                    int64_t dstElemSize = *maybeDstElemSize;

                    // Case 4b-i: Address-of array - treat like array decay
                    // char* p = reinterpret_cast<char*>(&arr[0]) should track full array
                    if (const auto *CAT = Ctx.getAsConstantArrayType(srcVarType)) {
                        int64_t count = CAT->getSize().getZExtValue();
                        QualType elemType = CAT->getElementType();
                        auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
                        if (!maybeElemSize) return;  // Can't determine size for dependent/incomplete types
                        int64_t elemSize = *maybeElemSize;
                        int64_t totalBytes = count * elemSize;

                        if (dstIsVoid) {
                            setVoidBufferProps(totalBytes);
                        } else if (dstElemSize == 1) {
                            // Byte pointer: size is total bytes
                            setBufferProps(totalBytes, SymValue::Const(0));
                        } else if (dstElemSize == elemSize) {
                            // Same element type: preserve count
                            setBufferProps(count, SymValue::Const(0));
                        } else if (dstElemSize > 0) {
                            // Different element type: convert through bytes
                            setBufferProps(totalBytes / dstElemSize, SymValue::Const(0));
                        }
                        setField(var, "_nonnull", SymValue::Const(1));
                        return;
                    }

                    // Case 4b-ii: Address-of struct - track struct size in bytes
                    if (srcVarType->isRecordType()) {
                        auto maybeStructSize = safeGetTypeSizeInChars(Ctx, srcVarType);
                        if (!maybeStructSize) return;  // Can't determine size for dependent/incomplete types
                        int64_t structSize = *maybeStructSize;

                        if (dstIsVoid) {
                            setVoidBufferProps(structSize);
                        } else if (dstElemSize > 0) {
                            setBufferProps(structSize / dstElemSize, SymValue::Const(0));
                        }
                        setField(var, "_nonnull", SymValue::Const(1));
                        return;
                    }

                    // Case 4b-iii: Address-of single non-array variable
                    if (!srcVarType->isArrayType()) {
                        // Compute size based on variable type vs pointer element type
                        auto maybeSrcSize = safeGetTypeSizeInChars(Ctx, srcVarType);
                        if (!maybeSrcSize) return;  // Can't determine size for dependent/incomplete types
                        int64_t srcSize = *maybeSrcSize;

                        if (dstIsVoid) {
                            setVoidBufferProps(srcSize);
                        } else if (dstElemSize > 0 && srcSize > 0) {
                            setBufferProps(srcSize / dstElemSize, SymValue::Const(0));
                        } else {
                            setBufferProps(1, SymValue::Const(0));  // Fallback: 1 element
                        }
                        setField(var, "_nonnull", SymValue::Const(1));
                        return;
                    }
                }
            }
        }

        // Case 3: Array + arithmetic - int* p = arr + k;
        if (auto *BO = dyn_cast<BinaryOperator>(Init)) {
            if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
                Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
                Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
                // arr + k
                if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                    QualType arrType = DRE->getType();
                    if (const auto *CAT = Ctx.getAsConstantArrayType(arrType)) {
                        int64_t count = CAT->getSize().getZExtValue();
                        SymValue offsetVal = exprToSymValueImpl(RHS, Ctx);
                        if (BO->getOpcode() == BO_Sub) {
                            offsetVal = SymValue::Sub(SymValue::Const(0), offsetVal);
                        }
                        // Check bounds on pointer creation per C++ standard:
                        // offset must be in [0, count] (one-past-end is valid)
                        auto evalOffset = offsetVal.eval();
                        if (evalOffset) {
                            int64_t off = *evalOffset;
                            if (off < 0 || off > count) {
                                Diag.Report(Init->getBeginLoc(), ErrPointerArithOOB)
                                    << (int)off << (int)count;
                            }
                        }
                        setBufferProps(count, offsetVal);
                        setField(var, "_nonnull", SymValue::Const(1));
                        return;
                    }
                    // Pointer arithmetic from another tracked pointer
                    std::string srcPath = DRE->getDecl()->getNameAsString();
                    SymValue srcBaseLen = getField(srcPath, "_base_length");
                    if (srcBaseLen.kind != SymValue::UNKNOWN) {
                        SymValue srcOffset = getField(srcPath, "_offset");
                        SymValue offsetVal = exprToSymValueImpl(RHS, Ctx);
                        SymValue newOffset = (BO->getOpcode() == BO_Add)
                            ? SymValue::Add(srcOffset, offsetVal)
                            : SymValue::Sub(srcOffset, offsetVal);

                        // Bounds check: new offset must be in [0, base_length]
                        auto evalNewOffset = newOffset.eval();
                        auto evalBaseLen = srcBaseLen.eval();
                        if (evalNewOffset && evalBaseLen) {
                            int64_t off = *evalNewOffset;
                            int64_t len = *evalBaseLen;
                            if (off < 0 || off > len) {
                                Diag.Report(Init->getBeginLoc(), ErrPointerArithOOB)
                                    << (int)off << (int)len;
                            }
                        }

                        // Update tracking with new offset
                        setField(var, "_base_length", srcBaseLen);
                        setField(var, "_offset", newOffset);
                        SymValue size = SymValue::Sub(srcBaseLen, newOffset);
                        setField(var, "_size", size);
                        setField(var, "_nonnull", getField(srcPath, "_nonnull"));
                        return;
                    }
                    // No buffer tracking - check for null pointer arithmetic
                    SymValue srcNonnull = getField(srcPath, "_nonnull");
                    if (srcNonnull.kind == SymValue::CONST && srcNonnull.constVal == 0) {
                        // Pointer arithmetic on known null pointer is UB
                        Diag.Report(Init->getBeginLoc(), ErrNullPtrArith);
                        return;
                    }
                    // Propagate nullability even without buffer tracking
                    setField(var, "_nonnull", srcNonnull);
                }
            }
        }

        // Case 4: new[] expression - int* p = new int[N];
        if (auto *NE = dyn_cast<CXXNewExpr>(Init)) {
            if (NE->isArray()) {
                if (auto *ArrSizeExpr = NE->getArraySize().value_or(nullptr)) {
                    SymValue sizeVal = exprToSymValueImpl(const_cast<Expr*>(ArrSizeExpr), Ctx);
                    if (sizeVal.kind == SymValue::CONST) {
                        setBufferProps(sizeVal.constVal, SymValue::Const(0));
                    } else {
                        // Symbolic size
                        setField(var, "_base_length", sizeVal);
                        setField(var, "_offset", SymValue::Const(0));
                    }
                    setField(var, "_nonnull", SymValue::Const(1));
                    // Track allocation source for lifetime analysis
                    PointerLifetimeState pls;
                    pls.lifetime = PointerLifetime::Valid;
                    pls.allocLocation = NE->getBeginLoc();
                    if (isPlacementNew(NE)) {
                        pls.allocSource = AllocSource::Placement;
                    } else {
                        pls.allocSource = AllocSource::NewArray;
                    }
                    PointerLifetimes[var] = pls;
                    return;
                }
            }
        }

        // Case 5: Type cast - char* c = (char*)arr; or char* c = (char*)v;
        // Preserve byte count, recompute element count based on new element size
        const CastExpr *CE = nullptr;
        if (auto *CSC = dyn_cast<CStyleCastExpr>(Init)) CE = CSC;
        else if (auto *CC = dyn_cast<CXXStaticCastExpr>(Init)) CE = CC;
        else if (auto *RC = dyn_cast<CXXReinterpretCastExpr>(Init)) CE = RC;

        if (CE) {
            const Expr *SubExpr = CE->getSubExpr()->IgnoreParenImpCasts();
            QualType srcType = SubExpr->getType();
            int64_t dstElemSize = 1;
            if (!dstIsVoid) {
                auto maybeDstElemSize = safeGetTypeSizeInChars(Ctx, dstPointee);
                if (!maybeDstElemSize) return;  // Can't determine size for dependent/incomplete types
                dstElemSize = *maybeDstElemSize;
            }

            // Check if casting from array to pointer
            if (const auto *CAT = Ctx.getAsConstantArrayType(srcType)) {
                // Casting array directly: char* c = (char*)arr or void* v = (void*)arr
                int64_t srcCount = CAT->getSize().getZExtValue();
                QualType srcElem = CAT->getElementType();
                auto maybeSrcElemSize = safeGetTypeSizeInChars(Ctx, srcElem);
                if (!maybeSrcElemSize) return;  // Can't determine size for dependent/incomplete types
                int64_t srcElemSize = *maybeSrcElemSize;
                int64_t totalBytes = srcCount * srcElemSize;

                if (dstIsVoid) {
                    setVoidBufferProps(totalBytes);
                } else {
                    int64_t dstCount = (dstElemSize > 0) ? totalBytes / dstElemSize : 0;
                    setBufferProps(dstCount, SymValue::Const(0));
                }
                setField(var, "_nonnull", SymValue::Const(1));
                return;
            }

            // Check if casting from tracked pointer
            if (auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
                std::string srcPath = DRE->getDecl()->getNameAsString();
                QualType srcPointee = srcType->isPointerType() ? srcType->getPointeeType() : QualType();
                bool srcIsVoid = srcPointee.isNull() ? false : srcPointee->isVoidType();

                // Source is void*: use _size_bytes
                SymValue srcSizeBytes = getField(srcPath, "_size_bytes");
                if (srcIsVoid && srcSizeBytes.kind != SymValue::UNKNOWN) {
                    if (dstIsVoid) {
                        // void* to void*: copy _size_bytes
                        setField(var, "_size_bytes", srcSizeBytes);
                    } else {
                        // void* to typed*: convert bytes to elements
                        if (srcSizeBytes.kind == SymValue::CONST && dstElemSize > 0) {
                            int64_t dstCount = srcSizeBytes.constVal / dstElemSize;
                            setBufferProps(dstCount, SymValue::Const(0));
                        }
                    }
                    setField(var, "_nonnull", getField(srcPath, "_nonnull"));
                    return;
                }

                // Source is typed*: use _base_length
                SymValue srcBaseLen = getField(srcPath, "_base_length");
                if (srcBaseLen.kind != SymValue::UNKNOWN) {
                    int64_t srcElemSize = 0;
                    if (!srcPointee.isNull()) {
                        auto maybeSrcElemSize = safeGetTypeSizeInChars(Ctx, srcPointee);
                        if (maybeSrcElemSize) srcElemSize = *maybeSrcElemSize;
                    }

                    if (dstIsVoid) {
                        // typed* to void*: convert to bytes
                        if (srcBaseLen.kind == SymValue::CONST && srcElemSize > 0) {
                            SymValue srcOffset = getField(srcPath, "_offset");
                            int64_t srcSize = srcBaseLen.constVal;
                            if (srcOffset.kind == SymValue::CONST) {
                                srcSize -= srcOffset.constVal;
                            }
                            setVoidBufferProps(srcSize * srcElemSize);
                        }
                    } else {
                        // typed* to typed*: convert through bytes
                        if (srcBaseLen.kind == SymValue::CONST && srcElemSize > 0 && dstElemSize > 0) {
                            int64_t totalBytes = srcBaseLen.constVal * srcElemSize;
                            int64_t dstCount = totalBytes / dstElemSize;
                            setBufferProps(dstCount, SymValue::Const(0));
                        }
                    }
                    setField(var, "_nonnull", getField(srcPath, "_nonnull"));
                    return;
                }
            }

            // Check if casting from address-of expression: (char*)&arr[0] or (char*)&s
            if (auto *UO = dyn_cast<UnaryOperator>(SubExpr)) {
                if (UO->getOpcode() == UO_AddrOf) {
                    Expr *AddrSub = UO->getSubExpr()->IgnoreParenImpCasts();

                    // (char*)&arr[k] - address of array element
                    if (auto *ASE = dyn_cast<ArraySubscriptExpr>(AddrSub)) {
                        Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
                        if (auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
                            QualType arrType = DRE->getType();
                            if (const auto *CAT = Ctx.getAsConstantArrayType(arrType)) {
                                int64_t count = CAT->getSize().getZExtValue();
                                QualType elemType = CAT->getElementType();
                                auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
                                if (!maybeElemSize) return;  // Can't determine size for dependent/incomplete types
                                int64_t elemSize = *maybeElemSize;
                                int64_t totalBytes = count * elemSize;
                                SymValue idx = exprToSymValueImpl(ASE->getIdx(), Ctx);

                                // Compute remaining bytes from offset
                                int64_t offsetBytes = 0;
                                if (idx.kind == SymValue::CONST) {
                                    offsetBytes = idx.constVal * elemSize;
                                }
                                int64_t remainingBytes = totalBytes - offsetBytes;

                                if (dstIsVoid) {
                                    setVoidBufferProps(remainingBytes);
                                } else if (dstElemSize > 0) {
                                    setBufferProps(remainingBytes / dstElemSize, SymValue::Const(0));
                                }
                                setField(var, "_nonnull", SymValue::Const(1));
                                return;
                            }
                        }
                    }

                    // (char*)&var - address of variable (array or struct or scalar)
                    if (auto *DRE = dyn_cast<DeclRefExpr>(AddrSub)) {
                        QualType addrType = DRE->getType();

                        // Address of array
                        if (const auto *CAT = Ctx.getAsConstantArrayType(addrType)) {
                            int64_t count = CAT->getSize().getZExtValue();
                            QualType elemType = CAT->getElementType();
                            auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
                            if (!maybeElemSize) return;  // Can't determine size for dependent/incomplete types
                            int64_t elemSize = *maybeElemSize;
                            int64_t totalBytes = count * elemSize;

                            if (dstIsVoid) {
                                setVoidBufferProps(totalBytes);
                            } else if (dstElemSize > 0) {
                                setBufferProps(totalBytes / dstElemSize, SymValue::Const(0));
                            }
                            setField(var, "_nonnull", SymValue::Const(1));
                            return;
                        }

                        // Address of struct or scalar
                        auto maybeVarSize = safeGetTypeSizeInChars(Ctx, addrType);
                        if (!maybeVarSize) return;  // Can't determine size for dependent/incomplete types
                        int64_t varSize = *maybeVarSize;
                        if (dstIsVoid) {
                            setVoidBufferProps(varSize);
                        } else if (dstElemSize > 0 && varSize > 0) {
                            setBufferProps(varSize / dstElemSize, SymValue::Const(0));
                        }
                        setField(var, "_nonnull", SymValue::Const(1));
                        return;
                    }
                }
            }
        }
    }

    bool VisitVarDecl(VarDecl *D) {
        std::string var = D->getNameAsString();

        // Extract template parameters from type (std::array, std::span, user templates)
        extractTemplateParams(var, D->getType());

        // Track array sizes for requires annotation checking
        QualType varType = D->getType();
        if (const auto *CAT = Ctx.getAsConstantArrayType(varType)) {
            int64_t count = CAT->getSize().getZExtValue();
            setField(var, "_size", SymValue::Const(count));
            setField(var, "_base_length", SymValue::Const(count));
            setField(var, "_offset", SymValue::Const(0));
            // Also compute bytes for void* compatibility
            QualType elemType = CAT->getElementType();
            auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
            if (maybeElemSize) {
                setField(var, "_size_bytes", SymValue::Const(count * (*maybeElemSize)));
            }
        }

        // Track pointer nullability on initialization
        if (varType->isPointerType() && D->hasInit()) {
            Expr *Init = D->getInit()->IgnoreParenCasts();

            // Use helper to infer nullability from expression
            if (auto nonnull = inferNonnullFromExpr(Init)) {
                setField(var, "_nonnull", SymValue::Const(*nonnull));
                // Track lifetime and size for new expressions
                if (auto *NewE = dyn_cast<CXXNewExpr>(Init)) {
                    PointerLifetimeState pls;
                    pls.lifetime = PointerLifetime::Valid;
                    pls.allocLocation = NewE->getBeginLoc();
                    if (isPlacementNew(NewE)) {
                        pls.allocSource = AllocSource::Placement;
                    } else {
                        pls.allocSource = NewE->isArray() ? AllocSource::NewArray : AllocSource::New;
                    }
                    PointerLifetimes[var] = pls;
                    // Track allocation size for bounds checking
                    if (NewE->isArray()) {
                        if (auto ArraySizeExpr = NewE->getArraySize()) {
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(*ArraySizeExpr, R, Ctx)) {
                                if (auto count = safeGetInt64(R.Val.getInt())) {
                                    setField(var, "_size", SymValue::Const(*count));
                                    setField(var, "_base_length", SymValue::Const(*count));
                                    setField(var, "_offset", SymValue::Const(0));
                                }
                            }
                        }
                    } else {
                        // Scalar new: size is 1 element
                        setField(var, "_size", SymValue::Const(1));
                        setField(var, "_base_length", SymValue::Const(1));
                        setField(var, "_offset", SymValue::Const(0));
                    }
                }
            } else if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
                // Copying from another pointer - copy its nullability and proofs
                std::string srcPath = DRE->getDecl()->getNameAsString();
                // Check if source pointer is uninitialized - using it is UB
                SymValue srcInit = getField(srcPath, "_initialized");
                if (srcInit.kind == SymValue::CONST && srcInit.constVal == 0) {
                    Diag.Report(D->getInit()->getBeginLoc(), ErrUninitializedPtr) << srcPath;
                }
                setField(var, "_nonnull", getField(srcPath, "_nonnull"));
                // Copy proofs for consistency with assignment path
                if (Env.find(srcPath + "._proof__nonnull!=0") != Env.end()) {
                    addProof(var, "_nonnull!=0");
                }
                if (Env.find(srcPath + "._proof__nonnull==0") != Env.end()) {
                    addProof(var, "_nonnull==0");
                }
                // Track pointer alias for future proof propagation
                PointerAliases[var] = srcPath;
                // Copy initialization state (to propagate uninitialized through copies)
                SymValue initState = getField(srcPath, "_initialized");
                if (initState.kind != SymValue::UNKNOWN) {
                    setField(var, "_initialized", initState);
                } else {
                    setField(var, "_initialized", SymValue::Const(1));
                }
            } else if (auto *CE = dyn_cast<CallExpr>(Init)) {
                // Check if callee has @returns_nonnull from function ghost state (Pass 1)
                bool gotNonnull = false;
                if (auto *callee = CE->getDirectCallee()) {
                    std::string calleeKey = getFunctionKey(callee);
                    auto funcIt = FunctionAnalysis.find(calleeKey);
                    if (funcIt != FunctionAnalysis.end() &&
                        funcIt->second.returnsNonnull.value_or(false)) {
                        // All return paths return non-null - set _nonnull = 1
                        setField(var, "_nonnull", SymValue::Const(1));
                        gotNonnull = true;
                    }
                }
                if (!gotNonnull) {
                    setField(var, "_nonnull", SymValue::Unknown());
                    // Track malloc/calloc/realloc provenance for enhanced diagnostics
                    std::string allocName = getAllocatorName(CE);
                    if (!allocName.empty()) {
                        recordAllocProvenance(var, CE, allocName);
                        // Track allocation source for lifetime analysis
                        PointerLifetimeState pls;
                        pls.lifetime = PointerLifetime::Valid;
                        pls.allocLocation = CE->getBeginLoc();
                        if (allocName == "aligned_alloc" || allocName == "posix_memalign") {
                            pls.allocSource = AllocSource::AlignedAlloc;
                        } else {
                            pls.allocSource = AllocSource::Malloc;
                        }
                        PointerLifetimes[var] = pls;
                        // Track allocation size for bounds checking
                        QualType pointeeType = varType->getPointeeType();
                        bool isVoidPtr = pointeeType->isVoidType();
                        auto maybeElemSize = isVoidPtr ? std::optional<int64_t>(1) : safeGetTypeSizeInChars(Ctx, pointeeType);
                        if (allocName == "calloc" && CE->getNumArgs() >= 2) {
                            // calloc(count, size) - count is first arg
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(CE->getArg(0), R, Ctx)) {
                                if (auto count = safeGetInt64(R.Val.getInt())) {
                                    if (isVoidPtr) {
                                        Expr::EvalResult R2;
                                        if (safeEvaluateAsInt(CE->getArg(1), R2, Ctx)) {
                                            if (auto elemSize = safeGetInt64(R2.Val.getInt())) {
                                                setField(var, "_size_bytes", SymValue::Const(*count * *elemSize));
                                            }
                                        }
                                    } else {
                                        setField(var, "_size", SymValue::Const(*count));
                                        setField(var, "_base_length", SymValue::Const(*count));
                                        setField(var, "_offset", SymValue::Const(0));
                                    }
                                }
                            }
                        } else if ((allocName == "malloc" || allocName == "realloc" ||
                                    allocName == "aligned_alloc" || allocName == "reallocarray") &&
                                   CE->getNumArgs() >= 1) {
                            // malloc(size), realloc(ptr, size), aligned_alloc(align, size)
                            unsigned sizeArgIdx = (allocName == "realloc" || allocName == "aligned_alloc") ? 1 : 0;
                            if (CE->getNumArgs() > sizeArgIdx) {
                                Expr::EvalResult R;
                                if (safeEvaluateAsInt(CE->getArg(sizeArgIdx), R, Ctx)) {
                                    if (auto sizeBytes = safeGetInt64(R.Val.getInt())) {
                                        setField(var, "_size_bytes", SymValue::Const(*sizeBytes));
                                        if (!isVoidPtr && maybeElemSize && *maybeElemSize > 0) {
                                            int64_t count = *sizeBytes / *maybeElemSize;
                                            setField(var, "_size", SymValue::Const(count));
                                            setField(var, "_base_length", SymValue::Const(count));
                                            setField(var, "_offset", SymValue::Const(0));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                // Unknown source - explicitly mark as unknown
                // This ensures state is well-defined and any stale values are cleared
                setField(var, "_nonnull", SymValue::Unknown());
            }
            // Pointer has initializer - mark as initialized
            setField(var, "_initialized", SymValue::Const(1));
        } else if (varType->isPointerType() && !D->hasInit()) {
            // Uninitialized local pointer - using it in any way is UB
            // Only track for local variables, not parameters or extern
            // Function parameters (ParmVarDecl) are initialized by the caller
            if (D->hasLocalStorage() && !D->isExceptionVariable() && !isa<ParmVarDecl>(D)) {
                setField(var, "_initialized", SymValue::Const(0));
                setField(var, "_nonnull", SymValue::Unknown());
            }
        }

        for (auto *A : D->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:", 0) == 0) {
                    auto rest = s.substr(4);
                    auto eqPos = rest.find('=');
                    if (eqPos != std::string::npos) {
                        std::string field = rest.substr(0, eqPos);
                        auto value = safeStoll(rest.substr(eqPos + 1));
                        if (value) setField(var, field, SymValue::Const(*value));
                    }
                }
            }
        }
        if (D->hasInit()) {
            auto *Init = D->getInit()->IgnoreParenImpCasts();
            
            // Unwrap ExprWithCleanups and CXXBindTemporaryExpr to find CallExpr
            Expr *Unwrapped = Init;
            if (auto *EWC = dyn_cast<ExprWithCleanups>(Unwrapped))
                Unwrapped = EWC->getSubExpr()->IgnoreParenImpCasts();
            if (auto *BTE = dyn_cast<CXXBindTemporaryExpr>(Unwrapped))
                Unwrapped = BTE->getSubExpr()->IgnoreParenImpCasts();
            
            if (auto *Call = dyn_cast<CallExpr>(Unwrapped)) {
                auto oldVals = captureOldValues(Call);
                applyPostconditions(var, Call, oldVals);
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
                clearProperties(var);
                std::string srcVar = DRE->getDecl()->getNameAsString();
                copyProperties(var, srcVar);
                // Copy VarBoundProofs from source variable to this new variable
                // e.g., if j = i and i has proof "i < v._size", add "j < v._size"
                std::vector<VarBoundProof> newProofs;
                for (const auto& p : VarBoundProofs) {
                    if (p.varName == srcVar) {
                        newProofs.push_back({var, p.objPath, p.field, p.op});
                    }
                }
                for (const auto& np : newProofs) {
                    VarBoundProofs.push_back(np);
                }
            }
            if (auto *CE = dyn_cast<CXXConstructExpr>(Init)) {
                // Apply constructor postconditions
                auto *Ctor = CE->getConstructor();
                if (Ctor && Ctor->getParent()) {
                    std::string className = Ctor->getParent()->getNameAsString();
                    std::string key = className + "::" + className + "/" + std::to_string(CE->getNumArgs());
                    auto it = MethodAnnotations.find(key);
                    if (it != MethodAnnotations.end()) {
                        // Collect argument values for substitution
                        std::map<std::string, SymValue> argVals;
                        for (unsigned i = 0; i < CE->getNumArgs(); i++) {
                            Expr *ArgExpr = CE->getArg(i)->IgnoreParenImpCasts();
                            std::string argKey = "arg" + std::to_string(i);
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                                auto val = safeGetInt64(R.Val.getInt());
                                if (val) argVals[argKey] = SymValue::Const(*val);
                                else argVals[argKey] = SymValue::Unknown();
                            } else {
                                std::string argPath = getExprPath(ArgExpr);
                                if (!argPath.empty() && !hasRuntimeIndex(argPath)) {
                                    argVals[argKey] = SymValue::Var(argPath);
                                } else {
                                    argVals[argKey] = SymValue::Unknown();
                                }
                            }
                        }
                        
                        for (const auto& post : it->second.ensures) {
                            SymValue result = post.expr.substitute(argVals);
                            setField(var, post.resultField, result);
                        }
                    }
                }
                for (unsigned i = 0; i < CE->getNumArgs(); i++) {
                    auto *Arg = CE->getArg(i)->IgnoreParenImpCasts();
                    if (auto *Call = dyn_cast<CallExpr>(Arg)) {
                        auto oldVals = captureOldValues(Call);
                        applyPostconditions(var, Call, oldVals);
                    }
                    if (auto *DRE = dyn_cast<DeclRefExpr>(Arg)) {
                        std::string srcPath = DRE->getDecl()->getNameAsString();
                        // Only copy properties if source and dest have the same base type
                        // This handles copy construction (Vec w = v) but not different types
                        // For std::span<T,N> sp(data), data is an array, not a span, so skip
                        QualType srcType = DRE->getType().getCanonicalType();
                        QualType dstType = D->getType().getCanonicalType();

                        // Check if types are the same (ignoring qualifiers like const)
                        if (srcType.getUnqualifiedType() == dstType.getUnqualifiedType()) {
                            // Same type - this is copy construction, copy all properties
                            clearProperties(var);
                            copyProperties(var, srcPath);
                        }
                        // Otherwise, don't touch the destination's properties
                        // (template-derived _size should remain)
                    }
                    // Handle move construction: Vec v2(std::move(v1))
                    if (isStdMove(Arg)) {
                        std::string srcPath = getMovedFromPath(Arg);
                        if (!srcPath.empty()) {
                            clearProperties(var);
                            copyProperties(var, srcPath);
                            invalidateProperties(srcPath);
                        }
                    }
                    if (auto *SIL = dyn_cast<CXXStdInitializerListExpr>(Arg)) {
                        if (auto *IL = dyn_cast<InitListExpr>(SIL->getSubExpr()->IgnoreParenImpCasts())) {
                            setField(var, "len", SymValue::Const(IL->getNumInits()));
                            setField(var, "size", SymValue::Const(IL->getNumInits()));
                        }
                    }
                }
            }
            if (auto *IL = dyn_cast<InitListExpr>(Init)) {
                setField(var, "len", SymValue::Const(IL->getNumInits()));
                setField(var, "size", SymValue::Const(IL->getNumInits()));
            }
            
            // Track integer variable assignments for index arithmetic
            // e.g., int mid = v.size() / 2;
            QualType type = D->getType().getCanonicalType();
            if (type->isIntegerType()) {
                SymValue val = exprToSymValueImpl(Init, Ctx);
                if (val.kind != SymValue::UNKNOWN) {
                    Env[var] = val;
                }
            }
        }

        // Buffer pointer tracking: track bounds for pointers derived from arrays
        // This must run AFTER the hasInit block which may call clearProperties
        if (D->getType()->isPointerType() && D->hasInit()) {
            trackBufferPointerInit(var, D->getType(), D->getInit()->IgnoreParenImpCasts());
        }

        return true;
    }

    bool VisitBinaryOperator(BinaryOperator *B) {
        auto *LHS = B->getLHS()->IgnoreParenImpCasts();
        auto *RHS = B->getRHS()->IgnoreParenImpCasts();
        std::string lhsPath = getExprPath(LHS);

        // Handle pointer compound assignment: p += n, p -= n
        if ((B->getOpcode() == BO_AddAssign || B->getOpcode() == BO_SubAssign) &&
            LHS->getType()->isPointerType()) {
            if (!lhsPath.empty()) {
                // Check for null pointer arithmetic (UB)
                SymValue nonnull = getField(lhsPath, "_nonnull");
                if (nonnull.kind == SymValue::CONST && nonnull.constVal == 0) {
                    Diag.Report(B->getBeginLoc(), ErrNullPtrArith);
                    return true;
                }

                SymValue baseLen = getField(lhsPath, "_base_length");
                if (baseLen.kind != SymValue::UNKNOWN) {
                    SymValue offset = getField(lhsPath, "_offset");
                    SymValue delta = exprToSymValueImpl(RHS, Ctx);
                    SymValue newOffset = (B->getOpcode() == BO_AddAssign)
                        ? SymValue::Add(offset, delta)
                        : SymValue::Sub(offset, delta);
                    setField(lhsPath, "_offset", newOffset);
                }
            }
            return true;
        }

        if (B->getOpcode() != BO_Assign) return true;

        if (!lhsPath.empty() && !hasRuntimeIndex(lhsPath)) {
            // Handle pointer reassignment: p = p + n, p = p - n, p = q + n
            QualType lhsType = B->getLHS()->getType();
            if (lhsType->isPointerType()) {
                if (auto *BinOp = dyn_cast<BinaryOperator>(RHS)) {
                    if (BinOp->getOpcode() == BO_Add || BinOp->getOpcode() == BO_Sub) {
                        auto *OpLHS = BinOp->getLHS()->IgnoreParenImpCasts();
                        auto *OpRHS = BinOp->getRHS()->IgnoreParenImpCasts();

                        // Find the pointer operand and delta
                        Expr *ptrExpr = nullptr;
                        Expr *deltaExpr = nullptr;
                        if (OpLHS->getType()->isPointerType()) {
                            ptrExpr = OpLHS;
                            deltaExpr = OpRHS;
                        } else if (OpRHS->getType()->isPointerType()) {
                            ptrExpr = OpRHS;
                            deltaExpr = OpLHS;
                        }

                        if (ptrExpr && deltaExpr) {
                            std::string srcPath = getExprPath(ptrExpr);
                            if (!srcPath.empty()) {
                                SymValue baseLen = getField(srcPath, "_base_length");
                                if (baseLen.kind != SymValue::UNKNOWN) {
                                    // Copy buffer tracking from source
                                    SymValue srcOffset = getField(srcPath, "_offset");
                                    SymValue delta = exprToSymValueImpl(deltaExpr, Ctx);
                                    SymValue newOffset = (BinOp->getOpcode() == BO_Add)
                                        ? SymValue::Add(srcOffset, delta)
                                        : SymValue::Sub(srcOffset, delta);

                                    setField(lhsPath, "_base_length", baseLen);
                                    setField(lhsPath, "_offset", newOffset);
                                    setField(lhsPath, "_nonnull", SymValue::Const(1));
                                    return true;  // Skip default nullability handling
                                }
                            }
                        }
                    }
                }
            }
            // Track pointer nullability on assignment
            if (lhsType->isPointerType()) {
                // Invalidate old nullability state before setting new value
                invalidateNullability(lhsPath);
                // Clear old alias relationship - pointer now points to different value
                PointerAliases.erase(lhsPath);
                // Also invalidate aliases that point TO this pointer (they're now stale)
                for (auto it = PointerAliases.begin(); it != PointerAliases.end(); ) {
                    if (it->second == lhsPath) {
                        it = PointerAliases.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Use helper to infer nullability from expression
                if (auto nonnull = inferNonnullFromExpr(RHS)) {
                    setField(lhsPath, "_nonnull", SymValue::Const(*nonnull));
                    // Track lifetime and size for new expressions
                    if (auto *NewE = dyn_cast<CXXNewExpr>(RHS)) {
                        PointerLifetimeState pls;
                        pls.lifetime = PointerLifetime::Valid;
                        pls.allocLocation = NewE->getBeginLoc();
                        if (isPlacementNew(NewE)) {
                            pls.allocSource = AllocSource::Placement;
                        } else {
                            pls.allocSource = NewE->isArray() ? AllocSource::NewArray : AllocSource::New;
                        }
                        PointerLifetimes[lhsPath] = pls;
                        // Track allocation size for bounds checking
                        if (NewE->isArray()) {
                            if (auto ArraySizeExpr = NewE->getArraySize()) {
                                Expr::EvalResult R;
                                if (safeEvaluateAsInt(*ArraySizeExpr, R, Ctx)) {
                                    if (auto count = safeGetInt64(R.Val.getInt())) {
                                        setField(lhsPath, "_size", SymValue::Const(*count));
                                        setField(lhsPath, "_base_length", SymValue::Const(*count));
                                        setField(lhsPath, "_offset", SymValue::Const(0));
                                    }
                                }
                            }
                        } else {
                            // Scalar new: size is 1 element
                            setField(lhsPath, "_size", SymValue::Const(1));
                            setField(lhsPath, "_base_length", SymValue::Const(1));
                            setField(lhsPath, "_offset", SymValue::Const(0));
                        }
                    }
                } else if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                    // Copy nullability from source pointer
                    std::string srcPath = DRE->getDecl()->getNameAsString();
                    setField(lhsPath, "_nonnull", getField(srcPath, "_nonnull"));
                    // Also copy proofs
                    if (Env.find(srcPath + "._proof__nonnull!=0") != Env.end()) {
                        addProof(lhsPath, "_nonnull!=0");
                    }
                    if (Env.find(srcPath + "._proof__nonnull==0") != Env.end()) {
                        addProof(lhsPath, "_nonnull==0");
                    }
                    // Track pointer alias for future proof propagation
                    PointerAliases[lhsPath] = srcPath;
                } else if (auto *CE = dyn_cast<CallExpr>(RHS->IgnoreParenCasts())) {
                    // Function call - mark as unknown and track malloc provenance
                    setField(lhsPath, "_nonnull", SymValue::Unknown());
                    std::string allocName = getAllocatorName(CE);
                    if (!allocName.empty()) {
                        recordAllocProvenance(lhsPath, CE, allocName);
                        // Track allocation source for lifetime analysis
                        PointerLifetimeState pls;
                        pls.lifetime = PointerLifetime::Valid;
                        pls.allocLocation = CE->getBeginLoc();
                        if (allocName == "aligned_alloc" || allocName == "posix_memalign") {
                            pls.allocSource = AllocSource::AlignedAlloc;
                        } else {
                            pls.allocSource = AllocSource::Malloc;
                        }
                        PointerLifetimes[lhsPath] = pls;
                        // Track allocation size for bounds checking
                        QualType pointeeType = lhsType->getPointeeType();
                        bool isVoidPtr = pointeeType->isVoidType();
                        auto maybeElemSize = isVoidPtr ? std::optional<int64_t>(1) : safeGetTypeSizeInChars(Ctx, pointeeType);
                        if (allocName == "calloc" && CE->getNumArgs() >= 2) {
                            // calloc(count, size) - count is first arg
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(CE->getArg(0), R, Ctx)) {
                                if (auto count = safeGetInt64(R.Val.getInt())) {
                                    if (isVoidPtr) {
                                        Expr::EvalResult R2;
                                        if (safeEvaluateAsInt(CE->getArg(1), R2, Ctx)) {
                                            if (auto elemSize = safeGetInt64(R2.Val.getInt())) {
                                                setField(lhsPath, "_size_bytes", SymValue::Const(*count * *elemSize));
                                            }
                                        }
                                    } else {
                                        setField(lhsPath, "_size", SymValue::Const(*count));
                                        setField(lhsPath, "_base_length", SymValue::Const(*count));
                                        setField(lhsPath, "_offset", SymValue::Const(0));
                                    }
                                }
                            }
                        } else if ((allocName == "malloc" || allocName == "realloc" ||
                                    allocName == "aligned_alloc" || allocName == "reallocarray") &&
                                   CE->getNumArgs() >= 1) {
                            // malloc(size), realloc(ptr, size), aligned_alloc(align, size)
                            unsigned sizeArgIdx = (allocName == "realloc" || allocName == "aligned_alloc") ? 1 : 0;
                            if (CE->getNumArgs() > sizeArgIdx) {
                                Expr::EvalResult R;
                                if (safeEvaluateAsInt(CE->getArg(sizeArgIdx), R, Ctx)) {
                                    if (auto sizeBytes = safeGetInt64(R.Val.getInt())) {
                                        setField(lhsPath, "_size_bytes", SymValue::Const(*sizeBytes));
                                        if (!isVoidPtr && maybeElemSize && *maybeElemSize > 0) {
                                            int64_t count = *sizeBytes / *maybeElemSize;
                                            setField(lhsPath, "_size", SymValue::Const(count));
                                            setField(lhsPath, "_base_length", SymValue::Const(count));
                                            setField(lhsPath, "_offset", SymValue::Const(0));
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // Unknown source - mark as unknown
                    setField(lhsPath, "_nonnull", SymValue::Unknown());
                }
            }

            if (auto *Call = dyn_cast<CallExpr>(RHS)) {
                auto oldVals = captureOldValues(Call);
                applyPostconditions(lhsPath, Call, oldVals);
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                clearProperties(lhsPath);
                copyProperties(lhsPath, DRE->getDecl()->getNameAsString());
            }

            // Handle object assignment v2 = v1 or v2 = std::move(v1)
            // On macOS with libc++, class assignments may come through as BinaryOperator
            // instead of CXXOperatorCallExpr. Handle record type assignments here.
            if (lhsType->isRecordType()) {
                if (isStdMove(RHS)) {
                    std::string rhsPath = getMovedFromPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                        invalidateProperties(rhsPath);
                    }
                } else {
                    std::string rhsPath = getExprPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                    }
                }
            }
        }
        
        if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
            std::string basePath = getExprPath(Mem->getBase());
            std::string field = Mem->getMemberDecl()->getNameAsString();
            if (!basePath.empty()) {
                if (hasRuntimeIndex(basePath)) {
                    std::string arrayBase = getArrayBase(basePath);
                    if (!arrayBase.empty()) invalidateArrayElements(arrayBase, field);
                } else {
                    invalidateProofs(basePath, field);
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(RHS, R, Ctx)) {
                        auto val = safeGetInt64(R.Val.getInt());
                        if (val) setField(basePath, field, SymValue::Const(*val));
                        else setField(basePath, field, SymValue::Unknown());
                    }
                    else if (auto *BinOp = dyn_cast<BinaryOperator>(RHS)) {
                        auto opcode = BinOp->getOpcode();
                        if (opcode == BO_Add || opcode == BO_Sub || opcode == BO_Mul || opcode == BO_Div) {
                            auto *L = BinOp->getLHS()->IgnoreParenImpCasts();
                            auto *Rh = BinOp->getRHS()->IgnoreParenImpCasts();
                            SymValue lVal = SymValue::Unknown(), rVal = SymValue::Unknown();
                            std::string lBase = getBasePath(L), lField = getFieldName(L);
                            if (!lBase.empty() && !lField.empty() && !hasRuntimeIndex(lBase)) {
                                lVal = getField(lBase, lField);
                                // If field is unknown, use VAR so Z3 can reason about it
                                if (lVal.kind == SymValue::UNKNOWN) lVal = SymValue::Var(lBase + "." + lField);
                            }
                            Expr::EvalResult R2;
                            if (safeEvaluateAsInt(Rh, R2, Ctx)) {
                                auto val = safeGetInt64(R2.Val.getInt());
                                if (val) rVal = SymValue::Const(*val);
                            }
                            else { 
                                std::string rBase = getBasePath(Rh), rField = getFieldName(Rh);
                                if (!rBase.empty() && !rField.empty() && !hasRuntimeIndex(rBase)) {
                                    rVal = getField(rBase, rField);
                                    // If field is unknown, use VAR so Z3 can reason about it
                                    if (rVal.kind == SymValue::UNKNOWN) rVal = SymValue::Var(rBase + "." + rField);
                                }
                            }
                            SymValue result;
                            switch (opcode) {
                                case BO_Add: result = SymValue::Add(lVal, rVal); break;
                                case BO_Sub: result = SymValue::Sub(lVal, rVal); break;
                                case BO_Mul: result = SymValue::Mul(lVal, rVal); break;
                                case BO_Div: result = SymValue::Div(lVal, rVal); break;
                                default: result = SymValue::Unknown(); break;
                            }
                            setField(basePath, field, result);
                        }
                    }
                    else setField(basePath, field, SymValue::Unknown());
                }
            }
        }
        return true;
    }
    
    struct GuardInfo {
        std::string basePath, field, proofKey, negProofKey;
        std::string extraNegProofKey;  // For unsigned cast: (unsigned)n > C implies n < 0 || n > C
                                       // So negation is n >= 0 && n <= C (two proofs needed)
    };

    // Variable guard info for "if (i < v.size())"
    struct VarGuardInfo {
        std::string varName;
        std::string objPath;
        std::string field;
        Constraint::Op op;
        Constraint::Op negOp;
    };

    // Pre-increment/decrement guard info for "while (++i <= N)" or "while (--i >= 0)"
    struct PreIncDecGuardInfo {
        std::string varName;
        std::optional<int64_t> lowerBound;  // Established by pre-inc (e.g., i >= 1)
        std::optional<int64_t> upperBound;  // From comparison RHS
        Constraint::Op upperOp;             // LT or LE
    };

    // Extract pre-increment/decrement guard from condition
    // Handles: ++i <= N, ++i < N, --i >= N, --i > N
    std::optional<PreIncDecGuardInfo> extractPreIncDecGuard(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();

        auto *BO = dyn_cast<BinaryOperator>(Cond);
        if (!BO) return std::nullopt;

        auto opcode = BO->getOpcode();
        if (opcode != BO_LT && opcode != BO_LE && opcode != BO_GT && opcode != BO_GE) {
            return std::nullopt;
        }

        auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
        auto *RHS = BO->getRHS()->IgnoreParenImpCasts();

        // Check for pre-increment/decrement on LHS: ++i or --i
        auto *UO = dyn_cast<UnaryOperator>(LHS);
        if (!UO) return std::nullopt;

        bool isPreInc = (UO->getOpcode() == UO_PreInc);
        bool isPreDec = (UO->getOpcode() == UO_PreDec);
        if (!isPreInc && !isPreDec) return std::nullopt;

        // Get the variable being incremented/decremented
        auto *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
        auto *DRE = dyn_cast<DeclRefExpr>(SubExpr);
        if (!DRE) return std::nullopt;

        std::string varName = DRE->getDecl()->getNameAsString();

        // Try to evaluate RHS as constant
        Expr::EvalResult R;
        if (!safeEvaluateAsInt(RHS, R, Ctx)) return std::nullopt;
        auto boundOpt = safeGetInt64(R.Val.getInt());
        if (!boundOpt) return std::nullopt;
        int64_t bound = *boundOpt;

        PreIncDecGuardInfo result;
        result.varName = varName;

        if (isPreInc) {
            // ++i <= N: after increment, i >= 1 and i <= N
            // ++i < N: after increment, i >= 1 and i < N
            result.lowerBound = 1;  // Pre-increment from 0 means i >= 1 in body
            result.upperBound = bound;
            result.upperOp = (opcode == BO_LE) ? Constraint::LE : Constraint::LT;
        } else {  // isPreDec
            // --i >= N: after decrement, i >= N (comparison passed)
            // --i > N: after decrement, i > N
            // For --i >= 0, we know i >= 0 in body
            result.lowerBound = bound;
            if (opcode == BO_GE) {
                // --i >= 0 means i >= 0 after decrement
                result.upperOp = Constraint::LE;  // placeholder, no upper bound from this
            } else {
                // --i > N means i > N, i.e., i >= N+1
                result.lowerBound = bound + 1;
                result.upperOp = Constraint::LE;
            }
            // No upper bound from pre-decrement pattern
            result.upperBound = std::nullopt;
        }

        return result;
    }

    std::optional<VarGuardInfo> extractVarGuard(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();

        // Handle && for compound guards like "i >= 0 && i < v.size()"
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            if (BO->getOpcode() == BO_LAnd) {
                // Try to extract from both sides, prefer the upper bound
                auto rightGuard = extractVarGuard(BO->getRHS());
                if (rightGuard) {
                    // Check if left side provides lower bound (i >= 0)
                    auto leftGuard = extractVarGuard(BO->getLHS());
                    if (leftGuard && leftGuard->varName == rightGuard->varName) {
                        // Both bounds on same variable - use upper bound
                        return rightGuard;
                    }
                    return rightGuard;
                }
                return extractVarGuard(BO->getLHS());
            }

            // Check for: i < v.size() or i < v._size or similar
            // Also handles reversed form: v._size > i or v.size() > i
            auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
            auto *RHS = BO->getRHS()->IgnoreParenImpCasts();

            std::string varName;
            std::string objPath, field;
            bool reversed = false;

            // Try normal form first: variable op field (e.g., i < v._size)
            if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                varName = DRE->getDecl()->getNameAsString();
            }

            if (!varName.empty()) {
                // Normal form: LHS is variable, RHS should be field
                if (auto *MCE = dyn_cast<CXXMemberCallExpr>(RHS)) {
                    auto *Method = MCE->getMethodDecl();
                    if (Method && Method->getParent()) {
                        std::string className = Method->getParent()->getNameAsString();
                        std::string methodName = Method->getNameAsString();
                        std::string key = className + "::" + methodName + "/" + std::to_string(MCE->getNumArgs());

                        auto it = MethodAnnotations.find(key);
                        if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
                            objPath = getExprPath(MCE->getImplicitObjectArgument());
                            field = it->second.returns;
                        }
                    }
                } else if (auto *ME = dyn_cast<MemberExpr>(RHS)) {
                    objPath = getExprPath(ME->getBase());
                    field = ME->getMemberDecl()->getNameAsString();
                } else if (auto *SubBO = dyn_cast<BinaryOperator>(RHS)) {
                    // Handle RHS expressions like: v._size - 1
                    // i < _size - N implies i < _size for positive N
                    if (SubBO->getOpcode() == BO_Sub) {
                        auto *SubLHS = SubBO->getLHS()->IgnoreParenImpCasts();
                        auto *SubRHS = SubBO->getRHS()->IgnoreParenImpCasts();
                        // Check if it's MemberExpr - constant
                        if (auto *ME = dyn_cast<MemberExpr>(SubLHS)) {
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(SubRHS, R, Ctx)) {
                                auto val = safeGetInt64(R.Val.getInt());
                                if (val && *val > 0) {
                                    // i < _size - N (N > 0) implies i < _size
                                    objPath = getExprPath(ME->getBase());
                                    field = ME->getMemberDecl()->getNameAsString();
                                }
                            }
                        }
                    }
                }
            } else {
                // Try reversed form: field op variable (e.g., v._size > i)
                if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                    varName = DRE->getDecl()->getNameAsString();
                }
                if (varName.empty()) return std::nullopt;

                reversed = true;
                if (auto *MCE = dyn_cast<CXXMemberCallExpr>(LHS)) {
                    auto *Method = MCE->getMethodDecl();
                    if (Method && Method->getParent()) {
                        std::string className = Method->getParent()->getNameAsString();
                        std::string methodName = Method->getNameAsString();
                        std::string key = className + "::" + methodName + "/" + std::to_string(MCE->getNumArgs());

                        auto it = MethodAnnotations.find(key);
                        if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
                            objPath = getExprPath(MCE->getImplicitObjectArgument());
                            field = it->second.returns;
                        }
                    }
                } else if (auto *ME = dyn_cast<MemberExpr>(LHS)) {
                    objPath = getExprPath(ME->getBase());
                    field = ME->getMemberDecl()->getNameAsString();
                }
            }

            if (objPath.empty() || field.empty()) return std::nullopt;

            Constraint::Op op, negOp;
            auto opcode = BO->getOpcode();
            // For reversed form, flip the operator: v._size > i becomes i < v._size
            if (reversed) {
                switch (opcode) {
                    case BO_LT: opcode = BO_GT; break;
                    case BO_LE: opcode = BO_GE; break;
                    case BO_GT: opcode = BO_LT; break;
                    case BO_GE: opcode = BO_LE; break;
                    default: break;
                }
            }
            switch (opcode) {
                case BO_LT:  op = Constraint::LT;  negOp = Constraint::GE; break;
                case BO_LE:  op = Constraint::LE;  negOp = Constraint::GT; break;
                case BO_GT:  op = Constraint::GT;  negOp = Constraint::LE; break;
                case BO_GE:  op = Constraint::GE;  negOp = Constraint::LT; break;
                default: return std::nullopt;
            }

            return VarGuardInfo{varName, objPath, field, op, negOp};
        }

        return std::nullopt;
    }

    // Extract multiple guards from compound conditions
    std::vector<GuardInfo> extractAllGuards(Expr *Cond) {
        std::vector<GuardInfo> result;
        Cond = Cond->IgnoreParenImpCasts();

        // Handle && for compound guards like "k >= 0 && k < 10"
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            if (BO->getOpcode() == BO_LAnd) {
                auto leftGuards = extractAllGuards(BO->getLHS());
                auto rightGuards = extractAllGuards(BO->getRHS());
                result.insert(result.end(), leftGuards.begin(), leftGuards.end());
                result.insert(result.end(), rightGuards.begin(), rightGuards.end());
                return result;
            }
        }

        // Try single guard extraction
        auto single = extractGuard(Cond);
        if (single) result.push_back(*single);
        return result;
    }

    // Extract guards from OR conditions (for early return patterns)
    // e.g., "tag < 0 || tag > 30" returns [{tag<0}, {tag>30}]
    // When the then-block terminates, the NEGATION of all guards holds
    std::vector<GuardInfo> extractOrGuards(Expr *Cond) {
        std::vector<GuardInfo> result;
        Cond = Cond->IgnoreParenImpCasts();

        // Handle || for compound guards like "tag < 0 || tag > 30"
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            if (BO->getOpcode() == BO_LOr) {
                auto leftGuards = extractOrGuards(BO->getLHS());
                auto rightGuards = extractOrGuards(BO->getRHS());
                result.insert(result.end(), leftGuards.begin(), leftGuards.end());
                result.insert(result.end(), rightGuards.begin(), rightGuards.end());
                return result;
            }
        }

        // Try single guard extraction
        auto single = extractGuard(Cond);
        if (single) result.push_back(*single);
        return result;
    }

    std::optional<GuardInfo> extractGuard(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();

        // Handle negation: !(expr) - recursively extract and swap proof keys
        // e.g., !(ptr == nullptr) is equivalent to (ptr != nullptr)
        if (auto *UO = dyn_cast<UnaryOperator>(Cond)) {
            if (UO->getOpcode() == UO_LNot) {
                auto inner = extractGuard(UO->getSubExpr());
                if (inner) {
                    // Swap proofKey and negProofKey to invert the condition
                    std::swap(inner->proofKey, inner->negProofKey);
                    return inner;
                }
            }
        }

        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            auto *LHS = BO->getLHS()->IgnoreParenImpCasts();

            // Handle method call on LHS: v.size() > 0
            if (auto *MCE = dyn_cast<CXXMemberCallExpr>(LHS)) {
                auto *Method = MCE->getMethodDecl();
                if (Method && Method->getParent()) {
                    std::string className = Method->getParent()->getNameAsString();
                    std::string methodName = Method->getNameAsString();
                    std::string key = className + "::" + methodName + "/" + std::to_string(MCE->getNumArgs());
                    
                    auto it = MethodAnnotations.find(key);
                    if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
                        GuardInfo gi;
                        gi.basePath = getExprPath(MCE->getImplicitObjectArgument());
                        gi.field = it->second.returns;  // _size
                        if (gi.basePath.empty()) return std::nullopt;
                        
                        Expr::EvalResult R;
                        int64_t rhsVal = 0;
                        if (safeEvaluateAsInt(BO->getRHS(), R, Ctx)) {
                            auto val = safeGetInt64(R.Val.getInt());
                            if (val) rhsVal = *val;
                        }

                        const char *op = "", *neg = "";
                        switch (BO->getOpcode()) {
                            case BO_GT: op = ">"; neg = "<="; break;
                            case BO_GE: op = ">="; neg = "<"; break;
                            case BO_LT: op = "<"; neg = ">="; break;
                            case BO_LE: op = "<="; neg = ">"; break;
                            case BO_EQ: op = "=="; neg = "!="; break;
                            case BO_NE: op = "!="; neg = "=="; break;
                            default: return std::nullopt;
                        }
                        gi.proofKey = gi.field + op + std::to_string(rhsVal);
                        gi.negProofKey = gi.field + neg + std::to_string(rhsVal);
                        return gi;
                    }
                }
            }

            // Handle field access on LHS: v.len > 0
            // Skip pointer member null checks - let the pointer handler below handle them
            if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
                // Check if this is a pointer member null check - let specialized handler below handle it
                bool isPointerNullCheck = Mem->getType()->isPointerType() &&
                    (BO->getOpcode() == BO_EQ || BO->getOpcode() == BO_NE);
                if (isPointerNullCheck) {
                    auto *RHS = BO->getRHS()->IgnoreParenImpCasts();
                    if (isa<CXXNullPtrLiteralExpr>(RHS) || isa<GNUNullExpr>(RHS) ||
                        (isa<IntegerLiteral>(RHS) && cast<IntegerLiteral>(RHS)->getValue() == 0)) {
                        // Fall through to pointer null check handler
                    }
                    else {
                        isPointerNullCheck = false;
                    }
                }

                if (!isPointerNullCheck) {
                    GuardInfo gi;
                    gi.basePath = getExprPath(Mem->getBase());
                    gi.field = Mem->getMemberDecl()->getNameAsString();
                    if (gi.basePath.empty() || hasRuntimeIndex(gi.basePath)) return std::nullopt;
                    Expr::EvalResult R;
                    int64_t rhsVal = 0;
                    if (safeEvaluateAsInt(BO->getRHS(), R, Ctx)) {
                        auto val = safeGetInt64(R.Val.getInt());
                        if (val) rhsVal = *val;
                    }
                    const char *op = "", *neg = "";
                    switch (BO->getOpcode()) {
                        case BO_GT: op = ">"; neg = "<="; break;
                        case BO_GE: op = ">="; neg = "<"; break;
                        case BO_LT: op = "<"; neg = ">="; break;
                        case BO_LE: op = "<="; neg = ">"; break;
                        case BO_EQ: op = "=="; neg = "!="; break;
                        case BO_NE: op = "!="; neg = "=="; break;
                        default: return std::nullopt;
                    }
                    gi.proofKey = gi.field + op + std::to_string(rhsVal);
                    gi.negProofKey = gi.field + neg + std::to_string(rhsVal);
                    // For _-prefixed fields (size/count convention), field == 0 negation implies > 0
                    // These fields are conventionally non-negative, so != 0 means > 0
                    if (BO->getOpcode() == BO_EQ && rhsVal == 0 &&
                        !gi.field.empty() && gi.field[0] == '_') {
                        gi.extraNegProofKey = gi.field + ">0";
                    }
                    return gi;
                }
            }

            // Handle pointer null checks: ptr != nullptr, ptr == nullptr, ptr != 0, ptr == 0
            // This MUST come before the generic DeclRefExpr handler
            if (BO->getOpcode() == BO_NE || BO->getOpcode() == BO_EQ) {
                auto *RHS = BO->getRHS()->IgnoreParenImpCasts();

                // Check if LHS is a pointer and RHS is nullptr/NULL/0
                if (LHS->getType()->isPointerType()) {
                    std::string ptrPath = getExprPath(LHS);
                    bool rhsIsNull = false;

                    if (isa<CXXNullPtrLiteralExpr>(RHS)) {
                        rhsIsNull = true;
                    } else if (isa<GNUNullExpr>(RHS)) {
                        rhsIsNull = true;
                    } else if (auto *IL = dyn_cast<IntegerLiteral>(RHS)) {
                        rhsIsNull = (IL->getValue() == 0);
                    }

                    if (!ptrPath.empty() && rhsIsNull) {
                        GuardInfo gi;
                        gi.basePath = ptrPath;
                        gi.field = "_nonnull";
                        if (BO->getOpcode() == BO_NE) {
                            gi.proofKey = "_nonnull!=0";
                            gi.negProofKey = "_nonnull==0";
                        } else {
                            gi.proofKey = "_nonnull==0";
                            gi.negProofKey = "_nonnull!=0";
                        }
                        return gi;
                    }
                }
                // Also check RHS for pointer, LHS for null (nullptr == ptr)
                else if (RHS->getType()->isPointerType()) {
                    std::string ptrPath = getExprPath(RHS);
                    bool lhsIsNull = false;

                    if (isa<CXXNullPtrLiteralExpr>(LHS)) {
                        lhsIsNull = true;
                    } else if (isa<GNUNullExpr>(LHS)) {
                        lhsIsNull = true;
                    } else if (auto *IL = dyn_cast<IntegerLiteral>(LHS)) {
                        lhsIsNull = (IL->getValue() == 0);
                    }

                    if (!ptrPath.empty() && lhsIsNull) {
                        GuardInfo gi;
                        gi.basePath = ptrPath;
                        gi.field = "_nonnull";
                        if (BO->getOpcode() == BO_NE) {
                            gi.proofKey = "_nonnull!=0";
                            gi.negProofKey = "_nonnull==0";
                        } else {
                            gi.proofKey = "_nonnull==0";
                            gi.negProofKey = "_nonnull!=0";
                        }
                        return gi;
                    }
                }
            }

            // Handle simple variable guard: n > 0, n < 8
            // Also handle cast to unsigned: (unsigned)n > 0
            Expr *VarExpr = LHS;
            bool hasUnsignedCast = false;
            // Strip ALL casts and check for signed→unsigned conversion anywhere in the chain
            while (auto *Cast = dyn_cast<CastExpr>(VarExpr)) {
                // Check if this cast or any implicit cast converts signed to unsigned
                QualType destType = Cast->getType();
                // Get the innermost source type (after stripping all implicit casts)
                Expr *innerExpr = Cast->getSubExpr();
                while (auto *innerCast = dyn_cast<ImplicitCastExpr>(innerExpr)) {
                    innerExpr = innerCast->getSubExpr();
                }
                QualType srcType = innerExpr->getType();
                if (destType->isUnsignedIntegerType() && srcType->isSignedIntegerType()) {
                    hasUnsignedCast = true;
                }
                VarExpr = Cast->getSubExpr()->IgnoreParenImpCasts();
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(VarExpr)) {
                GuardInfo gi;
                gi.basePath = DRE->getDecl()->getNameAsString();
                gi.field = "_value";  // Use special field name for the variable itself

                Expr::EvalResult R;
                int64_t rhsVal = 0;
                if (safeEvaluateAsInt(BO->getRHS(), R, Ctx)) {
                    auto val = safeGetInt64(R.Val.getInt());
                    if (val) rhsVal = *val;
                }

                const char *op = "", *neg = "";
                switch (BO->getOpcode()) {
                    case BO_GT: op = ">"; neg = "<="; break;
                    case BO_GE: op = ">="; neg = "<"; break;
                    case BO_LT: op = "<"; neg = ">="; break;
                    case BO_LE: op = "<="; neg = ">"; break;
                    case BO_EQ: op = "=="; neg = "!="; break;
                    case BO_NE: op = "!="; neg = "=="; break;
                    default: return std::nullopt;
                }
                gi.proofKey = gi.field + op + std::to_string(rhsVal);
                gi.negProofKey = gi.field + neg + std::to_string(rhsVal);

                // For unsigned cast comparisons with > or >=:
                // (unsigned)n > C means n < 0 || n > C
                // So negation (unsigned)n <= C means n >= 0 && n <= C
                // We need to add an extra proof for n >= 0
                if (hasUnsignedCast && (BO->getOpcode() == BO_GT || BO->getOpcode() == BO_GE)) {
                    gi.extraNegProofKey = gi.field + ">=0";
                }

                return gi;
            }

            // Handle reversed simple variable guard: 8 > n, 0 < n
            auto *RHS = BO->getRHS()->IgnoreParenImpCasts();
            if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                Expr::EvalResult R;
                int64_t lhsVal = 0;
                if (safeEvaluateAsInt(LHS, R, Ctx)) {
                    auto val = safeGetInt64(R.Val.getInt());
                    if (val) lhsVal = *val;
                } else {
                    return std::nullopt;  // LHS must be constant for reversed form
                }

                GuardInfo gi;
                gi.basePath = DRE->getDecl()->getNameAsString();
                gi.field = "_value";

                // Flip the operator: "8 > n" becomes "n < 8"
                const char *op = "", *neg = "";
                switch (BO->getOpcode()) {
                    case BO_GT: op = "<"; neg = ">="; break;  // const > var -> var < const
                    case BO_GE: op = "<="; neg = ">"; break;  // const >= var -> var <= const
                    case BO_LT: op = ">"; neg = "<="; break;  // const < var -> var > const
                    case BO_LE: op = ">="; neg = "<"; break;  // const <= var -> var >= const
                    case BO_EQ: op = "=="; neg = "!="; break;
                    case BO_NE: op = "!="; neg = "=="; break;
                    default: return std::nullopt;
                }
                gi.proofKey = gi.field + op + std::to_string(lhsVal);
                gi.negProofKey = gi.field + neg + std::to_string(lhsVal);
                return gi;
            }
        }

        // Handle implicit pointer-to-bool: if (ptr) or if (!ptr)
        {
            Expr *CheckExpr = Cond;
            bool negated = false;

            // Handle !ptr
            if (auto *UO = dyn_cast<UnaryOperator>(Cond)) {
                if (UO->getOpcode() == UO_LNot) {
                    negated = true;
                    CheckExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                }
            }

            // Check if it's a pointer being used as bool
            if (CheckExpr->getType()->isPointerType()) {
                std::string ptrPath = getExprPath(CheckExpr);
                if (!ptrPath.empty()) {
                    GuardInfo gi;
                    gi.basePath = ptrPath;
                    gi.field = "_nonnull";
                    if (negated) {
                        // if (!ptr): null in then branch, nonnull in else
                        gi.proofKey = "_nonnull==0";
                        gi.negProofKey = "_nonnull!=0";
                    } else {
                        // if (ptr): nonnull in then branch, null in else
                        gi.proofKey = "_nonnull!=0";
                        gi.negProofKey = "_nonnull==0";
                    }
                    return gi;
                }
            }
        }

        return std::nullopt;
    }

    struct BoolCallInfo {
        std::string fnName;
        std::string argPath;
        bool negated;
        bool isMethod;
        std::string className;
        unsigned argCount = 0;  // For method overload matching
    };
    
    std::optional<BoolCallInfo> extractBoolCall(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();
        
        bool negated = false;
        if (auto *UO = dyn_cast<UnaryOperator>(Cond)) {
            if (UO->getOpcode() == UO_LNot) {
                negated = true;
                Cond = UO->getSubExpr()->IgnoreParenImpCasts();
            }
        }
        
        // Method call: v.empty()
        if (auto *MCE = dyn_cast<CXXMemberCallExpr>(Cond)) {
            auto *Method = MCE->getMethodDecl();
            if (!Method || !Method->getParent()) return std::nullopt;

            std::string className = Method->getParent()->getNameAsString();
            std::string methodName = Method->getNameAsString();
            std::string key = className + "::" + methodName + "/" + std::to_string(MCE->getNumArgs());
            
            auto it = MethodAnnotations.find(key);
            if (it == MethodAnnotations.end() || it->second.boolPostconds.empty()) return std::nullopt;
            
            std::string objPath = getExprPath(MCE->getImplicitObjectArgument());
            if (objPath.empty()) return std::nullopt;
            
            return BoolCallInfo{methodName, objPath, negated, true, className, MCE->getNumArgs()};
        }
        
        // Free function call
        if (auto *Call = dyn_cast<CallExpr>(Cond)) {
            auto *Callee = Call->getDirectCallee();
            if (!Callee || Call->getNumArgs() < 1) return std::nullopt;
            
            std::string fn = Callee->getNameAsString();
            if (FnBoolPostconditions.find(fn) == FnBoolPostconditions.end()) return std::nullopt;
            
            auto *Arg = Call->getArg(0)->IgnoreParenImpCasts();
            std::string argPath = getExprPath(Arg);
            if (argPath.empty()) return std::nullopt;
            
            return BoolCallInfo{fn, argPath, negated, false, "", 0};
        }
        return std::nullopt;
    }

    // Check if a statement block terminates (ends with return/throw/break/continue)
    bool doesBlockTerminate(Stmt *S) {
        if (!S) return false;

        // For compound statements, check the last statement
        if (auto *CS = dyn_cast<CompoundStmt>(S)) {
            if (CS->body_empty()) return false;
            return doesBlockTerminate(CS->body_back());
        }

        // Direct terminating statements
        if (isa<ReturnStmt>(S)) return true;
        if (isa<CXXThrowExpr>(S)) return true;
        if (isa<BreakStmt>(S)) return true;
        if (isa<ContinueStmt>(S)) return true;

        return false;
    }

    bool TraverseIfStmt(IfStmt *If) {
        // Try variable guard first (i < v.size())
        auto varGuard = extractVarGuard(If->getCond());
        if (varGuard) {
            addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->op);
            TraverseStmt(If->getThen());
            removeVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field);

            // Early return: if then-block terminates and no else, negation holds after if
            if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->negOp);
            }

            if (If->getElse()) {
                addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->negOp);
                TraverseStmt(If->getElse());
                removeVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field);
            }
            return true;
        }

        // Try extracting multiple guards (handles compound conditions like k >= 0 && k < 10)
        auto guards = extractAllGuards(If->getCond());
        if (!guards.empty()) {
            // Check for uninitialized pointers in null-check conditions
            // Comparing an uninitialized pointer is UB even with a null check
            for (const auto& g : guards) {
                if (g.field == "_nonnull") {
                    // This is a pointer null check - verify pointer is initialized
                    checkUninitializedPointer(g.basePath, If->getCond()->getBeginLoc());
                }
            }
            // Add all proofs for then-block
            for (const auto& g : guards) {
                addProof(g.basePath, g.proofKey);
            }
            TraverseStmt(If->getThen());
            // Remove proofs after then-block
            for (const auto& g : guards) {
                Env.erase(g.basePath + "._proof_" + g.proofKey);
            }

            // Early return: if then-block terminates and no else, negation holds after if
            if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                // Add negation proofs that persist after the if-statement
                if (guards.size() == 1) {
                    addProof(guards[0].basePath, guards[0].negProofKey);
                    if (!guards[0].extraNegProofKey.empty()) {
                        addProof(guards[0].basePath, guards[0].extraNegProofKey);
                    }
                    // Array element propagation: if arr != NULL is proven (for pointer-to-pointer),
                    // also mark arr[?]._nonnull = 1 (trust array elements are valid)
                    if (guards[0].negProofKey == "_nonnull!=0") {
                        setField(guards[0].basePath + "[?]", "_nonnull", SymValue::Const(1));
                    }
                }
            }

            if (If->getElse()) {
                // Add negative proofs for else-block (only for single guard case)
                if (guards.size() == 1) {
                    addProof(guards[0].basePath, guards[0].negProofKey);
                    if (!guards[0].extraNegProofKey.empty()) {
                        addProof(guards[0].basePath, guards[0].extraNegProofKey);
                    }
                    TraverseStmt(If->getElse());
                    Env.erase(guards[0].basePath + "._proof_" + guards[0].negProofKey);
                    if (!guards[0].extraNegProofKey.empty()) {
                        Env.erase(guards[0].basePath + "._proof_" + guards[0].extraNegProofKey);
                    }

                    // Else-return: if else terminates, original condition holds after if-else
                    if (doesBlockTerminate(If->getElse())) {
                        addProof(guards[0].basePath, guards[0].proofKey);
                    }
                } else {
                    // For compound conditions, we can't negate properly, just traverse
                    TraverseStmt(If->getElse());
                }
            }
            return true;
        }

        // Handle OR conditions for early return guards
        // e.g., if (tag < 0 || tag > 30) return;
        // After the return, we know: tag >= 0 && tag <= 30 (negation of all)
        // Also handles partial extraction: if (ptr == nullptr || flag) return;
        // Even if 'flag' doesn't produce a guard, ptr != nullptr still holds after
        auto orGuards = extractOrGuards(If->getCond());
        if (!orGuards.empty()) {  // At least one guard extracted from OR condition
            // In the then-block, we only know one condition is true (not which)
            // So we traverse without adding proofs
            TraverseStmt(If->getThen());

            // Early return: if then-block terminates and no else, ALL negations hold
            if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                for (const auto& g : orGuards) {
                    addProof(g.basePath, g.negProofKey);
                    if (!g.extraNegProofKey.empty()) {
                        addProof(g.basePath, g.extraNegProofKey);
                    }
                    // Array element propagation for pointer-to-pointer guards
                    if (g.negProofKey == "_nonnull!=0") {
                        setField(g.basePath + "[?]", "_nonnull", SymValue::Const(1));
                    }
                }
            }

            if (If->getElse()) {
                // In else-block, none of the OR conditions are true
                // So all negations hold
                for (const auto& g : orGuards) {
                    addProof(g.basePath, g.negProofKey);
                    if (!g.extraNegProofKey.empty()) {
                        addProof(g.basePath, g.extraNegProofKey);
                    }
                }
                TraverseStmt(If->getElse());
                for (const auto& g : orGuards) {
                    Env.erase(g.basePath + "._proof_" + g.negProofKey);
                    if (!g.extraNegProofKey.empty()) {
                        Env.erase(g.basePath + "._proof_" + g.extraNegProofKey);
                    }
                }

                // Else-return: if else terminates, at least one original condition holds
                // But we can't determine which, so no proofs added
            }
            return true;
        }

        auto boolCall = extractBoolCall(If->getCond());
        if (boolCall) {
            std::vector<BoolPostcondition>* postconds = nullptr;

            if (boolCall->isMethod) {
                std::string key = boolCall->className + "::" + boolCall->fnName + "/" + std::to_string(boolCall->argCount);
                auto it = MethodAnnotations.find(key);
                if (it != MethodAnnotations.end()) postconds = &it->second.boolPostconds;
            } else {
                auto it = FnBoolPostconditions.find(boolCall->fnName);
                if (it != FnBoolPostconditions.end()) postconds = &it->second;
            }
            
            if (postconds) {
                bool thenBranchIsTrue = !boolCall->negated;
                
                for (const auto& bp : *postconds) {
                    if (bp.onTrue == thenBranchIsTrue) {
                        std::string actualPath = boolCall->argPath;
                        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
                        std::string proofKey = bp.field + ops[bp.op] + std::to_string(bp.rhs);
                        addProof(actualPath, proofKey);
                    }
                }
                
                TraverseStmt(If->getThen());
                
                for (const auto& bp : *postconds) {
                    if (bp.onTrue == thenBranchIsTrue) {
                        std::string actualPath = boolCall->argPath;
                        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
                        std::string proofKey = bp.field + ops[bp.op] + std::to_string(bp.rhs);
                        Env.erase(actualPath + "._proof_" + proofKey);
                    }
                }
                
                if (If->getElse()) {
                    for (const auto& bp : *postconds) {
                        if (bp.onTrue != thenBranchIsTrue) {
                            std::string actualPath = boolCall->argPath;
                            const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
                            std::string proofKey = bp.field + ops[bp.op] + std::to_string(bp.rhs);
                            addProof(actualPath, proofKey);
                        }
                    }
                    
                    TraverseStmt(If->getElse());
                    
                    for (const auto& bp : *postconds) {
                        if (bp.onTrue != thenBranchIsTrue) {
                            std::string actualPath = boolCall->argPath;
                            const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
                            std::string proofKey = bp.field + ops[bp.op] + std::to_string(bp.rhs);
                            Env.erase(actualPath + "._proof_" + proofKey);
                        }
                    }
                }
                return true;
            }
        }
        
        TraverseStmt(If->getCond());
        TraverseStmt(If->getThen());
        if (If->getElse()) TraverseStmt(If->getElse());
        return true;
    }

    // Handle while loops to track guard as proof within body
    bool TraverseWhileStmt(WhileStmt *While) {
        if (!While->getCond()) {
            return RecursiveASTVisitor::TraverseWhileStmt(While);
        }

        // Try variable guard (i < v.size())
        auto varGuard = extractVarGuard(While->getCond());
        if (varGuard) {
            addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->op);
            TraverseStmt(While->getBody());
            removeVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field);
            return true;
        }

        // Try extracting multiple guards
        auto guards = extractAllGuards(While->getCond());
        if (!guards.empty()) {
            // Check for uninitialized pointers in null-check conditions
            for (const auto& g : guards) {
                if (g.field == "_nonnull") {
                    checkUninitializedPointer(g.basePath, While->getCond()->getBeginLoc());
                }
            }
            for (const auto& g : guards) {
                addProof(g.basePath, g.proofKey);
            }
            TraverseStmt(While->getBody());
            for (const auto& g : guards) {
                Env.erase(g.basePath + "._proof_" + g.proofKey);
            }
            return true;
        }

        // Try pre-increment/decrement pattern: while (++i <= N) or while (--i >= 0)
        // Pre-increment: ++i executes BEFORE comparison, so in body i >= 1
        // Pre-decrement: --i executes BEFORE comparison, so in body i < original
        if (auto preIncGuard = extractPreIncDecGuard(While->getCond())) {
            std::vector<std::string> proofsToRemove;

            // Add lower bound proof (for pre-increment: i >= 1)
            if (preIncGuard->lowerBound) {
                std::string proofKey = preIncGuard->varName + "._proof__value>=" + std::to_string(*preIncGuard->lowerBound);
                Env[proofKey] = SymValue::Const(1);
                proofsToRemove.push_back(proofKey);
            }

            // Add upper bound proof
            if (preIncGuard->upperBound) {
                std::string opStr = (preIncGuard->upperOp == Constraint::LT) ? "<" : "<=";
                std::string proofKey = preIncGuard->varName + "._proof__value" + opStr + std::to_string(*preIncGuard->upperBound);
                Env[proofKey] = SymValue::Const(1);
                proofsToRemove.push_back(proofKey);
            }

            // Clear concrete value so variable is treated as symbolic with bounds
            SymValue savedValue;
            auto it = Env.find(preIncGuard->varName);
            if (it != Env.end()) {
                savedValue = it->second;
                Env.erase(it);
            }

            TraverseStmt(While->getBody());

            // Restore
            if (savedValue.kind != SymValue::UNKNOWN) {
                Env[preIncGuard->varName] = savedValue;
            }
            for (const auto& key : proofsToRemove) {
                Env.erase(key);
            }
            return true;
        }

        // Default: traverse condition and body normally
        TraverseStmt(While->getCond());
        TraverseStmt(While->getBody());
        return true;
    }

    // Helper: Find all pointer variables that are incremented/decremented in a statement
    std::set<std::string> findIncrementedPointers(Stmt *S) {
        std::set<std::string> result;
        if (!S) return result;

        // Check if this is a pointer increment
        if (auto *UO = dyn_cast<UnaryOperator>(S)) {
            if (UO->isIncrementDecrementOp()) {
                Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();
                if (Sub->getType()->isPointerType()) {
                    std::string path = getExprPath(Sub);
                    if (!path.empty()) result.insert(path);
                }
            }
        }

        // Recurse into children
        for (Stmt *Child : S->children()) {
            auto childResult = findIncrementedPointers(Child);
            result.insert(childResult.begin(), childResult.end());
        }
        return result;
    }

    // Handle for loops to track loop variable bounds
    // Supports: for (int i = N; i < bound; i++) and for (int i = N; i <= bound; i++)
    // Where bound can be: v.size(), v.field, or constant integer
    bool TraverseForStmt(ForStmt *For) {
        std::string loopVar;
        std::string objPath;
        std::string boundField;
        std::optional<int64_t> lowerBound;
        std::optional<int64_t> constUpperBound;
        std::optional<int64_t> stepValue;
        bool hasFieldUpperBound = false;
        Constraint::Op upperOp = Constraint::LT;
        bool isForwardCondition = true;  // < or <= or != (ascending)
        bool incrementMatchesCondition = true;  // Verify direction matches

        // Extract loop variable and lower bound from init
        if (auto *Init = For->getInit()) {
            // Case 1: Declaration (for (int i = 0; ...))
            if (auto *DS = dyn_cast<DeclStmt>(Init)) {
                if (DS->isSingleDecl()) {
                    if (auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl())) {
                        loopVar = VD->getNameAsString();
                        if (VD->hasInit()) {
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(VD->getInit(), R, Ctx)) {
                                lowerBound = safeGetInt64(R.Val.getInt());
                            }
                        }
                    }
                }
            }
            // Case 2: Assignment (for (i = 0; ...) with pre-declared i)
            else if (auto *ExprStmt = dyn_cast<Expr>(Init)) {
                if (auto *BO = dyn_cast<BinaryOperator>(ExprStmt->IgnoreParenImpCasts())) {
                    if (BO->getOpcode() == BO_Assign) {
                        if (auto *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts())) {
                            loopVar = DRE->getDecl()->getNameAsString();
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(BO->getRHS(), R, Ctx)) {
                                lowerBound = safeGetInt64(R.Val.getInt());
                            }
                        }
                    }
                }
            }
        }

        // Extract upper bound from condition
        if (!loopVar.empty()) {
            if (auto *Cond = For->getCond()) {
                if (auto *BO = dyn_cast<BinaryOperator>(Cond->IgnoreParenImpCasts())) {
                    auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
                    if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                        if (DRE->getDecl()->getNameAsString() == loopVar) {
                            auto *RHS = BO->getRHS()->IgnoreParenImpCasts();

                            // Track the comparison operator
                            if (BO->getOpcode() == BO_LT) {
                                upperOp = Constraint::LT;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_LE) {
                                upperOp = Constraint::LE;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_NE) {
                                // != implies < when incrementing from start to bound
                                upperOp = Constraint::LT;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_GT) {
                                upperOp = Constraint::GT;
                                isForwardCondition = false;
                            }
                            else if (BO->getOpcode() == BO_GE) {
                                upperOp = Constraint::GE;
                                isForwardCondition = false;
                            }
                            else goto default_traverse;

                            // Case 1: Method call (v.size())
                            if (auto *MCE = dyn_cast<CXXMemberCallExpr>(RHS)) {
                                auto *Method = MCE->getMethodDecl();
                                if (Method && Method->getParent()) {
                                    std::string className = Method->getParent()->getNameAsString();
                                    std::string methodName = Method->getNameAsString();
                                    std::string key = className + "::" + methodName + "/" + std::to_string(MCE->getNumArgs());

                                    auto it = MethodAnnotations.find(key);
                                    if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
                                        objPath = getExprPath(MCE->getImplicitObjectArgument());
                                        boundField = it->second.returns;
                                        hasFieldUpperBound = true;
                                    }
                                }
                            }
                            // Case 2: Member access (v.len, v._size)
                            else if (auto *ME = dyn_cast<MemberExpr>(RHS)) {
                                objPath = getExprPath(ME->getBase());
                                boundField = ME->getMemberDecl()->getNameAsString();
                                hasFieldUpperBound = true;
                            }
                            // Case 3: Plain variable (i < n where n is symbolic or constexpr)
                            else if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                                // First, try to evaluate as constant (handles constexpr, static const, enum)
                                Expr::EvalResult R;
                                if (safeEvaluateAsInt(RHS, R, Ctx)) {
                                    constUpperBound = safeGetInt64(R.Val.getInt());
                                } else {
                                    // Fall back to symbolic variable tracking
                                    boundField = DRE->getDecl()->getNameAsString();
                                    hasFieldUpperBound = true;
                                }
                            }
                            // Case 4: Binary expression (i < n - 1, i < n + k, etc.)
                            else if (auto *SubBO = dyn_cast<BinaryOperator>(RHS)) {
                                // Special case: i < _size - N for positive N implies i < _size
                                if (SubBO->getOpcode() == BO_Sub) {
                                    auto *SubLHS = SubBO->getLHS()->IgnoreParenImpCasts();
                                    auto *SubRHS = SubBO->getRHS()->IgnoreParenImpCasts();
                                    if (auto *ME = dyn_cast<MemberExpr>(SubLHS)) {
                                        Expr::EvalResult R;
                                        if (safeEvaluateAsInt(SubRHS, R, Ctx)) {
                                            auto val = safeGetInt64(R.Val.getInt());
                                            if (val && *val > 0) {
                                                // i < _size - N implies i < _size
                                                objPath = getExprPath(ME->getBase());
                                                boundField = ME->getMemberDecl()->getNameAsString();
                                                hasFieldUpperBound = true;
                                            }
                                        }
                                    }
                                }
                                // Fallback: create synthetic bound
                                if (!hasFieldUpperBound) {
                                    SymValue boundVal = exprToSymValueImpl(SubBO, Ctx);
                                    if (boundVal.kind != SymValue::UNKNOWN) {
                                        static int loopBoundCounter = 0;
                                        boundField = "__loop_bound_" + std::to_string(loopBoundCounter++);
                                        Env[boundField] = boundVal;
                                        hasFieldUpperBound = true;
                                    }
                                }
                            }
                            // Case 5: Constant integer
                            else {
                                Expr::EvalResult R;
                                if (safeEvaluateAsInt(RHS, R, Ctx)) {
                                    constUpperBound = safeGetInt64(R.Val.getInt());
                                }
                            }
                        }
                    }
                }
            }
        }

        // Verify increment direction matches condition direction (soundness)
        // Also extract step value for tighter bound computation
        if (auto *Inc = For->getInc()) {
            auto *IncExpr = Inc->IgnoreParenImpCasts();
            bool isIncrement = false;
            bool isDecrement = false;

            // Check for ++i, i++
            if (auto *UO = dyn_cast<UnaryOperator>(IncExpr)) {
                if (UO->getOpcode() == UO_PreInc || UO->getOpcode() == UO_PostInc) {
                    isIncrement = true;
                    stepValue = 1;
                } else if (UO->getOpcode() == UO_PreDec || UO->getOpcode() == UO_PostDec) {
                    isDecrement = true;
                    stepValue = 1;
                }
            }
            // Check for i += N, i -= N, i = i + N, i = i - N
            else if (auto *BO = dyn_cast<BinaryOperator>(IncExpr)) {
                if (BO->getOpcode() == BO_AddAssign) {
                    isIncrement = true;
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(BO->getRHS(), R, Ctx)) {
                        stepValue = safeGetInt64(R.Val.getInt());
                    }
                } else if (BO->getOpcode() == BO_SubAssign) {
                    isDecrement = true;
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(BO->getRHS(), R, Ctx)) {
                        stepValue = safeGetInt64(R.Val.getInt());
                    }
                } else if (BO->getOpcode() == BO_Assign) {
                    // i = i + N or i = i - N
                    if (auto *RHS = dyn_cast<BinaryOperator>(BO->getRHS()->IgnoreParenImpCasts())) {
                        if (RHS->getOpcode() == BO_Add) {
                            isIncrement = true;
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(RHS->getRHS(), R, Ctx)) {
                                stepValue = safeGetInt64(R.Val.getInt());
                            }
                        } else if (RHS->getOpcode() == BO_Sub) {
                            isDecrement = true;
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(RHS->getRHS(), R, Ctx)) {
                                stepValue = safeGetInt64(R.Val.getInt());
                            }
                        }
                    }
                }
            }
            // Check for i = expr (compound)
            else if (auto *COCE = dyn_cast<CXXOperatorCallExpr>(IncExpr)) {
                auto OpKind = COCE->getOperator();
                if (OpKind == OO_PlusPlus) { isIncrement = true; stepValue = 1; }
                else if (OpKind == OO_MinusMinus) { isDecrement = true; stepValue = 1; }
                else if (OpKind == OO_PlusEqual) { isIncrement = true; }
                else if (OpKind == OO_MinusEqual) { isDecrement = true; }
            }

            // Direction mismatch detection
            if (isForwardCondition && isDecrement) {
                incrementMatchesCondition = false;
            } else if (!isForwardCondition && isIncrement) {
                incrementMatchesCondition = false;
            }
        }

        // Add proofs and traverse if we found bounds AND direction matches
        if (!loopVar.empty() && (hasFieldUpperBound || constUpperBound) && incrementMatchesCondition) {
            std::vector<std::string> proofsToRemove;

            // Add lower bound proof: loopVar._value >= N
            // Using _value field so it can be matched against arg0 constraints
            if (lowerBound) {
                std::string proofKey = loopVar + "._proof__value>=" + std::to_string(*lowerBound);
                Env[proofKey] = SymValue::Const(1);
                proofsToRemove.push_back(proofKey);
            }

            // Add upper bound proof
            if (hasFieldUpperBound) {
                // Field-based bound: loopVar < obj._size (uses VarBoundProof)
                addVarBoundProof(loopVar, objPath, boundField, upperOp);
                // Track synthetic loop bound variables for cleanup
                if (boundField.find("__loop_bound_") == 0) {
                    proofsToRemove.push_back(boundField);
                }
            }
            if (constUpperBound) {
                // Constant bound: loopVar._value < N or loopVar._value <= N
                std::string opStr = (upperOp == Constraint::LT) ? "<" : "<=";
                std::string proofKey = loopVar + "._proof__value" + opStr + std::to_string(*constUpperBound);
                Env[proofKey] = SymValue::Const(1);
                proofsToRemove.push_back(proofKey);

                // When step > 1, compute tighter upper bound
                // For i < bound with step S starting from L: max i = L + floor((bound-1-L)/S)*S
                // This enables proving arr[i+K] safe when K < S
                if (stepValue && *stepValue > 1 && lowerBound && upperOp == Constraint::LT) {
                    int64_t L = *lowerBound;
                    int64_t B = *constUpperBound;
                    int64_t S = *stepValue;
                    if (B > L && S > 0) {
                        int64_t maxI = L + ((B - 1 - L) / S) * S;
                        // Add tighter <= proof
                        std::string tightProofKey = loopVar + "._proof__value<=" + std::to_string(maxI);
                        Env[tightProofKey] = SymValue::Const(1);
                        proofsToRemove.push_back(tightProofKey);
                    }
                }
            }

            // Traverse loop body with proofs active
            if (For->getInit()) TraverseStmt(For->getInit());

            // IMPORTANT: Remove the loop variable's concrete value from Env.
            // The init (e.g., "int i = 0") sets i to a constant, but during
            // loop body analysis, i should be treated as symbolic with only
            // the loop bounds as constraints (not the initial value).
            SymValue savedLoopVarValue;
            auto loopVarIt = Env.find(loopVar);
            if (loopVarIt != Env.end()) {
                savedLoopVarValue = loopVarIt->second;
                Env.erase(loopVarIt);
            }

            if (For->getCond()) TraverseStmt(For->getCond());
            if (For->getInc()) TraverseStmt(For->getInc());

            // Set current loop variable for pointer increment tracking
            std::string savedLoopVar = CurrentLoopVar;
            CurrentLoopVar = loopVar;

            // Find pointers that are incremented in the loop body
            // Set their offsets to symbolic (initial + loopVar) before traversal
            auto incrementedPtrs = findIncrementedPointers(For->getBody());
            std::map<std::string, SymValue> savedOffsets;
            for (const auto& ptrPath : incrementedPtrs) {
                SymValue baseLen = getField(ptrPath, "_base_length");
                if (baseLen.kind != SymValue::UNKNOWN) {
                    SymValue offset = getField(ptrPath, "_offset");
                    savedOffsets[ptrPath] = offset;
                    // Set offset = initial + loopVar (symbolic)
                    SymValue loopVarSym = SymValue::Var(loopVar);
                    SymValue newOffset = (offset.kind == SymValue::CONST && offset.constVal == 0)
                        ? loopVarSym
                        : SymValue::Add(offset, loopVarSym);
                    setField(ptrPath, "_offset", newOffset);
                }
            }

            TraverseStmt(For->getBody());

            // Restore original offsets (or leave as symbolic for post-loop access)
            // After loop, pointer has been incremented by loop count
            for (const auto& [ptrPath, savedOffset] : savedOffsets) {
                // After loop, offset is indeterminate - leave as symbolic
                // (could be initial + final_loop_value, but we don't track that)
            }

            // Restore previous loop variable (for nested loops)
            CurrentLoopVar = savedLoopVar;

            // Restore loop variable value after loop (for code after the loop)
            if (savedLoopVarValue.kind != SymValue::UNKNOWN) {
                // After loop, i has an indeterminate value - don't restore constant
                // Just leave it erased so subsequent uses are symbolic
            }

            // Remove proofs after loop
            if (hasFieldUpperBound) {
                removeVarBoundProof(loopVar, objPath, boundField);
            }
            for (const auto& key : proofsToRemove) {
                Env.erase(key);
            }

            return true;
        }

        // Default traversal if we couldn't extract bounds
        default_traverse:
        return RecursiveASTVisitor::TraverseForStmt(For);
    }

    bool VisitSwitchStmt(SwitchStmt *S) {
        if (!CurrentFunctionDecl) return true;
        std::string fn = CurrentFunctionDecl->getNameAsString();
        if (TotalFunctions.find(fn) == TotalFunctions.end()) return true;
        
        auto *Cond = S->getCond();
        if (!Cond) return true;
        QualType condType = Cond->IgnoreParenImpCasts()->getType();
        if (!condType->isEnumeralType()) return true;
        auto *enumType = condType->getAs<EnumType>();
        if (!enumType) return true;
        EnumDecl *ED = enumType->getDecl();
        if (!ED) return true;
        
        std::set<int64_t> allValues;
        std::map<int64_t, std::string> valueNames;
        for (auto *EC : ED->enumerators()) {
            auto val = safeGetInt64(EC->getInitVal());
            if (val) {
                allValues.insert(*val);
                valueNames[*val] = EC->getNameAsString();
            }
        }
        
        std::set<int64_t> coveredValues;
        bool hasDefault = false;
        SwitchCase *SC = S->getSwitchCaseList();
        while (SC) {
            if (auto *CS = dyn_cast<CaseStmt>(SC)) {
                if (auto *CE = CS->getLHS()) {
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(CE, R, Ctx)) {
                        auto val = safeGetInt64(R.Val.getInt());
                        if (val) coveredValues.insert(*val);
                    }
                }
            }
            if (isa<DefaultStmt>(SC)) hasDefault = true;
            SC = SC->getNextSwitchCase();
        }
        
        if (hasDefault) return true;
        
        for (int64_t val : allValues) {
            if (coveredValues.find(val) == coveredValues.end()) {
                Diag.Report(S->getBeginLoc(), ErrNotTotal) << valueNames[val];
                Diag.Report(S->getBeginLoc(), NoteCase) << valueNames[val];
            }
        }
        return true;
    }
    
    // Handle method calls
    bool VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
        auto *Method = E->getMethodDecl();
        if (!Method || !Method->getParent()) return true;

        std::string className = Method->getParent()->getNameAsString();
        std::string methodName = Method->getNameAsString();
        
        // Handle swap method (exchanges properties)
        if (methodName == "swap" && E->getNumArgs() == 1) {
            std::string objPath = getExprPath(E->getImplicitObjectArgument());
            std::string argPath = getExprPath(E->getArg(0));
            if (!objPath.empty() && !argPath.empty() && objPath != argPath) {
                swapProperties(objPath, argPath);
            }
            return true;  // swap doesn't need further annotation checking
        }

        // Handle operator= (copy/move assignment) - may appear as CXXMemberCallExpr on some platforms
        if (methodName == "operator=" && E->getNumArgs() == 1) {
            std::string lhsPath = getExprPath(E->getImplicitObjectArgument());
            Expr *RHS = E->getArg(0)->IgnoreParenImpCasts();

            if (!lhsPath.empty()) {
                if (isStdMove(RHS)) {
                    std::string rhsPath = getMovedFromPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                        invalidateProperties(rhsPath);
                    }
                } else {
                    std::string rhsPath = getExprPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                    }
                }
            }
            return true;
        }

        // Include argument count to match correct overload
        std::string key = className + "::" + methodName + "/" + std::to_string(E->getNumArgs());

        // Get object path early - needed for both annotated and unannotated methods
        std::string objPath = getExprPath(E->getImplicitObjectArgument());
        if (objPath.empty()) return true;

        auto it = MethodAnnotations.find(key);
        if (it == MethodAnnotations.end()) {
            // No annotations - if non-const method, invalidate the object (Category C)
            if (!Method->isConst()) {
                // Check if this is an arrow call (ptr->method())
                // For arrow calls, the method mutates the pointee, not the pointer
                // itself, so preserve the pointer's _nonnull property
                bool isArrowCall = false;
                if (auto *ME = dyn_cast<MemberExpr>(E->getCallee()->IgnoreParens())) {
                    isArrowCall = ME->isArrow();
                }

                // Save _nonnull before invalidation for arrow calls
                std::optional<SymValue> savedNonnull;
                if (isArrowCall) {
                    std::string nonnullKey = objPath + "._nonnull";
                    auto nit = Env.find(nonnullKey);
                    if (nit != Env.end()) {
                        savedNonnull = nit->second;
                    }
                }

                std::string reason = "call to unannotated method '" + className + "::" + methodName + "()'";
                invalidatePropertiesWithProvenance(objPath, E->getBeginLoc(), reason);

                // Restore _nonnull for arrow calls since pointer nullability is unchanged
                if (isArrowCall && savedNonnull) {
                    Env[objPath + "._nonnull"] = *savedNonnull;
                }
            }
            return true;
        }

        const MethodInfo& info = it->second;
        
        // Capture argument values for constraint checking
        std::map<std::string, SymValue> argVals;
        for (unsigned i = 0; i < E->getNumArgs(); i++) {
            Expr *ArgExpr = E->getArg(i)->IgnoreParenImpCasts();
            std::string argKey = "arg" + std::to_string(i);
            
            // Try to evaluate as constant
            Expr::EvalResult R;
            if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                auto val = safeGetInt64(R.Val.getInt());
                if (val) argVals[argKey] = SymValue::Const(*val);
                else argVals[argKey] = SymValue::Unknown();
            } else {
                // Try to get as variable reference
                std::string argPath = getExprPath(ArgExpr);
                if (!argPath.empty() && !hasRuntimeIndex(argPath)) {
                    argVals[argKey] = SymValue::Var(argPath);
                } else {
                    argVals[argKey] = SymValue::Unknown();
                }
            }
        }
        
        // Check preconditions
        for (const auto& req : info.preconditions) {
            // For expression constraints, substitute args and check
            if (req.isExprConstraint) {
                // Build full substitution map
                std::map<std::string, SymValue> subst = argVals;
                // Add field values
                for (auto& kv : Env) {
                    if (kv.first.find("._proof_") != std::string::npos) continue;
                    if (kv.first.rfind(objPath + ".", 0) == 0) {
                        std::string field = kv.first.substr(objPath.size() + 1);
                        // If field already starts with _ or @ (ghost state), use as-is, otherwise add _
                        if (!field.empty() && (field[0] == '_' || field[0] == '@')) {
                            subst[field] = kv.second;
                        } else {
                            subst["_" + field] = kv.second;
                        }
                    }
                }
                
                SymValue lhsEval = req.lhsExpr.substitute(subst);
                SymValue rhsEval = req.rhsExpr.substitute(subst);
                auto lhs = lhsEval.eval();
                auto rhs = rhsEval.eval();
                
                if (lhs && rhs) {
                    if (!checkOp(req.op, *lhs, *rhs)) {
                        Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName
                            << req.str() << objPath << "expr" << (lhsEval.str() + " vs " + rhsEval.str());
                    }
                } else {
                    // Can't evaluate - Category D (solver couldn't resolve)
                    // In quiet mode, silently allow
                    if (GlobalMode != DTTMode::Quiet) {
                        emitCategoryD(E->getBeginLoc(), objPath, "expr", req.str(), "could not evaluate expression");
                    }
                }
                continue;
            }

            // Handle arg0 < _size style constraints
            if (req.isArgVsField) {
                auto argIt = argVals.find("arg" + std::to_string(req.argIdx));
                if (argIt == argVals.end()) {
                    // Check if this is Category C (lost tracking)
                    if (!emitCategoryC(E->getBeginLoc(), objPath, req.rhsFieldName, req.str())) {
                        // Not lost tracking - Category D or other
                        if (GlobalMode != DTTMode::Quiet) {
                            emitCategoryD(E->getBeginLoc(), objPath, req.rhsFieldName, req.str(), "argument not available");
                        }
                    }
                    continue;
                }

                auto argEval = argIt->second.eval();
                SymValue fieldVal = getField(objPath, req.rhsFieldName);
                auto fieldEval = fieldVal.eval();

                if (argEval && fieldEval) {
                    if (!checkOp(req.op, *argEval, *fieldEval)) {
                        Diag.Report(E->getBeginLoc(), ErrIndexBounds) << className << methodName
                            << std::to_string(*argEval) << std::to_string(*fieldEval)
                            << objPath << req.rhsFieldName << fieldVal.str();
                    }
                } else if (!argEval) {
                    // Runtime argument - check for variable bound proofs
                    // (handled elsewhere for loops/guards)
                } else {
                    // Field couldn't be evaluated - check if Category C (lost tracking)
                    if (!emitCategoryC(E->getBeginLoc(), objPath, req.rhsFieldName, req.str())) {
                        // Not lost tracking - Category D (solver failure)
                        if (GlobalMode != DTTMode::Quiet) {
                            emitCategoryD(E->getBeginLoc(), objPath, req.rhsFieldName, req.str(), "could not evaluate field");
                        }
                    }
                }
                continue;
            }

            // Check for Category C (lost tracking) BEFORE calling checkConstraint
            // Z3 would return false (counterexample) for unconstrained variables
            TrackingState trackState = getFieldTrackingState(objPath, req.field);
            if (trackState == TrackingState::UnknownLost) {
                // Category C: tracking was lost - emit warning/error based on mode
                emitCategoryC(E->getBeginLoc(), objPath, req.field, req.str());
                continue;
            }
            // Ghost state (@-prefixed) always has a default value of 0 and should be checked
            bool isGhostState = !req.field.empty() && req.field[0] == '@';
            if (trackState == TrackingState::Untracked && !hasAnyProofs(objPath, req.field) && !isGhostState) {
                // Category A: regular field never tracked AND no proofs - silently allow
                // The user never established tracking, so DTT has no information
                continue;
            }

            // Standard constraint checking
            auto result = checkConstraint(objPath, req, true);
            if (!result.has_value()) {
                // Category D: solver couldn't resolve (but field is tracked)
                if (GlobalMode != DTTMode::Quiet) {
                    emitCategoryD(E->getBeginLoc(), objPath, req.field, req.str(), "constraint undecidable");
                }
            } else if (!*result) {
                // Category F: constraint proven violated
                auto val = getField(objPath, req.field);
                Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName << req.str() << objPath << req.field << val.str();
            }
        }
        
        // Apply postconditions if present
        if (!info.ensures.empty()) {
            auto oldVals = captureOldValuesMethod(objPath);
            applyMethodPostconditions(objPath, info, oldVals, argVals);
        }
        // Methods with DTT annotations but no ensures are trusted - the user has
        // explicitly annotated the method and would add ensures if it mutated.
        // This matches the behavior for free functions.

        return true;
    }

    // Helper to detect if expression is std::move(x)
    bool isStdMove(const Expr *E) {
        auto *Call = dyn_cast<CallExpr>(E->IgnoreParenImpCasts());
        if (!Call) return false;

        auto *Callee = Call->getDirectCallee();
        if (!Callee || Callee->getNameAsString() != "move") return false;

        // Walk up namespace chain to find "std" (handles inline namespaces like std::__1)
        for (auto *DC = Callee->getDeclContext(); DC; DC = DC->getParent()) {
            if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                if (NS->getNameAsString() == "std") return true;
            }
        }
        return false;
    }
    
    // Get the inner expression from std::move(x)
    std::string getMovedFromPath(const Expr *E) {
        E = E->IgnoreParenImpCasts();
        if (auto *Call = dyn_cast<CallExpr>(E)) {
            if (Call->getNumArgs() >= 1) {
                return getExprPath(Call->getArg(0));
            }
        }
        return "";
    }
    
    // Handle operator[] and operator=
    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
        auto Op = E->getOperator();

        // Handle operator= for copy/move assignment
        if (Op == OO_Equal && E->getNumArgs() >= 2) {
            std::string lhsPath = getExprPath(E->getArg(0));
            Expr *RHS = E->getArg(1)->IgnoreParenImpCasts();

            // Handle chain assignment: v3 = (v2 = v1)
            // Process inner assignment first so v2 has correct properties
            if (auto *InnerAssign = dyn_cast<CXXOperatorCallExpr>(RHS)) {
                if (InnerAssign->getOperator() == OO_Equal) {
                    VisitCXXOperatorCallExpr(InnerAssign);  // Recursively process inner
                }
            }
            
            if (!lhsPath.empty()) {
                // Check if RHS is std::move(x)
                if (isStdMove(RHS)) {
                    std::string rhsPath = getMovedFromPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                        invalidateProperties(rhsPath);  // Moved-from = Unknown
                    }
                } else {
                    std::string rhsPath = getExprPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                    }
                }
            }
            return true;
        }
        
        // Handle operator[]
        if (Op != OO_Subscript) return true;
        
        auto *Method = dyn_cast<CXXMethodDecl>(E->getCalleeDecl());
        if (!Method || !Method->getParent()) return true;
        
        std::string className = Method->getParent()->getNameAsString();
        // Strip template params
        auto anglePos = className.find('<');
        if (anglePos != std::string::npos) className = className.substr(0, anglePos);
        
        // Use Method->getNumParams() since operator[] has implicit this
        std::string key = className + "::operator[]/" + std::to_string(Method->getNumParams());
        
        // DEBUG
        
        auto it = MethodAnnotations.find(key);
        if (it == MethodAnnotations.end()) return true;
        
        const MethodInfo& info = it->second;
        
        // Get the object (first argument to operator[])
        if (E->getNumArgs() < 2) return true;
        std::string objPath = getExprPath(E->getArg(0));
        if (objPath.empty()) return true;
        
        // Check argVsField constraints
        for (const auto& req : info.preconditions) {
            if (req.isArgVsField) {
                // Get argument as expression
                Expr *ArgExpr = E->getArg(1)->IgnoreParenImpCasts();

                // Get this.field value (e.g., _size)
                SymValue fieldVal = getField(objPath, req.rhsFieldName);
                auto fieldEval = fieldVal.eval();

                // Check if index is a variable with proofs (loop/guard variable)
                // If so, require matching proof - don't trust constant evaluation
                if (auto *DRE = dyn_cast<DeclRefExpr>(ArgExpr)) {
                    std::string varName = DRE->getDecl()->getNameAsString();

                    // Check if this variable has ANY proof (indicates loop/guard control)
                    bool varHasAnyProof = std::any_of(VarBoundProofs.begin(), VarBoundProofs.end(),
                        [&](const VarBoundProof& p) { return p.varName == varName; });

                    if (varHasAnyProof) {
                        // Variable is under proof control - require matching proof
                        if (hasVarBoundProof(varName, objPath, req.rhsFieldName, req.op)) {
                            continue;  // Matching proof exists for this object
                        }
                        // No matching proof for this object - error
                        if (fieldEval) {
                            Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                                << className << "operator[]" << varName
                                << std::to_string(*fieldEval) << objPath << req.rhsFieldName << fieldVal.str();
                        }
                        continue;
                    }

                    // No proofs for this variable - check Env proofs for constant bounds
                    if (fieldEval) {
                        std::string opStr = (req.op == Constraint::LT) ? "<" :
                                           (req.op == Constraint::LE) ? "<=" : "";
                        if (!opStr.empty()) {
                            std::string proofKey = varName + "._proof__value" + opStr + std::to_string(*fieldEval);
                            if (Env.find(proofKey) != Env.end()) {
                                continue;  // Constant bound proof exists
                            }
                            if (req.op == Constraint::LE) {
                                std::string ltProofKey = varName + "._proof__value<" + std::to_string(*fieldEval);
                                if (Env.find(ltProofKey) != Env.end()) {
                                    continue;  // < implies <=
                                }
                            }
                        }
                    }
                }

                // Try constant evaluation for non-variable expressions or variables without proofs
                Expr::EvalResult R;
                if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                    auto argValOpt = safeGetInt64(R.Val.getInt());
                    if (argValOpt && fieldEval) {
                        int64_t argVal = *argValOpt;
                        bool ok = checkOp(req.op, argVal, *fieldEval);
                        if (!ok) {
                            Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                                << className << "operator[]" << std::to_string(argVal)
                                << std::to_string(*fieldEval) << objPath << req.rhsFieldName << fieldVal.str();
                        }
                    }
                    continue;
                }
                
                // Try symbolic evaluation for index arithmetic
                // e.g., v[mid] where mid = v.size() / 2
                SymValue indexVal = exprToSymValueImpl(ArgExpr, Ctx);
                
                if (indexVal.kind != SymValue::UNKNOWN) {
                    // Try to evaluate symbolically
                    auto indexEval = indexVal.eval();
                    if (indexEval && fieldEval) {
                        // Both can be evaluated to constants
                        bool ok = checkOp(req.op, *indexEval, *fieldEval);
                        if (!ok) {
                            Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                                << className << "operator[]" << indexVal.str()
                                << std::to_string(*fieldEval) << objPath << req.rhsFieldName << fieldVal.str();
                        }
                        continue;
                    }
                    
                    // Try Z3 for symbolic bounds: prove indexVal < fieldVal
                    // Build constraint: indexVal < fieldVal
                    Constraint c;
                    c.op = req.op;
                    c.isExprConstraint = true;
                    c.lhsExpr = indexVal;
                    c.rhsExpr = fieldVal;
                    
                    auto z3Result = checkWithZ3(objPath, c);
                    if (z3Result.has_value() && !*z3Result) {
                        Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                            << className << "operator[]" << indexVal.str()
                            << fieldVal.str() << objPath << req.rhsFieldName << fieldVal.str();
                    } else if (z3Result.has_value() && *z3Result) {
                        // Z3 proved it's safe
                        continue;
                    }
                    // If Z3 returns nullopt, we allow it (can't prove either way)
                    continue;
                }
                // No proof found - allow it (runtime bounds checking will catch)
            }
        }
        return true;
    }

    // Check if a pointer dereference is safe (has nonnull proof)
    // Returns: true = safe, false = known null, nullopt = unknown
    std::optional<bool> checkNonnull(const std::string& ptrPath) {
        if (ptrPath.empty()) return std::nullopt;

        // Check for explicit nonnull proof
        std::string proofKey = ptrPath + "._proof__nonnull!=0";
        if (Env.find(proofKey) != Env.end()) return true;

        // Check for known null (proof that _nonnull == 0)
        std::string nullProofKey = ptrPath + "._proof__nonnull==0";
        if (Env.find(nullProofKey) != Env.end()) return false;

        // Check direct _nonnull value
        auto val = getField(ptrPath, "_nonnull");
        if (val.kind == SymValue::CONST) {
            return val.constVal != 0;
        }

        // Check alias chain: if ptrPath is an alias of another pointer, check that too
        auto aliasIt = PointerAliases.find(ptrPath);
        if (aliasIt != PointerAliases.end()) {
            const std::string& srcPath = aliasIt->second;
            // Check if alias source has proof (handles guard on original, use via alias)
            std::string srcProofKey = srcPath + "._proof__nonnull!=0";
            if (Env.find(srcProofKey) != Env.end()) return true;
            std::string srcNullProofKey = srcPath + "._proof__nonnull==0";
            if (Env.find(srcNullProofKey) != Env.end()) return false;
            // Check source's _nonnull value
            auto srcVal = getField(srcPath, "_nonnull");
            if (srcVal.kind == SymValue::CONST) {
                return srcVal.constVal != 0;
            }
        }

        // Fallback for array elements: if path has runtime index like arr[?],
        // check if arr[?]._nonnull is set (indicating all elements are non-null)
        if (hasRuntimeIndex(ptrPath)) {
            std::string arrayBase = getArrayBase(ptrPath);
            if (!arrayBase.empty()) {
                // Check arr[?]._nonnull (wildcard for all elements)
                auto wildcardVal = getField(arrayBase + "[?]", "_nonnull");
                if (wildcardVal.kind == SymValue::CONST && wildcardVal.constVal != 0) {
                    return true;
                }
            }
        }

        return std::nullopt;  // Unknown nullability
    }

    // Check if accessing a freed pointer (use-after-free)
    // Returns true if UAF was detected and reported
    bool checkUseAfterFree(const std::string& ptrPath, SourceLocation useLoc) {
        if (ptrPath.empty()) return false;

        auto it = PointerLifetimes.find(ptrPath);
        if (it != PointerLifetimes.end() && it->second.lifetime == PointerLifetime::Freed) {
            Diag.Report(useLoc, ErrUseAfterFree) << ptrPath;
            if (it->second.freeLocation.isValid()) {
                Diag.Report(it->second.freeLocation, NoteFirstFree);
            }
            return true;
        }
        return false;
    }

    // Check if pointer is uninitialized - using it in any way is UB
    bool checkUninitializedPointer(const std::string& ptrPath, SourceLocation useLoc) {
        if (ptrPath.empty()) return false;

        // Check _initialized field
        SymValue initVal = getField(ptrPath, "_initialized");
        if (initVal.kind == SymValue::CONST && initVal.constVal == 0) {
            Diag.Report(useLoc, ErrUninitializedPtr) << ptrPath;
            return true;
        }
        return false;
    }

    // Helper to emit provenance note for a pointer path
    // Returns the base variable name (stripping array indices) for provenance lookup
    std::string getBaseVarName(const std::string& ptrPath) {
        // Strip array indices and member accesses to get base variable name
        // e.g., "str[i]->field" -> "str", "name" -> "name"
        size_t bracket = ptrPath.find('[');
        size_t arrow = ptrPath.find("->");
        size_t dot = ptrPath.find('.');
        size_t end = ptrPath.size();
        if (bracket != std::string::npos) end = std::min(end, bracket);
        if (arrow != std::string::npos) end = std::min(end, arrow);
        if (dot != std::string::npos) end = std::min(end, dot);
        return ptrPath.substr(0, end);
    }

    void emitNullProvenance(const std::string& ptrPath, SourceLocation diagLoc) {
        std::string baseName = getBaseVarName(ptrPath);
        auto it = NullProvenanceMap.find(baseName);
        if (it != NullProvenanceMap.end()) {
            Diag.Report(it->second.location, NoteNullProvenance)
                << baseName << it->second.reason;
        }
    }

    // Check if a CallExpr is to an allocation function (malloc, calloc, realloc, aligned_alloc)
    // Returns the function name if it's an allocator, empty string otherwise
    std::string getAllocatorName(const CallExpr* CE) {
        if (!CE) return "";
        const FunctionDecl* FD = CE->getDirectCallee();
        if (!FD) return "";
        std::string name = FD->getNameAsString();
        if (name == "malloc" || name == "calloc" || name == "realloc" ||
            name == "aligned_alloc" || name == "posix_memalign" || name == "reallocarray") {
            return name;
        }
        return "";
    }

    // Record provenance when a pointer is assigned from an allocation function
    void recordAllocProvenance(const std::string& varName, const CallExpr* CE,
                               const std::string& allocName) {
        NullProvenanceMap[varName] = NullProvenance{
            CE->getBeginLoc(),
            "return value from " + allocName + "() may be NULL (allocation can fail)"
        };
    }

    // Handle pointer dereference via unary operator (*ptr)
    bool VisitUnaryOperator(UnaryOperator *UO) {
        Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();

        // Handle pointer increment/decrement: p++, ++p, p--, --p
        // Update tracked offset when pointer is modified
        if (UO->isIncrementDecrementOp() && SubExpr->getType()->isPointerType()) {
            std::string ptrPath = getExprPath(SubExpr);
            if (!ptrPath.empty()) {
                // Check for null pointer arithmetic (UB)
                SymValue nonnull = getField(ptrPath, "_nonnull");
                if (nonnull.kind == SymValue::CONST && nonnull.constVal == 0) {
                    Diag.Report(UO->getBeginLoc(), ErrNullPtrArith);
                    return true;
                }

                SymValue baseLen = getField(ptrPath, "_base_length");
                if (baseLen.kind != SymValue::UNKNOWN) {
                    // Always update offset (even if symbolic from loop)
                    // This handles patterns like ++p; *p where increment comes first
                    SymValue offset = getField(ptrPath, "_offset");
                    int64_t delta = UO->isIncrementOp() ? 1 : -1;
                    SymValue newOffset = SymValue::Add(offset, SymValue::Const(delta));
                    setField(ptrPath, "_offset", newOffset);
                }
            }
            return true;
        }

        if (UO->getOpcode() != UO_Deref) return true;

        if (!SubExpr->getType()->isPointerType()) return true;

        // Skip null check for 'this' pointer - assumed always valid in member functions
        if (isa<CXXThisExpr>(SubExpr)) return true;

        std::string ptrPath = getExprPath(SubExpr);
        if (ptrPath.empty()) return true;

        // Use-after-free check: detect dereference of freed pointer
        if (checkUseAfterFree(ptrPath, UO->getOperatorLoc())) {
            return true;  // Already reported UAF, skip further checks
        }

        // Uninitialized pointer check: using uninitialized pointer is UB
        if (checkUninitializedPointer(ptrPath, UO->getOperatorLoc())) {
            return true;  // Already reported, skip further checks
        }

        auto nonnullCheck = checkNonnull(ptrPath);
        if (!nonnullCheck.has_value()) {
            // Check if this is a parameter in a static function - defer warning to call sites
            int paramIdx = getParamIndex(CurrentFunctionDecl, ptrPath);
            if (paramIdx >= 0 && hasInternalLinkage(CurrentFunctionDecl)) {
                // Record param dereference for call-site analysis instead of warning here
                std::string funcKey = getFunctionKey(CurrentFunctionDecl);
                FunctionAnalysis[funcKey].paramDereferenced.insert(static_cast<unsigned>(paramIdx));
            } else {
                // Unknown nullability - report error with suggestion and provenance
                Diag.Report(UO->getBeginLoc(), ErrNullDerefUnk) << ptrPath;
                emitNullProvenance(ptrPath, UO->getBeginLoc());
                Diag.Report(UO->getBeginLoc(), NoteNullGuard) << ptrPath;
            }
        } else if (!*nonnullCheck) {
            // Known to be null - definite error
            Diag.Report(UO->getBeginLoc(), ErrNullDeref) << ptrPath;
        }

        // Buffer bounds check for *ptr (equivalent to ptr[0])
        SymValue baseLen = getField(ptrPath, "_base_length");
        SymValue offset = getField(ptrPath, "_offset");
        if (baseLen.kind != SymValue::UNKNOWN) {
            // Calculate remaining size: size = baseLen - offset
            SymValue size = (offset.kind == SymValue::CONST && offset.constVal == 0)
                ? baseLen
                : SymValue::Sub(baseLen, offset);

            auto offsetVal = offset.eval();
            auto sizeVal = size.eval();

            // Check for negative offset (before start of buffer)
            if (offsetVal && *offsetVal < 0) {
                Diag.Report(UO->getBeginLoc(), ErrBufferOOB) << 0 << 0;
                auto baseLenVal = baseLen.eval();
                if (baseLenVal) {
                    Diag.Report(UO->getBeginLoc(), NoteBufferSize)
                        << ptrPath << (int)*offsetVal << (int)*baseLenVal;
                }
            } else if (sizeVal && *sizeVal <= 0) {
                // Constant size <= 0 means definite OOB
                Diag.Report(UO->getBeginLoc(), ErrBufferOOB) << 0 << (int)*sizeVal;
                if (offset.kind == SymValue::CONST) {
                    auto baseLenVal = baseLen.eval();
                    if (baseLenVal) {
                        Diag.Report(UO->getBeginLoc(), NoteBufferSize)
                            << ptrPath << (int)offset.constVal << (int)*baseLenVal;
                    }
                }
            } else if (!sizeVal) {
                // Symbolic size - use Z3 to check if offset < baseLen
                auto result = checkBufferBoundsWithZ3(offset, baseLen, 0, ptrPath);
                if (result.has_value() && !*result) {
                    // Proven OOB
                    Diag.Report(UO->getBeginLoc(), ErrBufferOOB)
                        << offset.str() << baseLen.str();
                }
            }
        }

        return true;
    }

    // Buffer pointer tracking: check bounds on array subscript (p[n] or arr[n])
    bool VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
        Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
        Expr *Idx = ASE->getIdx();

        SymValue size;
        SymValue offset;
        std::string basePath;
        int64_t minIdx = 0;

        if (Base->getType()->isPointerType()) {
            // Pointer subscript: use tracking info
            basePath = getExprPath(Base);
            if (basePath.empty()) return true;

            // Use-after-free check: detect array access on freed pointer
            if (checkUseAfterFree(basePath, ASE->getBeginLoc())) {
                return true;  // Already reported UAF, skip further checks
            }

            SymValue baseLen = getField(basePath, "_base_length");
            offset = getField(basePath, "_offset");

            if (baseLen.kind == SymValue::UNKNOWN) return true;

            size = (offset.kind == SymValue::CONST && offset.constVal == 0)
                ? baseLen
                : SymValue::Sub(baseLen, offset);

            auto offsetVal = offset.eval();
            minIdx = offsetVal ? -(*offsetVal) : 0;
        } else if (auto *AT = dyn_cast<ConstantArrayType>(Base->getType().getTypePtr())) {
            // Direct array subscript: get size from type
            // Skip if result is still an array (multidimensional array row access)
            if (ASE->getType()->isArrayType()) {
                return true;
            }
            uint64_t arraySize = AT->getSize().getZExtValue();
            size = SymValue::Const(arraySize);
            offset = SymValue::Const(0);
            basePath = getExprPath(Base);
            if (basePath.empty()) {
                basePath = "<array>";
            }
        } else {
            // Variable-length array or other - skip
            return true;
        }

        // Get index value
        SymValue idx = exprToSymValueImpl(Idx, Ctx);

        // Try to evaluate both statically
        auto sizeVal = size.eval();
        auto idxVal = idx.eval();

        if (sizeVal && idxVal) {
            // Both constant - can check statically
            // Valid range: [minIdx, size)
            if (*idxVal < minIdx || *idxVal >= *sizeVal) {
                Diag.Report(ASE->getBeginLoc(), ErrBufferOOB)
                    << (int)*idxVal << (int)*sizeVal;
                if (offset.kind == SymValue::CONST) {
                    auto baseLenEval = size.eval();
                    if (baseLenEval) {
                        Diag.Report(ASE->getBeginLoc(), NoteBufferSize)
                            << basePath << (int)offset.constVal << (int)*baseLenEval;
                    }
                }
            }
        } else {
            // Check if index type range guarantees safe access
            // E.g., unsigned char indexing [256] array is always safe
            QualType idxType = Idx->getType();
            bool typeSafe = false;

            if (sizeVal) {
                // Get the maximum value this type can hold
                if (idxType->isUnsignedIntegerType()) {
                    uint64_t typeMax = 0;
                    if (idxType->isSpecificBuiltinType(BuiltinType::UChar) ||
                        idxType->isSpecificBuiltinType(BuiltinType::Char_U)) {
                        typeMax = 255;
                    } else if (idxType->isSpecificBuiltinType(BuiltinType::UShort)) {
                        typeMax = 65535;
                    }
                    // If array size > type max, type guarantees valid index
                    if (typeMax > 0 && static_cast<uint64_t>(*sizeVal) > typeMax) {
                        typeSafe = true;
                    }
                }
            }

            // Check bitmask/modulo patterns that guarantee bounds
            // x & MASK guarantees result in [0, MASK]
            // x % N guarantees result in [0, N-1] for unsigned indices
            bool patternSafe = false;
            if (sizeVal) {
                auto upperBound = idx.upperBound();
                if (upperBound.has_value()) {
                    // Pattern gives guaranteed upper bound
                    bool isUnsignedIdx = idxType->isUnsignedIntegerType();
                    bool lowerOk = idx.isGuaranteedNonNegative() || isUnsignedIdx || minIdx <= 0;
                    bool upperOk = *upperBound < *sizeVal;
                    if (lowerOk && upperOk) {
                        patternSafe = true;
                    }
                }
            }

            if (typeSafe || patternSafe) {
                // Type range or pattern guarantees index is always valid - no action needed
            } else {
                // Symbolic bounds - use Z3 to check
                bool isUnsignedIdx = idxType->isUnsignedIntegerType();
                auto result = checkBufferBoundsWithZ3(idx, size, minIdx, basePath, isUnsignedIdx);
                if (result.has_value()) {
                    if (!*result) {
                        // Counterexample found - index can be out of bounds
                        Diag.Report(ASE->getBeginLoc(), ErrBufferOOB)
                            << idx.str() << size.str();
                        Diag.Report(ASE->getBeginLoc(), NoteBufferGuard)
                            << idx.str() << size.str();
                    }
                    // else: proven safe, no action needed
                } else {
                    // Category D: can't prove bounds are safe
                    std::string constraint = idx.str() + " < " + size.str();
                    emitCategoryD(ASE->getBeginLoc(), basePath, "_size", constraint,
                                  "buffer bounds could not be verified");
                }
            }
        }

        return true;
    }

    // Handle pointer dereference via arrow operator (ptr->member)
    bool VisitMemberExpr(MemberExpr *ME) {
        if (!ME->isArrow()) return true;

        Expr *Base = ME->getBase()->IgnoreParenImpCasts();
        if (!Base->getType()->isPointerType()) return true;

        // Skip null check for 'this' pointer - assumed always valid in member functions
        if (isa<CXXThisExpr>(Base)) return true;

        std::string ptrPath = getExprPath(Base);
        if (ptrPath.empty()) return true;

        // Use-after-free check: detect arrow access on freed pointer
        if (checkUseAfterFree(ptrPath, ME->getOperatorLoc())) {
            return true;  // Already reported UAF, skip further checks
        }

        // Uninitialized pointer check: using uninitialized pointer is UB
        if (checkUninitializedPointer(ptrPath, ME->getOperatorLoc())) {
            return true;  // Already reported, skip further checks
        }

        auto nonnullCheck = checkNonnull(ptrPath);
        if (!nonnullCheck.has_value()) {
            // Check if this is a parameter in a static function - defer warning to call sites
            int paramIdx = getParamIndex(CurrentFunctionDecl, ptrPath);
            if (paramIdx >= 0 && hasInternalLinkage(CurrentFunctionDecl)) {
                // Record param dereference for call-site analysis instead of warning here
                std::string funcKey = getFunctionKey(CurrentFunctionDecl);
                FunctionAnalysis[funcKey].paramDereferenced.insert(static_cast<unsigned>(paramIdx));
            } else {
                // Unknown nullability - report error with suggestion and provenance
                Diag.Report(ME->getBeginLoc(), ErrNullDerefUnk) << ptrPath;
                emitNullProvenance(ptrPath, ME->getBeginLoc());
                Diag.Report(ME->getBeginLoc(), NoteNullGuard) << ptrPath;
            }
        } else if (!*nonnullCheck) {
            // Known to be null - definite error
            Diag.Report(ME->getBeginLoc(), ErrNullDeref) << ptrPath;
        }
        // If nonnullCheck is true, the dereference is safe

        return true;
    }

    bool VisitCallExpr(CallExpr *E) {
        // Skip if it's a member call or operator call (handled separately)
        if (isa<CXXMemberCallExpr>(E) || isa<CXXOperatorCallExpr>(E)) return true;

        auto *Callee = E->getDirectCallee();
        if (!Callee) return true;
        std::string fn = Callee->getNameAsString();

        // Handle free() calls for lifetime tracking
        if (isDeallocationFunction(Callee) && E->getNumArgs() >= 1) {
            std::string ptrPath = getExprPath(E->getArg(0));
            if (!ptrPath.empty()) {
                auto it = PointerLifetimes.find(ptrPath);
                if (it != PointerLifetimes.end()) {
                    // Double-free check
                    if (it->second.lifetime == PointerLifetime::Freed) {
                        Diag.Report(E->getBeginLoc(), ErrDoubleFree) << ptrPath;
                        if (it->second.freeLocation.isValid()) {
                            Diag.Report(it->second.freeLocation, NoteFirstFree);
                        }
                    }
                    // Mismatched deallocation check: free() requires malloc/calloc/realloc/aligned_alloc
                    else if (it->second.allocSource != AllocSource::Malloc &&
                             it->second.allocSource != AllocSource::AlignedAlloc &&
                             it->second.allocSource != AllocSource::Unknown) {
                        Diag.Report(E->getBeginLoc(), ErrMismatchedDealloc)
                            << ptrPath
                            << allocSourceToString(it->second.allocSource)
                            << "free()";
                    }
                    // Mark as freed
                    it->second.lifetime = PointerLifetime::Freed;
                    it->second.freeLocation = E->getBeginLoc();
                }
            }
        }

        // Handle std::swap(a, b)
        if (fn == "swap" && E->getNumArgs() == 2) {
            // Check if it's in std namespace
            bool isStdSwap = false;
            if (auto *DC = Callee->getDeclContext()) {
                if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                    isStdSwap = (NS->getNameAsString() == "std");
                }
            }
            if (isStdSwap) {
                std::string path1 = getExprPath(E->getArg(0));
                std::string path2 = getExprPath(E->getArg(1));
                if (!path1.empty() && !path2.empty() && path1 != path2) {
                    swapProperties(path1, path2);
                }
                return true;
            }
        }
        
        if (Callee->getReturnType()->isVoidType()) {
            auto oldVals = captureOldValues(E);
            applyVoidPostconditions(E, oldVals);
        }
        
        std::vector<Constraint> preconditions;
        for (auto *A : Callee->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:requires=", 0) == 0) {
                    // Translate parameter names to argN before parsing
                    std::string translated = translateParamNames(s.substr(13), Callee);
                    auto c = parseConstraint(translated);
                    if (c) preconditions.push_back(*c);
                }
            }
        }

        std::vector<std::string> argPaths;
        for (unsigned i = 0; i < E->getNumArgs(); i++) {
            auto *Arg = E->getArg(i)->IgnoreParenImpCasts();
            argPaths.push_back(getExprPath(Arg));
        }

        for (const auto& req : preconditions) {
            if (req.isMultiArg) {
                auto result = checkMultiArgConstraint(req, argPaths);
                if (!result.has_value() || !*result) {
                    int lhsIdx = std::atoi(req.lhsArg.c_str() + 3);
                    int rhsIdx = std::atoi(req.rhsArg.c_str() + 3);
                    if (lhsIdx < (int)argPaths.size() && rhsIdx < (int)argPaths.size()) {
                        SymValue lhsVal = getField(argPaths[lhsIdx], req.field);
                        SymValue rhsVal = getField(argPaths[rhsIdx], req.rhsField);
                        Diag.Report(E->getBeginLoc(), ErrMultiArg)
                            << fn << req.str() << argPaths[lhsIdx] << req.field << lhsVal.str()
                            << argPaths[rhsIdx] << req.rhsField << rhsVal.str();
                    }
                }
                continue;
            }

            // Handle expression constraints like "arg0._size >= arg1"
            if (req.isExprConstraint) {
                // Build substitution map: argN -> value, argN._field -> property
                std::map<std::string, SymValue> subst;
                for (unsigned i = 0; i < E->getNumArgs(); i++) {
                    std::string argKey = "arg" + std::to_string(i);
                    auto *Arg = E->getArg(i)->IgnoreParenImpCasts();

                    // Get argument's direct value (for argN in RHS)
                    if (auto *IL = dyn_cast<IntegerLiteral>(Arg)) {
                        subst[argKey] = SymValue::Const(IL->getValue().getSExtValue());
                    } else {
                        std::string argPath = i < argPaths.size() ? argPaths[i] : "";
                        if (!argPath.empty()) {
                            // Check for a tracked value
                            SymValue argVal = getField(argPath, "_value");
                            if (argVal.kind == SymValue::CONST) {
                                subst[argKey] = argVal;
                            } else {
                                subst[argKey] = SymValue::Unknown();
                            }

                            // Add field properties from the argument's tracked state
                            std::string prefix = argPath + ".";
                            for (auto& kv : Env) {
                                if (kv.first.rfind(prefix, 0) == 0 && kv.first.find("._proof_") == std::string::npos) {
                                    std::string field = kv.first.substr(prefix.size());
                                    subst[argKey + "." + field] = kv.second;
                                }
                            }
                        } else {
                            subst[argKey] = SymValue::Unknown();
                        }
                    }
                }

                SymValue lhsEval = req.lhsExpr.substitute(subst);
                SymValue rhsEval = req.rhsExpr.substitute(subst);
                auto lhs = lhsEval.eval();
                auto rhs = rhsEval.eval();

                if (lhs && rhs) {
                    if (!checkOp(req.op, *lhs, *rhs)) {
                        // Constraint violated
                        Diag.Report(E->getBeginLoc(), ErrFail) << fn << req.str()
                            << lhsEval.str() << "evaluated" << (std::to_string(*lhs) + " vs " + std::to_string(*rhs));
                    }
                } else {
                    // Could not evaluate - Category D
                    if (GlobalMode != DTTMode::Quiet) {
                        // Check if any arg is untracked (Category C)
                        bool hasUntracked = false;
                        for (unsigned i = 0; i < argPaths.size(); i++) {
                            if (argPaths[i].empty()) continue;
                            if (getFieldTrackingState(argPaths[i], "_size") == TrackingState::UnknownLost) {
                                hasUntracked = true;
                                emitCategoryC(E->getBeginLoc(), argPaths[i], "_size", req.str());
                                break;
                            }
                        }
                        if (!hasUntracked) {
                            emitCategoryD(E->getBeginLoc(), "", "expr", req.str(), "could not evaluate");
                        }
                    }
                }
                continue;
            }

            // Handle nullptr/zero constraints on arguments (arg0 != nullptr, arg0 != 0)
            if (req.isArgVsField && req.rhsFieldName == "0") {
                if (req.argIdx >= (int)argPaths.size()) continue;
                auto *Arg = E->getArg(req.argIdx)->IgnoreParenImpCasts();

                // Only treat as nullptr check if argument is actually a pointer type
                // For non-pointer types (int, etc.), treat as value comparison: arg0 != 0
                if (!Arg->getType()->isPointerType()) {
                    // Integer comparison: check actual value against 0
                    std::string argPath = argPaths[req.argIdx];
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(Arg, R, Ctx)) {
                        int64_t val = R.Val.getInt().getSExtValue();
                        bool satisfied = (req.op == Constraint::NE) ? (val != 0) : (val == 0);
                        if (!satisfied) {
                            Diag.Report(E->getBeginLoc(), ErrFail) << fn << req.str()
                                << argPath << "_value" << std::to_string(val);
                        }
                    }
                    // If can't evaluate, skip (runtime value)
                    continue;
                }

                std::string argPath = argPaths[req.argIdx];
                if (argPath.empty()) continue;

                // Check the _nonnull property of the argument
                auto nonnullCheck = checkNonnull(argPath);
                if (!nonnullCheck.has_value()) {
                    // Unknown nullability - report error with provenance
                    Diag.Report(E->getBeginLoc(), ErrNullDerefUnk) << argPath;
                    emitNullProvenance(argPath, E->getBeginLoc());
                    Diag.Report(E->getBeginLoc(), NoteNullGuard) << argPath;
                } else if (!*nonnullCheck && req.op == Constraint::NE) {
                    // Known null but requires non-null
                    Diag.Report(E->getBeginLoc(), ErrNullDeref) << argPath;
                } else if (*nonnullCheck && req.op == Constraint::EQ) {
                    // Known non-null but requires null
                    Diag.Report(E->getBeginLoc(), ErrFail) << fn << req.str() << argPath << "_nonnull" << "non-null";
                }
                // Otherwise the constraint is satisfied
                continue;
            }

            // Determine which argument to check based on constraint type
            // For "argN._field > constant" constraints, use argN; otherwise use arg0
            unsigned argIdxToCheck = 0;
            if (req.isArgVsField && req.rhsFieldName.empty() && !req.field.empty()) {
                argIdxToCheck = req.argIdx;
            }

            if (E->getNumArgs() <= argIdxToCheck) continue;
            auto *Arg = E->getArg(argIdxToCheck)->IgnoreParenImpCasts();
            std::string argPath = getExprPath(Arg);
            if (argPath.empty() || hasRuntimeIndex(argPath)) continue;

            // Check for Category C (lost tracking) BEFORE calling checkConstraint
            TrackingState trackState = getFieldTrackingState(argPath, req.field);
            if (trackState == TrackingState::UnknownLost) {
                // Category C: tracking was lost - emit warning/error based on mode
                emitCategoryC(E->getBeginLoc(), argPath, req.field, req.str());
                continue;
            }

            auto result = checkConstraint(argPath, req, true);
            if (!result.has_value()) {
                // Category D: solver couldn't resolve (but field is tracked)
                if (GlobalMode != DTTMode::Quiet) {
                    emitCategoryD(E->getBeginLoc(), argPath, req.field, req.str(), "constraint undecidable");
                }
            } else if (!*result) {
                // Category F: constraint proven violated
                auto val = getField(argPath, req.field);
                Diag.Report(E->getBeginLoc(), ErrFail) << fn << req.str() << argPath << req.field << val.str();
            }
        }

        // Invalidate properties for arguments passed to modifiable parameters
        // of unannotated functions.
        //
        // For REFERENCES (T&, T&&): Always invalidate - function can modify the argument
        // For POINTERS (T*): Only invalidate if argument is &obj (address-of expression)
        //   - &obj passed to T*: function can modify obj via ptr->field, invalidate obj
        //   - p passed to T*: pointer copied by value, local p unchanged, DON'T invalidate
        //
        // This distinction matters for pointer/buffer tracking:
        //   int* p = arr;  f(p);  // p still points to arr with same bounds
        //   Vec obj;       f(&obj); // obj's fields can be modified
        //
        // Functions with ANY dtt: annotation are trusted - the user has opted into
        // the DTT system and would add ensures= if the function mutated arguments.
        // Skip std::move - it's just a cast, doesn't actually modify the object.
        if (!hasDTTAnnotations(Callee) && !isStdMove(E)) {
            for (unsigned i = 0; i < E->getNumArgs() && i < Callee->getNumParams(); i++) {
                ParmVarDecl* Param = Callee->getParamDecl(i);
                QualType paramType = Param->getType();
                Expr *Arg = E->getArg(i)->IgnoreParenImpCasts();

                // Check if this parameter/argument combination should trigger invalidation
                bool shouldInvalidate = isModifiableRef(paramType) ||
                                        shouldInvalidateForPointerParam(paramType, Arg);

                if (shouldInvalidate) {
                    std::string argPath;

                    // If argument is std::move(x), invalidate x (the moved-from object)
                    if (isStdMove(Arg)) {
                        argPath = getMovedFromPath(Arg);
                    } else {
                        argPath = getExprPath(Arg);
                    }

                    if (!argPath.empty() && !hasRuntimeIndex(argPath)) {
                        // Category C: Record provenance for lost tracking
                        std::string reason = "call to unannotated function '" + fn + "()'";
                        invalidatePropertiesWithProvenance(argPath, E->getBeginLoc(), reason);
                    }
                }
            }
        }

        return true;
    }

    // Handle delete/delete[] for lifetime tracking
    bool VisitCXXDeleteExpr(CXXDeleteExpr *DE) {
        Expr *Arg = DE->getArgument();
        if (!Arg) return true;

        std::string ptrPath = getExprPath(Arg);
        if (ptrPath.empty()) return true;

        auto it = PointerLifetimes.find(ptrPath);
        if (it != PointerLifetimes.end()) {
            // Double-free check
            if (it->second.lifetime == PointerLifetime::Freed) {
                Diag.Report(DE->getBeginLoc(), ErrDoubleFree) << ptrPath;
                if (it->second.freeLocation.isValid()) {
                    Diag.Report(it->second.freeLocation, NoteFirstFree);
                }
            } else {
                // Mismatched deallocation check
                bool isArrayDelete = DE->isArrayForm();
                AllocSource expected = isArrayDelete ? AllocSource::NewArray : AllocSource::New;
                AllocSource actual = it->second.allocSource;

                // Check for mismatch (skip Unknown sources)
                if (actual != AllocSource::Unknown) {
                    bool mismatch = false;
                    std::string deallocStr = isArrayDelete ? "delete[]" : "delete";

                    if (isArrayDelete) {
                        // delete[] requires new[]
                        mismatch = (actual != AllocSource::NewArray);
                    } else {
                        // delete requires new (scalar)
                        mismatch = (actual != AllocSource::New);
                    }

                    if (mismatch) {
                        Diag.Report(DE->getBeginLoc(), ErrMismatchedDealloc)
                            << ptrPath
                            << allocSourceToString(actual)
                            << deallocStr;
                    }
                }
            }

            // Mark as freed
            it->second.lifetime = PointerLifetime::Freed;
            it->second.freeLocation = DE->getBeginLoc();
        }

        return true;
    }

    // Emit Category C diagnostic (lost tracking) at first unsafe use
    // Returns true if emitted or suppressed (caller should stop processing this requirement)
    bool emitCategoryC(SourceLocation useLoc, const std::string& basePath,
                       const std::string& field, const std::string& constraint) {
        if (!DiagLostTracking.shouldEmit(GlobalMode)) return false;

        TrackingState state = getFieldTrackingState(basePath, field);
        if (state != TrackingState::UnknownLost) return false;

        // Warn-once per (basePath, field) combination
        if (hasWarnedLostTracking(basePath, field)) return true;
        markWarnedLostTracking(basePath, field);

        Diag.Report(useLoc, DiagLostTracking.get(GlobalMode))
            << constraint << (basePath + "." + field);

        if (auto provenance = getFieldLossProvenance(basePath, field)) {
            Diag.Report(provenance->location, NoteLostAt) << provenance->reason;
        }

        return true;
    }

    // Emit Category D diagnostic (solver couldn't resolve)
    // Returns true if emitted (caller may want to suppress further diagnostics)
    bool emitCategoryD(SourceLocation useLoc, const std::string& basePath,
                       const std::string& field, const std::string& constraint,
                       const std::string& reason) {
        if (!DiagSolverFailed.shouldEmit(GlobalMode)) return false;

        Diag.Report(useLoc, DiagSolverFailed.get(GlobalMode))
            << constraint << (basePath + "." + field) << reason;
        return true;
    }
};

class DTTConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &Ctx) override {
        // Clear all state for new translation unit
        clearPerFunctionState();
        FnPostconditions.clear();
        FnBoolPostconditions.clear();
        TotalFunctions.clear();
        MethodAnnotations.clear();
        FunctionAnalysis.clear();
        registerSyntheticAnnotations();  // Register std:: type bounds checking

        // ====================================================
        // PASS 1: Collect call sites and return statements
        // ====================================================
        AnalysisCollector(Ctx).TraverseDecl(Ctx.getTranslationUnitDecl());

        // ====================================================
        // BETWEEN PASSES: Compute function ghost states
        // ====================================================
        computeFunctionGhostStates();

        // ====================================================
        // PASS 2: Main DTT analysis with enriched Env
        // ====================================================
        DTTVisitor(Ctx).TraverseDecl(Ctx.getTranslationUnitDecl());

        // ====================================================
        // POST PASS 2: Propagate and emit call-site warnings
        // ====================================================
        propagateParamDereferenced();  // Compute transitive closure
        emitCallSiteWarnings(Ctx);
    }
};

class DTTPlugin : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, StringRef) override {
        return std::make_unique<DTTConsumer>();
    }
    bool ParseArgs(const CompilerInstance& CI, const std::vector<std::string>& Args) override {
        for (const auto& arg : Args) {
            if (arg == "--dtt-mode=inform" || arg == "-dtt-mode=inform") {
                GlobalMode = DTTMode::Inform;
            } else if (arg == "--dtt-mode=enforce" || arg == "-dtt-mode=enforce") {
                GlobalMode = DTTMode::Enforce;
            } else if (arg == "--dtt-mode=quiet" || arg == "-dtt-mode=quiet") {
                GlobalMode = DTTMode::Quiet;
            } else if (arg.rfind("--dtt-mode=", 0) == 0 || arg.rfind("-dtt-mode=", 0) == 0) {
                // Unknown mode value
                llvm::errs() << "DTT: unknown mode '" << arg << "'. Valid modes: inform, enforce, quiet\n";
                return false;
            }
            // Ignore unknown args (might be for other plugins)
        }
        return true;
    }
    ActionType getActionType() override { return AddAfterMainAction; }
};

}

static FrontendPluginRegistry::Add<DTTPlugin> X("dtt", "DTT v25");
