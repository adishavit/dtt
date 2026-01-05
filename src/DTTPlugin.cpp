// DTT Plugin v8 — Constant Array Indices + Z3 Non-Aliasing

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
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <array>
#include <memory>
#include <regex>

using namespace clang;

namespace {

// Forward declaration
class DTTVisitor;
ASTContext *GlobalCtx = nullptr;

// ============================================================
// EXPRESSION PATH HELPER — Now handles array subscripts
// ============================================================

std::string getExprPath(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    
    if (auto *DRE = dyn_cast<DeclRefExpr>(E))
        return DRE->getDecl()->getNameAsString();
    
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        std::string base = getExprPath(ME->getBase());
        if (base.empty()) return "";
        return base + "." + ME->getMemberDecl()->getNameAsString();
    }
    
    // NEW: Array subscript with constant index
    if (auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        std::string base = getExprPath(ASE->getBase());
        if (base.empty()) return "";
        
        // Try to evaluate index as compile-time constant
        if (GlobalCtx) {
            Expr::EvalResult R;
            if (ASE->getIdx()->EvaluateAsInt(R, *GlobalCtx)) {
                int64_t idx = R.Val.getInt().getExtValue();
                return base + "[" + std::to_string(idx) + "]";
            }
        }
        // Runtime index — return special marker
        return base + "[?]";
    }
    
    return "";
}

std::string getBasePath(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    if (auto *ME = dyn_cast<MemberExpr>(E))
        return getExprPath(ME->getBase());
    return "";
}

std::string getFieldName(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    if (auto *ME = dyn_cast<MemberExpr>(E))
        return ME->getMemberDecl()->getNameAsString();
    return "";
}

// Check if path contains runtime index marker
bool hasRuntimeIndex(const std::string& path) {
    return path.find("[?]") != std::string::npos;
}

// Get array base from path: "arr[0].len" -> "arr", "arr[?].len" -> "arr"
std::string getArrayBase(const std::string& path) {
    auto pos = path.find('[');
    if (pos == std::string::npos) return "";
    return path.substr(0, pos);
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
            case VAR: { 
                std::string v = varRef; 
                for (char& c : v) { if (c == '.' || c == '[' || c == ']') c = '_'; }
                return v; 
            }
            case ADD: return "(+ " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            case SUB: return "(- " + left->toSMT(varPrefix) + " " + right->toSMT(varPrefix) + ")";
            default: return "0";
        }
    }
    
    void collectVars(std::set<std::string>& vars) const {
        if (kind == VAR) { 
            std::string v = varRef; 
            for (char& c : v) { if (c == '.' || c == '[' || c == ']') c = '_'; }
            vars.insert(v); 
        }
        else if (kind == ADD || kind == SUB) { 
            if (left) left->collectVars(vars); 
            if (right) right->collectVars(vars); 
        }
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
    
    std::string str() const {
        if (op == AND) return "(" + left->str() + " && " + right->str() + ")";
        if (op == OR) return "(" + left->str() + " || " + right->str() + ")";
        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
        if (isMultiArg) return lhsArg + "." + field + " " + ops[op] + " " + rhsArg + "." + rhsField;
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
            while (!lhs.empty() && lhs.back() == ' ') lhs.pop_back();
            while (!rhs.empty() && rhs.front() == ' ') rhs = rhs.substr(1);
            
            if (rhs.find("arg") == 0 && rhs.find('.') != std::string::npos) {
                c.isMultiArg = true;
                auto lhsDot = lhs.find('.');
                if (lhsDot != std::string::npos) { c.lhsArg = lhs.substr(0, lhsDot); c.field = lhs.substr(lhsDot + 1); }
                auto rhsDot = rhs.find('.');
                if (rhsDot != std::string::npos) { c.rhsArg = rhs.substr(0, rhsDot); c.rhsField = rhs.substr(rhsDot + 1); }
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
    while (!c.field.empty() && c.field.back() == ' ') c.field.pop_back();
    while (!c.field.empty() && c.field.front() == ' ') c.field = c.field.substr(1);
    c.rhs = std::atoi(s.c_str() + pos + opLen);
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

struct Postcondition { std::string resultField; SymValue expr; };

std::optional<Postcondition> parsePostcondition(const std::string& s) {
    auto eqPos = s.find("==");
    if (eqPos == std::string::npos) eqPos = s.find("=");
    if (eqPos == std::string::npos) return std::nullopt;
    
    int skip = (s.size() > eqPos + 1 && s[eqPos + 1] == '=') ? 2 : 1;
    std::string lhs = s.substr(0, eqPos);
    std::string rhs = s.substr(eqPos + skip);
    auto trim = [](std::string& str) {
        while (!str.empty() && str.front() == ' ') str = str.substr(1);
        while (!str.empty() && str.back() == ' ') str.pop_back();
    };
    trim(lhs); trim(rhs);
    
    Postcondition p;
    if (lhs.rfind("result.", 0) == 0) p.resultField = lhs.substr(7);
    else return std::nullopt;
    
    auto plusPos = rhs.find('+');
    auto minusPos = rhs.find('-');
    if (plusPos != std::string::npos || minusPos != std::string::npos) {
        bool isPlus = (plusPos != std::string::npos && (minusPos == std::string::npos || plusPos < minusPos));
        size_t opPos = isPlus ? plusPos : minusPos;
        std::string left = rhs.substr(0, opPos);
        std::string right = rhs.substr(opPos + 1);
        trim(left); trim(right);
        p.expr = isPlus ? SymValue::Add(SymValue::Var(left), SymValue::Const(std::atoi(right.c_str())))
                        : SymValue::Sub(SymValue::Var(left), SymValue::Const(std::atoi(right.c_str())));
        return p;
    }
    if (rhs.find("arg") == 0 || rhs.find('.') != std::string::npos) { p.expr = SymValue::Var(rhs); return p; }
    bool isNum = !rhs.empty();
    for (char c : rhs) if (!isdigit(c) && c != '-') isNum = false;
    if (isNum) { p.expr = SymValue::Const(std::atoi(rhs.c_str())); return p; }
    return std::nullopt;
}

// ============================================================
// ENVIRONMENT
// ============================================================

std::map<std::string, SymValue> Env;
std::map<std::string, std::vector<Postcondition>> FnPostconditions;
std::set<std::string> TotalFunctions;

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

// NEW: Invalidate all array elements when writing to runtime index
void invalidateArrayElements(const std::string& arrayBase, const std::string& field) {
    std::vector<std::string> toRemove;
    std::string pattern = arrayBase + "[";
    for (auto& kv : Env) {
        if (kv.first.rfind(pattern, 0) == 0) {
            // Check if this is the field we're invalidating or a proof about it
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
    for (char& c : result) {
        if (c == '.' || c == '[' || c == ']') c = '_';
    }
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
    unsigned ErrFail, ErrUnk, NoteGuard, ErrMultiArg, ErrNotTotal, NoteCase;
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
                if (s == "dtt:total") TotalFunctions.insert(fn);
            }
        }
        
        bool result = RecursiveASTVisitor::TraverseFunctionDecl(F);
        CurrentFunctionDecl = nullptr;
        return result;
    }

    void applyPostconditions(const std::string& resultPath, CallExpr *Call) {
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
        for (const auto& post : it->second) {
            SymValue result = post.expr.substitute(subst);
            setField(resultPath, post.resultField, result);
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
            if (auto *Call = dyn_cast<CallExpr>(Init)) applyPostconditions(var, Call);
            if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) copyProperties(var, DRE->getDecl()->getNameAsString());
            if (auto *CE = dyn_cast<CXXConstructExpr>(Init)) {
                for (unsigned i = 0; i < CE->getNumArgs(); i++) {
                    auto *Arg = CE->getArg(i)->IgnoreParenImpCasts();
                    if (auto *Call = dyn_cast<CallExpr>(Arg)) applyPostconditions(var, Call);
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
            if (auto *Call = dyn_cast<CallExpr>(RHS)) applyPostconditions(lhsPath, Call);
            if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) copyProperties(lhsPath, DRE->getDecl()->getNameAsString());
        }
        
        if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
            std::string basePath = getExprPath(Mem->getBase());
            std::string field = Mem->getMemberDecl()->getNameAsString();
            
            if (!basePath.empty()) {
                // Check for runtime index — invalidate all array elements
                if (hasRuntimeIndex(basePath)) {
                    std::string arrayBase = getArrayBase(basePath);
                    if (!arrayBase.empty()) {
                        invalidateArrayElements(arrayBase, field);
                    }
                } else {
                    // Constant index or no index — normal handling
                    invalidateProofs(basePath, field);
                    
                    Expr::EvalResult R;
                    if (RHS->EvaluateAsInt(R, Ctx)) {
                        setField(basePath, field, SymValue::Const(R.Val.getInt().getExtValue()));
                    }
                    else if (auto *BinOp = dyn_cast<BinaryOperator>(RHS)) {
                        if (BinOp->getOpcode() == BO_Add || BinOp->getOpcode() == BO_Sub) {
                            auto *L = BinOp->getLHS()->IgnoreParenImpCasts();
                            auto *Rh = BinOp->getRHS()->IgnoreParenImpCasts();
                            SymValue lVal = SymValue::Unknown(), rVal = SymValue::Unknown();
                            std::string lBase = getBasePath(L), lField = getFieldName(L);
                            if (!lBase.empty() && !lField.empty() && !hasRuntimeIndex(lBase)) 
                                lVal = getField(lBase, lField);
                            Expr::EvalResult R2;
                            if (Rh->EvaluateAsInt(R2, Ctx)) rVal = SymValue::Const(R2.Val.getInt().getExtValue());
                            else { 
                                std::string rBase = getBasePath(Rh), rField = getFieldName(Rh);
                                if (!rBase.empty() && !rField.empty() && !hasRuntimeIndex(rBase)) 
                                    rVal = getField(rBase, rField); 
                            }
                            setField(basePath, field, BinOp->getOpcode() == BO_Add ? SymValue::Add(lVal, rVal) : SymValue::Sub(lVal, rVal));
                        }
                    }
                    else {
                        setField(basePath, field, SymValue::Unknown());
                    }
                }
            }
        }
        return true;
    }
    
    struct GuardInfo { std::string basePath, field, proofKey, negProofKey; };
    
    std::optional<GuardInfo> extractGuard(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
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
    
    bool TraverseIfStmt(IfStmt *If) {
        auto guard = extractGuard(If->getCond());
        if (!guard) {
            TraverseStmt(If->getCond());
            TraverseStmt(If->getThen());
            if (If->getElse()) TraverseStmt(If->getElse());
            return true;
        }
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
    
    bool VisitCallExpr(CallExpr *E) {
        auto *Callee = E->getDirectCallee();
        if (!Callee) return true;
        std::string fn = Callee->getNameAsString();
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
        Env.clear(); FnPostconditions.clear(); TotalFunctions.clear();
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

static FrontendPluginRegistry::Add<DTTPlugin> X("dtt", "DTT v8");
