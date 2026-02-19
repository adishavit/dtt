// DTTAnnotationFile.cpp - .dtt annotation file parser and built-in loader
//
// Line-oriented parser for the .dtt declarative annotation format.
// Also provides registerBuiltinAnnotations() which auto-loads the built-in
// .dtt files (std.dtt, libc.dtt, win32.dtt) from the annotations directory.

#include "DTTAnnotationFile.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#ifndef _WIN32
#include <dlfcn.h>
#endif

#ifndef DTT_ANNOTATIONS_DIR
#define DTT_ANNOTATIONS_DIR ""
#endif

namespace dtt {

// ============================================================
// EXPRESSION PARSER (produces ConstraintExprPtr and SymValue)
// ============================================================

namespace {

// Trim whitespace from both ends
llvm::StringRef trim(llvm::StringRef s) {
    return s.trim();
}

// Skip whitespace, return rest
llvm::StringRef skipWS(llvm::StringRef s) {
    return s.ltrim();
}

// Intern a string into a single global pool.
// Returned pointer is stable for the process lifetime (leaked intentionally —
// annotation loading is once-per-TU so the total allocation is bounded).
const char *internString(llvm::StringRef s) {
    static llvm::StringMap<const char*> Pool;
    auto it = Pool.find(s);
    if (it != Pool.end()) return it->second;
    char *dup = static_cast<char*>(malloc(s.size() + 1));
    memcpy(dup, s.data(), s.size());
    dup[s.size()] = '\0';
    return Pool.insert({s, dup}).first->second;
}

// Global enum value table for resolving EnumName.MEMBER to integer values.
// Populated by `enum` blocks in .dtt files. Process-lifetime (like internString).
llvm::StringMap<int64_t> EnumValues;

// Check if character is identifier-valid
bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Parse an integer from the front of s, advance s past it
bool parseInt(llvm::StringRef &s, int64_t &out) {
    s = skipWS(s);
    if (s.empty()) return false;
    bool neg = false;
    if (s.front() == '-') {
        neg = true;
        s = s.drop_front();
    }
    if (s.empty() || !std::isdigit(static_cast<unsigned char>(s.front())))
        return false;
    out = 0;
    while (!s.empty() && std::isdigit(static_cast<unsigned char>(s.front()))) {
        out = out * 10 + (s.front() - '0');
        s = s.drop_front();
    }
    if (neg) out = -out;
    return true;
}

// Parse a value token: integer literal or enum reference.
// Advances s past the consumed token. Returns true on success.
bool parseValueToken(llvm::StringRef &s, int64_t &out) {
    s = skipWS(s);
    // Try integer literal first
    size_t numEnd = 0;
    if (!s.empty() && s.front() == '-') numEnd++;
    while (numEnd < s.size() &&
           std::isdigit(static_cast<unsigned char>(s[numEnd])))
        numEnd++;
    if (numEnd > 0 && !(numEnd == 1 && s.front() == '-')) {
        if (!s.substr(0, numEnd).getAsInteger(10, out)) {
            s = s.drop_front(numEnd);
            return true;
        }
    }
    // Try enum reference (e.g., EnumName.MEMBER)
    llvm::StringRef saved = s;
    size_t i = 0;
    while (i < s.size() && (isIdentChar(s[i]) || s[i] == '@' || s[i] == '.'))
        ++i;
    if (i > 0) {
        llvm::StringRef ident = s.substr(0, i);
        auto it = EnumValues.find(ident);
        if (it != EnumValues.end()) {
            out = it->second;
            s = s.drop_front(i);
            return true;
        }
    }
    s = saved;
    return false;
}

// Parse an identifier (letters, digits, underscores, @, .)
// Stops at whitespace, operators, parens
llvm::StringRef parseIdent(llvm::StringRef &s) {
    s = skipWS(s);
    size_t i = 0;
    while (i < s.size() && (isIdentChar(s[i]) || s[i] == '@' || s[i] == '.'))
        ++i;
    llvm::StringRef result = s.substr(0, i);
    s = s.drop_front(i);
    return result;
}

// Forward declaration for recursive descent (atom needs to call top-level for parens)
ConstraintExprPtr parseConstraintOr(llvm::StringRef &s);

// Parse a constraint expression atom: NUMBER | argN | argN._field | _field | (expr)
ConstraintExprPtr parseConstraintAtom(llvm::StringRef &s) {
    s = skipWS(s);
    if (s.empty()) return nullptr;

    // Parenthesized sub-expression
    if (s.front() == '(') {
        s = s.drop_front();
        auto inner = parseConstraintOr(s);
        if (!inner) return nullptr;
        s = skipWS(s);
        if (!s.empty() && s.front() == ')')
            s = s.drop_front();
        return inner;
    }

    // Number
    if (std::isdigit(static_cast<unsigned char>(s.front())) ||
        (s.front() == '-' && s.size() > 1 &&
         std::isdigit(static_cast<unsigned char>(s[1])))) {
        int64_t val;
        if (parseInt(s, val))
            return std::make_shared<LiteralExpr>(val);
        return nullptr;
    }

    // Identifier: argN, argN._field, _field, old(...)
    llvm::StringRef ident = parseIdent(s);
    if (ident.empty()) return nullptr;

    if (ident.starts_with("arg")) {
        llvm::StringRef rest = ident.drop_front(3);
        // Check for argN._field
        size_t dotPos = rest.find('.');
        if (dotPos != llvm::StringRef::npos) {
            llvm::StringRef idxStr = rest.substr(0, dotPos);
            unsigned idx;
            if (!idxStr.getAsInteger(10, idx)) {
                return std::make_shared<ArgFieldRefExpr>(idx, rest.substr(dotPos + 1));
            }
        }
        // Just argN
        unsigned idx;
        if (!rest.getAsInteger(10, idx))
            return std::make_shared<ArgRefExpr>(idx);
    }

    // Enum reference: EnumName.MEMBER → integer literal
    auto enumIt = EnumValues.find(ident);
    if (enumIt != EnumValues.end())
        return std::make_shared<LiteralExpr>(enumIt->second);

    // Field reference: _size, _nonnull, _has_value, etc.
    return std::make_shared<FieldRefExpr>(ident.str());
}

// Parse comparison operator
bool parseCmpOp(llvm::StringRef &s, ConstraintExpr::Kind &op) {
    s = skipWS(s);
    if (s.starts_with("==")) { op = ConstraintExpr::CmpEQ; s = s.drop_front(2); return true; }
    if (s.starts_with("!=")) { op = ConstraintExpr::CmpNE; s = s.drop_front(2); return true; }
    if (s.starts_with(">=")) { op = ConstraintExpr::CmpGE; s = s.drop_front(2); return true; }
    if (s.starts_with("<=")) { op = ConstraintExpr::CmpLE; s = s.drop_front(2); return true; }
    if (s.starts_with(">"))  { op = ConstraintExpr::CmpGT; s = s.drop_front(1); return true; }
    if (s.starts_with("<"))  { op = ConstraintExpr::CmpLT; s = s.drop_front(1); return true; }
    return false;
}

// Parse comparison: atom OP atom
ConstraintExprPtr parseComparison(llvm::StringRef &s) {
    auto lhs = parseConstraintAtom(s);
    if (!lhs) return nullptr;

    s = skipWS(s);
    ConstraintExpr::Kind op;
    if (!parseCmpOp(s, op)) return lhs;  // No operator: return atom as-is

    auto rhs = parseConstraintAtom(s);
    if (!rhs) return nullptr;

    return std::make_shared<BinaryExpr>(op, lhs, rhs);
}

// Parse logical AND: comparison (&& comparison)*
ConstraintExprPtr parseLogicalAnd(llvm::StringRef &s) {
    auto left = parseComparison(s);
    if (!left) return nullptr;
    while (true) {
        s = skipWS(s);
        if (!s.starts_with("&&")) break;
        s = s.drop_front(2);
        auto right = parseComparison(s);
        if (!right) return nullptr;
        left = std::make_shared<BinaryExpr>(ConstraintExpr::LogicalAnd, left, right);
    }
    return left;
}

// Parse logical OR: logicalAnd (|| logicalAnd)*
ConstraintExprPtr parseConstraintOr(llvm::StringRef &s) {
    auto left = parseLogicalAnd(s);
    if (!left) return nullptr;
    while (true) {
        s = skipWS(s);
        if (!s.starts_with("||")) break;
        s = s.drop_front(2);
        auto right = parseLogicalAnd(s);
        if (!right) return nullptr;
        left = std::make_shared<BinaryExpr>(ConstraintExpr::LogicalOr, left, right);
    }
    return left;
}

// Parse a full constraint expression with logical operators
// Precedence: atom < comparison < && < ||
ConstraintExprPtr parseConstraintExpr(llvm::StringRef s) {
    s = trim(s);
    return parseConstraintOr(s);
}

// ============================================================
// SYMVALUE EXPRESSION PARSER (for ensures RHS)
// ============================================================

// Parse a SymValue expression: NUMBER | argN | old(FIELD) | EXPR +/- EXPR
SymValue parseSymAtom(llvm::StringRef &s, bool &usesOld) {
    s = skipWS(s);
    if (s.empty()) return SymValue::Unknown();

    // old(FIELD)
    if (s.starts_with("old(")) {
        s = s.drop_front(4);  // "old("
        size_t close = s.find(')');
        if (close == llvm::StringRef::npos) return SymValue::Unknown();
        llvm::StringRef field = s.substr(0, close);
        s = s.drop_front(close + 1);
        usesOld = true;
        return SymValue::Var(("old:" + field).str());
    }

    // Number
    if (std::isdigit(static_cast<unsigned char>(s.front())) ||
        (s.front() == '-' && s.size() > 1 &&
         std::isdigit(static_cast<unsigned char>(s[1])))) {
        int64_t val;
        if (parseInt(s, val))
            return SymValue::Const(val);
        return SymValue::Unknown();
    }

    // Identifier: argN, argN._field, _field, or EnumName.MEMBER
    llvm::StringRef ident = parseIdent(s);
    if (ident.empty()) return SymValue::Unknown();

    // Enum reference: EnumName.MEMBER → constant
    auto enumIt = EnumValues.find(ident);
    if (enumIt != EnumValues.end())
        return SymValue::Const(enumIt->second);

    return SymValue::Var(ident.str());
}

SymValue parseSymExpr(llvm::StringRef s, bool &usesOld) {
    s = trim(s);
    auto lhs = parseSymAtom(s, usesOld);

    s = skipWS(s);
    if (s.empty()) return lhs;

    if (s.starts_with("+")) {
        s = s.drop_front(1);
        auto rhs = parseSymAtom(s, usesOld);
        return SymValue::Add(lhs, rhs);
    }
    if (s.starts_with("-")) {
        s = s.drop_front(1);
        auto rhs = parseSymAtom(s, usesOld);
        return SymValue::Sub(lhs, rhs);
    }

    return lhs;
}

// ============================================================
// PROOF OPERATOR PARSER (for free function requires/scope_exit)
// ============================================================

bool parseProofOp(llvm::StringRef &s, ProofOp &op) {
    s = skipWS(s);
    if (s.starts_with("==")) { op = ProofOp::EQ; s = s.drop_front(2); return true; }
    if (s.starts_with("!=")) { op = ProofOp::NE; s = s.drop_front(2); return true; }
    if (s.starts_with(">=")) { op = ProofOp::GE; s = s.drop_front(2); return true; }
    if (s.starts_with("<=")) { op = ProofOp::LE; s = s.drop_front(2); return true; }
    if (s.starts_with(">"))  { op = ProofOp::GT; s = s.drop_front(1); return true; }
    if (s.starts_with("<"))  { op = ProofOp::LT; s = s.drop_front(1); return true; }
    return false;
}

// ============================================================
// BOOL POSTCONDITION PARSER
// ============================================================

// Parse: _field OP VALUE (e.g., _size == 0, _has_value >= 1, _state == State.OPEN)
bool parseBoolPostExpr(llvm::StringRef s, std::string &field, ProofOp &op, int64_t &rhs) {
    s = trim(s);
    llvm::StringRef f = parseIdent(s);
    if (f.empty()) return false;
    field = f.str();

    if (!parseProofOp(s, op)) return false;

    return parseValueToken(s, rhs);
}

// ============================================================
// ARITY PARSING
// ============================================================

// Parse arity: single int or range "N..M"
// Returns list of arities
bool parseArity(llvm::StringRef s, llvm::SmallVectorImpl<unsigned> &arities) {
    s = trim(s);
    size_t dotdot = s.find("..");
    if (dotdot != llvm::StringRef::npos) {
        unsigned lo, hi;
        if (s.substr(0, dotdot).getAsInteger(10, lo)) return false;
        if (s.substr(dotdot + 2).getAsInteger(10, hi)) return false;
        for (unsigned i = lo; i <= hi; ++i)
            arities.push_back(i);
        return true;
    }
    unsigned val;
    if (s.getAsInteger(10, val)) return false;
    arities.push_back(val);
    return true;
}

// ============================================================
// FREE FUNCTION REQUIRES PARSER
// ============================================================

// Parse: argN.FIELD OP VALUE ["error message"]  or  argN OP VALUE ["error message"]
bool parseFnRequires(llvm::StringRef s, FreeFunctionPrecondition &pre) {
    s = trim(s);

    // Parse argN[.FIELD]
    if (!s.starts_with("arg")) return false;
    s = s.drop_front(3);

    // Find where the arg index ends: either at a dot (argN.FIELD) or whitespace/operator
    size_t dotPos = s.find('.');
    size_t spacePos = s.find_first_of(" \t<>=!");
    bool hasField = (dotPos != llvm::StringRef::npos &&
                     (spacePos == llvm::StringRef::npos || dotPos < spacePos));

    if (hasField) {
        llvm::StringRef idxStr = s.substr(0, dotPos);
        unsigned argIdx;
        if (idxStr.getAsInteger(10, argIdx)) return false;
        pre.argIndex = argIdx;
        pre.isRawArg = false;

        s = s.drop_front(dotPos + 1);

        // Parse field name
        llvm::StringRef field = parseIdent(s);
        if (field.empty()) return false;
        pre.field = internString(field);
    } else {
        // Raw argument value: argN OP VALUE (no field)
        size_t idxEnd = 0;
        while (idxEnd < s.size() &&
               std::isdigit(static_cast<unsigned char>(s[idxEnd])))
            ++idxEnd;
        if (idxEnd == 0) return false;
        unsigned argIdx;
        if (s.substr(0, idxEnd).getAsInteger(10, argIdx)) return false;
        pre.argIndex = argIdx;
        pre.isRawArg = true;
        pre.field = "";
        s = s.drop_front(idxEnd);
    }

    // Parse operator
    ProofOp op;
    if (!parseProofOp(s, op)) return false;
    pre.checkOp = op;

    // Parse value (integer literal or enum reference)
    int64_t val;
    if (!parseValueToken(s, val)) return false;
    pre.forbiddenValue = val;

    // Parse optional error message in quotes
    s = skipWS(s);
    if (!s.empty() && s.front() == '"') {
        s = s.drop_front(1);
        size_t endQuote = s.find('"');
        if (endQuote != llvm::StringRef::npos) {
            llvm::StringRef msg = s.substr(0, endQuote);
            pre.errorReason = internString(msg);
        }
    } else {
        pre.errorReason = "";
    }

    return true;
}

// ============================================================
// FREE FUNCTION ENSURES PARSER
// ============================================================

// Parse: argN.FIELD = VALUE  or  result.FIELD = VALUE
// Returns: target (0=arg, 1=result), argIndex, field (interned), value
struct FnEnsuresResult {
    bool isResult = false;
    int argIndex = 0;
    const char* field = "";
    int64_t value = 0;
};

bool parseFnEnsures(llvm::StringRef s, FnEnsuresResult &out) {
    s = trim(s);

    if (s.starts_with("result.")) {
        out.isResult = true;
        s = s.drop_front(7);
    } else if (s.starts_with("arg")) {
        out.isResult = false;
        s = s.drop_front(3);
        size_t dotPos = s.find('.');
        if (dotPos == llvm::StringRef::npos) return false;
        unsigned idx;
        if (s.substr(0, dotPos).getAsInteger(10, idx)) return false;
        out.argIndex = idx;
        s = s.drop_front(dotPos + 1);
    } else {
        return false;
    }

    // Parse field name (up to = or whitespace)
    size_t eqPos = s.find('=');
    if (eqPos == llvm::StringRef::npos) return false;
    llvm::StringRef field = s.substr(0, eqPos).trim();
    out.field = internString(field);

    s = s.drop_front(eqPos + 1);
    // Skip second '=' if '=='
    if (!s.empty() && s.front() == '=') s = s.drop_front(1);

    int64_t val;
    if (!parseValueToken(s, val)) return false;
    out.value = val;
    return true;
}

// ============================================================
// FREE FUNCTION SCOPE_EXIT PARSER
// ============================================================

// Parse: result.FIELD OP VALUE  or  argN.FIELD OP VALUE
bool parseFnScopeExit(llvm::StringRef s, FreeFunctionScopeExitReq &req) {
    s = trim(s);
    if (s.starts_with("result.")) {
        req.isResult = true;
        s = s.drop_front(7);
    } else if (s.starts_with("arg")) {
        req.isResult = false;
        s = s.drop_front(3);
        size_t dotPos = s.find('.');
        if (dotPos == llvm::StringRef::npos) return false;
        unsigned idx;
        if (s.substr(0, dotPos).getAsInteger(10, idx)) return false;
        req.argIndex = idx;
        s = s.drop_front(dotPos + 1);
    } else {
        return false;
    }

    llvm::StringRef field = parseIdent(s);
    if (field.empty()) return false;
    req.field = internString(field);

    ProofOp op;
    if (!parseProofOp(s, op)) return false;
    req.op = op;

    int64_t val;
    if (!parseValueToken(s, val)) return false;
    req.requiredValue = val;
    return true;
}

// ============================================================
// MAIN LINE-ORIENTED PARSER
// ============================================================

enum class BlockType { None, Trait, Type, Fn, Template };

struct TraitDef {
    std::string name;
    // Stored as raw method annotation lines grouped by method signature
    struct MethodEntry {
        std::string methodName;
        llvm::SmallVector<unsigned, 4> arities;
        llvm::SmallVector<std::string, 4> lines;  // requires/ensures/returns lines
    };
    llvm::SmallVector<MethodEntry, 16> methods;
};

// Apply a trait's methods to a class, populating MethodAnnotations
void applyTrait(const TraitDef &trait, llvm::StringRef className,
                llvm::DenseMap<MethodKey, MethodInfo> &methodAnnotations) {
    for (const auto &method : trait.methods) {
        std::string resolvedName = method.methodName;
        // @ctor expands to the class name
        if (resolvedName == "@ctor") {
            resolvedName = className.str();
        }

        for (unsigned arity : method.arities) {
            MethodKey key{className.str(), resolvedName, arity};

            // Don't overwrite explicit definitions
            if (methodAnnotations.count(key)) continue;

            MethodInfo info;
            for (const auto &line : method.lines) {
                llvm::StringRef l(line);
                l = trim(l);

                if (l.starts_with("requires ")) {
                    auto expr = parseConstraintExpr(l.drop_front(9));
                    if (expr) info.preconditions.push_back(expr);
                } else if (l.starts_with("ensures_if_true ")) {
                    std::string field;
                    ProofOp op;
                    int64_t rhs;
                    if (parseBoolPostExpr(l.drop_front(16), field, op, rhs))
                        info.boolPostconds.push_back({"", field, op, rhs, true});
                } else if (l.starts_with("ensures_if_false ")) {
                    std::string field;
                    ProofOp op;
                    int64_t rhs;
                    if (parseBoolPostExpr(l.drop_front(17), field, op, rhs))
                        info.boolPostconds.push_back({"", field, op, rhs, false});
                } else if (l.starts_with("ensures ")) {
                    llvm::StringRef rest = l.drop_front(8);
                    // Parse: FIELD = EXPR
                    size_t eqPos = rest.find('=');
                    if (eqPos != llvm::StringRef::npos) {
                        llvm::StringRef field = rest.substr(0, eqPos).trim();
                        llvm::StringRef rhs = rest.substr(eqPos + 1).trim();
                        // Skip second = if ==
                        if (!rhs.empty() && rhs.front() == '=')
                            rhs = rhs.drop_front(1).ltrim();

                        Postcondition post;
                        post.field = field.str();
                        bool usesOld = false;
                        post.expr = parseSymExpr(rhs, usesOld);
                        post.usesOld = usesOld;
                        info.ensures.push_back(post);
                    }
                } else if (l.starts_with("returns ")) {
                    info.returns = l.drop_front(8).trim().str();
                }
            }
            methodAnnotations[key] = info;
        }
    }
}

struct Parser {
    llvm::StringRef content;
    llvm::DenseMap<MethodKey, MethodInfo> &methodAnnotations;
    llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> &freeFuncAnnotations;
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>> &templateAliases;
    llvm::StringMap<TraitDef> traits;
    unsigned lineNum = 0;

    void error(const llvm::Twine &msg) {
        llvm::errs() << "DTT annotation parse error (line " << lineNum << "): "
                     << msg << "\n";
    }

    bool parse() {
        llvm::SmallVector<llvm::StringRef, 512> lines;
        content.split(lines, '\n');

        size_t i = 0;
        while (i < lines.size()) {
            lineNum = i + 1;
            llvm::StringRef line = lines[i].trim();

            // Skip blank lines and comments
            if (line.empty() || line.starts_with("#")) {
                ++i;
                continue;
            }

            if (line.starts_with("enum ")) {
                i = parseEnum(lines, i);
            } else if (line.starts_with("trait ")) {
                i = parseTrait(lines, i);
            } else if (line.starts_with("type ")) {
                i = parseType(lines, i);
            } else if (line.starts_with("fn ")) {
                i = parseFn(lines, i);
            } else if (line.starts_with("template ")) {
                i = parseTemplate(lines, i);
            } else {
                error("unexpected line: " + line);
                ++i;
            }
        }
        return true;
    }

    // Find closing brace, return index of line after '}'
    size_t findClosingBrace(const llvm::SmallVectorImpl<llvm::StringRef> &lines,
                            size_t start) {
        for (size_t i = start; i < lines.size(); ++i) {
            if (lines[i].trim() == "}") return i + 1;
        }
        return lines.size();
    }

    // Parse method entries within a block (trait or type)
    // Returns the parsed methods and the line index after '}'
    size_t parseMethodBlock(const llvm::SmallVectorImpl<llvm::StringRef> &lines,
                            size_t start,
                            llvm::SmallVectorImpl<TraitDef::MethodEntry> &methods) {
        size_t i = start;
        // Index into methods; SIZE_MAX = no current method yet.
        // Use index (not pointer) to survive SmallVector reallocation.
        size_t currentIdx = SIZE_MAX;

        while (i < lines.size()) {
            llvm::StringRef line = lines[i].trim();
            if (line == "}") return i + 1;
            if (line.empty() || line.starts_with("#")) {
                ++i;
                continue;
            }

            // Check if this is a method header: name(arity):
            size_t parenOpen = line.find('(');
            size_t parenClose = line.find(')');
            bool isMethodHeader = parenOpen != llvm::StringRef::npos &&
                                  parenClose != llvm::StringRef::npos &&
                                  parenClose > parenOpen &&
                                  line.back() == ':';

            if (isMethodHeader) {
                methods.push_back({});
                currentIdx = methods.size() - 1;
                methods[currentIdx].methodName = line.substr(0, parenOpen).str();
                llvm::StringRef arityStr = line.substr(parenOpen + 1,
                                                        parenClose - parenOpen - 1);
                parseArity(arityStr, methods[currentIdx].arities);
            } else if (currentIdx != SIZE_MAX) {
                // Annotation line: requires/ensures/returns/etc.
                methods[currentIdx].lines.push_back(line.str());
            } else {
                lineNum = i + 1;
                error("annotation line outside method block: " + line);
            }
            ++i;
        }
        return i;
    }

    size_t parseEnum(const llvm::SmallVectorImpl<llvm::StringRef> &lines,
                     size_t start) {
        llvm::StringRef line = lines[start].trim();
        // "enum NAME {"
        llvm::StringRef rest = line.drop_front(5);  // "enum "
        size_t bracePos = rest.find('{');
        if (bracePos == llvm::StringRef::npos) {
            error("expected '{' in enum declaration");
            return start + 1;
        }
        llvm::StringRef enumName = rest.substr(0, bracePos).trim();

        size_t i = start + 1;
        while (i < lines.size()) {
            llvm::StringRef bodyLine = lines[i].trim();
            if (bodyLine == "}") { ++i; break; }
            if (bodyLine.empty() || bodyLine.starts_with("#")) { ++i; continue; }

            // Parse: MEMBER = VALUE
            size_t eqPos = bodyLine.find('=');
            if (eqPos == llvm::StringRef::npos) {
                lineNum = i + 1;
                error("expected MEMBER = VALUE in enum");
                ++i;
                continue;
            }
            llvm::StringRef member = bodyLine.substr(0, eqPos).trim();
            llvm::StringRef valStr = bodyLine.substr(eqPos + 1).trim();
            int64_t val;
            if (valStr.getAsInteger(10, val)) {
                lineNum = i + 1;
                error("invalid integer value in enum");
                ++i;
                continue;
            }

            // Store as "EnumName.MEMBER" → val
            llvm::SmallString<64> key;
            key += enumName;
            key += ".";
            key += member;
            EnumValues[key] = val;
            ++i;
        }
        return i;
    }

    size_t parseTrait(const llvm::SmallVectorImpl<llvm::StringRef> &lines,
                      size_t start) {
        llvm::StringRef line = lines[start].trim();
        // "trait NAME {"
        llvm::StringRef rest = line.drop_front(6);  // "trait "
        size_t bracePos = rest.find('{');
        if (bracePos == llvm::StringRef::npos) {
            error("expected '{' in trait declaration");
            return start + 1;
        }
        llvm::StringRef name = rest.substr(0, bracePos).trim();

        TraitDef trait;
        trait.name = name.str();
        size_t nextLine = parseMethodBlock(lines, start + 1, trait.methods);
        traits[name] = std::move(trait);
        return nextLine;
    }

    size_t parseType(const llvm::SmallVectorImpl<llvm::StringRef> &lines,
                     size_t start) {
        llvm::StringRef line = lines[start].trim();
        // "type NAME [: Trait1, Trait2] {"
        llvm::StringRef rest = line.drop_front(5);  // "type "
        size_t bracePos = rest.find('{');
        if (bracePos == llvm::StringRef::npos) {
            error("expected '{' in type declaration");
            return start + 1;
        }
        llvm::StringRef header = rest.substr(0, bracePos).trim();

        // Parse class name and optional trait list
        std::string className;
        llvm::SmallVector<llvm::StringRef, 4> traitNames;
        size_t colonPos = header.find(':');
        if (colonPos != llvm::StringRef::npos) {
            className = header.substr(0, colonPos).trim().str();
            llvm::StringRef traitList = header.substr(colonPos + 1).trim();
            llvm::SmallVector<llvm::StringRef, 4> parts;
            traitList.split(parts, ',');
            for (auto &t : parts) {
                llvm::StringRef trimmed = t.trim();
                if (!trimmed.empty()) traitNames.push_back(trimmed);
            }
        } else {
            className = header.str();
        }

        // Parse explicit method entries
        llvm::SmallVector<TraitDef::MethodEntry, 16> methods;
        size_t nextLine = parseMethodBlock(lines, start + 1, methods);

        // First, add explicit methods (so they take priority)
        for (const auto &method : methods) {
            std::string resolvedName = method.methodName;
            if (resolvedName == "@ctor") resolvedName = className;

            for (unsigned arity : method.arities) {
                MethodKey key{className, resolvedName, arity};
                MethodInfo info;

                for (const auto &annLine : method.lines) {
                    llvm::StringRef l(annLine);
                    l = trim(l);
                    parseAnnotationLine(l, info);
                }
                methodAnnotations[key] = info;
            }
        }

        // Then apply traits (won't overwrite explicit definitions)
        for (auto traitName : traitNames) {
            auto it = traits.find(traitName);
            if (it != traits.end()) {
                applyTrait(it->second, className, methodAnnotations);
            } else {
                lineNum = start + 1;
                error("unknown trait: " + traitName);
            }
        }

        return nextLine;
    }

    // Parse a single annotation line into MethodInfo
    void parseAnnotationLine(llvm::StringRef l, MethodInfo &info) {
        if (l.starts_with("requires ")) {
            auto expr = parseConstraintExpr(l.drop_front(9));
            if (expr) info.preconditions.push_back(expr);
        } else if (l.starts_with("ensures_if_true ")) {
            std::string field;
            ProofOp op;
            int64_t rhs;
            if (parseBoolPostExpr(l.drop_front(16), field, op, rhs))
                info.boolPostconds.push_back({"", field, op, rhs, true});
        } else if (l.starts_with("ensures_if_false ")) {
            std::string field;
            ProofOp op;
            int64_t rhs;
            if (parseBoolPostExpr(l.drop_front(17), field, op, rhs))
                info.boolPostconds.push_back({"", field, op, rhs, false});
        } else if (l.starts_with("ensures ")) {
            llvm::StringRef rest = l.drop_front(8);
            size_t eqPos = rest.find('=');
            if (eqPos != llvm::StringRef::npos) {
                llvm::StringRef field = rest.substr(0, eqPos).trim();
                llvm::StringRef rhs = rest.substr(eqPos + 1).trim();
                if (!rhs.empty() && rhs.front() == '=')
                    rhs = rhs.drop_front(1).ltrim();

                Postcondition post;
                post.field = field.str();
                bool usesOld = false;
                post.expr = parseSymExpr(rhs, usesOld);
                post.usesOld = usesOld;
                info.ensures.push_back(post);
            }
        } else if (l.starts_with("returns ")) {
            info.returns = l.drop_front(8).trim().str();
        } else {
            llvm::errs() << "DTT: unknown annotation directive: " << l << "\n";
        }
    }

    size_t parseFn(const llvm::SmallVectorImpl<llvm::StringRef> &lines,
                   size_t start) {
        llvm::StringRef line = lines[start].trim();
        // "fn NAME(ARITY) {" or "fn NAME(ARITY..ARITY) {"
        llvm::StringRef rest = line.drop_front(3);  // "fn "
        size_t parenOpen = rest.find('(');
        size_t parenClose = rest.find(')');
        if (parenOpen == llvm::StringRef::npos || parenClose == llvm::StringRef::npos) {
            error("expected fn NAME(ARITY) {");
            return findClosingBrace(lines, start + 1);
        }

        std::string fnName = rest.substr(0, parenOpen).trim().str();
        llvm::StringRef arityStr = rest.substr(parenOpen + 1,
                                                parenClose - parenOpen - 1);
        llvm::SmallVector<unsigned, 4> arities;
        if (!parseArity(arityStr, arities)) {
            error("invalid arity in fn declaration");
            return findClosingBrace(lines, start + 1);
        }

        const char *internedName = internString(fnName);

        // Parse body lines until '}'
        FreeFunctionInfo info;
        size_t i = start + 1;
        while (i < lines.size()) {
            llvm::StringRef bodyLine = lines[i].trim();
            if (bodyLine == "}") { ++i; break; }
            if (bodyLine.empty() || bodyLine.starts_with("#")) { ++i; continue; }

            if (bodyLine.starts_with("requires ")) {
                FreeFunctionPrecondition pre;
                if (parseFnRequires(bodyLine.drop_front(9), pre))
                    info.preconditions.push_back(pre);
            } else if (bodyLine.starts_with("ensures ")) {
                FnEnsuresResult ens;
                if (parseFnEnsures(bodyLine.drop_front(8), ens)) {
                    if (ens.isResult) {
                        info.resultPostconditions.push_back({ens.field, ens.value});
                    } else {
                        info.postconditions.push_back({ens.argIndex, ens.field, ens.value});
                    }
                }
            } else if (bodyLine.starts_with("scope_exit ")) {
                FreeFunctionScopeExitReq req;
                if (parseFnScopeExit(bodyLine.drop_front(11), req))
                    info.scopeExitRequires.push_back(req);
            } else {
                llvm::errs() << "DTT: unknown fn directive: " << bodyLine << "\n";
            }
            ++i;
        }

        // Register for all arities
        for (unsigned arity : arities) {
            freeFuncAnnotations[{internedName, arity}] = info;
        }
        return i;
    }

    size_t parseTemplate(const llvm::SmallVectorImpl<llvm::StringRef> &lines,
                         size_t start) {
        llvm::StringRef line = lines[start].trim();
        // "template QUALIFIED_NAME {"
        llvm::StringRef rest = line.drop_front(9);  // "template "
        size_t bracePos = rest.find('{');
        if (bracePos == llvm::StringRef::npos) {
            error("expected '{' in template declaration");
            return start + 1;
        }
        llvm::StringRef qualName = rest.substr(0, bracePos).trim();

        llvm::SmallVector<TemplateParamAlias, 2> aliases;
        size_t i = start + 1;
        while (i < lines.size()) {
            llvm::StringRef bodyLine = lines[i].trim();
            if (bodyLine == "}") { ++i; break; }
            if (bodyLine.empty() || bodyLine.starts_with("#")) { ++i; continue; }

            // "param INDEX => PROPERTY [skip VALUE]"
            if (bodyLine.starts_with("param ")) {
                llvm::StringRef paramRest = bodyLine.drop_front(6);
                // Parse index
                size_t arrowPos = paramRest.find("=>");
                if (arrowPos == llvm::StringRef::npos) {
                    lineNum = i + 1;
                    error("expected '=>' in param declaration");
                    ++i;
                    continue;
                }
                llvm::StringRef idxStr = paramRest.substr(0, arrowPos).trim();
                llvm::StringRef propRest = paramRest.substr(arrowPos + 2).trim();

                int paramIdx;
                if (idxStr.getAsInteger(10, paramIdx)) {
                    lineNum = i + 1;
                    error("invalid param index");
                    ++i;
                    continue;
                }

                // Check for "skip VALUE"
                std::optional<int64_t> skipVal;
                size_t skipPos = propRest.find("skip");
                std::string propName;
                if (skipPos != llvm::StringRef::npos) {
                    propName = propRest.substr(0, skipPos).trim().str();
                    llvm::StringRef skipStr = propRest.substr(skipPos + 4).trim();
                    int64_t sv;
                    if (!skipStr.getAsInteger(10, sv))
                        skipVal = sv;
                } else {
                    propName = propRest.str();
                }

                aliases.push_back({paramIdx, propName, skipVal});
            }
            ++i;
        }

        templateAliases[qualName] = std::move(aliases);
        return i;
    }
};

} // anonymous namespace

// ============================================================
// GLOBAL ANNOTATION MAPS
// ============================================================

llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;
llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;
llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>> TemplateAliases;

// ============================================================
// PUBLIC API
// ============================================================

void registerBuiltinAnnotations() {
    // Try to resolve the annotations directory relative to the plugin .so,
    // so the plugin works even if installed to a different prefix.
    // Falls back to the compile-time DTT_ANNOTATIONS_DIR.
    llvm::SmallString<256> resolvedDir;
#ifndef _WIN32
    Dl_info dlInfo;
    if (dladdr(reinterpret_cast<void*>(registerBuiltinAnnotations), &dlInfo) &&
        dlInfo.dli_fname) {
        resolvedDir = dlInfo.dli_fname;
        llvm::sys::path::remove_filename(resolvedDir);
        llvm::sys::path::append(resolvedDir, "..", "annotations");
        // Verify the directory exists by checking if std.dtt is loadable
        llvm::SmallString<256> probe(resolvedDir);
        llvm::sys::path::append(probe, "std.dtt");
        if (!llvm::sys::fs::exists(probe))
            resolvedDir.clear();  // fall back to compile-time path
    }
#endif
    if (resolvedDir.empty())
        resolvedDir = DTT_ANNOTATIONS_DIR;

    if (resolvedDir.empty()) {
        llvm::errs() << "DTT: annotations directory not configured\n";
        return;
    }

    static constexpr const char *BuiltinFiles[] = {
        "std.dtt", "libc.dtt", "win32.dtt"
    };
    for (const char *file : BuiltinFiles) {
        llvm::SmallString<256> path(resolvedDir);
        llvm::sys::path::append(path, file);
        // Skip files that don't exist on the resolved path
        if (llvm::sys::fs::exists(path))
            parseAnnotationFile(path, MethodAnnotations, FreeFunctionAnnotations,
                                TemplateAliases);
    }
}

bool parseAnnotations(
    llvm::StringRef content,
    llvm::DenseMap<MethodKey, MethodInfo>& methodAnnotations,
    llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo>& freeFuncAnnotations,
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>>& templateAliases) {

    Parser parser{content, methodAnnotations, freeFuncAnnotations,
                  templateAliases, {}, 0};
    return parser.parse();
}

bool parseAnnotationFile(
    llvm::StringRef path,
    llvm::DenseMap<MethodKey, MethodInfo>& methodAnnotations,
    llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo>& freeFuncAnnotations,
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>>& templateAliases) {

    auto bufOrErr = llvm::MemoryBuffer::getFile(path);
    if (!bufOrErr) {
        llvm::errs() << "DTT: cannot open annotation file '" << path
                     << "': " << bufOrErr.getError().message() << "\n";
        return false;
    }

    return parseAnnotations((*bufOrErr)->getBuffer(),
                            methodAnnotations, freeFuncAnnotations,
                            templateAliases);
}

} // namespace dtt
