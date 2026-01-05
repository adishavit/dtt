// DTT Plugin v5 — Struct Field Paths
// Proofs work through nested member access: s.v.len, player.inventory.len

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Attr.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include <map>
#include <string>
#include <optional>
#include <vector>
#include <set>

using namespace clang;

namespace {

// ============================================================
// EXPRESSION PATH HELPER
// ============================================================

// Recursively build path: s.v.len -> "s.v.len"
std::string getExprPath(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    
    if (auto *DRE = dyn_cast<DeclRefExpr>(E))
        return DRE->getDecl()->getNameAsString();
    
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        std::string base = getExprPath(ME->getBase());
        if (base.empty()) return "";
        return base + "." + ME->getMemberDecl()->getNameAsString();
    }
    
    return "";  // Can't track (e.g., array subscript, function call)
}

// Get base object path: s.v.len -> "s.v"
std::string getBasePath(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    
    if (auto *ME = dyn_cast<MemberExpr>(E))
        return getExprPath(ME->getBase());
    
    return "";
}

// Get field name: s.v.len -> "len"
std::string getFieldName(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    
    if (auto *ME = dyn_cast<MemberExpr>(E))
        return ME->getMemberDecl()->getNameAsString();
    
    return "";
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
    
    SymValue substitute(const std::map<std::string, SymValue>& env) const {
        switch (kind) {
            case VAR: { auto it = env.find(varRef); return it != env.end() ? it->second : *this; }
            case ADD: return Add(left->substitute(env), right->substitute(env));
            case SUB: return Sub(left->substitute(env), right->substitute(env));
            default: return *this;
        }
    }
};

// ============================================================
// CONSTRAINTS
// ============================================================

struct Constraint {
    enum Op { GT, GE, LT, LE, EQ, NE, AND, OR } op;
    std::string field;
    int rhs = 0;
    std::shared_ptr<Constraint> left, right;
    
    std::string str() const {
        if (op == AND) return "(" + left->str() + " && " + right->str() + ")";
        if (op == OR) return "(" + left->str() + " || " + right->str() + ")";
        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
        return field + " " + ops[op] + " " + std::to_string(rhs);
    }
};

std::optional<Constraint> parseSimpleConstraint(const std::string& s) {
    Constraint c;
    size_t pos; int opLen = 1;
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

// ============================================================
// POSTCONDITIONS
// ============================================================

struct Postcondition {
    std::string resultField;
    SymValue expr;
};

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
        bool isPlus = (plusPos != std::string::npos && 
                      (minusPos == std::string::npos || plusPos < minusPos));
        size_t opPos = isPlus ? plusPos : minusPos;
        std::string left = rhs.substr(0, opPos);
        std::string right = rhs.substr(opPos + 1);
        trim(left); trim(right);
        
        SymValue leftVal = SymValue::Var(left);
        int rightVal = std::atoi(right.c_str());
        p.expr = isPlus ? SymValue::Add(leftVal, SymValue::Const(rightVal))
                        : SymValue::Sub(leftVal, SymValue::Const(rightVal));
        return p;
    }
    
    if (rhs.find("arg") == 0 || rhs.find('.') != std::string::npos) {
        p.expr = SymValue::Var(rhs);
        return p;
    }
    
    bool isNum = !rhs.empty();
    for (char c : rhs) if (!isdigit(c) && c != '-') isNum = false;
    if (isNum) { p.expr = SymValue::Const(std::atoi(rhs.c_str())); return p; }
    
    return std::nullopt;
}

// ============================================================
// ENVIRONMENT — Keyed by full path (e.g., "s.v.len", "s.v._proof_len>0")
// ============================================================

std::map<std::string, SymValue> Env;
std::map<std::string, std::vector<Postcondition>> FnPostconditions;

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

bool hasProof(const std::string& basePath, const std::string& proofKey) {
    return Env.count(basePath + "._proof_" + proofKey) > 0;
}

void invalidateProofs(const std::string& basePath, const std::string& field) {
    std::vector<std::string> toRemove;
    for (auto& kv : Env) {
        if (kv.first.rfind(basePath + "._proof_" + field, 0) == 0)
            toRemove.push_back(kv.first);
    }
    for (auto& k : toRemove) Env.erase(k);
}

// Copy all properties from one path to another
void copyProperties(const std::string& toPath, const std::string& fromPath) {
    std::vector<std::pair<std::string, SymValue>> toCopy;
    for (auto& kv : Env) {
        if (kv.first.rfind(fromPath + ".", 0) == 0) {
            std::string suffix = kv.first.substr(fromPath.size());
            toCopy.push_back({toPath + suffix, kv.second});
        }
    }
    for (auto& p : toCopy) Env[p.first] = p.second;
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
    
    int pVal = parsed->rhs;
    int cVal = c.rhs;
    
    // Check if proof implies constraint
    if (parsed->op == Constraint::GT) {
        if (c.op == Constraint::GT && pVal >= cVal) return true;
        if (c.op == Constraint::GE && pVal >= cVal) return true;
    }
    if (parsed->op == Constraint::GE) {
        if (c.op == Constraint::GE && pVal >= cVal) return true;
        if (c.op == Constraint::GT && pVal > cVal) return true;
    }
    if (parsed->op == Constraint::LT) {
        if (c.op == Constraint::LT && pVal <= cVal) return true;
        if (c.op == Constraint::LE && pVal <= cVal) return true;
    }
    if (parsed->op == Constraint::LE) {
        if (c.op == Constraint::LE && pVal <= cVal) return true;
        if (c.op == Constraint::LT && pVal < cVal) return true;
    }
    if (parsed->op == Constraint::EQ) {
        if (c.op == Constraint::EQ && pVal == cVal) return true;
        if (c.op == Constraint::GE && pVal >= cVal) return true;
        if (c.op == Constraint::LE && pVal <= cVal) return true;
        if (c.op == Constraint::GT && pVal > cVal) return true;
        if (c.op == Constraint::LT && pVal < cVal) return true;
    }
    
    return proofKey == (c.field + (c.op == Constraint::GT ? ">" : c.op == Constraint::GE ? ">=" :
                        c.op == Constraint::LT ? "<" : c.op == Constraint::LE ? "<=" :
                        c.op == Constraint::EQ ? "==" : "!=") + std::to_string(c.rhs));
}

std::optional<bool> checkConstraint(const std::string& basePath, const Constraint& c) {
    if (c.op == Constraint::AND) {
        auto l = checkConstraint(basePath, *c.left);
        auto r = checkConstraint(basePath, *c.right);
        if (l && r) return *l && *r;
        if ((l && !*l) || (r && !*r)) return false;
        return std::nullopt;
    }
    if (c.op == Constraint::OR) {
        auto l = checkConstraint(basePath, *c.left);
        auto r = checkConstraint(basePath, *c.right);
        if (l && r) return *l || *r;
        if ((l && *l) || (r && *r)) return true;
        return std::nullopt;
    }
    
    // Check proofs attached to this path
    std::string prefix = basePath + "._proof_";
    for (auto& kv : Env) {
        if (kv.first.rfind(prefix, 0) == 0) {
            std::string proofKey = kv.first.substr(prefix.size());
            if (proofImplies(proofKey, c)) return true;
        }
    }
    
    // Check actual value
    auto val = getField(basePath, c.field);
    auto v = val.eval();
    if (!v) return std::nullopt;
    return checkOp(c.op, *v, c.rhs);
}

// ============================================================
// VISITOR
// ============================================================

class DTTVisitor : public RecursiveASTVisitor<DTTVisitor> {
    ASTContext &Ctx;
    DiagnosticsEngine &Diag;
    unsigned ErrFail, ErrUnk, NoteGuard;

public:
    DTTVisitor(ASTContext &C) : Ctx(C), Diag(C.getDiagnostics()) {
        ErrFail = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: %0 requires %1, but %2.%3 = %4");
        ErrUnk = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: %0 requires %1, but %2.%3 is unknown (no proof)");
        NoteGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "add guard: if (%0.%1 > 0) { ... }");
    }

    bool VisitFunctionDecl(FunctionDecl *F) {
        std::string fn = F->getNameAsString();
        for (auto *A : F->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:ensures=", 0) == 0) {
                    auto post = parsePostcondition(s.substr(12));
                    if (post) FnPostconditions[fn].push_back(*post);
                }
            }
        }
        return true;
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
            if (argPath.empty()) continue;
            
            std::string argPrefix = "arg" + std::to_string(i) + ".";
            for (auto& kv : Env) {
                if (kv.first.rfind(argPath + ".", 0) == 0 &&
                    kv.first.find("._proof_") == std::string::npos) {
                    std::string field = kv.first.substr(argPath.size() + 1);
                    subst[argPrefix + field] = kv.second;
                }
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
            
            if (auto *Call = dyn_cast<CallExpr>(Init))
                applyPostconditions(var, Call);
            
            // Copy constructor / assignment from another variable
            if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
                copyProperties(var, DRE->getDecl()->getNameAsString());
            }
            
            if (auto *CE = dyn_cast<CXXConstructExpr>(Init)) {
                for (unsigned i = 0; i < CE->getNumArgs(); i++) {
                    auto *Arg = CE->getArg(i)->IgnoreParenImpCasts();
                    if (auto *Call = dyn_cast<CallExpr>(Arg))
                        applyPostconditions(var, Call);
                    if (auto *DRE = dyn_cast<DeclRefExpr>(Arg))
                        copyProperties(var, DRE->getDecl()->getNameAsString());
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
        
        // path = fn(args) — apply postconditions
        if (!lhsPath.empty()) {
            if (auto *Call = dyn_cast<CallExpr>(RHS))
                applyPostconditions(lhsPath, Call);
            
            // path = otherPath — copy properties
            if (auto *DRE = dyn_cast<DeclRefExpr>(RHS))
                copyProperties(lhsPath, DRE->getDecl()->getNameAsString());
        }
        
        // path.field = value
        if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
            std::string basePath = getExprPath(Mem->getBase());
            std::string field = Mem->getMemberDecl()->getNameAsString();
            
            if (!basePath.empty()) {
                // Invalidate proofs about this field
                invalidateProofs(basePath, field);
                
                Expr::EvalResult R;
                if (RHS->EvaluateAsInt(R, Ctx)) {
                    setField(basePath, field, SymValue::Const(R.Val.getInt().getExtValue()));
                }
                else if (auto *BinOp = dyn_cast<BinaryOperator>(RHS)) {
                    if (BinOp->getOpcode() == BO_Add || BinOp->getOpcode() == BO_Sub) {
                        auto *L = BinOp->getLHS()->IgnoreParenImpCasts();
                        auto *Rh = BinOp->getRHS()->IgnoreParenImpCasts();
                        
                        SymValue lVal = SymValue::Unknown();
                        SymValue rVal = SymValue::Unknown();
                        
                        // Try to get left operand value
                        std::string lPath = getExprPath(L);
                        std::string lField = getFieldName(L);
                        std::string lBase = getBasePath(L);
                        if (!lBase.empty() && !lField.empty())
                            lVal = getField(lBase, lField);
                        
                        // Try to get right operand value
                        Expr::EvalResult R2;
                        if (Rh->EvaluateAsInt(R2, Ctx)) {
                            rVal = SymValue::Const(R2.Val.getInt().getExtValue());
                        } else {
                            std::string rBase = getBasePath(Rh);
                            std::string rField = getFieldName(Rh);
                            if (!rBase.empty() && !rField.empty())
                                rVal = getField(rBase, rField);
                        }
                        
                        if (BinOp->getOpcode() == BO_Add)
                            setField(basePath, field, SymValue::Add(lVal, rVal));
                        else
                            setField(basePath, field, SymValue::Sub(lVal, rVal));
                    }
                }
                else {
                    // Unknown RHS
                    setField(basePath, field, SymValue::Unknown());
                }
            }
        }
        return true;
    }
    
    struct GuardInfo { 
        std::string basePath;  // e.g., "s.v" or "v"
        std::string field;     // e.g., "len"
        std::string proofKey;  // e.g., "len>0"
        std::string negProofKey; 
    };
    
    std::optional<GuardInfo> extractGuard(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
            
            if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
                GuardInfo gi;
                gi.basePath = getExprPath(Mem->getBase());
                gi.field = Mem->getMemberDecl()->getNameAsString();
                
                if (gi.basePath.empty()) return std::nullopt;
                
                Expr::EvalResult R;
                int rhsVal = 0;
                if (BO->getRHS()->EvaluateAsInt(R, Ctx))
                    rhsVal = R.Val.getInt().getExtValue();
                
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
            // Can't extract guard, traverse normally
            TraverseStmt(If->getCond());
            TraverseStmt(If->getThen());
            if (If->getElse()) TraverseStmt(If->getElse());
            return true;
        }
        
        // THEN branch: add proof
        addProof(guard->basePath, guard->proofKey);
        TraverseStmt(If->getThen());
        
        // ELSE branch
        if (If->getElse()) {
            Env.erase(guard->basePath + "._proof_" + guard->proofKey);
            addProof(guard->basePath, guard->negProofKey);
            TraverseStmt(If->getElse());
            Env.erase(guard->basePath + "._proof_" + guard->negProofKey);
        }
        // If no else: proof persists
        
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
        
        for (const auto& req : requires) {
            if (E->getNumArgs() < 1) continue;
            
            auto *Arg = E->getArg(0)->IgnoreParenImpCasts();
            std::string argPath = getExprPath(Arg);
            if (argPath.empty()) continue;
            
            auto result = checkConstraint(argPath, req);
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
        Env.clear();
        FnPostconditions.clear();
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

static FrontendPluginRegistry::Add<DTTPlugin> X("dtt", "DTT v5");
