// DTT Plugin v2 — Generalized Property Tracking
// Supports: any field, arbitrary constraints, postconditions

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
#include <vector>
#include <sstream>

using namespace clang;

namespace {

// ============================================================
// SYMBOLIC VALUES & EXPRESSIONS
// ============================================================

struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL } kind;
    int constVal = 0;
    std::shared_ptr<SymValue> left, right;
    
    static SymValue Const(int v) { SymValue s; s.kind = CONST; s.constVal = v; return s; }
    static SymValue Unknown() { SymValue s; s.kind = UNKNOWN; return s; }
    static SymValue Add(SymValue a, SymValue b) {
        // Simplify if both const
        if (a.kind == CONST && b.kind == CONST)
            return Const(a.constVal + b.constVal);
        SymValue s; s.kind = ADD;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }
    static SymValue Sub(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST)
            return Const(a.constVal - b.constVal);
        SymValue s; s.kind = SUB;
        s.left = std::make_shared<SymValue>(a);
        s.right = std::make_shared<SymValue>(b);
        return s;
    }
    
    std::optional<int> eval() const {
        switch (kind) {
            case CONST: return constVal;
            case UNKNOWN: return std::nullopt;
            case ADD: {
                auto l = left->eval(), r = right->eval();
                if (l && r) return *l + *r;
                return std::nullopt;
            }
            case SUB: {
                auto l = left->eval(), r = right->eval();
                if (l && r) return *l - *r;
                return std::nullopt;
            }
            default: return std::nullopt;
        }
    }
    
    std::string str() const {
        switch (kind) {
            case CONST: return std::to_string(constVal);
            case UNKNOWN: return "?";
            case ADD: return "(" + left->str() + "+" + right->str() + ")";
            case SUB: return "(" + left->str() + "-" + right->str() + ")";
            default: return "?";
        }
    }
};

// ============================================================
// CONSTRAINT SYSTEM
// ============================================================

struct Constraint {
    enum Op { GT, GE, LT, LE, EQ, NE, AND, OR } op;
    std::string field;  // e.g., "len", "size", "capacity"
    int rhs = 0;
    std::shared_ptr<Constraint> left, right;  // for AND/OR
    
    std::string str() const {
        if (op == AND) return "(" + left->str() + " && " + right->str() + ")";
        if (op == OR) return "(" + left->str() + " || " + right->str() + ")";
        const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
        return field + " " + ops[op] + " " + std::to_string(rhs);
    }
};

// Parse: "len > 0", "size >= 5", "len > 0 && size < 10"
std::optional<Constraint> parseConstraint(const std::string& s);

// Forward declaration
std::optional<Constraint> parseSimpleConstraint(const std::string& s) {
    Constraint c;
    size_t pos;
    int opLen = 1;
    
    if ((pos = s.find(">=")) != std::string::npos) { c.op = Constraint::GE; opLen = 2; }
    else if ((pos = s.find("<=")) != std::string::npos) { c.op = Constraint::LE; opLen = 2; }
    else if ((pos = s.find("==")) != std::string::npos) { c.op = Constraint::EQ; opLen = 2; }
    else if ((pos = s.find("!=")) != std::string::npos) { c.op = Constraint::NE; opLen = 2; }
    else if ((pos = s.find(">")) != std::string::npos) { c.op = Constraint::GT; }
    else if ((pos = s.find("<")) != std::string::npos) { c.op = Constraint::LT; }
    else return std::nullopt;
    
    c.field = s.substr(0, pos);
    // Trim whitespace
    while (!c.field.empty() && (c.field.back() == ' ' || c.field.back() == '\t'))
        c.field.pop_back();
    while (!c.field.empty() && (c.field.front() == ' ' || c.field.front() == '\t'))
        c.field = c.field.substr(1);
    
    c.rhs = std::atoi(s.c_str() + pos + opLen);
    return c;
}

std::optional<Constraint> parseConstraint(const std::string& s) {
    // Check for && (AND)
    size_t andPos = s.find("&&");
    if (andPos != std::string::npos) {
        auto left = parseConstraint(s.substr(0, andPos));
        auto right = parseConstraint(s.substr(andPos + 2));
        if (left && right) {
            Constraint c;
            c.op = Constraint::AND;
            c.left = std::make_shared<Constraint>(*left);
            c.right = std::make_shared<Constraint>(*right);
            return c;
        }
    }
    
    // Check for || (OR)
    size_t orPos = s.find("||");
    if (orPos != std::string::npos) {
        auto left = parseConstraint(s.substr(0, orPos));
        auto right = parseConstraint(s.substr(orPos + 2));
        if (left && right) {
            Constraint c;
            c.op = Constraint::OR;
            c.left = std::make_shared<Constraint>(*left);
            c.right = std::make_shared<Constraint>(*right);
            return c;
        }
    }
    
    return parseSimpleConstraint(s);
}

// ============================================================
// ENVIRONMENT
// ============================================================

// Maps "varname.fieldname" -> SymValue
std::map<std::string, SymValue> Env;

// Guards: maps "varname.fieldname" -> set of constraints known true
std::map<std::string, std::set<std::string>> Guards;

void setField(const std::string& var, const std::string& field, SymValue v) {
    Env[var + "." + field] = v;
}

SymValue getField(const std::string& var, const std::string& field) {
    auto it = Env.find(var + "." + field);
    return (it != Env.end()) ? it->second : SymValue::Unknown();
}

void addGuard(const std::string& var, const std::string& field, const std::string& constraint) {
    Guards[var + "." + field].insert(constraint);
}

bool hasGuard(const std::string& var, const std::string& field, const std::string& pattern) {
    auto it = Guards.find(var + "." + field);
    if (it == Guards.end()) return false;
    return it->second.count(pattern) > 0;
}

void clearGuard(const std::string& var, const std::string& field) {
    Guards.erase(var + "." + field);
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

// Returns: true (satisfied), false (violated), nullopt (unknown)
std::optional<bool> checkConstraint(const std::string& var, const Constraint& c) {
    if (c.op == Constraint::AND) {
        auto l = checkConstraint(var, *c.left);
        auto r = checkConstraint(var, *c.right);
        if (l && r) return *l && *r;
        if ((l && !*l) || (r && !*r)) return false;
        return std::nullopt;
    }
    if (c.op == Constraint::OR) {
        auto l = checkConstraint(var, *c.left);
        auto r = checkConstraint(var, *c.right);
        if (l && r) return *l || *r;
        if ((l && *l) || (r && *r)) return true;
        return std::nullopt;
    }
    
    // Check guards first
    std::string guardKey = c.field + (c.op == Constraint::GT ? ">" : 
                                      c.op == Constraint::GE ? ">=" :
                                      c.op == Constraint::LT ? "<" :
                                      c.op == Constraint::LE ? "<=" :
                                      c.op == Constraint::EQ ? "==" : "!=") 
                          + std::to_string(c.rhs);
    if (hasGuard(var, c.field, guardKey)) return true;
    
    // Special case: > 0 is satisfied by any > N guard
    if (c.op == Constraint::GT && c.rhs == 0) {
        for (int i = 0; i <= 100; i++) {
            if (hasGuard(var, c.field, c.field + ">" + std::to_string(i))) return true;
            if (hasGuard(var, c.field, c.field + ">=" + std::to_string(i+1))) return true;
        }
    }
    
    // Check actual value
    auto val = getField(var, c.field);
    auto v = val.eval();
    if (!v) return std::nullopt;
    return checkOp(c.op, *v, c.rhs);
}

// ============================================================
// POSTCONDITION TRACKING
// ============================================================

struct Postcondition {
    std::string resultField;  // e.g., "len"
    enum Kind { CONST_VAL, ARG_FIELD, ARG_ADD } kind;
    int constVal = 0;
    int argIndex = 0;
    std::string argField;
    int addend = 0;  // for ARG_ADD
};

std::map<std::string, std::vector<Postcondition>> FnPostconditions;

// Parse: "len = 0", "len = a.len", "len = a.len + b.len"
std::optional<Postcondition> parsePostcondition(const std::string& s) {
    Postcondition p;
    
    auto eqPos = s.find('=');
    if (eqPos == std::string::npos) return std::nullopt;
    
    p.resultField = s.substr(0, eqPos);
    while (!p.resultField.empty() && p.resultField.back() == ' ') p.resultField.pop_back();
    
    std::string rhs = s.substr(eqPos + 1);
    while (!rhs.empty() && rhs.front() == ' ') rhs = rhs.substr(1);
    
    // Try constant
    bool isNum = !rhs.empty();
    for (char c : rhs) if (!isdigit(c) && c != '-') isNum = false;
    if (isNum) {
        p.kind = Postcondition::CONST_VAL;
        p.constVal = std::atoi(rhs.c_str());
        return p;
    }
    
    // Try a.field + b.field pattern (simplified: just detect "+" )
    auto plusPos = rhs.find('+');
    if (plusPos != std::string::npos) {
        p.kind = Postcondition::ARG_ADD;
        // For now, mark as special — full implementation would parse both sides
        return p;
    }
    
    // Try a.field pattern
    auto dotPos = rhs.find('.');
    if (dotPos != std::string::npos) {
        p.kind = Postcondition::ARG_FIELD;
        // Extract arg name and field
        std::string argName = rhs.substr(0, dotPos);
        p.argField = rhs.substr(dotPos + 1);
        // argIndex would need to be resolved from param names
        return p;
    }
    
    return std::nullopt;
}

// ============================================================
// AST VISITOR
// ============================================================

class DTTVisitor : public RecursiveASTVisitor<DTTVisitor> {
    ASTContext &Ctx;
    DiagnosticsEngine &Diag;
    unsigned ErrFail, ErrUnk, NoteGuard, NotePostcond;

public:
    DTTVisitor(ASTContext &C) : Ctx(C), Diag(C.getDiagnostics()) {
        ErrFail = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: %0 requires %1, but %2.%3 = %4");
        ErrUnk = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: %0 requires %1, but %2.%3 is unknown");
        NoteGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "add guard: if (%0.%1 > 0) { ... }");
        NotePostcond = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "postcondition: %0 = %1");
    }

    // --- Track variable declarations ---
    bool VisitVarDecl(VarDecl *D) {
        std::string var = D->getNameAsString();
        
        for (auto *A : D->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                // [[clang::annotate("dtt:field=value")]]
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
        
        // Track initializer_list
        if (D->hasInit()) {
            auto *Init = D->getInit()->IgnoreParenImpCasts();
            if (auto *CE = dyn_cast<CXXConstructExpr>(Init)) {
                for (unsigned i = 0; i < CE->getNumArgs(); i++) {
                    auto *Arg = CE->getArg(i)->IgnoreParenImpCasts();
                    if (auto *SIL = dyn_cast<CXXStdInitializerListExpr>(Arg)) {
                        if (auto *IL = dyn_cast<InitListExpr>(
                                SIL->getSubExpr()->IgnoreParenImpCasts())) {
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
    
    // --- Track assignments: v.field = N ---
    bool VisitBinaryOperator(BinaryOperator *B) {
        if (B->getOpcode() != BO_Assign) return true;
        
        auto *LHS = B->getLHS()->IgnoreParenImpCasts();
        auto *RHS = B->getRHS()->IgnoreParenImpCasts();
        
        if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
            std::string field = Mem->getMemberDecl()->getNameAsString();
            
            if (auto *Base = dyn_cast<DeclRefExpr>(
                    Mem->getBase()->IgnoreParenImpCasts())) {
                std::string var = Base->getDecl()->getNameAsString();
                
                Expr::EvalResult R;
                if (RHS->EvaluateAsInt(R, Ctx)) {
                    setField(var, field, SymValue::Const(R.Val.getInt().getExtValue()));
                }
                // Check for v.len = a.len + b.len pattern
                else if (auto *BinOp = dyn_cast<BinaryOperator>(RHS)) {
                    if (BinOp->getOpcode() == BO_Add) {
                        // Try to get both operands
                        auto *L = BinOp->getLHS()->IgnoreParenImpCasts();
                        auto *R = BinOp->getRHS()->IgnoreParenImpCasts();
                        
                        SymValue lVal = SymValue::Unknown();
                        SymValue rVal = SymValue::Unknown();
                        
                        if (auto *LMem = dyn_cast<MemberExpr>(L)) {
                            if (auto *LBase = dyn_cast<DeclRefExpr>(LMem->getBase()->IgnoreParenImpCasts())) {
                                lVal = getField(LBase->getDecl()->getNameAsString(),
                                               LMem->getMemberDecl()->getNameAsString());
                            }
                        }
                        if (auto *RMem = dyn_cast<MemberExpr>(R)) {
                            if (auto *RBase = dyn_cast<DeclRefExpr>(RMem->getBase()->IgnoreParenImpCasts())) {
                                rVal = getField(RBase->getDecl()->getNameAsString(),
                                               RMem->getMemberDecl()->getNameAsString());
                            }
                        }
                        
                        setField(var, field, SymValue::Add(lVal, rVal));
                    }
                }
            }
        }
        return true;
    }
    
    // --- Track guards: if (v.field > N) { ... } ---
    bool VisitIfStmt(IfStmt *If) {
        auto *Cond = If->getCond()->IgnoreParenImpCasts();
        
        std::string var, field;
        std::string guardStr;
        
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
            
            if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
                field = Mem->getMemberDecl()->getNameAsString();
                if (auto *Base = dyn_cast<DeclRefExpr>(
                        Mem->getBase()->IgnoreParenImpCasts())) {
                    var = Base->getDecl()->getNameAsString();
                    
                    // Get RHS value
                    Expr::EvalResult R;
                    int rhsVal = 0;
                    if (BO->getRHS()->EvaluateAsInt(R, Ctx)) {
                        rhsVal = R.Val.getInt().getExtValue();
                    }
                    
                    // Build guard string
                    const char* opStr = "";
                    switch (BO->getOpcode()) {
                        case BO_GT: opStr = ">"; break;
                        case BO_GE: opStr = ">="; break;
                        case BO_LT: opStr = "<"; break;
                        case BO_LE: opStr = "<="; break;
                        case BO_EQ: opStr = "=="; break;
                        case BO_NE: opStr = "!="; break;
                        default: return true;
                    }
                    guardStr = field + opStr + std::to_string(rhsVal);
                    
                    // Add guard for then branch
                    addGuard(var, field, guardStr);
                    TraverseStmt(If->getThen());
                    clearGuard(var, field);
                    
                    if (If->getElse())
                        TraverseStmt(If->getElse());
                    
                    return false;
                }
            }
        }
        return true;
    }
    
    // --- Check function calls ---
    bool VisitCallExpr(CallExpr *E) {
        auto *Callee = E->getDirectCallee();
        if (!Callee) return true;
        
        std::string fn = Callee->getNameAsString();
        
        // Collect postconditions and requirements
        std::vector<Constraint> requires;
        std::vector<std::pair<std::string, std::string>> ensures;  // field, expr
        
        for (auto *A : Callee->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                auto s = Ann->getAnnotation().str();
                
                if (s.rfind("dtt:requires=", 0) == 0) {
                    auto c = parseConstraint(s.substr(13));
                    if (c) requires.push_back(*c);
                }
                
                if (s.rfind("dtt:ensures=", 0) == 0) {
                    auto rest = s.substr(12);
                    auto eqPos = rest.find('=');
                    if (eqPos != std::string::npos) {
                        ensures.push_back({rest.substr(0, eqPos), rest.substr(eqPos + 1)});
                    }
                }
            }
        }
        
        // Check requirements
        for (const auto& req : requires) {
            if (E->getNumArgs() < 1) continue;
            
            auto *Arg = E->getArg(0)->IgnoreParenImpCasts();
            std::string argName;
            if (auto *DRE = dyn_cast<DeclRefExpr>(Arg))
                argName = DRE->getDecl()->getNameAsString();
            if (argName.empty()) continue;
            
            auto result = checkConstraint(argName, req);
            if (!result.has_value()) {
                Diag.Report(E->getBeginLoc(), ErrUnk) 
                    << fn << req.str() << argName << req.field;
                Diag.Report(E->getBeginLoc(), NoteGuard) << argName << req.field;
            } else if (!*result) {
                auto val = getField(argName, req.field);
                Diag.Report(E->getBeginLoc(), ErrFail) 
                    << fn << req.str() << argName << req.field << val.str();
            }
        }
        
        // Apply postconditions to result (if assigned)
        // This is tracked via parent analysis — simplified for now
        
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
        Guards.clear();
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
    bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &) override {
        return true;
    }
    ActionType getActionType() override { return AddAfterMainAction; }
};

}

static FrontendPluginRegistry::Add<DTTPlugin> X("dtt", "DTT v2");
