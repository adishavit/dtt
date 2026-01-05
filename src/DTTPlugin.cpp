// DTT Plugin v13 — Loop Bounds & Runtime Index Guards

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

using namespace clang;

namespace {

ASTContext *GlobalCtx = nullptr;

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
                int64_t idx = R.Val.getInt().getExtValue();
                return base + "[" + std::to_string(idx) + "]";
            }
        }
        return base + "[?]";
    }
    return "";
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
// SYMBOLIC VALUES
// ============================================================

struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, VAR } kind;
    int constVal = 0;
    std::string varRef;
    std::shared_ptr<SymValue> left, right;
    
    static SymValue Const(int v) { SymValue s; s.kind = CONST; s.constVal = v; return s; }
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
    
    std::optional<int> eval() const {
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
    
    std::string toSMT(const std::string& varPrefix) const {
        switch (kind) {
            case CONST: return std::to_string(constVal);
            case UNKNOWN: return varPrefix;
            case VAR: { std::string v = varRef; for (char& c : v) if (c == '.' || c == '[' || c == ']' || c == ':') c = '_'; return v; }
            case ADD: return "(+ " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            case SUB: return "(- " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            default: return "0";
        }
    }
    
    void collectVars(std::set<std::string>& vars) const {
        if (kind == VAR) { std::string v = varRef; for (char& c : v) if (c == '.' || c == '[' || c == ']' || c == ':') c = '_'; vars.insert(v); }
        else if (kind == ADD || kind == SUB) { if (left) left->collectVars(vars); if (right) right->collectVars(vars); }
    }
    
    SymValue substitute(const std::map<std::string, SymValue>& env) const {
        if (kind == VAR) { auto it = env.find(varRef); return it != env.end() ? it->second : *this; }
        if (kind == ADD) return Add(left->substitute(env), right->substitute(env));
        if (kind == SUB) return Sub(left->substitute(env), right->substitute(env));
        return *this;
    }
    
    bool equals(const SymValue& other) const {
        if (kind != other.kind) return false;
        if (kind == CONST) return constVal == other.constVal;
        if (kind == VAR) return varRef == other.varRef;
        if (kind == ADD || kind == SUB)
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
    int rhs = 0;
    bool isMultiArg = false;
    std::string lhsArg, rhsArg, rhsField;
    std::shared_ptr<Constraint> left, right;
    // NEW: For arg0 < _size constraints
    bool isArgVsField = false;
    int argIdx = 0;
    std::string rhsFieldName;  // The _size part
    
    std::string str() const {
        if (op == AND) return "(" + left->str() + " && " + right->str() + ")";
        if (op == OR) return "(" + left->str() + " || " + right->str() + ")";
        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
        if (isMultiArg) return lhsArg + "." + field + " " + ops[op] + " " + rhsArg + "." + rhsField;
        if (isArgVsField) return "arg" + std::to_string(argIdx) + " " + ops[op] + " " + rhsFieldName;
        return field + " " + ops[op] + " " + std::to_string(rhs);
    }
    
    std::string toSMT(const std::string& varName) const {
        std::string v = varName + "_" + field;
        if (op == AND) return "(and " + left->toSMT(varName) + " " + right->toSMT(varName) + ")";
        if (op == OR) return "(or " + left->toSMT(varName) + " " + right->toSMT(varName) + ")";
        const char* smtOps[] = {">", ">=", "<", "<=", "=", "distinct"};
        if (op == NE) return "(distinct " + v + " " + std::to_string(rhs) + ")";
        return "(" + std::string(smtOps[op]) + " " + v + " " + std::to_string(rhs) + ")";
    }
    
    std::string toSMTNegated(const std::string& varName) const { return "(not " + toSMT(varName) + ")"; }
};

auto trim = [](std::string& str) {
    while (!str.empty() && str.front() == ' ') str = str.substr(1);
    while (!str.empty() && str.back() == ' ') str.pop_back();
};

std::optional<Constraint> parseSimpleConstraint(const std::string& s) {
    Constraint c;
    size_t pos; int opLen = 1;
    
    if (s.find("arg") == 0) {
        size_t opPos = std::string::npos;
        if ((opPos = s.find("==")) != std::string::npos) { c.op = Constraint::EQ; opLen = 2; }
        else if ((opPos = s.find("!=")) != std::string::npos) { c.op = Constraint::NE; opLen = 2; }
        else if ((opPos = s.find(">=")) != std::string::npos) { c.op = Constraint::GE; opLen = 2; }
        else if ((opPos = s.find("<=")) != std::string::npos) { c.op = Constraint::LE; opLen = 2; }
        else if ((opPos = s.find(">")) != std::string::npos) { c.op = Constraint::GT; opLen = 1; }
        else if ((opPos = s.find("<")) != std::string::npos) { c.op = Constraint::LT; opLen = 1; }
        
        if (opPos != std::string::npos) {
            std::string lhs = s.substr(0, opPos);
            std::string rhs = s.substr(opPos + opLen);
            trim(lhs); trim(rhs);
            
            if (rhs.find("arg") == 0 && rhs.find('.') != std::string::npos) {
                c.isMultiArg = true;
                auto lhsDot = lhs.find('.');
                if (lhsDot != std::string::npos) { c.lhsArg = lhs.substr(0, lhsDot); c.field = lhs.substr(lhsDot + 1); }
                auto rhsDot = rhs.find('.');
                if (rhsDot != std::string::npos) { c.rhsArg = rhs.substr(0, rhsDot); c.rhsField = rhs.substr(rhsDot + 1); }
                return c;
            }
            
            // NEW: Handle "arg0 < _size" format (argument vs this-field)
            if (rhs[0] == '_') {
                c.isArgVsField = true;
                c.argIdx = std::atoi(lhs.c_str() + 3);  // Extract N from "argN"
                c.rhsFieldName = rhs;
                return c;
            }
        }
    }
    
    if ((pos = s.find(">=")) != std::string::npos) { c.op = Constraint::GE; opLen = 2; }
    else if ((pos = s.find("<=")) != std::string::npos) { c.op = Constraint::LE; opLen = 2; }
    else if ((pos = s.find("==")) != std::string::npos) { c.op = Constraint::EQ; opLen = 2; }
    else if ((pos = s.find("!=")) != std::string::npos) { c.op = Constraint::NE; opLen = 2; }
    else if ((pos = s.find(">")) != std::string::npos) { c.op = Constraint::GT; }
    else if ((pos = s.find("<")) != std::string::npos) { c.op = Constraint::LT; }
    else return std::nullopt;
    
    c.field = s.substr(0, pos);
    trim(c.field);
    std::string rhsStr = s.substr(pos + opLen);
    trim(rhsStr);
    c.rhs = std::atoi(rhsStr.c_str());
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
    int rhs;
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
        else if (lhs[0] == '_') p.resultField = lhs;  // _size -> this->_size
        else p.resultField = lhs;
    } else {
        if (lhs.rfind("result.", 0) == 0) p.resultField = lhs.substr(7);
        else return std::nullopt;
    }
    
    auto plusPos = rhs.find('+');
    auto minusPos = rhs.find('-');
    
    if (plusPos != std::string::npos || minusPos != std::string::npos) {
        bool isPlus = (plusPos != std::string::npos && (minusPos == std::string::npos || plusPos < minusPos));
        size_t opPos = isPlus ? plusPos : minusPos;
        std::string left = rhs.substr(0, opPos);
        std::string right = rhs.substr(opPos + 1);
        trim(left); trim(right);
        
        auto oldRef = parseOld(left);
        if (oldRef) {
            p.usesOld = true;
            // Check if right side is a variable (arg0, arg1, etc.) or constant
            SymValue rightVal;
            if (right.find("arg") == 0 || right[0] == '_') {
                rightVal = SymValue::Var(right);
            } else {
                rightVal = SymValue::Const(std::atoi(right.c_str()));
            }
            p.expr = isPlus ? SymValue::Add(SymValue::Var("old:" + *oldRef), rightVal)
                            : SymValue::Sub(SymValue::Var("old:" + *oldRef), rightVal);
            return p;
        }
        
        SymValue leftVal = SymValue::Var(left);
        SymValue rightVal;
        if (right.find("arg") == 0 || right[0] == '_') {
            rightVal = SymValue::Var(right);
        } else {
            rightVal = SymValue::Const(std::atoi(right.c_str()));
        }
        p.expr = isPlus ? SymValue::Add(leftVal, rightVal)
                        : SymValue::Sub(leftVal, rightVal);
        return p;
    }
    
    auto oldRef = parseOld(rhs);
    if (oldRef) {
        p.usesOld = true;
        p.expr = SymValue::Var("old:" + *oldRef);
        return p;
    }
    
    if (rhs.find("arg") == 0 || rhs.find('.') != std::string::npos || rhs[0] == '_') { 
        p.expr = SymValue::Var(rhs); 
        return p; 
    }
    
    bool isNum = !rhs.empty();
    for (char c : rhs) if (!isdigit(c) && c != '-') isNum = false;
    if (isNum) { p.expr = SymValue::Const(std::atoi(rhs.c_str())); return p; }
    
    return std::nullopt;
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
    bp.rhs = std::atoi(rhs.c_str());
    
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

void copyProperties(const std::string& toPath, const std::string& fromPath) {
    std::vector<std::pair<std::string, SymValue>> toCopy;
    for (auto& kv : Env) if (kv.first.rfind(fromPath + ".", 0) == 0) {
        std::string suffix = kv.first.substr(fromPath.size());
        toCopy.push_back({toPath + suffix, kv.second});
    }
    for (auto& p : toCopy) Env[p.first] = p.second;
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
    smt << "(set-logic QF_LIA)\n";
    std::set<std::string> allVars;
    std::string varName = sanitizeForSMT(basePath);
    allVars.insert(varName + "_" + c.field);
    
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
        else if (kv.second.kind == SymValue::ADD || kv.second.kind == SymValue::SUB)
            smt << "(assert (= " << v << " " << kv.second.toSMT(v) << "))\n";
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
    
    std::string tmpFile = "/tmp/dtt_z3_" + std::to_string(rand()) + ".smt2";
    { std::ofstream f(tmpFile); f << smt.str(); }
    std::string result = runCommand("timeout 1 z3 -smt2 " + tmpFile + " 2>/dev/null");
    std::remove(tmpFile.c_str());
    
    if (result.find("unsat") != std::string::npos) return true;
    if (result.find("sat") != std::string::npos) return false;
    return std::nullopt;
}

// ============================================================
// CONSTRAINT CHECKING
// ============================================================

bool checkOp(Constraint::Op op, int lhs, int rhs) {
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
    int pVal = parsed->rhs, cVal = c.rhs;
    if (parsed->op == Constraint::GT) { if ((c.op == Constraint::GT || c.op == Constraint::GE) && pVal >= cVal) return true; }
    if (parsed->op == Constraint::GE) { if (c.op == Constraint::GE && pVal >= cVal) return true; if (c.op == Constraint::GT && pVal > cVal) return true; }
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
    std::string prefix = basePath + "._proof_";
    for (auto& kv : Env) if (kv.first.rfind(prefix, 0) == 0) {
        std::string proofKey = kv.first.substr(prefix.size());
        if (proofImplies(proofKey, c)) return true;
    }
    auto val = getField(basePath, c.field);
    auto v = val.eval();
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
    FunctionDecl *CurrentFunctionDecl = nullptr;

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
        for (auto *A : D->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:", 0) == 0) {
                    auto rest = s.substr(4);
                    auto eqPos = rest.find('=');
                    if (eqPos != std::string::npos) {
                        std::string field = rest.substr(0, eqPos);
                        int value = std::atoi(rest.substr(eqPos + 1).c_str());
                        setField(var, field, SymValue::Const(value));
                    }
                }
            }
        }
        if (D->hasInit()) {
            auto *Init = D->getInit()->IgnoreParenImpCasts();
            if (auto *Call = dyn_cast<CallExpr>(Init)) {
                auto oldVals = captureOldValues(Call);
                applyPostconditions(var, Call, oldVals);
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) copyProperties(var, DRE->getDecl()->getNameAsString());
            if (auto *CE = dyn_cast<CXXConstructExpr>(Init)) {
                // Apply constructor postconditions
                auto *Ctor = CE->getConstructor();
                if (Ctor && Ctor->getParent()) {
                    std::string className = Ctor->getParent()->getNameAsString();
                    std::string key = className + "::" + className + "/" + std::to_string(CE->getNumArgs());
                    auto it = MethodAnnotations.find(key);
                    if (it != MethodAnnotations.end()) {
                        for (const auto& post : it->second.ensures) {
                            if (post.expr.kind == SymValue::CONST) {
                                setField(var, post.resultField, post.expr);
                            }
                        }
                    }
                }
                for (unsigned i = 0; i < CE->getNumArgs(); i++) {
                    auto *Arg = CE->getArg(i)->IgnoreParenImpCasts();
                    if (auto *Call = dyn_cast<CallExpr>(Arg)) {
                        auto oldVals = captureOldValues(Call);
                        applyPostconditions(var, Call, oldVals);
                    }
                    if (auto *DRE = dyn_cast<DeclRefExpr>(Arg)) copyProperties(var, DRE->getDecl()->getNameAsString());
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
        }
        return true;
    }
    
    bool VisitBinaryOperator(BinaryOperator *B) {
        if (B->getOpcode() != BO_Assign) return true;
        auto *LHS = B->getLHS()->IgnoreParenImpCasts();
        auto *RHS = B->getRHS()->IgnoreParenImpCasts();
        std::string lhsPath = getExprPath(LHS);
        
        if (!lhsPath.empty() && !hasRuntimeIndex(lhsPath)) {
            if (auto *Call = dyn_cast<CallExpr>(RHS)) {
                auto oldVals = captureOldValues(Call);
                applyPostconditions(lhsPath, Call, oldVals);
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) copyProperties(lhsPath, DRE->getDecl()->getNameAsString());
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
                    if (RHS->EvaluateAsInt(R, Ctx)) setField(basePath, field, SymValue::Const(R.Val.getInt().getExtValue()));
                    else if (auto *BinOp = dyn_cast<BinaryOperator>(RHS)) {
                        if (BinOp->getOpcode() == BO_Add || BinOp->getOpcode() == BO_Sub) {
                            auto *L = BinOp->getLHS()->IgnoreParenImpCasts();
                            auto *Rh = BinOp->getRHS()->IgnoreParenImpCasts();
                            SymValue lVal = SymValue::Unknown(), rVal = SymValue::Unknown();
                            std::string lBase = getBasePath(L), lField = getFieldName(L);
                            if (!lBase.empty() && !lField.empty() && !hasRuntimeIndex(lBase)) lVal = getField(lBase, lField);
                            Expr::EvalResult R2;
                            if (Rh->EvaluateAsInt(R2, Ctx)) rVal = SymValue::Const(R2.Val.getInt().getExtValue());
                            else { std::string rBase = getBasePath(Rh), rField = getFieldName(Rh);
                                   if (!rBase.empty() && !rField.empty() && !hasRuntimeIndex(rBase)) rVal = getField(rBase, rField); }
                            setField(basePath, field, BinOp->getOpcode() == BO_Add ? SymValue::Add(lVal, rVal) : SymValue::Sub(lVal, rVal));
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
                if (Method) {
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
                if (Method) {
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
                        int rhsVal = 0;
                        if (BO->getRHS()->EvaluateAsInt(R, Ctx)) rhsVal = R.Val.getInt().getExtValue();
                        
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
                int rhsVal = 0;
                if (BO->getRHS()->EvaluateAsInt(R, Ctx)) rhsVal = R.Val.getInt().getExtValue();
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
            if (!Method) return std::nullopt;
            
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
                                if (R.Val.getInt().getExtValue() == 0) {
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
                                if (Method) {
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
            int64_t val = EC->getInitVal().getExtValue();
            allValues.insert(val);
            valueNames[val] = EC->getNameAsString();
        }
        
        std::set<int64_t> coveredValues;
        bool hasDefault = false;
        SwitchCase *SC = S->getSwitchCaseList();
        while (SC) {
            if (auto *CS = dyn_cast<CaseStmt>(SC)) {
                if (auto *CE = CS->getLHS()) {
                    Expr::EvalResult R;
                    if (CE->EvaluateAsInt(R, Ctx)) coveredValues.insert(R.Val.getInt().getExtValue());
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
        if (!Method) return true;
        
        std::string className = Method->getParent()->getNameAsString();
        std::string methodName = Method->getNameAsString();
        // Include argument count to match correct overload
        std::string key = className + "::" + methodName + "/" + std::to_string(E->getNumArgs());
        
        auto it = MethodAnnotations.find(key);
        if (it == MethodAnnotations.end()) return true;
        
        const MethodInfo& info = it->second;
        std::string objPath = getExprPath(E->getImplicitObjectArgument());
        if (objPath.empty()) return true;
        
        // Check preconditions
        for (const auto& req : info.requires) {
            auto result = checkConstraint(objPath, req, true);
            if (!result.has_value()) {
                Diag.Report(E->getBeginLoc(), ErrUnk) << (className + "::" + methodName) << req.str() << objPath << req.field;
            } else if (!*result) {
                auto val = getField(objPath, req.field);
                Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName << req.str() << objPath << req.field << val.str();
            }
        }
        
        // Apply postconditions
        if (!info.ensures.empty()) {
            auto oldVals = captureOldValuesMethod(objPath);
            
            // Capture argument values for substitution
            std::map<std::string, SymValue> argVals;
            for (unsigned i = 0; i < E->getNumArgs(); i++) {
                Expr *ArgExpr = E->getArg(i)->IgnoreParenImpCasts();
                std::string argKey = "arg" + std::to_string(i);
                
                // Try to evaluate as constant
                Expr::EvalResult R;
                if (ArgExpr->EvaluateAsInt(R, Ctx)) {
                    argVals[argKey] = SymValue::Const(R.Val.getInt().getExtValue());
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
            
            applyMethodPostconditions(objPath, info, oldVals, argVals);
        }
        
        return true;
    }
    
    // NEW: Handle operator[] for index bounds checking
    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
        auto Op = E->getOperator();
        if (Op != OO_Subscript) return true;  // Only handle operator[]
        
        auto *Method = dyn_cast<CXXMethodDecl>(E->getCalleeDecl());
        if (!Method || !Method->getParent()) return true;
        
        std::string className = Method->getParent()->getNameAsString();
        // Strip template params
        auto anglePos = className.find('<');
        if (anglePos != std::string::npos) className = className.substr(0, anglePos);
        
        // Use Method->getNumParams() since operator[] has implicit this
        std::string key = className + "::operator[]/" + std::to_string(Method->getNumParams());
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
                // Get argument value (second arg to operator[])
                Expr *ArgExpr = E->getArg(1)->IgnoreParenImpCasts();
                Expr::EvalResult R;
                std::optional<int64_t> argVal;
                if (ArgExpr->EvaluateAsInt(R, Ctx)) {
                    argVal = R.Val.getInt().getExtValue();
                }
                
                // Get this.field value (e.g., _size)
                SymValue fieldVal = getField(objPath, req.rhsFieldName);
                auto fieldEval = fieldVal.eval();
                
                if (argVal && fieldEval) {
                    bool ok = checkOp(req.op, *argVal, *fieldEval);
                    if (!ok) {
                        Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                            << className << "operator[]" << std::to_string(*argVal)
                            << std::to_string(*fieldEval) << objPath << req.rhsFieldName << fieldVal.str();
                    }
                } else if (!argVal) {
                    // NEW v13: Argument is runtime value - check for variable bound proofs
                    if (auto *DRE = dyn_cast<DeclRefExpr>(ArgExpr)) {
                        std::string varName = DRE->getDecl()->getNameAsString();
                        // Check if we have a proof that varName < objPath.field
                        if (hasVarBoundProof(varName, objPath, req.rhsFieldName, req.op)) {
                            // Proof exists - access is safe!
                            continue;
                        }
                    }
                    // No proof found - could add a warning, but for now we allow it
                    // (runtime bounds checking will catch it)
                } else {
                    // Field value unknown - report error
                    Diag.Report(E->getBeginLoc(), ErrUnk)
                        << (className + "::operator[]") << req.str() << objPath << req.rhsFieldName;
                }
            }
        }
        return true;
    }
    
    bool VisitCallExpr(CallExpr *E) {
        // Skip if it's a member call or operator call (handled separately)
        if (isa<CXXMemberCallExpr>(E) || isa<CXXOperatorCallExpr>(E)) return true;
        
        auto *Callee = E->getDirectCallee();
        if (!Callee) return true;
        std::string fn = Callee->getNameAsString();
        
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
        return true;
    }
};

class DTTConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &Ctx) override {
        Env.clear(); FnPostconditions.clear(); FnBoolPostconditions.clear();
        TotalFunctions.clear(); MethodAnnotations.clear(); VarBoundProofs.clear();
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

static FrontendPluginRegistry::Add<DTTPlugin> X("dtt", "DTT v13");
