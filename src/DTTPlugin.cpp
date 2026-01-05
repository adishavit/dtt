// DTT Plugin — Dependent Type Checking for C++
// Tracks .len fields and enforces preconditions at compile time
// Supports: annotations, assignment tracking, branch refinement

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Attr.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include <map>
#include <string>
#include <optional>
#include <set>

using namespace clang;

namespace {

// ============================================================
// TRACKED VALUES
// ============================================================

struct TrackedVal {
    enum Kind { CONST, UNKNOWN } kind;
    int constVal = 0;
    
    static TrackedVal Const(int v) { 
        TrackedVal t; t.kind = CONST; t.constVal = v; return t; 
    }
    static TrackedVal Unknown() { 
        TrackedVal t; t.kind = UNKNOWN; return t; 
    }
};

// Global environment: "varname" -> TrackedVal
std::map<std::string, TrackedVal> Env;

// Variables currently inside a guard (e.g., inside "if (v.len > 0) { ... }")
std::set<std::string> GuardedVars;

// ============================================================
// CONSTRAINT PARSING
// ============================================================

struct Constraint {
    enum Op { GT, GE, LT, LE, EQ } op;
    int rhs;
    std::string str;
};

std::optional<Constraint> parseConstraint(const std::string& s) {
    Constraint c;
    c.str = s;
    size_t pos;
    int opLen = 1;
    
    if ((pos = s.find(">=")) != std::string::npos) { c.op = Constraint::GE; opLen = 2; }
    else if ((pos = s.find("<=")) != std::string::npos) { c.op = Constraint::LE; opLen = 2; }
    else if ((pos = s.find("==")) != std::string::npos) { c.op = Constraint::EQ; opLen = 2; }
    else if ((pos = s.find(">")) != std::string::npos) { c.op = Constraint::GT; }
    else if ((pos = s.find("<")) != std::string::npos) { c.op = Constraint::LT; }
    else return std::nullopt;
    
    c.rhs = std::atoi(s.c_str() + pos + opLen);
    return c;
}

bool checkConstraint(const Constraint& c, int val) {
    switch (c.op) {
        case Constraint::GT: return val > c.rhs;
        case Constraint::GE: return val >= c.rhs;
        case Constraint::LT: return val < c.rhs;
        case Constraint::LE: return val <= c.rhs;
        case Constraint::EQ: return val == c.rhs;
    }
    return false;
}

bool guardSatisfies(const std::string& var, const Constraint& c) {
    // If var is inside "if (v.len > 0)" and constraint is "len > 0", it's satisfied
    if (GuardedVars.count(var) && c.op == Constraint::GT && c.rhs == 0)
        return true;
    return false;
}

// ============================================================
// AST VISITOR
// ============================================================

class DTTVisitor : public RecursiveASTVisitor<DTTVisitor> {
    ASTContext &Ctx;
    DiagnosticsEngine &Diag;
    unsigned ErrFail, ErrUnk, NoteGuard;

public:
    DTTVisitor(ASTContext &C) : Ctx(C), Diag(C.getDiagnostics()) {
        ErrFail = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: %0 requires %1, but %2.len = %3");
        ErrUnk = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: %0 requires %1, but %2.len is unknown");
        NoteGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "add guard: if (%0.len > 0) { ... } or annotate with [[clang::annotate(\"dtt:len=N\")]]");
    }

    // --- Track variable declarations ---
    bool VisitVarDecl(VarDecl *D) {
        std::string var = D->getNameAsString();
        
        // Check for [[clang::annotate("dtt:len=N")]]
        for (auto *A : D->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:len=", 0) == 0) {
                    Env[var] = TrackedVal::Const(std::atoi(s.c_str() + 8));
                }
            }
        }
        
        // Track initializer_list lengths (for std::vector, etc.)
        if (D->hasInit()) {
            auto *Init = D->getInit()->IgnoreParenImpCasts();
            
            // CXXConstructExpr with std::initializer_list
            if (auto *CE = dyn_cast<CXXConstructExpr>(Init)) {
                for (unsigned i = 0; i < CE->getNumArgs(); i++) {
                    auto *Arg = CE->getArg(i)->IgnoreParenImpCasts();
                    if (auto *SIL = dyn_cast<CXXStdInitializerListExpr>(Arg)) {
                        if (auto *IL = dyn_cast<InitListExpr>(
                                SIL->getSubExpr()->IgnoreParenImpCasts())) {
                            Env[var] = TrackedVal::Const(IL->getNumInits());
                        }
                    }
                }
            }
            
            // Direct InitListExpr
            if (auto *IL = dyn_cast<InitListExpr>(Init)) {
                Env[var] = TrackedVal::Const(IL->getNumInits());
            }
        }
        return true;
    }
    
    // --- Track assignments: v.len = N ---
    bool VisitBinaryOperator(BinaryOperator *B) {
        if (B->getOpcode() != BO_Assign) return true;
        
        auto *LHS = B->getLHS()->IgnoreParenImpCasts();
        auto *RHS = B->getRHS()->IgnoreParenImpCasts();
        
        if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
            if (Mem->getMemberDecl()->getNameAsString() != "len") return true;
            
            if (auto *Base = dyn_cast<DeclRefExpr>(
                    Mem->getBase()->IgnoreParenImpCasts())) {
                std::string var = Base->getDecl()->getNameAsString();
                
                Expr::EvalResult R;
                if (RHS->EvaluateAsInt(R, Ctx)) {
                    Env[var] = TrackedVal::Const(R.Val.getInt().getExtValue());
                }
                // else: runtime value, stays unknown unless guarded
            }
        }
        return true;
    }
    
    // --- Track guards: if (v.len > 0) { ... } ---
    bool VisitIfStmt(IfStmt *If) {
        auto *Cond = If->getCond()->IgnoreParenImpCasts();
        
        // Check for v.len > 0 pattern
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            if (BO->getOpcode() == BO_GT || BO->getOpcode() == BO_GE ||
                BO->getOpcode() == BO_NE) {
                
                auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
                if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
                    if (Mem->getMemberDecl()->getNameAsString() == "len") {
                        if (auto *Base = dyn_cast<DeclRefExpr>(
                                Mem->getBase()->IgnoreParenImpCasts())) {
                            
                            std::string var = Base->getDecl()->getNameAsString();
                            
                            // Mark var as guarded, traverse 'then' branch
                            GuardedVars.insert(var);
                            TraverseStmt(If->getThen());
                            GuardedVars.erase(var);
                            
                            // Traverse 'else' branch without guard
                            if (If->getElse())
                                TraverseStmt(If->getElse());
                            
                            return false;  // Don't re-traverse children
                        }
                    }
                }
            }
        }
        return true;
    }
    
    // --- Check function calls against contracts ---
    bool VisitCallExpr(CallExpr *E) {
        auto *Callee = E->getDirectCallee();
        if (!Callee || E->getNumArgs() < 1) return true;
        
        std::string fn = Callee->getNameAsString();
        
        for (auto *A : Callee->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                if (s.rfind("dtt:requires=", 0) != 0) continue;
                
                auto constraint = parseConstraint(s.substr(13));
                if (!constraint) continue;
                
                // Get first argument name
                auto *Arg = E->getArg(0)->IgnoreParenImpCasts();
                std::string argName;
                if (auto *DRE = dyn_cast<DeclRefExpr>(Arg)) {
                    argName = DRE->getDecl()->getNameAsString();
                }
                if (argName.empty()) continue;
                
                // Check if inside a satisfying guard
                if (guardSatisfies(argName, *constraint))
                    continue;  // OK — guard acts as proof
                
                // Check tracked value
                auto it = Env.find(argName);
                if (it == Env.end() || it->second.kind == TrackedVal::UNKNOWN) {
                    Diag.Report(E->getBeginLoc(), ErrUnk) 
                        << fn << constraint->str << argName;
                    Diag.Report(E->getBeginLoc(), NoteGuard) << argName;
                } else if (it->second.kind == TrackedVal::CONST) {
                    if (!checkConstraint(*constraint, it->second.constVal)) {
                        Diag.Report(E->getBeginLoc(), ErrFail) 
                            << fn << constraint->str << argName << it->second.constVal;
                    }
                }
            }
        }
        return true;
    }
};

// ============================================================
// PLUGIN BOILERPLATE
// ============================================================

class DTTConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &Ctx) override {
        Env.clear();
        GuardedVars.clear();
        DTTVisitor V(Ctx);
        V.TraverseDecl(Ctx.getTranslationUnitDecl());
    }
};

class DTTPlugin : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(
            CompilerInstance &CI, StringRef) override {
        return std::make_unique<DTTConsumer>();
    }
    
    bool ParseArgs(const CompilerInstance &CI,
                   const std::vector<std::string> &Args) override {
        return true;
    }
    
    ActionType getActionType() override {
        return AddAfterMainAction;
    }
};

} // namespace

// Register the plugin
static FrontendPluginRegistry::Add<DTTPlugin>
    X("dtt", "Dependent Type Checking for C++");
