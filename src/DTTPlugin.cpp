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
#include <fstream>
#include <cstdlib>
#include <array>
#include <memory>
#include <atomic>
#include <cerrno>
#include <unistd.h>

using namespace clang;

namespace {

ASTContext *GlobalCtx = nullptr;

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
    // NEW: Handle 'this' pointer for method body analysis
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
            if (ASE->getIdx()->EvaluateAsInt(R, *GlobalCtx)) {
                const llvm::APSInt &Val = R.Val.getInt();
                if (Val.isRepresentableByInt64()) {
                    int64_t idx = Val.getExtValue();
                    return base + "[" + std::to_string(idx) + "]";
                }
            }
        }
        return base + "[?]";
    }
    // NEW v16: Handle chain assignment (v3 = v2 = v1) — result of v2 = v1 is v2
    if (auto *OpCall = dyn_cast<CXXOperatorCallExpr>(E)) {
        if (OpCall->getOperator() == OO_Equal && OpCall->getNumArgs() >= 1) {
            return getExprPath(OpCall->getArg(0));
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

    // Unknown expression - caller must handle (e.g., DeclRefExpr for copying)
    return std::nullopt;
}

// ============================================================
// SYMBOLIC VALUES
// ============================================================

struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR } kind;
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
    
    std::optional<int64_t> eval() const {
        switch (kind) {
            case CONST: return constVal;
            case ADD: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l + *r; } return std::nullopt;
            case SUB: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l - *r; } return std::nullopt;
            case MUL: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r) return *l * *r; } return std::nullopt;
            case DIV: if (left && right) { auto l = left->eval(), r = right->eval(); if (l && r && *r != 0) return *l / *r; } return std::nullopt;
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
            default: return "?";
        }
    }
    
    std::string toSMT(const std::string& varPrefix) const {
        switch (kind) {
            case CONST: return std::to_string(constVal);
            case UNKNOWN: return varPrefix;
            case VAR: { std::string v = varRef; for (char& c : v) if (c == '.' || c == '[' || c == ']' || c == ':') c = '_'; return v; }
            case ADD: return "(+ " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            case SUB: return "(- " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            case MUL: return "(* " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            case DIV: return "(div " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            default: return "0";
        }
    }
    
    void collectVars(std::set<std::string>& vars) const {
        if (kind == VAR) { std::string v = varRef; for (char& c : v) if (c == '.' || c == '[' || c == ']' || c == ':') c = '_'; vars.insert(v); }
        else if (kind == ADD || kind == SUB || kind == MUL || kind == DIV) { if (left) left->collectVars(vars); if (right) right->collectVars(vars); }
    }
    
    SymValue substitute(const std::map<std::string, SymValue>& env) const {
        if (kind == VAR) { auto it = env.find(varRef); return it != env.end() ? it->second : *this; }
        if (kind == ADD) return Add(left->substitute(env), right->substitute(env));
        if (kind == SUB) return Sub(left->substitute(env), right->substitute(env));
        if (kind == MUL) return Mul(left->substitute(env), right->substitute(env));
        if (kind == DIV) return Div(left->substitute(env), right->substitute(env));
        return *this;
    }
    
    bool equals(const SymValue& other) const {
        if (kind != other.kind) return false;
        if (kind == CONST) return constVal == other.constVal;
        if (kind == VAR) return varRef == other.varRef;
        if (kind == ADD || kind == SUB || kind == MUL || kind == DIV)
            return left && other.left && right && other.right &&
                   left->equals(*other.left) && right->equals(*other.right);
        return false;
    }
};

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
    // NEW v14: For arbitrary expression constraints like "arg0 + arg1 < _size"
    bool isExprConstraint = false;
    SymValue lhsExpr, rhsExpr;
    
    std::string str() const {
        if (op == AND) return "(" + left->str() + " && " + right->str() + ")";
        if (op == OR) return "(" + left->str() + " || " + right->str() + ")";
        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
        if (isExprConstraint) return lhsExpr.str() + " " + ops[op] + " " + rhsExpr.str();
        if (isMultiArg) return lhsArg + "." + field + " " + ops[op] + " " + rhsArg + "." + rhsField;
        if (isArgVsField) return "arg" + std::to_string(argIdx) + " " + ops[op] + " " + rhsFieldName;
        return field + " " + ops[op] + " " + std::to_string(rhs);
    }
    
    std::string toSMT(const std::string& varName) const {
        std::string v = varName + "_" + field;
        if (op == AND) return "(and " + left->toSMT(varName) + " " + right->toSMT(varName) + ")";
        if (op == OR) return "(or " + left->toSMT(varName) + " " + right->toSMT(varName) + ")";
        const char* smtOps[] = {">", ">=", "<", "<=", "=", "distinct"};
        if (isExprConstraint) {
            if (op == NE) return "(distinct " + lhsExpr.toSMT(varName) + " " + rhsExpr.toSMT(varName) + ")";
            return "(" + std::string(smtOps[op]) + " " + lhsExpr.toSMT(varName) + " " + rhsExpr.toSMT(varName) + ")";
        }
        if (op == NE) return "(distinct " + v + " " + std::to_string(rhs) + ")";
        return "(" + std::string(smtOps[op]) + " " + v + " " + std::to_string(rhs) + ")";
    }
    
    std::string toSMTNegated(const std::string& varName) const { return "(not " + toSMT(varName) + ")"; }
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
        else if (parenDepth == 0 && (s[i] == '+' || s[i] == '*' || s[i] == '/')) return true;
        // Note: '-' is tricky because of negative numbers, skip for detection
    }
    return false;
}

// ============================================================
// CONSTRAINT PARSING
// ============================================================

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
    
    // NEW v14: Check for arithmetic expressions (e.g., "arg0 + arg1 < _size")
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
        
        // Handle "arg0 < _size" format (argument vs this-field)
        if (!rhsStr.empty() && rhsStr[0] == '_') {
            c.isArgVsField = true;
            c.argIdx = std::atoi(lhsStr.c_str() + 3);  // Extract N from "argN"
            c.rhsFieldName = rhsStr;
            return c;
        }
    }
    
    // NEW: Handle nullptr constraints: "arg0 != nullptr", "ptr != nullptr", "_result != nullptr"
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
                c.rhsFieldName = "_nullptr_check";  // Marker for nullptr constraint
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
    
    // NEW v15: Use expression parser for all RHS expressions
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
    } else if (lhs[0] == '_') {
        bp.argRef = "this";
        bp.field = lhs;
    } else {
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

// NEW: Method annotations storage
struct MethodInfo {
    std::vector<Constraint> requires;
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
        info.requires.push_back(makeBoundsConstraint());
        MethodAnnotations["array::operator[]/1"] = info;
    }
    
    // std::span<T, N>::operator[](size_t)
    {
        MethodInfo info;
        info.requires.push_back(makeBoundsConstraint());
        MethodAnnotations["span::operator[]/1"] = info;
    }
    
    // std::bitset<N>::operator[](size_t)
    {
        MethodInfo info;
        info.requires.push_back(makeBoundsConstraint());
        MethodAnnotations["bitset::operator[]/1"] = info;
    }
}

// NEW v13: Variable bounds proofs
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

void setField(const std::string& basePath, const std::string& field, SymValue v) { 
    Env[basePath + "." + field] = v; 
}

SymValue getField(const std::string& basePath, const std::string& field) {
    auto it = Env.find(basePath + "." + field);
    return (it != Env.end()) ? it->second : SymValue::Unknown();
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

// Invalidate all tracked properties for a given variable path
// Called when variable is passed by non-const reference to function without postcondition
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

// Check if a parameter type can be used to modify the argument
bool isModifiableRef(QualType paramType) {
    // Check for non-const lvalue reference
    if (paramType->isLValueReferenceType()) {
        QualType pointee = paramType->getPointeeType();
        return !pointee.isConstQualified();
    }

    // Check for non-const pointer
    if (paramType->isPointerType()) {
        QualType pointee = paramType->getPointeeType();
        return !pointee.isConstQualified();
    }

    // Pass by value - doesn't modify original
    return false;
}

// NEW: Convert a simple constraint to a proof key string
// For constraint "_size > 0" returns "_size>0"
std::string constraintToProofKey(const Constraint& c) {
    if (c.op == Constraint::AND || c.op == Constraint::OR) {
        // Compound constraints not supported as single proof keys
        return "";
    }
    const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
    return c.field + ops[c.op] + std::to_string(c.rhs);
}

// NEW: Inject a requires constraint as an assumption inside the function body
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
            if (!fieldName.empty() && fieldName[0] != '_') {
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

// NEW v16: Clear all properties for a path (for assignment LHS)
void clearProperties(const std::string& path) {
    std::vector<std::string> toRemove;
    for (auto& kv : Env) {
        if (kv.first.rfind(path + ".", 0) == 0) {
            toRemove.push_back(kv.first);
        }
    }
    for (auto& k : toRemove) Env.erase(k);
}

// NEW v16: Exchange properties between two paths (for swap)
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

// NEW v15: Convert an expression to a SymValue for symbolic tracking
SymValue exprToSymValueImpl(const Expr *E, ASTContext &Ctx) {
    if (!E) return SymValue::Unknown();
    E = E->IgnoreParenImpCasts();
    
    // Constant integer
    Expr::EvalResult R;
    if (E->EvaluateAsInt(R, const_cast<ASTContext&>(Ctx))) {
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
    
    // Binary operator (+, -, *, /)
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        SymValue left = exprToSymValueImpl(BO->getLHS(), Ctx);
        SymValue right = exprToSymValueImpl(BO->getRHS(), Ctx);
        switch (BO->getOpcode()) {
            case BO_Add: return SymValue::Add(left, right);
            case BO_Sub: return SymValue::Sub(left, right);
            case BO_Mul: return SymValue::Mul(left, right);
            case BO_Div: return SymValue::Div(left, right);
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
// Z3 SMT SOLVER
// ============================================================

std::string runCommand(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) result += buffer.data();
    return result;
}

std::string sanitizeForSMT(const std::string& s) {
    std::string result = s;
    for (char& c : result) if (c == '.' || c == '[' || c == ']' || c == ':') c = '_';
    return result;
}

std::optional<bool> checkWithZ3(const std::string& basePath, const Constraint& c) {
    std::stringstream smt;
    smt << "(set-logic QF_NIA)\n";  // Changed to NIA to support multiplication
    std::set<std::string> allVars;
    std::string varName = sanitizeForSMT(basePath);
    
    // Collect vars from constraint
    if (c.isExprConstraint) {
        c.lhsExpr.collectVars(allVars);
        c.rhsExpr.collectVars(allVars);
    } else {
        allVars.insert(varName + "_" + c.field);
    }
    
    for (auto& kv : Env) {
        if (kv.first.find("._proof_") != std::string::npos) continue;
        std::string v = sanitizeForSMT(kv.first);
        allVars.insert(v);
        kv.second.collectVars(allVars);
    }
    
    for (const auto& v : allVars) smt << "(declare-const " << v << " Int)\n";
    
    for (auto& kv : Env) {
        if (kv.first.find("._proof_") != std::string::npos) continue;
        std::string v = sanitizeForSMT(kv.first);
        if (kv.second.kind == SymValue::CONST) 
            smt << "(assert (= " << v << " " << kv.second.constVal << "))\n";
        else if (kv.second.kind == SymValue::ADD || kv.second.kind == SymValue::SUB || 
                 kv.second.kind == SymValue::MUL || kv.second.kind == SymValue::DIV)
            smt << "(assert (= " << v << " " << kv.second.toSMT(v) << "))\n";
        else if (kv.second.kind == SymValue::VAR) {
            // NEW v14: Add equality for variable assignments (for transitive proofs)
            std::string rhsVar = kv.second.varRef;
            for (char& ch : rhsVar) if (ch == '.' || ch == '[' || ch == ']' || ch == ':') ch = '_';
            smt << "(assert (= " << v << " " << rhsVar << "))\n";
        }
    }
    
    for (auto& kv : Env) {
        size_t proofPos = kv.first.find("._proof_");
        if (proofPos == std::string::npos) continue;
        std::string base = kv.first.substr(0, proofPos);
        std::string proofKey = kv.first.substr(proofPos + 8);
        auto parsed = parseSimpleConstraint(proofKey);
        if (!parsed) continue;
        std::string v = sanitizeForSMT(base);
        smt << "(assert " << parsed->toSMT(v) << ")\n";
    }
    
    smt << "(assert " << c.toSMTNegated(varName) << ")\n(check-sat)\n";

    static std::atomic<uint64_t> counter{0};
    std::string tmpFile = "/tmp/dtt_z3_" + std::to_string(getpid()) + "_" + std::to_string(counter++) + ".smt2";
    { std::ofstream f(tmpFile); f << smt.str(); }
    std::string result = runCommand("z3 -t:1000 -smt2 " + tmpFile + " 2>/dev/null");
    std::remove(tmpFile.c_str());
    
    if (result.find("unsat") != std::string::npos) return true;
    if (result.find("sat") != std::string::npos) return false;
    return std::nullopt;
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
    
    // NEW v14: Handle expression constraints
    if (c.isExprConstraint) {
        // Build substitution map from environment
        std::map<std::string, SymValue> subst;
        for (auto& kv : Env) {
            if (kv.first.find("._proof_") != std::string::npos) continue;
            // Handle basePath._field -> _field mapping
            if (kv.first.rfind(basePath + ".", 0) == 0) {
                std::string field = kv.first.substr(basePath.size() + 1);
                subst["_" + field] = kv.second;
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
    
    // NEW v14: Transitive proof support
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
// VISITOR
// ============================================================

class DTTVisitor : public RecursiveASTVisitor<DTTVisitor> {
    ASTContext &Ctx;
    DiagnosticsEngine &Diag;
    unsigned ErrFail, ErrUnk, NoteGuard, ErrMultiArg, ErrNotTotal, NoteCase, ErrMethodReq, ErrIndexBounds;
    unsigned ErrNullDeref, ErrNullDerefUnk, NoteNullGuard;
    FunctionDecl *CurrentFunctionDecl = nullptr;

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
        // NEW: nullptr tracking diagnostics
        ErrNullDeref = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: potential null pointer dereference of '%0' (known to be null)");
        ErrNullDerefUnk = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: potential null pointer dereference of '%0' (nullability unknown)");
        NoteNullGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note, "add guard: if (%0 != nullptr) { ... }");
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
                    if (c) info.requires.push_back(*c);
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
        
        if (!info.requires.empty() || !info.ensures.empty() || !info.returns.empty() || !info.boolPostconds.empty()) {
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

        // Bug #2 fix: Clear per-function state when entering a new function body
        // This prevents variable names from one function polluting another function's Env
        if (F->hasBody() && F->isThisDeclarationADefinition()) {
            Env.clear();
            VarBoundProofs.clear();

            // NEW: Contract propagation — inject dtt:requires as assumptions inside function body
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
                        for (const auto& req : it->second.requires) {
                            injectRequiresAsAssumption(F, req);
                        }
                    }
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
    
    // NEW: Capture old values for method calls (this-based)
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
    
    // NEW: Apply method postconditions
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
                subst["_" + field] = kv.second;
            }
        }
        
        for (const auto& post : info.ensures) {
            SymValue result = post.expr.substitute(subst);
            setField(objPath, post.resultField, result);
        }
    }

    bool VisitVarDecl(VarDecl *D) {
        std::string var = D->getNameAsString();

        // Extract template parameters from type (std::array, std::span, user templates)
        extractTemplateParams(var, D->getType());

        // NEW: Track pointer nullability on initialization
        QualType varType = D->getType();
        if (varType->isPointerType() && D->hasInit()) {
            Expr *Init = D->getInit()->IgnoreParenImpCasts();

            // Use helper to infer nullability from expression
            if (auto nonnull = inferNonnullFromExpr(Init)) {
                setField(var, "_nonnull", SymValue::Const(*nonnull));
            } else if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
                // Copying from another pointer - copy its nullability and proofs
                std::string srcPath = DRE->getDecl()->getNameAsString();
                setField(var, "_nonnull", getField(srcPath, "_nonnull"));
                // Copy proofs for consistency with assignment path
                if (Env.find(srcPath + "._proof__nonnull!=0") != Env.end()) {
                    addProof(var, "_nonnull!=0");
                }
                if (Env.find(srcPath + "._proof__nonnull==0") != Env.end()) {
                    addProof(var, "_nonnull==0");
                }
            } else {
                // Unknown source - explicitly mark as unknown
                // This ensures state is well-defined and any stale values are cleared
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
            
            // NEW v15: Unwrap ExprWithCleanups and CXXBindTemporaryExpr to find CallExpr
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
                copyProperties(var, DRE->getDecl()->getNameAsString());
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
                            if (ArgExpr->EvaluateAsInt(R, Ctx)) {
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
            
            // NEW v15: Track integer variable assignments for index arithmetic
            // e.g., int mid = v.size() / 2;
            QualType type = D->getType().getCanonicalType();
            if (type->isIntegerType()) {
                SymValue val = exprToSymValueImpl(Init, Ctx);
                if (val.kind != SymValue::UNKNOWN) {
                    Env[var] = val;
                }
            }
        }
        return true;
    }
    
    bool VisitBinaryOperator(BinaryOperator *B) {
        if (B->getOpcode() != BO_Assign) return true;
        auto *LHS = B->getLHS()->IgnoreParenImpCasts();
        auto *RHS = B->getRHS()->IgnoreParenImpCasts();
        std::string lhsPath = getExprPath(LHS);

        if (!lhsPath.empty() && !hasRuntimeIndex(lhsPath)) {
            // NEW: Track pointer nullability on assignment
            QualType lhsType = B->getLHS()->getType();
            if (lhsType->isPointerType()) {
                // Invalidate old nullability state before setting new value
                invalidateNullability(lhsPath);

                // Use helper to infer nullability from expression
                if (auto nonnull = inferNonnullFromExpr(RHS)) {
                    setField(lhsPath, "_nonnull", SymValue::Const(*nonnull));
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

            // NEW: Handle object assignment v2 = v1 or v2 = std::move(v1)
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
                    if (RHS->EvaluateAsInt(R, Ctx)) {
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
                            if (Rh->EvaluateAsInt(R2, Ctx)) {
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
    
    struct GuardInfo { std::string basePath, field, proofKey, negProofKey; };

    // NEW v13: Variable guard info for "if (i < v.size())"
    struct VarGuardInfo {
        std::string varName;
        std::string objPath;
        std::string field;
        Constraint::Op op;
        Constraint::Op negOp;
    };

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
            auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
            auto *RHS = BO->getRHS()->IgnoreParenImpCasts();

            // LHS should be a variable reference
            std::string varName;
            if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                varName = DRE->getDecl()->getNameAsString();
            }
            if (varName.empty()) return std::nullopt;

            // RHS should be v.size() or v._size
            std::string objPath, field;

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
            }

            if (objPath.empty() || field.empty()) return std::nullopt;

            Constraint::Op op, negOp;
            switch (BO->getOpcode()) {
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

    std::optional<GuardInfo> extractGuard(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();
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
                        if (BO->getRHS()->EvaluateAsInt(R, Ctx)) {
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
            if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
                GuardInfo gi;
                gi.basePath = getExprPath(Mem->getBase());
                gi.field = Mem->getMemberDecl()->getNameAsString();
                if (gi.basePath.empty() || hasRuntimeIndex(gi.basePath)) return std::nullopt;
                Expr::EvalResult R;
                int64_t rhsVal = 0;
                if (BO->getRHS()->EvaluateAsInt(R, Ctx)) {
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

            // NEW: Handle pointer null checks: ptr != nullptr, ptr == nullptr, ptr != 0, ptr == 0
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

            // NEW v14: Handle simple variable guard: n > 0
            if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                GuardInfo gi;
                gi.basePath = DRE->getDecl()->getNameAsString();
                gi.field = "_value";  // Use special field name for the variable itself

                Expr::EvalResult R;
                int64_t rhsVal = 0;
                if (BO->getRHS()->EvaluateAsInt(R, Ctx)) {
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

        // NEW: Handle implicit pointer-to-bool: if (ptr) or if (!ptr)
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
    
    bool TraverseIfStmt(IfStmt *If) {
        // NEW v13: Try variable guard first (i < v.size())
        auto varGuard = extractVarGuard(If->getCond());
        if (varGuard) {
            addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->op);
            TraverseStmt(If->getThen());
            removeVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field);

            if (If->getElse()) {
                addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->negOp);
                TraverseStmt(If->getElse());
                removeVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field);
            }
            return true;
        }

        auto guard = extractGuard(If->getCond());
        if (guard) {
            addProof(guard->basePath, guard->proofKey);
            TraverseStmt(If->getThen());
            // Proof persists after then-block when no else (condition still holds)
            if (If->getElse()) {
                Env.erase(guard->basePath + "._proof_" + guard->proofKey);
                addProof(guard->basePath, guard->negProofKey);
                TraverseStmt(If->getElse());
                Env.erase(guard->basePath + "._proof_" + guard->negProofKey);
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

    // NEW v13: Handle for loops to track loop variable bounds
    bool TraverseForStmt(ForStmt *For) {
        // Pattern: for (int i = 0; i < v.size(); i++)
        // We add proofs: i >= 0 (from init), i < v._size (from cond)

        std::string loopVar;
        std::string objPath;
        std::string boundField;
        bool hasLowerBound = false;
        bool hasUpperBound = false;

        // Extract loop variable from init
        if (auto *Init = For->getInit()) {
            if (auto *DS = dyn_cast<DeclStmt>(Init)) {
                if (DS->isSingleDecl()) {
                    if (auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl())) {
                        loopVar = VD->getNameAsString();
                        if (VD->hasInit()) {
                            Expr::EvalResult R;
                            if (VD->getInit()->EvaluateAsInt(R, Ctx)) {
                                auto val = safeGetInt64(R.Val.getInt());
                                if (val && *val == 0) {
                                    hasLowerBound = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Extract bound from condition
        if (!loopVar.empty()) {
            if (auto *Cond = For->getCond()) {
                if (auto *BO = dyn_cast<BinaryOperator>(Cond->IgnoreParenImpCasts())) {
                    // Check if LHS is the loop variable
                    auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
                    if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                        if (DRE->getDecl()->getNameAsString() == loopVar) {
                            // Check if RHS is a method call (v.size())
                            auto *RHS = BO->getRHS()->IgnoreParenImpCasts();
                            if (auto *MCE = dyn_cast<CXXMemberCallExpr>(RHS)) {
                                auto *Method = MCE->getMethodDecl();
                                if (Method && Method->getParent()) {
                                    std::string className = Method->getParent()->getNameAsString();
                                    std::string methodName = Method->getNameAsString();
                                    std::string key = className + "::" + methodName + "/" + std::to_string(MCE->getNumArgs());

                                    auto it = MethodAnnotations.find(key);
                                    if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
                                        objPath = getExprPath(MCE->getImplicitObjectArgument());
                                        boundField = it->second.returns;  // _size

                                        if (BO->getOpcode() == BO_LT || BO->getOpcode() == BO_LE) {
                                            hasUpperBound = true;
                                        }
                                    }
                                }
                            }
                            // Also handle member access (v.len, v._size)
                            else if (auto *ME = dyn_cast<MemberExpr>(RHS)) {
                                objPath = getExprPath(ME->getBase());
                                boundField = ME->getMemberDecl()->getNameAsString();
                                if (BO->getOpcode() == BO_LT || BO->getOpcode() == BO_LE) {
                                    hasUpperBound = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Add proofs if we found valid bounds
        if (!loopVar.empty() && !objPath.empty() && hasUpperBound) {
            // Add proof: loopVar < objPath._size
            addVarBoundProof(loopVar, objPath, boundField, Constraint::LT);

            // Also add i >= 0 proof as a regular variable tracking
            if (hasLowerBound) {
                Env[loopVar + "._value_ge0"] = SymValue::Const(1);
            }

            // Traverse loop body with proofs active
            if (For->getInit()) TraverseStmt(For->getInit());
            if (For->getCond()) TraverseStmt(For->getCond());
            if (For->getInc()) TraverseStmt(For->getInc());
            TraverseStmt(For->getBody());

            // Remove proofs after loop
            removeVarBoundProof(loopVar, objPath, boundField);
            Env.erase(loopVar + "._value_ge0");

            return true;
        }

        // Default traversal if we couldn't extract bounds
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
                    if (CE->EvaluateAsInt(R, Ctx)) {
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
    
    // NEW: Handle method calls
    bool VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
        auto *Method = E->getMethodDecl();
        if (!Method || !Method->getParent()) return true;

        std::string className = Method->getParent()->getNameAsString();
        std::string methodName = Method->getNameAsString();
        
        // NEW v16: Handle swap method (exchanges properties)
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
            // No annotations - if non-const method, invalidate the object
            if (!Method->isConst()) {
                invalidateProperties(objPath);
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
            if (ArgExpr->EvaluateAsInt(R, Ctx)) {
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
        for (const auto& req : info.requires) {
            // For expression constraints, substitute args and check
            if (req.isExprConstraint) {
                // Build full substitution map
                std::map<std::string, SymValue> subst = argVals;
                // Add field values
                for (auto& kv : Env) {
                    if (kv.first.find("._proof_") != std::string::npos) continue;
                    if (kv.first.rfind(objPath + ".", 0) == 0) {
                        std::string field = kv.first.substr(objPath.size() + 1);
                        // If field already starts with _, use as-is, otherwise add _
                        if (!field.empty() && field[0] == '_') {
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
                    // Can't evaluate - could try Z3 here
                    Diag.Report(E->getBeginLoc(), ErrUnk) << (className + "::" + methodName) << req.str() << objPath << "expr";
                }
                continue;
            }
            
            // Handle arg0 < _size style constraints
            if (req.isArgVsField) {
                auto argIt = argVals.find("arg" + std::to_string(req.argIdx));
                if (argIt == argVals.end()) {
                    Diag.Report(E->getBeginLoc(), ErrUnk) << (className + "::" + methodName) << req.str() << objPath << req.rhsFieldName;
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
                    Diag.Report(E->getBeginLoc(), ErrUnk) << (className + "::" + methodName) << req.str() << objPath << req.rhsFieldName;
                }
                continue;
            }
            
            // Standard constraint checking
            auto result = checkConstraint(objPath, req, true);
            if (!result.has_value()) {
                Diag.Report(E->getBeginLoc(), ErrUnk) << (className + "::" + methodName) << req.str() << objPath << req.field;
            } else if (!*result) {
                auto val = getField(objPath, req.field);
                Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName << req.str() << objPath << req.field << val.str();
            }
        }
        
        // Apply postconditions if present
        // If method has DTT annotations but no ensures, assume user knows what they're doing
        // (they annotated it for preconditions, so it probably doesn't modify the object)
        if (!info.ensures.empty()) {
            auto oldVals = captureOldValuesMethod(objPath);
            applyMethodPostconditions(objPath, info, oldVals, argVals);
        }

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
    
    // NEW v16: Handle operator[] and operator=
    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
        auto Op = E->getOperator();

        // NEW v16: Handle operator= for copy/move assignment
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
        for (const auto& req : info.requires) {
            if (req.isArgVsField) {
                // Get argument as expression
                Expr *ArgExpr = E->getArg(1)->IgnoreParenImpCasts();
                
                // Get this.field value (e.g., _size)
                SymValue fieldVal = getField(objPath, req.rhsFieldName);
                auto fieldEval = fieldVal.eval();
                
                // Try constant evaluation first
                Expr::EvalResult R;
                if (ArgExpr->EvaluateAsInt(R, Ctx)) {
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
                
                // Check for variable bound proofs FIRST (from loops/guards)
                // This handles cases like if (i < v.size()) { v[i] = ... }
                if (auto *DRE = dyn_cast<DeclRefExpr>(ArgExpr)) {
                    std::string varName = DRE->getDecl()->getNameAsString();
                    if (hasVarBoundProof(varName, objPath, req.rhsFieldName, req.op)) {
                        continue;  // Proof exists, skip further checking
                    }
                }
                
                // NEW v15: Try symbolic evaluation for index arithmetic
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

    // NEW: Check if a pointer dereference is safe (has nonnull proof)
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

        return std::nullopt;  // Unknown nullability
    }

    // NEW: Handle pointer dereference via unary operator (*ptr)
    bool VisitUnaryOperator(UnaryOperator *UO) {
        if (UO->getOpcode() != UO_Deref) return true;

        Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
        if (!SubExpr->getType()->isPointerType()) return true;

        std::string ptrPath = getExprPath(SubExpr);
        if (ptrPath.empty()) return true;

        auto nonnullCheck = checkNonnull(ptrPath);
        if (!nonnullCheck.has_value()) {
            // Unknown nullability - report error with suggestion
            Diag.Report(UO->getBeginLoc(), ErrNullDerefUnk) << ptrPath;
            Diag.Report(UO->getBeginLoc(), NoteNullGuard) << ptrPath;
        } else if (!*nonnullCheck) {
            // Known to be null - definite error
            Diag.Report(UO->getBeginLoc(), ErrNullDeref) << ptrPath;
        }
        // If nonnullCheck is true, the dereference is safe

        return true;
    }

    // NEW: Handle pointer dereference via arrow operator (ptr->member)
    bool VisitMemberExpr(MemberExpr *ME) {
        if (!ME->isArrow()) return true;

        Expr *Base = ME->getBase()->IgnoreParenImpCasts();
        if (!Base->getType()->isPointerType()) return true;

        std::string ptrPath = getExprPath(Base);
        if (ptrPath.empty()) return true;

        auto nonnullCheck = checkNonnull(ptrPath);
        if (!nonnullCheck.has_value()) {
            // Unknown nullability - report error with suggestion
            Diag.Report(ME->getBeginLoc(), ErrNullDerefUnk) << ptrPath;
            Diag.Report(ME->getBeginLoc(), NoteNullGuard) << ptrPath;
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
        
        // NEW v16: Handle std::swap(a, b)
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
        
        std::vector<Constraint> requires;
        for (auto *A : Callee->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:requires=", 0) == 0) {
                    auto c = parseConstraint(s.substr(13));
                    if (c) requires.push_back(*c);
                }
            }
        }
        
        std::vector<std::string> argPaths;
        for (unsigned i = 0; i < E->getNumArgs(); i++) {
            auto *Arg = E->getArg(i)->IgnoreParenImpCasts();
            argPaths.push_back(getExprPath(Arg));
        }
        
        for (const auto& req : requires) {
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

            // NEW: Handle nullptr constraints on arguments (arg0 != nullptr)
            if (req.isArgVsField && req.rhsFieldName == "_nullptr_check") {
                if (req.argIdx >= (int)argPaths.size()) continue;
                std::string argPath = argPaths[req.argIdx];
                if (argPath.empty()) continue;

                // Check the _nonnull property of the argument
                auto nonnullCheck = checkNonnull(argPath);
                if (!nonnullCheck.has_value()) {
                    // Unknown nullability
                    Diag.Report(E->getBeginLoc(), ErrNullDerefUnk) << argPath;
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

            if (E->getNumArgs() < 1) continue;
            auto *Arg = E->getArg(0)->IgnoreParenImpCasts();
            std::string argPath = getExprPath(Arg);
            if (argPath.empty() || hasRuntimeIndex(argPath)) continue;

            auto result = checkConstraint(argPath, req, true);
            if (!result.has_value()) {
                Diag.Report(E->getBeginLoc(), ErrUnk) << fn << req.str() << argPath << req.field;
                Diag.Report(E->getBeginLoc(), NoteGuard) << argPath << req.field;
            } else if (!*result) {
                auto val = getField(argPath, req.field);
                Diag.Report(E->getBeginLoc(), ErrFail) << fn << req.str() << argPath << req.field << val.str();
            }
        }

        // NEW: Invalidate properties for non-const ref/pointer arguments
        // Only for VOID functions WITHOUT ANY DTT annotations.
        // If a function has any dtt: annotations (requires, ensures, etc.), assume user knows what it does.
        // Non-void functions typically take references for input efficiency, not modification.
        if (Callee->getReturnType()->isVoidType() && !hasDTTAnnotations(Callee)) {
            for (unsigned i = 0; i < E->getNumArgs() && i < Callee->getNumParams(); i++) {
                ParmVarDecl* Param = Callee->getParamDecl(i);
                QualType paramType = Param->getType();

                // Only invalidate if parameter is non-const ref or pointer
                if (isModifiableRef(paramType)) {
                    std::string argPath = getExprPath(E->getArg(i)->IgnoreParenImpCasts());
                    if (!argPath.empty() && !hasRuntimeIndex(argPath)) {
                        invalidateProperties(argPath);
                    }
                }
            }
        }

        return true;
    }
};

class DTTConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &Ctx) override {
        Env.clear(); FnPostconditions.clear(); FnBoolPostconditions.clear();
        TotalFunctions.clear(); MethodAnnotations.clear(); VarBoundProofs.clear();
        registerSyntheticAnnotations();  // Register std:: type bounds checking
        DTTVisitor(Ctx).TraverseDecl(Ctx.getTranslationUnitDecl());
    }
};

class DTTPlugin : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, StringRef) override {
        return std::make_unique<DTTConsumer>();
    }
    bool ParseArgs(const CompilerInstance&, const std::vector<std::string>&) override { return true; }
    ActionType getActionType() override { return AddAfterMainAction; }
};

}

static FrontendPluginRegistry::Add<DTTPlugin> X("dtt", "DTT v17");
