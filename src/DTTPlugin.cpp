// DTT Plugin — Dependent Type Tracking for C++

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Basic/Builtins.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include <map>
#include <unordered_map>
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
#include "DTTAnnotationFile.h"
#include "DTTSarif.h"

using namespace clang;
using dtt::ProofOp;
using dtt::TemplateAliases;

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

// External .dtt annotation files specified via --dtt-annotations=
llvm::SmallVector<std::string, 2> ExternalAnnotationFiles;

// SARIF output path (empty = no SARIF output)
llvm::SmallString<128> SarifOutputPath;

// Global pointer to active SARIF consumer (for diagnostic registration)
dtt::SarifDiagConsumer *GlobalSarifConsumer = nullptr;

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
    MaybeFreed, // Freed on some paths that reach here, not on others
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


// Forward declarations for FieldState (defined after SymValue)
struct LossProvenance;
struct FieldState;

// Forward declaration for safe expression evaluation
bool safeEvaluateAsInt(const Expr *E, Expr::EvalResult &R, ASTContext &Ctx);

// ============================================================
// VARIABLE IDENTITY
// ============================================================
// State is keyed by a variable's name. A declaration that shadows another
// visible where it is declared (an enclosing block's variable, a parameter,
// a global or capture the function uses) gets its own key, name'N, so the
// two never share state.
llvm::DenseMap<const ValueDecl *, std::string> ShadowingKeys;
llvm::DenseSet<const FunctionDecl *> KeyedFunctions;

void assignShadowingKeys(const FunctionDecl *F) {
    const Stmt *Body = F->getBody();
    if (!Body) return;
    // Variables declared in the body; the others the body names are visible
    // throughout it, as are the parameters.
    llvm::DenseSet<const VarDecl *> Inside;
    llvm::SmallVector<const VarDecl *, 16> Named;
    auto declares = [&](const Stmt *S, llvm::function_ref<void(const VarDecl *)> Fn) {
        if (auto *DS = dyn_cast<DeclStmt>(S))
            for (const Decl *D : DS->decls())
                if (auto *VD = dyn_cast<VarDecl>(D)) Fn(VD);
        if (auto *C = dyn_cast<CXXCatchStmt>(S))
            if (const VarDecl *VD = C->getExceptionDecl()) Fn(VD);
    };
    llvm::SmallVector<const Stmt *, 32> Work{Body};
    while (!Work.empty()) {
        const Stmt *S = Work.pop_back_val();
        if (!S || isa<LambdaExpr>(S)) continue;  // a lambda body is its own function
        declares(S, [&](const VarDecl *VD) { Inside.insert(VD); });
        if (auto *DRE = dyn_cast<DeclRefExpr>(S))
            if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) Named.push_back(VD);
        for (const Stmt *Child : S->children()) Work.push_back(Child);
    }
    llvm::StringSet<> Outer;
    for (const ParmVarDecl *P : F->parameters())
        if (P->getIdentifier()) Outer.insert(P->getName());
    for (const VarDecl *VD : Named)
        if (!Inside.count(VD) && VD->getIdentifier()) Outer.insert(VD->getName());
    // In order, with the names each open scope declares
    llvm::SmallVector<llvm::StringSet<>, 8> Scopes;
    llvm::StringMap<unsigned> Shadows;
    llvm::SmallVector<std::pair<const Stmt *, bool>, 32> Walk{{Body, false}};
    while (!Walk.empty()) {
        auto [S, Leaving] = Walk.pop_back_val();
        if (Leaving) {
            Scopes.pop_back();
            continue;
        }
        if (!S || isa<LambdaExpr>(S)) continue;
        if (isa<CompoundStmt, ForStmt, CXXForRangeStmt, WhileStmt, DoStmt, IfStmt, SwitchStmt,
                CXXCatchStmt>(S)) {
            Scopes.emplace_back();
            Walk.push_back({S, true});
        }
        declares(S, [&](const VarDecl *VD) {
            if (!VD->getIdentifier() || Scopes.empty()) return;
            llvm::StringRef Name = VD->getName();
            bool Visible = Outer.contains(Name) ||
                           llvm::any_of(Scopes, [&](const llvm::StringSet<> &Sc) { return Sc.contains(Name); });
            if (Visible) ShadowingKeys[VD] = (Name + "'" + llvm::Twine(++Shadows[Name])).str();
            Scopes.back().insert(Name);
        });
        llvm::SmallVector<const Stmt *, 8> Children(S->children());
        for (const Stmt *Child : llvm::reverse(Children)) Walk.push_back({Child, false});
    }
}

// The key a variable's state is stored under: its name, unless it shadows
// another declaration (assignShadowingKeys).
llvm::StringRef variableKey(const ValueDecl *D) {
    if (!D) return "";
    if (auto *VD = dyn_cast<VarDecl>(D); VD && VD->isLocalVarDeclOrParm())
        if (auto *F = dyn_cast<FunctionDecl>(VD->getDeclContext()); F && KeyedFunctions.insert(F).second)
            assignShadowingKeys(F);
    auto It = ShadowingKeys.find(D);
    if (It != ShadowingKeys.end()) return It->second;
    return D->getIdentifier() ? D->getName() : llvm::StringRef();
}

// ============================================================
// PHASE 1: TYPE-SAFE PATH REPRESENTATION
// ============================================================
// FieldPath provides type-safe path representation using AST node pointers
// instead of string concatenation. This eliminates string-matching bugs
// and enables proper tooling support.

// PathElement represents one step in a path traversal
struct PathElement {
    enum Kind {
        Field,          // .fieldName
        ArrayIndex,     // [N] where N is known constant
        RuntimeIndex,   // [?] where index is runtime value
        Deref,          // * (pointer dereference)
        SyntheticField  // ._size, ._nonnull, ._proof_*, etc.
    };

    Kind kind;
    const FieldDecl* fieldDecl = nullptr;      // For Field kind
    std::optional<int64_t> arrayIndex;          // For ArrayIndex kind
    std::string syntheticName;                  // For SyntheticField kind (_size, _nonnull, etc.)

    // Factory methods for clarity
    static PathElement field(const FieldDecl* FD) {
        PathElement e;
        e.kind = Field;
        e.fieldDecl = FD;
        return e;
    }

    static PathElement arrayIdx(int64_t idx) {
        PathElement e;
        e.kind = ArrayIndex;
        e.arrayIndex = idx;
        return e;
    }

    static PathElement runtimeIdx() {
        PathElement e;
        e.kind = RuntimeIndex;
        return e;
    }

    static PathElement deref() {
        PathElement e;
        e.kind = Deref;
        return e;
    }

    static PathElement synthetic(llvm::StringRef name) {
        PathElement e;
        e.kind = SyntheticField;
        e.syntheticName = name.str();
        return e;
    }

    bool operator==(const PathElement& other) const {
        if (kind != other.kind) return false;
        switch (kind) {
            case Field: return fieldDecl == other.fieldDecl;
            case ArrayIndex: return arrayIndex == other.arrayIndex;
            case RuntimeIndex: return true;
            case Deref: return true;
            case SyntheticField: return syntheticName == other.syntheticName;
        }
        return false;
    }

    std::string toString() const {
        switch (kind) {
            case Field:
                return fieldDecl ? ("." + fieldDecl->getName()).str() : ".<?>";
            case ArrayIndex:
                return "[" + std::to_string(*arrayIndex) + "]";
            case RuntimeIndex:
                return "[?]";
            case Deref:
                return "*";
            case SyntheticField:
                return "." + syntheticName;
        }
        return "";
    }
};

// FieldPath represents a complete path to a tracked value
// Examples: "v", "v._size", "arr[0]", "ptr._nonnull", "this._size"
struct FieldPath {
    // Base can be:
    // - A ValueDecl* for local variables, parameters, fields
    // - nullptr with isThis=true for 'this' pointer
    // - nullptr with syntheticBase for special paths
    const ValueDecl* baseDecl = nullptr;
    bool isThis = false;
    std::string syntheticBase;  // For paths like literal strings or special cases

    std::vector<PathElement> elements;

    // Cached string representation and hash for O(1) lookups after first use
    mutable std::string cachedStr;
    mutable size_t cachedHash = 0;
    mutable bool cacheValid = false;

    // Factory methods
    static FieldPath fromDecl(const ValueDecl* VD) {
        FieldPath p;
        p.baseDecl = VD;
        return p;
    }

    static FieldPath fromThis() {
        FieldPath p;
        p.isThis = true;
        return p;
    }

    // Parse a string path into canonical form
    // "a._size" becomes syntheticBase="a" with element={SyntheticField, "_size"}
    // This ensures consistent hashing regardless of how the path was constructed
    static FieldPath fromSynthetic(llvm::StringRef path) {
        FieldPath p;
        if (path.empty()) return p;

        // Find the first dot that starts a field access
        // We need to handle array subscripts like "arr[0]._field"
        size_t pos = 0;
        size_t baseEnd = path.size();

        // Find where the base ends (first '.' not inside brackets)
        int bracketDepth = 0;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '[') bracketDepth++;
            else if (path[i] == ']') bracketDepth--;
            else if (path[i] == '.' && bracketDepth == 0) {
                baseEnd = i;
                break;
            }
        }

        p.syntheticBase = path.substr(0, baseEnd).str();
        pos = baseEnd;

        // Parse remaining elements
        while (pos < path.size()) {
            if (path[pos] == '.') {
                // Field access - find end of field name
                size_t fieldStart = pos + 1;
                size_t fieldEnd = fieldStart;
                while (fieldEnd < path.size() && path[fieldEnd] != '.' && path[fieldEnd] != '[') {
                    fieldEnd++;
                }
                if (fieldEnd > fieldStart) {
                    p.elements.push_back(PathElement::synthetic(path.substr(fieldStart, fieldEnd - fieldStart)));
                }
                pos = fieldEnd;
            } else if (path[pos] == '[') {
                // Array subscript - find closing bracket
                size_t closePos = path.find(']', pos);
                if (closePos != llvm::StringRef::npos) {
                    llvm::StringRef indexStr = path.substr(pos + 1, closePos - pos - 1);
                    if (indexStr == "?" || indexStr.empty()) {
                        p.elements.push_back(PathElement::runtimeIdx());
                    } else {
                        int64_t idx = 0;
                        if (indexStr.getAsInteger(10, idx)) {
                            // Non-numeric index, treat as runtime
                            p.elements.push_back(PathElement::runtimeIdx());
                        } else {
                            p.elements.push_back(PathElement::arrayIdx(idx));
                        }
                    }
                    pos = closePos + 1;
                } else {
                    break;  // Malformed
                }
            } else {
                pos++;  // Skip unexpected characters
            }
        }
        return p;
    }

    static FieldPath invalid() {
        return FieldPath{};
    }

    bool isValid() const {
        return baseDecl != nullptr || isThis || !syntheticBase.empty();
    }

    // Invalidate cached hash/string (call after modification)
    void invalidateCache() const {
        cacheValid = false;
        cachedStr.clear();
        cachedHash = 0;
    }

    // Append a path element
    FieldPath with(PathElement elem) const {
        FieldPath result = *this;
        result.elements.push_back(std::move(elem));
        result.invalidateCache();  // Cache is stale after modification
        return result;
    }

    // Append a synthetic field (common case)
    FieldPath withSynthetic(llvm::StringRef name) const {
        return with(PathElement::synthetic(name));
    }

    // Get path with synthetic field stripped (for basePath operations)
    FieldPath base() const {
        if (elements.empty()) return *this;
        FieldPath result = *this;
        result.elements.pop_back();
        result.invalidateCache();  // Cache is stale after modification
        return result;
    }

    // Create a new path with different synthetic base but same elements
    FieldPath rebase(const std::string& newBase) const {
        FieldPath result;
        result.syntheticBase = newBase;
        result.elements = elements;
        // result.cacheValid is already false (default)
        return result;
    }

    // Check if path contains a runtime (non-constant) array index
    bool hasRuntimeIndex() const {
        for (const auto& elem : elements) {
            if (elem.kind == PathElement::RuntimeIndex) return true;
        }
        return false;
    }

    bool operator==(const FieldPath& other) const {
        // Fast path: if cached hashes differ, paths differ
        if (cacheValid && other.cacheValid && cachedHash != other.cachedHash) {
            return false;
        }
        // Compare canonical string representations
        return toString() == other.toString();
    }

    // Cached hash for O(1) lookups after first use
    size_t hash() const {
        ensureCache();
        return cachedHash;
    }

    // Convert to string for diagnostics and backward compatibility
    // Returns reference to cached string - no allocation after first call
    const std::string& toString() const {
        ensureCache();
        return cachedStr;
    }

    // Return StringRef view for efficient read-only access
    llvm::StringRef toStringRef() const {
        ensureCache();
        return cachedStr;
    }

private:
    void ensureCache() const {
        if (cacheValid) return;

        if (baseDecl) {
            cachedStr = variableKey(baseDecl).str();
        } else if (isThis) {
            cachedStr = "this";
        } else if (!syntheticBase.empty()) {
            cachedStr = syntheticBase;
        } else {
            cachedStr = "";  // Invalid path
        }

        for (const auto& elem : elements) {
            cachedStr += elem.toString();
        }

        cachedHash = std::hash<std::string>{}(cachedStr);
        cacheValid = true;
    }
};

// Hash function for FieldPath (for use in unordered_map)
// Uses cached hash for O(1) performance after first computation
struct FieldPathHash {
    size_t operator()(const FieldPath& path) const {
        return path.hash();
    }
};

} // end anonymous namespace

// DenseMapInfo specialization for FieldPath (enables llvm::DenseMap)
// Uses sentinel pointer values for empty/tombstone keys
namespace llvm {
template<> struct DenseMapInfo<FieldPath> {
    static inline FieldPath getEmptyKey() {
        FieldPath p;
        p.baseDecl = reinterpret_cast<const clang::ValueDecl*>(-1);
        return p;
    }
    static inline FieldPath getTombstoneKey() {
        FieldPath p;
        p.baseDecl = reinterpret_cast<const clang::ValueDecl*>(-2);
        return p;
    }
    static unsigned getHashValue(const FieldPath& path) {
        return static_cast<unsigned>(path.hash());
    }
    static bool isEqual(const FieldPath& lhs, const FieldPath& rhs) {
        // Sentinel check: compare pointers directly for sentinel values
        if (lhs.baseDecl == reinterpret_cast<const clang::ValueDecl*>(-1) ||
            lhs.baseDecl == reinterpret_cast<const clang::ValueDecl*>(-2) ||
            rhs.baseDecl == reinterpret_cast<const clang::ValueDecl*>(-1) ||
            rhs.baseDecl == reinterpret_cast<const clang::ValueDecl*>(-2)) {
            return lhs.baseDecl == rhs.baseDecl;
        }
        return lhs == rhs;
    }
};
} // end namespace llvm

namespace {

// ============================================================
// PHASE 1: STRUCTURED PROOF SYSTEM
// ============================================================
// Proofs are facts we've established about fields (e.g., _size > 0).
// Instead of storing them as string keys like "v._proof__size>0",
// we use structured Proof objects with proper typed representation.

// String representations of comparison operators (indexed by ProofOp)
constexpr const char* ProofOpStrings[] = {">", ">=", "<", "<=", "==", "!="};

// Build a proof key string without heap allocation for typical sizes.
// Format: field + op + value, e.g. "_size>0", "_nonnull==1"
static llvm::SmallString<32> buildProofKey(StringRef field, ProofOp op, int64_t value) {
    llvm::SmallString<32> key;
    key += field;
    key += ProofOpStrings[static_cast<int>(op)];
    key += std::to_string(value);
    return key;
}

// A Proof represents a proven fact about a field
// Examples: _size > 0, _nonnull != 0, _value >= 5
struct Proof {
    std::string field;    // The field this proof is about (_size, _nonnull, _value)
    ProofOp op;           // Comparison operator
    int64_t value;        // RHS constant

    bool operator==(const Proof& other) const {
        return field == other.field && op == other.op && value == other.value;
    }

    std::string toKey() const {
        auto key = buildProofKey(field, op, value);
        return std::string(key.begin(), key.end());
    }

    // Parse from legacy string key format
    static std::optional<Proof> fromKey(const std::string& key) {
        // Find operator position
        size_t opPos = std::string::npos;
        size_t opLen = 0;
        ProofOp foundOp;

        // Try two-char operators first
        if ((opPos = key.find(">=")) != std::string::npos) { opLen = 2; foundOp = ProofOp::GE; }
        else if ((opPos = key.find("<=")) != std::string::npos) { opLen = 2; foundOp = ProofOp::LE; }
        else if ((opPos = key.find("==")) != std::string::npos) { opLen = 2; foundOp = ProofOp::EQ; }
        else if ((opPos = key.find("!=")) != std::string::npos) { opLen = 2; foundOp = ProofOp::NE; }
        else if ((opPos = key.find(">")) != std::string::npos) { opLen = 1; foundOp = ProofOp::GT; }
        else if ((opPos = key.find("<")) != std::string::npos) { opLen = 1; foundOp = ProofOp::LT; }
        else return std::nullopt;

        std::string field = key.substr(0, opPos);
        std::string valStr = key.substr(opPos + opLen);

        if (field.empty() || valStr.empty()) return std::nullopt;

        errno = 0;
        char* end;
        int64_t val = std::strtoll(valStr.c_str(), &end, 10);
        if (errno != 0 || *end != '\0') return std::nullopt;

        return Proof{field, foundOp, val};
    }
};

// Hash function for Proof
struct ProofHash {
    size_t operator()(const Proof& p) const {
        size_t h = std::hash<std::string>{}(p.field);
        h ^= std::hash<int>{}(static_cast<int>(p.op)) << 1;
        h ^= std::hash<int64_t>{}(p.value) << 2;
        return h;
    }
};

} // end anonymous namespace

namespace llvm {
template<> struct DenseMapInfo<Proof> {
    static inline Proof getEmptyKey() {
        return {"\xff\xff", ProofOp::GT, INT64_MAX};
    }
    static inline Proof getTombstoneKey() {
        return {"\xff\xfe", ProofOp::GT, INT64_MAX};
    }
    static unsigned getHashValue(const Proof& p) {
        return llvm::hash_combine(p.field, static_cast<int>(p.op), p.value);
    }
    static bool isEqual(const Proof& lhs, const Proof& rhs) {
        return lhs == rhs;
    }
};
} // namespace llvm

namespace {

// ProofStore manages proofs separately from values
// Key: (FieldPath for base object, Proof for the fact)
// This replaces string keys like "v._proof__size>0"
class ProofStore {
public:
    using ProofSet = llvm::SmallDenseSet<Proof, 4>;

    ProofStore() = default;

    bool operator==(const ProofStore& Other) const {
        auto nonEmpty = [](const ProofStore& S) {
            return llvm::count_if(S.proofs_, [](const auto& E) { return !E.second.empty(); });
        };
        if (nonEmpty(*this) != nonEmpty(Other)) return false;
        for (const auto& [Base, Set] : proofs_) {
            if (Set.empty()) continue;
            auto It = Other.proofs_.find(Base);
            if (It == Other.proofs_.end() || It->second.size() != Set.size()) return false;
            for (const Proof& P : Set)
                if (!It->second.contains(P)) return false;
        }
        return true;
    }

    // Add a proof about a path - O(1)
    void add(const FieldPath& base, const Proof& p) {
        proofs_[base].insert(p);
    }

    // Remove a specific proof - O(1)
    void remove(const FieldPath& base, const Proof& p) {
        auto it = proofs_.find(base);
        if (it != proofs_.end()) {
            it->second.erase(p);
            if (it->second.empty()) {
                proofs_.erase(it);
            }
        }
    }

    // Check if a proof exists - O(1)
    bool has(const FieldPath& base, const Proof& p) const {
        auto it = proofs_.find(base);
        if (it == proofs_.end()) return false;
        return it->second.contains(p);
    }

    // Get all proofs for a path
    const ProofSet* getProofs(const FieldPath& base) const {
        auto it = proofs_.find(base);
        return it != proofs_.end() ? &it->second : nullptr;
    }

    // Invalidate all proofs about a specific field for a path, except those
    // Keep accepts
    void invalidateField(const FieldPath& base, const std::string& field,
                         llvm::function_ref<bool(const Proof&)> Keep = nullptr) {
        auto it = proofs_.find(base);
        if (it != proofs_.end()) {
            llvm::SmallVector<Proof, 4> toRemove;
            for (const auto& p : it->second) {
                if ((p.field == field || p.field.find(field) == 0) && !(Keep && Keep(p))) {
                    toRemove.push_back(p);
                }
            }
            for (const auto& p : toRemove) {
                it->second.erase(p);
            }
            if (it->second.empty()) {
                proofs_.erase(it);
            }
        }
    }

    // Invalidate ALL proofs for a path (when the base object changes entirely)
    void invalidateAll(const FieldPath& base) {
        proofs_.erase(base);
    }

    // Clear all proofs (called at function boundary)
    void clear() {
        proofs_.clear();
    }

    // Iterate over all proofs (for migration/debugging)
    template<typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [path, proofSet] : proofs_) {
            for (const auto& proof : proofSet) {
                fn(path, proof);
            }
        }
    }

    // Check if store is empty
    bool empty() const { return proofs_.empty(); }

    // Get number of base paths with proofs
    size_t size() const { return proofs_.size(); }

private:
    // SmallDenseSet for O(1) lookup/insert/remove - proofs per path is typically small
    llvm::DenseMap<FieldPath, ProofSet> proofs_;
};

// Entailment: does "field known_op ev" imply "field required_op rv"?
static constexpr bool entails(ProofOp known, int64_t ev,
                              ProofOp required, int64_t rv) {
    // clang-format off
    //                     GT      GE      LT      LE      EQ      NE
    // known\required  ──────────────────────────────────────────────────
    // GT  (f > ev)     e>=r    e>=r     ✗       ✗       ✗      e>=r
    // GE  (f >= ev)    e>r     e>=r     ✗       ✗       ✗      e>r
    // LT  (f < ev)      ✗       ✗      e<=r    e<=r     ✗      e<=r
    // LE  (f <= ev)     ✗       ✗      e<r     e<=r     ✗      e<r
    // EQ  (f == ev)    e>r     e>=r    e<r     e<=r    e==r    e!=r
    // NE  (f != ev)     ✗       ✗       ✗       ✗       ✗      e==r
    // clang-format on
    switch (known) {
    case ProofOp::GT:
        switch (required) {
        case ProofOp::GT: case ProofOp::GE: case ProofOp::NE: return ev >= rv;
        default: return false;
        }
    case ProofOp::GE:
        switch (required) {
        case ProofOp::GT: case ProofOp::NE: return ev > rv;
        case ProofOp::GE: return ev >= rv;
        default: return false;
        }
    case ProofOp::LT:
        switch (required) {
        case ProofOp::LT: case ProofOp::LE: case ProofOp::NE: return ev <= rv;
        default: return false;
        }
    case ProofOp::LE:
        switch (required) {
        case ProofOp::LT: case ProofOp::NE: return ev < rv;
        case ProofOp::LE: return ev <= rv;
        default: return false;
        }
    case ProofOp::EQ:
        switch (required) {
        case ProofOp::GT:  return ev > rv;
        case ProofOp::GE:  return ev >= rv;
        case ProofOp::LT:  return ev < rv;
        case ProofOp::LE:  return ev <= rv;
        case ProofOp::EQ:  return ev == rv;
        case ProofOp::NE:  return ev != rv;
        }
        break;
    case ProofOp::NE:
        if (required == ProofOp::NE) return ev == rv;
        return false;
    }
    return false;
}

// ProofContext provides entailment reasoning over proofs
class ProofContext {
public:
    explicit ProofContext(const ProofStore& store) : store_(store) {}

    // Check if a requirement can be proven from existing proofs
    // Uses entailment rules to deduce facts from what's known
    bool canProve(const FieldPath& base, const Proof& requirement) const {
        // Direct proof check first
        if (store_.has(base, requirement)) return true;

        // Get all proofs for this base path
        const auto* proofs = store_.getProofs(base);
        if (!proofs) return false;

        // Try entailment from each existing proof
        for (const auto& existing : *proofs) {
            if (existing.field != requirement.field) continue;
            if (implies(existing, requirement)) return true;
        }

        return false;
    }

    // Check if existingProof implies requiredProof.
    // Both proofs must be about the same field (caller ensures this).
    static bool implies(const Proof& existing, const Proof& required) {
        if (existing == required) return true;
        return entails(existing.op, existing.value,
                       required.op, required.value);
    }

    // Convenience: check if we can prove field > value
    bool canProveGT(const FieldPath& base, const std::string& field, int64_t value) const {
        return canProve(base, Proof{field, ProofOp::GT, value});
    }

    // Convenience: check if we can prove field >= value
    bool canProveGE(const FieldPath& base, const std::string& field, int64_t value) const {
        return canProve(base, Proof{field, ProofOp::GE, value});
    }

    // Convenience: check if we can prove field != value
    bool canProveNE(const FieldPath& base, const std::string& field, int64_t value) const {
        return canProve(base, Proof{field, ProofOp::NE, value});
    }

    // Convenience: check if we can prove field == value
    bool canProveEQ(const FieldPath& base, const std::string& field, int64_t value) const {
        return canProve(base, Proof{field, ProofOp::EQ, value});
    }

    // Check if we can prove a constraint
    // op: ProofOp enum value cast to int
    bool canProveConstraint(const FieldPath& base, const std::string& field,
                           int op, int64_t value) const {
        ProofOp proofOp;
        switch (op) {
            case 0: proofOp = ProofOp::LT; break;
            case 1: proofOp = ProofOp::LE; break;
            case 2: proofOp = ProofOp::GT; break;
            case 3: proofOp = ProofOp::GE; break;
            case 4: proofOp = ProofOp::EQ; break;
            case 5: proofOp = ProofOp::NE; break;
            default: return false;
        }
        return canProve(base, Proof{field, proofOp, value});
    }

private:
    const ProofStore& store_;
};


// Global proof context for entailment checking

// Forward declaration - will be used for type-safe environment
// std::unordered_map<FieldPath, SymValue, FieldPathHash> TypedEnv;

// Helper to get typed path from expression (parallel to getExprPath)
// Returns invalid FieldPath if expression can't be tracked
FieldPath getTypedPath(const Expr *E);

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
        return variableKey(DRE->getDecl()).str();
    // Handle 'this' pointer for method body analysis
    if (isa<CXXThisExpr>(E))
        return "this";
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        std::string base = getExprPath(ME->getBase());
        if (base.empty()) return "";
        return base + "." + ME->getMemberDecl()->getName().str();
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

// Extract nested field path from a MemberExpr chain rooted at 'this'.
// For this->inner.value, returns "inner.value" (excludes 'this.' prefix).
// Returns empty string if not rooted at 'this' or if the path has no DTT-tracked fields.
std::string getNestedFieldPathFromThis(const Expr *E) {
    E = E->IgnoreParenImpCasts();

    // Build path by walking up the MemberExpr chain
    llvm::SmallVector<llvm::StringRef, 4> pathParts;

    while (auto *ME = dyn_cast<MemberExpr>(E)) {
        llvm::StringRef fieldName = ME->getMemberDecl()->getName();
        pathParts.push_back(fieldName);
        E = ME->getBase()->IgnoreParenImpCasts();
    }

    // Check if ultimate base is 'this'
    if (!isa<CXXThisExpr>(E)) return "";

    // Must have at least one field
    if (pathParts.empty()) return "";

    // Build path from outermost to innermost (reverse order)
    std::string result;
    for (auto it = pathParts.rbegin(); it != pathParts.rend(); ++it) {
        if (!result.empty()) result += ".";
        result += it->str();
    }
    return result;
}

// Check if any component of a nested path starts with '_' (DTT tracked)
bool hasTrackedFieldInPath(llvm::StringRef path) {
    // Check if any component starts with '_'
    // e.g., "stats._count" has a tracked field, "inner.value" doesn't unless we track "inner"
    // For now, track if the LAST component starts with '_' or if any component starts with '_'
    size_t pos = 0;
    while (pos < path.size()) {
        size_t dot = path.find('.', pos);
        llvm::StringRef component = (dot == llvm::StringRef::npos)
            ? path.substr(pos)
            : path.substr(pos, dot - pos);
        if (!component.empty() && component[0] == '_') return true;
        if (dot == llvm::StringRef::npos) break;
        pos = dot + 1;
    }
    return false;
}

// Type-safe version of getExprPath - returns FieldPath instead of string
// This is the Phase 1 implementation for type-safe path representation
FieldPath getTypedPath(const Expr *E) {
    if (!E) return FieldPath::invalid();

    E = E->IgnoreParenImpCasts();

    // Base case: DeclRefExpr - reference to a variable
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        return FieldPath::fromDecl(DRE->getDecl());
    }

    // Handle 'this' pointer for method body analysis
    if (isa<CXXThisExpr>(E)) {
        return FieldPath::fromThis();
    }

    // Member access: base.field
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        FieldPath basePath = getTypedPath(ME->getBase());
        if (!basePath.isValid()) return FieldPath::invalid();

        // Get the field declaration
        if (auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
            return basePath.with(PathElement::field(FD));
        }
        // For non-field members (methods, etc.), fall back to synthetic
        return basePath.with(PathElement::synthetic(ME->getMemberDecl()->getNameAsString()));
    }

    // Array subscript: base[index]
    if (auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        FieldPath basePath = getTypedPath(ASE->getBase());
        if (!basePath.isValid()) return FieldPath::invalid();

        if (GlobalCtx) {
            Expr::EvalResult R;
            if (safeEvaluateAsInt(ASE->getIdx(), R, *GlobalCtx)) {
                const llvm::APSInt &Val = R.Val.getInt();
                if (Val.isRepresentableByInt64()) {
                    int64_t idx = Val.getExtValue();
                    return basePath.with(PathElement::arrayIdx(idx));
                }
            }
        }
        return basePath.with(PathElement::runtimeIdx());
    }

    // Handle chain assignment (v3 = v2 = v1) — result of v2 = v1 is v2
    if (auto *OpCall = dyn_cast<CXXOperatorCallExpr>(E)) {
        if (OpCall->getOperator() == OO_Equal && OpCall->getNumArgs() >= 1) {
            return getTypedPath(OpCall->getArg(0));
        }
    }

    // Handle C-style assignment: (local = ptr) — result is local
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->getOpcode() == BO_Assign) {
            return getTypedPath(BO->getLHS());
        }
    }

    // Handle address-of: &v -> return path for v
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
        if (UO->getOpcode() == UO_AddrOf) {
            return getTypedPath(UO->getSubExpr());
        }
    }

    // Handle method call chaining: b.push().push() -> walk chain to find base 'b'
    // Methods that return *this (Builder& or T&) allow chaining
    if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        auto *Method = MCE->getMethodDecl();
        if (Method) {
            QualType retType = Method->getReturnType();
            // Check if return type is a reference to the class type
            if (retType->isLValueReferenceType()) {
                QualType refType = retType->getPointeeType();
                if (auto *RD = refType->getAsCXXRecordDecl()) {
                    if (RD == Method->getParent() || RD->isDerivedFrom(Method->getParent())) {
                        // Method returns reference to same/derived class - assume it's *this
                        // Walk to the base object
                        return getTypedPath(MCE->getImplicitObjectArgument());
                    }
                }
            }
        }
    }

    return FieldPath::invalid();
}

// Validation helper - asserts that getTypedPath and getExprPath produce
// equivalent results. Used during migration to ensure correctness.
// Enable DTT_VALIDATE_PATHS to activate runtime checks.
#ifdef DTT_VALIDATE_PATHS
void validatePathEquivalence(const Expr* E) {
    std::string stringPath = getExprPath(E);
    FieldPath typedPath = getTypedPath(E);

    std::string typedAsString = typedPath.toString();

    // Both should be empty/invalid together
    if (stringPath.empty() != !typedPath.isValid()) {
        llvm::errs() << "DTT PATH VALIDATION FAILED: validity mismatch\n";
        llvm::errs() << "  String path: '" << stringPath << "'\n";
        llvm::errs() << "  Typed valid: " << typedPath.isValid() << "\n";
        assert(false && "Path validation failed");
    }

    // If both valid, strings should match
    if (typedPath.isValid() && stringPath != typedAsString) {
        llvm::errs() << "DTT PATH VALIDATION FAILED: string mismatch\n";
        llvm::errs() << "  String path: '" << stringPath << "'\n";
        llvm::errs() << "  Typed path:  '" << typedAsString << "'\n";
        assert(false && "Path validation failed");
    }
}
#else
inline void validatePathEquivalence(const Expr*) {}
#endif

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

// Use the unified SymValue from DTTTypes.h
using dtt::SymValue;

// Build a SymValue expression tree from an Expr, including arithmetic operations
// Used for tracking allocation sizes like malloc(len + 10) and memcpy sizes
// Returns nullopt if expression cannot be represented symbolically
std::optional<SymValue> buildSymValueFromExpr(const Expr *E) {
    if (!E) return std::nullopt;
    E = E->IgnoreParenImpCasts();

    // Constants - evaluate if possible
    if (GlobalCtx) {
        Expr::EvalResult R;
        if (safeEvaluateAsInt(E, R, *GlobalCtx)) {
            return SymValue::Const(R.Val.getInt().getSExtValue());
        }
    }

    // Simple variable reference
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        return SymValue::Var(variableKey(DRE->getDecl()).str());
    }

    // Member expression
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        std::string path = getExprPath(E);
        if (!path.empty()) {
            return SymValue::Var(path);
        }
        return std::nullopt;
    }

    // Binary arithmetic: +, -, *, /
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->isAdditiveOp() || BO->isMultiplicativeOp()) {
            auto lhs = buildSymValueFromExpr(BO->getLHS());
            auto rhs = buildSymValueFromExpr(BO->getRHS());
            if (!lhs || !rhs) return std::nullopt;

            switch (BO->getOpcode()) {
                case BO_Add: return SymValue::Add(*lhs, *rhs);
                case BO_Sub: return SymValue::Sub(*lhs, *rhs);
                case BO_Mul: return SymValue::Mul(*lhs, *rhs);
                case BO_Div: return SymValue::Div(*lhs, *rhs);
                default: return std::nullopt;
            }
        }
    }

    // sizeof expressions - try to evaluate
    if (auto *SOEL = dyn_cast<UnaryExprOrTypeTraitExpr>(E)) {
        if (SOEL->getKind() == UETT_SizeOf && GlobalCtx) {
            Expr::EvalResult R;
            if (safeEvaluateAsInt(E, R, *GlobalCtx)) {
                return SymValue::Const(R.Val.getInt().getSExtValue());
            }
        }
    }

    return std::nullopt;
}

// ============================================================
// PHASE 2: CONSTRAINT EXPRESSION AST
// ============================================================
// ConstraintExpr provides a proper AST for constraint expressions,
// replacing string-based parsing with structured representation.
// This enables constraint transformation, simplification, and
// better error reporting.

// Use the shared ConstraintExpr hierarchy from DTTTypes.h
using ConstraintExpr = dtt::ConstraintExpr;
using ConstraintExprPtr = dtt::ConstraintExprPtr;
using LiteralExpr = dtt::LiteralExpr;
using FieldRefExpr = dtt::FieldRefExpr;
using ArgRefExpr = dtt::ArgRefExpr;
using ArgFieldRefExpr = dtt::ArgFieldRefExpr;
using OldRefExpr = dtt::OldRefExpr;
using ResultRefExpr = dtt::ResultRefExpr;
using BinaryExpr = dtt::BinaryExpr;
using UnaryExpr = dtt::UnaryExpr;
using ForallExpr = dtt::ForallExpr;
using ExistsExpr = dtt::ExistsExpr;
using IndexExpr = dtt::IndexExpr;

// ============================================================
// NOTE: ConstraintExpr hierarchy is defined in DTTTypes.h
// (single source of truth). Local 'using' aliases above bring
// all types into this anonymous namespace.
// ============================================================

// ============================================================
// CONSTRAINT EXPRESSION ANALYSIS HELPERS
// ============================================================

// Check if expression is a simple field comparison: _field op literal
struct SimpleFieldComparison {
    std::string field;
    ConstraintExpr::Kind op;
    int64_t value;
};
std::optional<SimpleFieldComparison> asSimpleFieldComparison(const ConstraintExprPtr& expr) {
    if (!expr) return std::nullopt;
    if (expr->kind < ConstraintExpr::CmpGT || expr->kind > ConstraintExpr::CmpNE) return std::nullopt;
    auto* bin = static_cast<BinaryExpr*>(expr.get());
    if (bin->left->kind != ConstraintExpr::FieldRef) return std::nullopt;
    if (bin->right->kind != ConstraintExpr::Literal) return std::nullopt;
    auto* fieldRef = static_cast<FieldRefExpr*>(bin->left.get());
    auto* lit = static_cast<LiteralExpr*>(bin->right.get());
    return SimpleFieldComparison{fieldRef->fieldName, bin->kind, lit->value};
}

// Check if expression is arg vs field: argN op _field
struct ArgVsFieldComparison {
    unsigned argIdx;
    std::string argField;  // empty if just argN, non-empty if argN._field
    ConstraintExpr::Kind op;
    std::string rhsField;
};
std::optional<ArgVsFieldComparison> asArgVsFieldComparison(const ConstraintExprPtr& expr) {
    if (!expr) return std::nullopt;
    if (expr->kind < ConstraintExpr::CmpGT || expr->kind > ConstraintExpr::CmpNE) return std::nullopt;
    auto* bin = static_cast<BinaryExpr*>(expr.get());

    // LHS must be arg or arg._field
    unsigned argIdx = 0;
    std::string argField;
    switch (bin->left->kind) {
        case ConstraintExpr::ArgRef:
            argIdx = static_cast<ArgRefExpr*>(bin->left.get())->argIndex;
            break;
        case ConstraintExpr::ArgFieldRef: {
            auto* af = static_cast<ArgFieldRefExpr*>(bin->left.get());
            argIdx = af->argIndex;
            argField = std::string(af->fieldName);
            break;
        }
        default: return std::nullopt;
    }

    // RHS must be field
    if (bin->right->kind != ConstraintExpr::FieldRef) return std::nullopt;
    auto* fieldRef = static_cast<FieldRefExpr*>(bin->right.get());
    return ArgVsFieldComparison{argIdx, argField, bin->kind, fieldRef->fieldName};
}

// Check if expression is arg vs constant: argN op literal
struct ArgVsConstComparison {
    unsigned argIdx;
    std::string argField;  // empty if just argN, non-empty if argN._field
    ConstraintExpr::Kind op;
    int64_t value;
};
std::optional<ArgVsConstComparison> asArgVsConstComparison(const ConstraintExprPtr& expr) {
    if (!expr) return std::nullopt;
    if (expr->kind < ConstraintExpr::CmpGT || expr->kind > ConstraintExpr::CmpNE) return std::nullopt;
    auto* bin = static_cast<BinaryExpr*>(expr.get());

    // LHS must be arg or arg._field
    unsigned argIdx = 0;
    std::string argField;
    switch (bin->left->kind) {
        case ConstraintExpr::ArgRef:
            argIdx = static_cast<ArgRefExpr*>(bin->left.get())->argIndex;
            break;
        case ConstraintExpr::ArgFieldRef: {
            auto* af = static_cast<ArgFieldRefExpr*>(bin->left.get());
            argIdx = af->argIndex;
            argField = std::string(af->fieldName);
            break;
        }
        default: return std::nullopt;
    }

    // RHS must be literal (or nullptr/NULL which is 0)
    switch (bin->right->kind) {
        case ConstraintExpr::Literal:
            return ArgVsConstComparison{argIdx, argField, bin->kind,
                                        static_cast<LiteralExpr*>(bin->right.get())->value};
        case ConstraintExpr::FieldRef: {
            auto* fr = static_cast<FieldRefExpr*>(bin->right.get());
            if (fr->fieldName == "nullptr" || fr->fieldName == "NULL") {
                return ArgVsConstComparison{argIdx, argField, bin->kind, 0};
            }
            return std::nullopt;
        }
        default: return std::nullopt;
    }
}

// Check if expression is a compound (AND/OR)
bool isCompoundConstraint(const ConstraintExprPtr& expr) {
    if (!expr) return false;
    return expr->kind == ConstraintExpr::LogicalAnd || expr->kind == ConstraintExpr::LogicalOr;
}

// Check if expression is multi-arg comparison: arg0.field op arg1.field
struct MultiArgComparison {
    unsigned lhsArgIdx;
    std::string lhsField;
    ConstraintExpr::Kind op;
    unsigned rhsArgIdx;
    std::string rhsField;
};
std::optional<MultiArgComparison> asMultiArgComparison(const ConstraintExprPtr& expr) {
    if (!expr) return std::nullopt;
    if (expr->kind < ConstraintExpr::CmpGT || expr->kind > ConstraintExpr::CmpNE) return std::nullopt;
    auto* bin = static_cast<BinaryExpr*>(expr.get());

    // LHS must be arg or arg._field
    unsigned lhsArgIdx = 0;
    std::string lhsField;
    switch (bin->left->kind) {
        case ConstraintExpr::ArgRef:
            lhsArgIdx = static_cast<ArgRefExpr*>(bin->left.get())->argIndex;
            break;
        case ConstraintExpr::ArgFieldRef: {
            auto* af = static_cast<ArgFieldRefExpr*>(bin->left.get());
            lhsArgIdx = af->argIndex;
            lhsField = std::string(af->fieldName);
            break;
        }
        default: return std::nullopt;
    }

    // RHS must also be arg or arg._field
    unsigned rhsArgIdx = 0;
    std::string rhsField;
    switch (bin->right->kind) {
        case ConstraintExpr::ArgRef:
            rhsArgIdx = static_cast<ArgRefExpr*>(bin->right.get())->argIndex;
            break;
        case ConstraintExpr::ArgFieldRef: {
            auto* af = static_cast<ArgFieldRefExpr*>(bin->right.get());
            rhsArgIdx = af->argIndex;
            rhsField = std::string(af->fieldName);
            break;
        }
        default: return std::nullopt;
    }

    return MultiArgComparison{lhsArgIdx, lhsField, bin->kind, rhsArgIdx, rhsField};
}

// Check if expression is nullptr check: argN op nullptr/0
struct NullptrCheck {
    unsigned argIdx;
    ConstraintExpr::Kind op;  // typically NE (!=) or EQ (==)
};
std::optional<NullptrCheck> asNullptrCheck(const ConstraintExprPtr& expr) {
    if (auto avc = asArgVsConstComparison(expr)) {
        // argN != 0 or argN == 0 with no field access
        if (avc->argField.empty() && avc->value == 0 &&
            (avc->op == ConstraintExpr::CmpNE || avc->op == ConstraintExpr::CmpEQ)) {
            return NullptrCheck{avc->argIdx, avc->op};
        }
    }
    return std::nullopt;
}

// Check if expression involves complex expressions (arithmetic, multi-arg comparisons)
bool isComplexExprConstraint(const ConstraintExprPtr& expr) {
    if (!expr) return false;
    // If it's not a simple pattern (field vs const, arg vs field, arg vs const), it's complex
    if (asSimpleFieldComparison(expr)) return false;
    if (asArgVsFieldComparison(expr)) return false;
    if (asArgVsConstComparison(expr)) return false;
    if (asMultiArgComparison(expr)) return false;
    if (isCompoundConstraint(expr)) return false;
    return true;  // Everything else is complex
}


// ============================================================
// UNIFIED VALUE CONCEPTS REGISTRY
// ============================================================
// Concept name -> expansion mapping
// Contains both predefined and user-defined concepts (DRY)
// Initialized per translation unit, extended by dtt:define= annotations

// Annotation prefix constants (avoid magic numbers)
constexpr llvm::StringLiteral kDttPrefix = "dtt:";
constexpr llvm::StringLiteral kDefinePrefix = "dtt:define=";
constexpr llvm::StringLiteral kRequiresPrefix = "dtt:requires=";
constexpr llvm::StringLiteral kEnsuresPrefix = "dtt:ensures=";
constexpr llvm::StringLiteral kReturnsPrefix = "dtt:returns=";
constexpr llvm::StringLiteral kEnsuresIfTruePrefix = "dtt:ensures_if_true=";
constexpr llvm::StringLiteral kEnsuresIfFalsePrefix = "dtt:ensures_if_false=";

// Mutable registry - populated with predefined concepts, extended by user
llvm::StringMap<std::string> ConceptRegistry;

// Initialize the concept registry with predefined concepts
static void initializeConceptRegistry() {
    ConceptRegistry.clear();

    // Container state
    ConceptRegistry["nonempty"] = "_size > 0";
    ConceptRegistry["empty"] = "_size == 0";
    ConceptRegistry["full"] = "_size == _capacity";
    ConceptRegistry["has_space"] = "_size < _capacity";

    // Ordering (tracked via std::sort)
    ConceptRegistry["sorted"] = "_sorted == 1";

    // Pointer state
    ConceptRegistry["nonnull"] = "_nonnull == 1";
    ConceptRegistry["valid"] = "_lifetime == 1";
    ConceptRegistry["freed"] = "_lifetime == 0";
}

// Safe getName() that returns empty StringRef instead of crashing on operators,
// constructors, destructors, and conversion functions.
// getName() asserts Name.isIdentifier() — this guards against that assertion.
static StringRef safeGetName(const NamedDecl *D) {
    return (D && D->getIdentifier()) ? D->getName() : StringRef();
}

// Resolve the class name for annotation lookup.
// In libstdc++, methods like shared_ptr::operator* are actually defined on
// internal base classes (e.g., __shared_ptr_access). When the declaring class
// starts with __, we fall back to the object's actual type (e.g., shared_ptr).
// This mirrors the namespace traversal fix for libc++ (std::__1::move).
static StringRef resolveAnnotationClassName(const CXXMethodDecl *Method,
                                            const Expr *Object) {
    // Prefer the object's actual type over the declaring class.
    // This handles two cases:
    //   1. Internal classes (e.g., __shared_ptr_access) → resolve to shared_ptr
    //   2. Base classes (e.g., basic_istream) → resolve to basic_ifstream
    // Important: strip implicit casts first — Clang inserts UncheckedDerivedToBase
    // casts that change the type from the concrete to a base class.
    if (Object) {
        const Expr *Stripped = Object->IgnoreParenImpCasts();
        QualType ObjType = Stripped->getType();
        // Strip references and pointers
        if (ObjType->isReferenceType())
            ObjType = ObjType.getNonReferenceType();
        if (ObjType->isPointerType())
            ObjType = ObjType->getPointeeType();
        // Strip const/volatile
        ObjType = ObjType.getUnqualifiedType();

        if (const auto *RD = ObjType->getAsCXXRecordDecl()) {
            // For template specializations, get the template name
            if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
                StringRef name = safeGetName(Spec);
                if (!name.empty() && !name.starts_with("__"))
                    return name;
            }
            StringRef name = safeGetName(RD);
            if (!name.empty() && !name.starts_with("__"))
                return name;
        }
    }

    // Fall back to the declaring class
    return safeGetName(Method->getParent());
}

// Check if character is a valid identifier character (alphanumeric or underscore)
static inline bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Check if string is a valid identifier (for concept names)
static inline bool isValidIdentifier(llvm::StringRef s) {
    if (s.empty()) return false;
    // Must start with letter or underscore
    if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
    // Rest must be alphanumeric or underscore
    for (char c : s) {
        if (!isIdentChar(c)) return false;
    }
    return true;
}

// Expand concepts in a constraint string (supports recursive expansion)
// Returns expanded string
// Note: Cycle detection error goes to llvm::errs() (not Clang diagnostics) since
// this is a static function without DiagnosticsEngine access. This is acceptable
// because cycles are rare configuration errors, not code errors.
static std::string expandConcepts(llvm::StringRef input) {
    std::string result = input.str();
    std::string prev;

    // Iterate until fixpoint (no more expansions)
    // Handles nested concepts like: searchable := nonempty && sorted
    // Max 100 iterations catches circular definitions like: a := b, b := a
    constexpr int kMaxExpansionDepth = 100;
    int iterations = 0;

    // Pre-cache concept names to avoid repeated allocations
    llvm::SmallVector<std::pair<std::string, std::string>, 16> concepts;
    for (const auto &entry : ConceptRegistry) {
        concepts.emplace_back(entry.first().str(), entry.second);
    }

    while (result != prev && iterations < kMaxExpansionDepth) {
        prev = result;
        iterations++;

        // Word-boundary replacement for each concept
        for (const auto &[nameStr, expansion] : concepts) {
            size_t pos = 0;
            while ((pos = result.find(nameStr, pos)) != std::string::npos) {
                // Check word boundaries (using identifier characters)
                bool validStart = (pos == 0 || !isIdentChar(result[pos - 1]));
                bool validEnd = (pos + nameStr.size() >= result.size() ||
                                !isIdentChar(result[pos + nameStr.size()]));

                if (validStart && validEnd) {
                    result.replace(pos, nameStr.size(), "(" + expansion + ")");
                    pos += expansion.size() + 2;  // Skip past replacement
                } else {
                    pos += nameStr.size();
                }
            }
        }
    }

    if (iterations >= kMaxExpansionDepth) {
        llvm::errs() << "DTT error: concept expansion depth limit (" << kMaxExpansionDepth
                     << ") exceeded for '" << input << "' - possible circular definition\n";
    }

    return result;
}

// ============================================================
// CONSTRAINT EXPRESSION PARSER
// ============================================================
// Parses constraint strings into ConstraintExpr AST

class ConstraintExprParser {
public:
    // Parse a constraint string into an AST
    // First expands predefined concepts, then parses
    static ConstraintExprPtr parse(llvm::StringRef input) {
        std::string expanded = expandConcepts(input);
        ConstraintExprParser parser(expanded);
        return parser.parseLogicalOr();
    }

    // Parse a simple field or value (no operators)
    static ConstraintExprPtr parseAtom(llvm::StringRef input) {
        ConstraintExprParser parser(input);
        return parser.parseAtomInternal();
    }

private:
    llvm::StringRef input;
    size_t pos = 0;

    ConstraintExprParser(llvm::StringRef s) : input(s), pos(0) {}

    char peek() const {
        return pos < input.size() ? input[pos] : '\0';
    }

    char consume() {
        return pos < input.size() ? input[pos++] : '\0';
    }

    void skipWhitespace() {
        while (pos < input.size() && input[pos] == ' ') pos++;
    }

    bool match(llvm::StringRef expected) {
        skipWhitespace();
        if (input.substr(pos).starts_with(expected)) {
            pos += expected.size();
            return true;
        }
        return false;
    }

    bool isDigit(char c) const { return c >= '0' && c <= '9'; }
    bool isAlpha(char c) const {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '@';
    }
    bool isAlnum(char c) const { return isDigit(c) || isAlpha(c); }

    // Parse identifier (field name, arg0, etc.)
    // Returns the identifier as a substring of the input (zero-copy when possible)
    std::string parseIdentifier() {
        skipWhitespace();
        size_t start = pos;
        // Handle negative numbers
        if (peek() == '-' && pos + 1 < input.size() && isDigit(input[pos + 1])) {
            pos++;
        }
        while (pos < input.size() && (isAlnum(input[pos]) || input[pos] == '.')) {
            pos++;
        }
        return input.substr(start, pos - start).str();
    }

    // Parse a number literal (zero-copy: uses StringRef slice)
    ConstraintExprPtr parseNumber() {
        skipWhitespace();
        size_t start = pos;
        if (peek() == '-') pos++;
        while (pos < input.size() && isDigit(input[pos])) {
            pos++;
        }
        llvm::StringRef numStr = input.substr(start, pos - start);
        if (numStr.empty() || numStr == "-") return nullptr;
        int64_t val;
        if (numStr.getAsInteger(10, val)) return nullptr;
        return std::make_shared<LiteralExpr>(val);
    }

    // Parse a simple identifier (just alphanumeric, no dots)
    // Returns StringRef into input buffer - caller must convert to std::string if needed
    llvm::StringRef parseSimpleIdentifier() {
        skipWhitespace();
        size_t start = pos;
        while (pos < input.size() && isAlnum(input[pos])) {
            pos++;
        }
        return input.substr(start, pos - start);
    }

    // Parse quantifier body: forall(var, lo, hi, body) or exists(var, lo, hi, body)
    // Called after "forall(" or "exists(" has been matched
    ConstraintExprPtr parseQuantifier(bool isForall) {
        // Parse bound variable name - convert to std::string for AST storage
        llvm::StringRef boundVarRef = parseSimpleIdentifier();
        if (boundVarRef.empty()) return nullptr;
        std::string boundVar = boundVarRef.str();

        skipWhitespace();
        if (!match(",")) return nullptr;

        // Parse lower bound
        auto lo = parseAddSub();
        if (!lo) return nullptr;

        skipWhitespace();
        if (!match(",")) return nullptr;

        // Parse upper bound
        auto hi = parseAddSub();
        if (!hi) return nullptr;

        skipWhitespace();
        if (!match(",")) return nullptr;

        // Parse body - save the bound variable so we can recognize it
        // For now, we rely on the body parser recognizing unknown identifiers
        // as potential bound variables (treated as FieldRef temporarily)
        auto body = parseLogicalOr();
        if (!body) return nullptr;

        skipWhitespace();
        match(")");

        if (isForall) {
            return std::make_shared<ForallExpr>(boundVar, lo, hi, body);
        } else {
            return std::make_shared<ExistsExpr>(boundVar, lo, hi, body);
        }
    }

    // Parse atom: number, field, arg, old(...), forall(...), exists(...), etc.
    ConstraintExprPtr parseAtomInternal() {
        skipWhitespace();

        // Handle parentheses
        if (match("(")) {
            auto expr = parseLogicalOr();
            match(")");
            return expr;
        }

        // Handle forall(var, lo, hi, body)
        if (match("forall(")) {
            return parseQuantifier(true);
        }

        // Handle exists(var, lo, hi, body)
        if (match("exists(")) {
            return parseQuantifier(false);
        }

        // Handle old(field) - zero-copy extraction using StringRef
        if (match("old(")) {
            size_t start = pos;
            while (pos < input.size() && input[pos] != ')') {
                pos++;
            }
            llvm::StringRef field = input.substr(start, pos - start).trim();
            match(")");
            return std::make_shared<OldRefExpr>(field.str());
        }

        // Handle nullptr/NULL
        if (match("nullptr") || match("NULL")) {
            return std::make_shared<LiteralExpr>(0);
        }

        // Handle _result
        if (match("_result")) {
            return std::make_shared<ResultRefExpr>();
        }

        // Handle numbers
        if (isDigit(peek()) || (peek() == '-' && pos + 1 < input.size() && isDigit(input[pos + 1]))) {
            return parseNumber();
        }

        // Handle identifiers (fields, args)
        std::string ident = parseIdentifier();
        if (ident.empty()) return nullptr;

        // Check for array subscript: ident[expr]
        skipWhitespace();
        if (peek() == '[') {
            consume();  // consume '['
            auto index = parseAddSub();  // Parse index expression
            skipWhitespace();
            if (peek() == ']') consume();  // consume ']'
            return std::make_shared<IndexExpr>(ident, index);
        }

        // Check for arg pattern: arg0, arg1, arg0._size, etc.
        if (ident.find("arg") == 0) {
            size_t dotPos = ident.find('.');
            if (dotPos != std::string::npos) {
                // arg0._size
                llvm::StringRef argPart(ident.data(), dotPos);
                std::string fieldPart = ident.substr(dotPos + 1);
                unsigned argIdx = 0;
                argPart.drop_front(3).getAsInteger(10, argIdx);
                return std::make_shared<ArgFieldRefExpr>(argIdx, fieldPart);
            } else {
                // arg0
                unsigned argIdx = 0;
                llvm::StringRef(ident).drop_front(3).getAsInteger(10, argIdx);
                return std::make_shared<ArgRefExpr>(argIdx);
            }
        }

        // Regular field reference
        return std::make_shared<FieldRefExpr>(ident);
    }

    // Parse unary: !, -
    ConstraintExprPtr parseUnary() {
        skipWhitespace();
        if (match("!")) {
            auto operand = parseUnary();
            return std::make_shared<UnaryExpr>(ConstraintExpr::LogicalNot, operand);
        }
        return parseAtomInternal();
    }

    // Parse multiplication/division/modulo
    ConstraintExprPtr parseMulDiv() {
        auto left = parseUnary();
        if (!left) return nullptr;
        while (true) {
            skipWhitespace();
            ConstraintExpr::Kind op;
            if (match("*")) op = ConstraintExpr::Mul;
            else if (match("/")) op = ConstraintExpr::Div;
            else if (match("%")) op = ConstraintExpr::Mod;
            // Check for single & (bitwise AND) but not && (logical AND)
            else if (peek() == '&' && pos + 1 < input.size() && input[pos + 1] != '&') {
                consume();  // consume the single &
                op = ConstraintExpr::BitAnd;
            }
            else break;

            auto right = parseUnary();
            if (!right) return nullptr;
            left = std::make_shared<BinaryExpr>(op, left, right);
        }
        return left;
    }

    // Parse addition/subtraction
    ConstraintExprPtr parseAddSub() {
        auto left = parseMulDiv();
        if (!left) return nullptr;
        while (true) {
            skipWhitespace();
            ConstraintExpr::Kind op;
            if (match("+")) op = ConstraintExpr::Add;
            else if (match("-")) op = ConstraintExpr::Sub;
            else break;

            auto right = parseMulDiv();
            if (!right) return nullptr;
            left = std::make_shared<BinaryExpr>(op, left, right);
        }
        return left;
    }

    // Parse comparison: <, <=, >, >=, ==, !=
    ConstraintExprPtr parseComparison() {
        auto left = parseAddSub();
        if (!left) return nullptr;
        skipWhitespace();

        ConstraintExpr::Kind op;
        if (match(">=")) op = ConstraintExpr::CmpGE;
        else if (match("<=")) op = ConstraintExpr::CmpLE;
        else if (match("==")) op = ConstraintExpr::CmpEQ;
        else if (match("!=")) op = ConstraintExpr::CmpNE;
        else if (match(">")) op = ConstraintExpr::CmpGT;
        else if (match("<")) op = ConstraintExpr::CmpLT;
        else return left;  // No comparison operator

        auto right = parseAddSub();
        if (!right) return nullptr;
        return std::make_shared<BinaryExpr>(op, left, right);
    }

    // Parse logical AND
    ConstraintExprPtr parseLogicalAnd() {
        auto left = parseComparison();
        if (!left) return nullptr;
        while (match("&&")) {
            auto right = parseComparison();
            if (!right) return nullptr;
            left = std::make_shared<BinaryExpr>(ConstraintExpr::LogicalAnd, left, right);
        }
        return left;
    }

    // Parse logical OR
    ConstraintExprPtr parseLogicalOr() {
        auto left = parseLogicalAnd();
        if (!left) return nullptr;
        while (match("||")) {
            auto right = parseLogicalAnd();
            if (!right) return nullptr;
            left = std::make_shared<BinaryExpr>(ConstraintExpr::LogicalOr, left, right);
        }
        return left;
    }
};

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
    if (auto *ME = dyn_cast<MemberExpr>(E)) return ME->getMemberDecl()->getName().str();
    return "";
}

bool hasRuntimeIndex(llvm::StringRef path) { return path.find("[?]") != llvm::StringRef::npos; }

// Check if str starts with prefix followed by '.' (e.g., "obj._size" starts with "obj" + ".")
// Avoids string concatenation: replaces `str.rfind(prefix + ".", 0) == 0`
bool startsWithDotPrefix(llvm::StringRef str, llvm::StringRef prefix) {
    return str.size() > prefix.size() && str.starts_with(prefix) && str[prefix.size()] == '.';
}

std::string getArrayBase(llvm::StringRef path) {
    auto pos = path.find('[');
    return (pos == llvm::StringRef::npos) ? "" : path.substr(0, pos).str();
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
                std::string name = variableKey(DRE->getDecl()).str();
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
            std::string name = variableKey(DRE->getDecl()).str();
            if (name == "nothrow" || name == "nothrow_t") {
                return false;  // nothrow new is not placement new
            }
        }
    }
    // Has placement args that are not nothrow - true placement new
    return true;
}

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
llvm::StringMap<NullProvenance> NullProvenanceMap;

// State provenance — linked list of every state change for a tracked field.
// When DTT reports an error, this chain is walked to show the user exactly
// how the field value evolved to reach the failing state.
struct StateProvenance {
    SourceLocation location;
    llvm::SmallString<64> description;  // e.g., "default constructor", "push_back", "clear"
    SymValue value;                     // value after this event
    std::unique_ptr<StateProvenance> previous;  // linked list (single ownership)
};

// Map from "varPath.field" to provenance chain head
llvm::StringMap<std::unique_ptr<StateProvenance>> ProvenanceChains;

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



// Track which (varPath, field) combinations have already warned in current scope
struct WarningKey {
    std::string varPath;
    std::string field;

    bool operator==(const WarningKey& other) const {
        return varPath == other.varPath && field == other.field;
    }
};

} // end anonymous namespace

namespace llvm {
template<> struct DenseMapInfo<WarningKey> {
    static inline WarningKey getEmptyKey() {
        return {"", "\xff\xff"};  // Sentinel: invalid field name
    }
    static inline WarningKey getTombstoneKey() {
        return {"", "\xff\xfe"};  // Different sentinel
    }
    static unsigned getHashValue(const WarningKey& key) {
        return llvm::hash_combine(key.varPath, key.field);
    }
    static bool isEqual(const WarningKey& lhs, const WarningKey& rhs) {
        return lhs == rhs;
    }
};
} // namespace llvm

namespace {

// Maximum iterations for fixpoint algorithms (precondition propagation, frame inference)
constexpr int kMaxFixpointIterations = 100;

llvm::DenseSet<WarningKey> WarnedLostTracking;

// ============================================================
// CONSTRAINT EXPRESSION HELPERS
// ============================================================

auto trim = [](std::string& str) {
    while (!str.empty() && str.front() == ' ') str = str.substr(1);
    while (!str.empty() && str.back() == ' ') str.pop_back();
};

// ============================================================
// CONSTRAINT PARSING
// ============================================================

// Translate parameter names to argN format using function declaration
// e.g., "buf._size >= n" -> "arg0._size >= arg1" for process(int* buf, size_t n)
std::string translateParamNames(llvm::StringRef s, const FunctionDecl* FD) {
    if (!FD) return s.str();

    llvm::SmallString<128> result(s);
    for (unsigned i = 0; i < FD->getNumParams(); i++) {
        llvm::StringRef paramName = FD->getParamDecl(i)->getName();
        if (paramName.empty()) continue;

        // Build "argN" once per parameter
        llvm::SmallString<16> argN("arg");
        llvm::raw_svector_ostream(argN) << i;

        // Replace param._field with argN._field
        size_t pos = 0;
        while ((pos = result.find(paramName, pos)) != llvm::StringRef::npos) {
            // Check this is a complete token (not part of a longer word)
            // Exclude underscore prefix to avoid matching _size when param is "size"
            bool startOK = (pos == 0 || (!std::isalnum(result[pos - 1]) && result[pos - 1] != '_'));
            bool endOK = (pos + paramName.size() >= result.size() ||
                          (!std::isalnum(result[pos + paramName.size()]) &&
                           result[pos + paramName.size()] != '_'));

            if (startOK && endOK) {
                result.erase(result.begin() + pos, result.begin() + pos + paramName.size());
                result.insert(result.begin() + pos, argN.begin(), argN.end());
                pos += argN.size();
            } else {
                pos++;
            }
        }
    }

    // For void FREE functions (not methods) with at least one argument:
    // 1. Translate "result." to "arg0." (void functions use first arg as "result")
    // 2. Prefix bare field names with "arg0."
    // For methods, bare field names like @encrypted refer to `this`, handled separately
    if (FD->getReturnType()->isVoidType() && FD->getNumParams() > 0 && !isa<CXXMethodDecl>(FD)) {
        llvm::StringRef resultRef(result);

        // Translate "result." to "arg0." for void functions
        if (resultRef.starts_with("result.")) {
            llvm::SmallString<64> newResult("arg0.");
            newResult += resultRef.drop_front(7);  // Append everything after "result."
            result = newResult;
        }
        // Check if result has arg prefix - if so, skip bare field handling
        else if (!resultRef.starts_with("arg")) {
            // Prefix bare field names (starting with _ or @) with "arg0."
            if (!result.empty() && (result[0] == '@' || result[0] == '_')) {
                llvm::SmallString<64> newResult("arg0.");
                newResult += resultRef;
                result = newResult;
            }
        }
    }

    return std::string(result);
}

// Parse constraint string directly to ConstraintExpr AST
ConstraintExprPtr parseConstraintExpr(llvm::StringRef s) {
    return ConstraintExprParser::parse(s.trim());
}

// ============================================================
// POSTCONDITIONS (internal types for DTTPlugin.cpp)
// ============================================================

// Target of a postcondition: result or a specific argument
enum class PostconditionTarget { Result, Arg };

struct Postcondition {
    PostconditionTarget target = PostconditionTarget::Result;
    int argIndex = 0;           // Which argument (if target == Arg)
    std::string field;          // Field to set (e.g., "@open", "_size")
    SymValue expr;
    bool usesOld = false;
    std::optional<dtt::PostconditionGuard> guard;  // Optional `when` condition
};

struct BoolPostcondition {
    std::string argRef;
    std::string field;
    ProofOp op;
    int64_t rhs;
    bool onTrue;
};

std::optional<std::string> parseOld(llvm::StringRef s) {
    if (!s.starts_with("old(")) return std::nullopt;
    if (!s.ends_with(")")) return std::nullopt;
    return s.substr(4, s.size() - 5).str();
}

// Parse an expression string to SymValue using the new ConstraintExprParser
SymValue parseExprToSymValue(llvm::StringRef s) {
    auto expr = ConstraintExprParser::parse(s);
    if (expr) {
        auto symVal = expr->toSymValue();
        if (symVal) return *symVal;
    }
    return SymValue::Unknown();
}

std::optional<Postcondition> parsePostcondition(const std::string& s, bool forMethod = false) {
    // First, expand predefined concepts
    // e.g., "sorted" -> "_sorted == 1"
    std::string expanded = expandConcepts(s);

    // Strip outer parentheses if present (expansion adds them)
    while (!expanded.empty() && expanded.front() == '(' && expanded.back() == ')') {
        expanded = expanded.substr(1, expanded.size() - 2);
    }

    auto eqPos = expanded.find("==");
    if (eqPos == std::string::npos) eqPos = expanded.find("=");
    if (eqPos == std::string::npos) return std::nullopt;

    int skip = (expanded.size() > eqPos + 1 && expanded[eqPos + 1] == '=') ? 2 : 1;
    std::string lhs = expanded.substr(0, eqPos);
    std::string rhs = expanded.substr(eqPos + skip);
    trim(lhs); trim(rhs);

    Postcondition p;
    p.target = PostconditionTarget::Result;  // Default
    p.argIndex = 0;

    llvm::StringRef lhsRef(lhs);

    // Unified syntax for all functions:
    // - result.@field  -> applies to return value
    // - arg0.@field    -> applies to argument 0
    // - arg1.@field    -> applies to argument 1
    // - @field or _field (methods only) -> applies to 'this'

    if (lhsRef.starts_with("result.")) {
        p.target = PostconditionTarget::Result;
        p.field = lhsRef.drop_front(7).str();
    } else if (lhsRef.starts_with("arg")) {
        // Parse argN.field format
        llvm::StringRef rest = lhsRef.drop_front(3);  // Drop "arg"
        size_t dotPos = rest.find('.');
        if (dotPos == llvm::StringRef::npos) return std::nullopt;

        llvm::StringRef indexStr = rest.substr(0, dotPos);
        unsigned argIdx = 0;
        if (indexStr.getAsInteger(10, argIdx)) return std::nullopt;

        p.target = PostconditionTarget::Arg;
        p.argIndex = argIdx;
        p.field = rest.substr(dotPos + 1).str();
    } else if (forMethod) {
        // Method-only: bare field names apply to 'this'
        if (!lhs.empty() && (lhs[0] == '_' || lhs[0] == '@')) {
            p.field = lhs;
        } else {
            p.field = lhs;
        }
    } else {
        // For non-method functions, require explicit target
        // (void function bare fields are translated by translateParamNames)
        return std::nullopt;
    }

    // Check for old() references
    if (rhs.find("old(") != std::string::npos) {
        p.usesOld = true;
    }

    // Use expression parser for all RHS expressions
    // This handles: arg0, arg0._size, arg0._size + arg1._size, 5, etc.
    p.expr = parseExprToSymValue(rhs);
    return p;
}

std::optional<BoolPostcondition> parseBoolPostcondition(const std::string& s, bool onTrue) {
    BoolPostcondition bp;
    bp.onTrue = onTrue;
    
    size_t opPos = std::string::npos;
    int opLen = 1;
    
    if ((opPos = s.find("==")) != std::string::npos) { bp.op = ProofOp::EQ; opLen = 2; }
    else if ((opPos = s.find("!=")) != std::string::npos) { bp.op = ProofOp::NE; opLen = 2; }
    else if ((opPos = s.find(">=")) != std::string::npos) { bp.op = ProofOp::GE; opLen = 2; }
    else if ((opPos = s.find("<=")) != std::string::npos) { bp.op = ProofOp::LE; opLen = 2; }
    else if ((opPos = s.find(">")) != std::string::npos) { bp.op = ProofOp::GT; opLen = 1; }
    else if ((opPos = s.find("<")) != std::string::npos) { bp.op = ProofOp::LT; opLen = 1; }
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



llvm::StringMap<llvm::SmallVector<Postcondition, 2>> FnPostconditions;
llvm::StringMap<llvm::SmallVector<BoolPostcondition, 2>> FnBoolPostconditions;
llvm::StringSet<> TotalFunctions;

// ============================================================
// RESOURCE-CENTRIC TRACKING
// ============================================================
// Track resources (allocations) separately from variables (handles).
// Multiple variables can point to the same resource.
// Resource identity is the allocation site (SourceLocation).

// Resource ID: unique identifier for each resource (allocation site)
// Using SourceLocation directly - zero allocation, efficient comparison
using ResourceId = SourceLocation;


// ============================================================
// HEAP OBJECT INITIALIZATION TRACKING (Tier 5.2)
// ============================================================
// Track initialization state per ALLOCATION SITE, not per variable.
// This solves aliasing naturally - all pointers to the same allocation
// share the same initialization state.
//
// Example:
//   int* p = new int;   // rid=loc1, uninitialized
//   int* q = p;         // q points to same rid=loc1
//   *q = 42;            // marks loc1 as initialized
//   int x = *p;         // OK: loc1 is initialized (via q)

struct HeapObjectState {
    bool initialized;           // Is the pointed-to value initialized?
    SourceLocation allocLoc;    // Where allocated (for diagnostics)
};

// ============================================================
// STACK ALIASING TRACKING (Tier 5.1)
// ============================================================
// Track when pointers point to stack variables, so writing through
// the pointer can mark the stack variable as initialized.
//
// Example:
//   int x;              // x uninitialized
//   int* p = &x;        // rid=x.loc, VarToResource["p"]=rid, StackVarPaths[rid]="x"
//   *p = 42;            // rid=VarToResource["p"], varPath=StackVarPaths[rid]
//                       // setField(varPath, "_initialized", 1)
//   return x;           // OK: x is initialized via alias
//
// Uses VarToResource for pointer->resource mapping (shared with heap tracking)
// StackVarPaths provides reverse lookup: ResourceId -> stack variable path


// ============================================================
// SCOPE-EXIT CONSTRAINTS (~requires)
// ============================================================
// Scope-exit constraints specify conditions that must hold when a variable
// goes out of scope. This implements linear type-like resource tracking.
//
// Example:
//   [[clang::annotate("dtt:ensures=result.@open == 1")]]
//   [[clang::annotate("dtt:~requires=result.@open == 0")]]
//   FILE* fopen(const char*, const char*);
//
// When fopen() is called, the result has @open == 1 and must have @open == 0
// at scope exit (i.e., fclose must be called before the variable goes out of scope).

// Scope-exit requirement: a constraint that must hold at scope exit
struct ScopeExitRequirement {
    llvm::SmallString<16> field;  // Ghost field to check (e.g., "@open")
    ProofOp op;                   // Comparison operator
    int64_t requiredValue;        // Required value at scope exit
    SourceLocation declLoc;       // Where the variable was declared
    bool isResult = true;         // true = applies to result, false = applies to argN
    int argIndex = 0;             // Which argument (when isResult == false)
    std::optional<dtt::PostconditionGuard> guard;  // Optional `when` condition
};

// Per-function: function name -> scope-exit requirements for result
llvm::StringMap<llvm::SmallVector<ScopeExitRequirement, 2>> FnScopeExitRequires;

// Per-function tracking: resource -> scope-exit constraints
// Cleared at function boundaries
struct ScopeExitConstraint {
    llvm::SmallString<32> varPath;  // Variable path (e.g., "f") - for error messages
    ResourceId resourceId;          // Resource this constraint applies to (SourceLocation)
    llvm::SmallString<16> field;    // Ghost field (e.g., "@open")
    ProofOp op;                     // Required comparison
    int64_t requiredValue;          // Required value at exit
    SourceLocation acquireLoc;      // Where resource was acquired (for diagnostics)
    unsigned scopeDepth;            // Scope depth when constraint was registered
    std::optional<dtt::PostconditionGuard> guard;  // Optional `when` condition from .dtt
    unsigned callVisit = 0;         // Which evaluation of the call acquired it
};

// Pending postconditions from `when`-guarded result annotations.
// When a `when` guard isn't satisfied at call time (e.g., fopen's
// `ensures result.@open = 1 when result._nonnull != 0`), the postcondition
// is stored here. When a proof later satisfies the guard (e.g., entering
// `if (f)` proves _nonnull!=0), the postcondition is applied retroactively.
// Entries are never consumed — application is idempotent.
// Cleared at function boundaries in clearPerFunctionState().
struct PendingGuardedPostcondition {
    llvm::SmallString<32> targetPath;   // Variable path (e.g., "f")
    llvm::SmallString<16> field;        // Ghost field (e.g., "@open")
    int64_t value;                      // Value to set (e.g., 1)
    dtt::PostconditionGuard guard;      // The unsatisfied when-guard
    llvm::SmallString<32> calleeName;   // Function that produced this (e.g., "fopen")
    SourceLocation callLoc;             // Location of the call
    bool applied = false;               // Whether this was retroactively applied
    bool refuted = false;               // Whether the guard is proven false (no resource)
    unsigned callVisit = 0;             // Which evaluation of the call deferred it
};


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
    llvm::SmallVector<CallSiteInfo, 4> callSites;

    // Backward analysis: return statements in this function
    llvm::SmallVector<GuardedReturn, 2> returnStmts;

    // Variable nullability tracking (Gap 1)
    // Maps variable name -> nonnull status within this function
    llvm::StringMap<std::optional<bool>> varNullability;

    // Computed results (populated between passes)
    bool analyzed = false;
    bool returnsComputed = false;  // Track if returns were computed (for chaining)
    llvm::DenseMap<unsigned, std::optional<bool>> paramNonnull;  // paramIdx -> nonnull?
    std::optional<bool> returnsNonnull;                     // all returns nonnull?

    // Call-site analysis: track which params are dereferenced (for static functions)
    llvm::SmallDenseSet<unsigned, 8> paramDereferenced;  // param indices that are dereferenced in body

    // Frame inference: track which fields are written by this function
    // Key: parameter index, Value: set of field names written through that param
    // E.g., paramWrites[0] = {"_size", "_data"} means param 0's _size and _data are written
    llvm::DenseMap<unsigned, llvm::StringSet<>> paramWrites;

    // Track if this function is "pure" (no writes to tracked state via any param)
    bool isPure = true;
};

// Storage: qualified function name -> analysis data
// Using std::map for stable iteration order and structured binding compatibility
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
int getParamIndex(const FunctionDecl* F, StringRef path) {
    if (!F) return -1;
    for (unsigned i = 0; i < F->getNumParams(); i++) {
        if (F->getParamDecl(i)->getName() == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Record a field write for frame inference.
/// When code writes to param.field, we record that this function modifies
/// that field of that parameter. Fields NOT recorded are in the "frame".
void recordParamFieldWrite(const FunctionDecl* F, StringRef basePath,
                           StringRef field) {
    if (!F || basePath.empty()) return;

    // Extract parameter name from "param" or "param.subfield"
    StringRef paramName = basePath.split('.').first;

    int paramIdx = getParamIndex(F, paramName);
    if (paramIdx < 0) return;

    std::string funcKey = getFunctionKey(F);
    auto& data = FunctionAnalysis[funcKey];
    data.paramWrites[static_cast<unsigned>(paramIdx)].insert(field);
    data.isPure = false;
}

// ============================================================
// METHOD ANNOTATIONS (internal types for DTTPlugin.cpp)
// ============================================================

// Typed key for method annotations - avoids string concatenation
struct MethodKey {
    std::string className;
    std::string methodName;
    unsigned numParams;

    bool operator==(const MethodKey& other) const {
        return numParams == other.numParams && className == other.className && methodName == other.methodName;
    }
};

} // end anonymous namespace

namespace llvm {
template<> struct DenseMapInfo<MethodKey> {
    static inline MethodKey getEmptyKey() {
        return {"", "", UINT_MAX};
    }
    static inline MethodKey getTombstoneKey() {
        return {"", "", UINT_MAX - 1};
    }
    static unsigned getHashValue(const MethodKey& key) {
        return llvm::hash_combine(key.className, key.methodName, key.numParams);
    }
    static bool isEqual(const MethodKey& lhs, const MethodKey& rhs) {
        return lhs == rhs;
    }
};
} // end namespace llvm

namespace {

// Method annotations storage
struct MethodInfo {
    llvm::SmallVector<ConstraintExprPtr, 2> preconditions;  // Parsed constraint AST
    llvm::SmallVector<Postcondition, 2> ensures;
    std::string returns;  // dtt:returns=_size
    llvm::SmallVector<BoolPostcondition, 2> boolPostconds;
};
llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;

// Free function annotations (for C library functions)
using FreeFunctionKey = dtt::FreeFunctionKey;
using FreeFunctionInfo = dtt::FreeFunctionInfo;
llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;
llvm::DenseSet<llvm::StringRef> FreeFunctionAnnotatedNames;  // O(1) name lookup for arity mismatch

// Convert dtt::ConstraintExprPtr to internal ConstraintExprPtr
// ConstraintExpr is now a single hierarchy (dtt::ConstraintExpr in DTTTypes.h).
// No conversion needed — dtt::ConstraintExprPtr IS ConstraintExprPtr.

// Convert dtt:: annotation types to internal types
bool loadSyntheticAnnotations(DiagnosticsEngine &Diag) {
    if (!dtt::registerBuiltinAnnotations()) {
        Diag.Report(Diag.getCustomDiagID(DiagnosticsEngine::Error,
                    "DTT: failed to load built-in annotations"));
        return false;
    }

    // Copy from dtt::MethodAnnotations to internal MethodAnnotations
    for (const auto& [key, info] : dtt::MethodAnnotations) {
        MethodKey internalKey{key.className, key.methodName, key.numParams};
        MethodInfo internalInfo;

        // Preconditions use the shared dtt::ConstraintExpr — no conversion needed
        for (const auto& pre : info.preconditions) {
            if (pre) internalInfo.preconditions.push_back(pre);
        }

        // Copy postconditions - convert from dtt:: types to internal types
        for (const auto& post : info.ensures) {
            Postcondition internalPost;
            internalPost.target = post.target == dtt::PostconditionTarget::Result
                ? PostconditionTarget::Result : PostconditionTarget::Arg;
            internalPost.argIndex = post.argIndex;
            internalPost.field = post.field;
            internalPost.usesOld = post.usesOld;
            internalPost.guard = post.guard;
            // Convert dtt::SymValue to internal SymValue
            internalPost.expr = [&]() -> SymValue {
                switch (post.expr.kind) {
                    case dtt::SymValue::CONST:
                        return SymValue::Const(post.expr.constVal);
                    case dtt::SymValue::VAR:
                        return SymValue::Var(post.expr.varRef);
                    case dtt::SymValue::ADD:
                        if (post.expr.left && post.expr.right) {
                            auto left = post.expr.left->kind == dtt::SymValue::CONST
                                ? SymValue::Const(post.expr.left->constVal)
                                : SymValue::Var(post.expr.left->varRef);
                            auto right = post.expr.right->kind == dtt::SymValue::CONST
                                ? SymValue::Const(post.expr.right->constVal)
                                : SymValue::Var(post.expr.right->varRef);
                            return SymValue::Add(left, right);
                        }
                        return SymValue::Unknown();
                    case dtt::SymValue::SUB:
                        if (post.expr.left && post.expr.right) {
                            auto left = post.expr.left->kind == dtt::SymValue::CONST
                                ? SymValue::Const(post.expr.left->constVal)
                                : SymValue::Var(post.expr.left->varRef);
                            auto right = post.expr.right->kind == dtt::SymValue::CONST
                                ? SymValue::Const(post.expr.right->constVal)
                                : SymValue::Var(post.expr.right->varRef);
                            return SymValue::Sub(left, right);
                        }
                        return SymValue::Unknown();
                    default:
                        return SymValue::Unknown();
                }
            }();
            internalInfo.ensures.push_back(internalPost);
        }

        internalInfo.returns = info.returns;

        // Convert bool postconditions
        for (const auto& bp : info.boolPostconds) {
            internalInfo.boolPostconds.push_back({
                bp.argRef, bp.field, bp.op, bp.rhs, bp.onTrue
            });
        }

        MethodAnnotations[internalKey] = internalInfo;
    }

    // Copy free function annotations (C library)
    for (const auto& [key, info] : dtt::FreeFunctionAnnotations) {
        FreeFunctionAnnotations[key] = info;
        FreeFunctionAnnotatedNames.insert(StringRef(key.functionName));

        // Copy scope-exit requirements to FnScopeExitRequires for resource tracking
        for (const auto& req : info.scopeExitRequires) {
            ScopeExitRequirement internalReq;
            internalReq.field = req.field;
            internalReq.op = req.op;
            internalReq.requiredValue = req.requiredValue;
            internalReq.isResult = req.isResult;
            internalReq.argIndex = req.argIndex;
            internalReq.guard = req.guard;
            FnScopeExitRequires[key.functionName].push_back(std::move(internalReq));
        }
    }

    // Load external .dtt annotation files (user-specified via --dtt-annotations=)
    for (const auto &path : ExternalAnnotationFiles) {
        if (!dtt::parseAnnotationFile(path, dtt::MethodAnnotations,
                                      dtt::FreeFunctionAnnotations,
                                      dtt::TemplateAliases)) {
            auto ID = Diag.getCustomDiagID(DiagnosticsEngine::Error,
                                          "DTT: failed to load annotation file '%0'");
            Diag.Report(ID) << path;
            return false;
        }
        // Re-copy newly added entries from dtt:: maps to internal maps
        // (external files may add both method and free function annotations)
        for (const auto &[key, info] : dtt::MethodAnnotations) {
            MethodKey internalKey{key.className, key.methodName, key.numParams};
            if (MethodAnnotations.count(internalKey)) continue;  // Already loaded
            MethodInfo internalInfo;
            for (const auto &pre : info.preconditions) {
                if (pre) internalInfo.preconditions.push_back(pre);
            }
            for (const auto &post : info.ensures) {
                Postcondition internalPost;
                internalPost.target = post.target == dtt::PostconditionTarget::Result
                    ? PostconditionTarget::Result : PostconditionTarget::Arg;
                internalPost.argIndex = post.argIndex;
                internalPost.field = post.field;
                internalPost.usesOld = post.usesOld;
                internalPost.guard = post.guard;
                internalPost.expr = [&]() -> SymValue {
                    switch (post.expr.kind) {
                        case dtt::SymValue::CONST:
                            return SymValue::Const(post.expr.constVal);
                        case dtt::SymValue::VAR:
                            return SymValue::Var(post.expr.varRef);
                        case dtt::SymValue::ADD:
                            if (post.expr.left && post.expr.right) {
                                auto left = post.expr.left->kind == dtt::SymValue::CONST
                                    ? SymValue::Const(post.expr.left->constVal)
                                    : SymValue::Var(post.expr.left->varRef);
                                auto right = post.expr.right->kind == dtt::SymValue::CONST
                                    ? SymValue::Const(post.expr.right->constVal)
                                    : SymValue::Var(post.expr.right->varRef);
                                return SymValue::Add(left, right);
                            }
                            return SymValue::Unknown();
                        case dtt::SymValue::SUB:
                            if (post.expr.left && post.expr.right) {
                                auto left = post.expr.left->kind == dtt::SymValue::CONST
                                    ? SymValue::Const(post.expr.left->constVal)
                                    : SymValue::Var(post.expr.left->varRef);
                                auto right = post.expr.right->kind == dtt::SymValue::CONST
                                    ? SymValue::Const(post.expr.right->constVal)
                                    : SymValue::Var(post.expr.right->varRef);
                                return SymValue::Sub(left, right);
                            }
                            return SymValue::Unknown();
                        default:
                            return SymValue::Unknown();
                    }
                }();
                internalInfo.ensures.push_back(internalPost);
            }
            internalInfo.returns = info.returns;
            for (const auto &bp : info.boolPostconds) {
                internalInfo.boolPostconds.push_back({
                    bp.argRef, bp.field, bp.op, bp.rhs, bp.onTrue
                });
            }
            MethodAnnotations[internalKey] = internalInfo;
        }
        for (const auto &[key, info] : dtt::FreeFunctionAnnotations) {
            if (FreeFunctionAnnotations.count(key)) continue;
            FreeFunctionAnnotations[key] = info;
            FreeFunctionAnnotatedNames.insert(StringRef(key.functionName));
            for (const auto &req : info.scopeExitRequires) {
                ScopeExitRequirement internalReq;
                internalReq.field = req.field;
                internalReq.op = req.op;
                internalReq.requiredValue = req.requiredValue;
                internalReq.isResult = req.isResult;
                internalReq.argIndex = req.argIndex;
                internalReq.guard = req.guard;
                FnScopeExitRequires[key.functionName].push_back(std::move(internalReq));
            }
        }
    }
    return true;
}

} // end anonymous namespace

namespace {

// Variable bounds proofs - O(1) lookup via DenseMap
// Key: (varName, objPath, field) -> ProofOp (LT means var < obj.field)
struct VarBoundKey {
    std::string varName;
    std::string objPath;
    std::string field;

    bool operator==(const VarBoundKey& other) const {
        return varName == other.varName && objPath == other.objPath && field == other.field;
    }
};
} // end anonymous namespace

namespace llvm {
template<> struct DenseMapInfo<VarBoundKey> {
    static inline VarBoundKey getEmptyKey() {
        return {"", "", "\xff\xff"};  // Sentinel: invalid field name
    }
    static inline VarBoundKey getTombstoneKey() {
        return {"", "", "\xff\xfe"};  // Different sentinel
    }
    static unsigned getHashValue(const VarBoundKey& key) {
        return llvm::hash_combine(key.varName, key.objPath, key.field);
    }
    static bool isEqual(const VarBoundKey& lhs, const VarBoundKey& rhs) {
        return lhs == rhs;
    }
};
} // namespace llvm

namespace {


// ============================================================
// INFERRED PRECONDITIONS (for same-TU backward inference)
// ============================================================
// Stores preconditions inferred from function implementations.
// Persists across functions within the TU for call-site checking.

llvm::DenseMap<const clang::FunctionDecl*, llvm::SmallVector<dtt::InferredPrecondition, 4>> FnPreconditions;
llvm::SmallVector<dtt::InferredPrecondition, 4> CurrentFnPreconditions;
const clang::FunctionDecl* CurrentFnForPreconditions = nullptr;

// ============================================================
// POINTER BASE ORIGIN TRACKING (Phase 3)
// ============================================================
// Tracks the original base pointer and accumulated offset for pointers
// derived from pointer arithmetic. Enables rewriting preconditions like
// "p._size_bytes >= n" to "base._size_bytes >= n + off" when p = base + off.

struct BaseOriginInfo {
    FieldPath basePath;                      // Path to original base pointer
    SymValue offsetBytes;                    // Accumulated byte offset from base
    std::optional<unsigned> baseParamIdx;    // Parameter index if base is a parameter
    std::optional<unsigned> offsetParamIdx;  // Parameter index if offset is a simple parameter
    int64_t offsetElemSize = 1;              // Element size in bytes (for scaling at call sites)
    bool offsetNegated = false;              // True if offset was subtracted (p = buf - off)

    BaseOriginInfo() : offsetBytes(SymValue::Const(0)) {}
    BaseOriginInfo(FieldPath path, SymValue offset, std::optional<unsigned> paramIdx,
                   std::optional<unsigned> offParamIdx = std::nullopt,
                   int64_t elemSize = 1, bool negated = false)
        : basePath(std::move(path)), offsetBytes(std::move(offset)),
          baseParamIdx(paramIdx), offsetParamIdx(offParamIdx),
          offsetElemSize(elemSize), offsetNegated(negated) {}
};


// ============================================================
// PROGRAM STATE
// ============================================================
// Everything DTT knows at the current program point of the function being
// analyzed. Branches copy it, and paths that meet join it (joinStates), so
// every flow-sensitive store must be a member.
struct ProgramState {
    // Field values, keyed by FieldPath ("v._size", "f.@open")
    llvm::DenseMap<FieldPath, SymValue> TypedEnv;
    // Structured facts from guards and contracts ("i._value < 10")
    ProofStore GlobalProofs;
    // Variable bounds relative to an object field (i < v.size())
    llvm::DenseMap<VarBoundKey, ProofOp> VarBoundProofs;
    // "varPath.field" -> tracking state
    llvm::StringMap<FieldState> FieldStates;
    // std::any variable path -> canonical type of the contained value
    llvm::StringMap<QualType> AnyContainedTypes;
    // Pointer path -> lifetime (use-after-free, double free)
    llvm::StringMap<PointerLifetimeState> PointerLifetimes;
    // Release obligations checked at scope exit (~requires)
    llvm::SmallVector<ScopeExitConstraint, 4> CurrentScopeExitConstraints;
    // `when`-guarded postconditions waiting for their guard
    llvm::SmallVector<PendingGuardedPostcondition, 4> PendingGuardedPostconditions;
    // Variable -> resource (allocation site) it points to
    llvm::StringMap<ResourceId> VarToResource;
    // Resource -> initialization state of the pointed-to heap object
    llvm::DenseMap<ResourceId, HeapObjectState> HeapObjectStates;
    // Resource -> stack variable it is the address of
    llvm::DenseMap<ResourceId, llvm::SmallString<32>> StackVarPaths;
    // Alias -> source pointer ("alias" -> "ptr" after alias = ptr)
    llvm::StringMap<std::string> PointerAliases;
    // Array -> known element values from a literal initializer
    llvm::StringMap<llvm::SmallVector<int64_t, 16>> ArrayElementFacts;
    // Pointer -> the base it was derived from, with the offset
    llvm::DenseMap<FieldPath, BaseOriginInfo> PointerBaseOrigins;
    // No path reaches this point: it follows a return, break, continue, goto,
    // throw or call that does not return.
    bool Unreachable = false;
};

ProgramState State;

// Counts evaluations of calls: a loop evaluates the same call again, and
// each evaluation acquires a new resource.
unsigned CallVisit = 0;

// Provides canProve() with entailment reasoning over State.GlobalProofs
ProofContext GlobalProofContext(State.GlobalProofs);

// Count handles to a resource (excluding a specific variable)
inline unsigned countOtherHandles(ResourceId rid, llvm::StringRef excludeVar) {
    unsigned count = 0;
    for (const auto& entry : State.VarToResource) {
        if (entry.second == rid && entry.first() != excludeVar) {
            ++count;
        }
    }
    return count;
}

// Create and enqueue a deferred postcondition for later guard evaluation.
void deferPostcondition(llvm::StringRef targetPath, llvm::StringRef field,
                        int64_t value, const dtt::PostconditionGuard& guard,
                        llvm::StringRef calleeName, SourceLocation callLoc) {
    // One evaluation defers each postcondition once, even when several
    // contracts declare it (libc.dtt: open(2) and open(3)).
    auto& Pending = State.PendingGuardedPostconditions;
    if (llvm::any_of(Pending, [&](const PendingGuardedPostcondition& q) {
            return q.callVisit == CallVisit && q.callLoc == callLoc && q.targetPath == targetPath &&
                   q.field == field;
        }))
        return;
    // A new acquisition into the target replaces what an earlier one (a
    // previous loop pass) left there; its release was checked at the call.
    llvm::erase_if(Pending, [&](const PendingGuardedPostcondition& q) {
        return q.targetPath == targetPath && q.field == field;
    });
    State.TypedEnv.erase(FieldPath::fromSynthetic(targetPath).withSynthetic(field));
    PendingGuardedPostcondition p;
    p.targetPath = targetPath;
    p.field = field;
    p.value = value;
    p.guard = guard;
    p.calleeName = calleeName;
    p.callLoc = callLoc;
    p.callVisit = CallVisit;
    Pending.push_back(std::move(p));
}

void beginFunctionPreconditions(const clang::FunctionDecl* FD) {
    // Use canonical decl for consistent lookup across declarations
    CurrentFnForPreconditions = FD ? FD->getCanonicalDecl() : nullptr;
    CurrentFnPreconditions.clear();
    State.PointerBaseOrigins.clear();
}

void addInferredPrecondition(const std::string& path, dtt::PreconditionKind kind,
                              const std::string& boundStr,
                              std::optional<unsigned> argIndex,
                              std::optional<unsigned> boundArgIndex = std::nullopt,
                              int64_t boundConstant = 0) {
    // Avoid duplicates: require same path, kind, AND bound expression to dedupe
    // Different bound expressions (n vs n+8) must be kept as separate preconditions
    for (const auto& P : CurrentFnPreconditions) {
        if (P.path == path && P.kind == kind && P.boundStr == boundStr) return;
    }
    CurrentFnPreconditions.push_back({path, kind, boundStr, argIndex, boundArgIndex, boundConstant});
}

void endFunctionPreconditions() {
    if (CurrentFnForPreconditions && !CurrentFnPreconditions.empty()) {
        FnPreconditions[CurrentFnForPreconditions] = std::move(CurrentFnPreconditions);
    }
    CurrentFnForPreconditions = nullptr;
    CurrentFnPreconditions.clear();
}

// Helper to convert ProofOp to string for diagnostics
const char* proofOpToString(ProofOp op) {
    switch (op) {
        case ProofOp::EQ: return "==";
        case ProofOp::NE: return "!=";
        case ProofOp::GT: return ">";
        case ProofOp::GE: return ">=";
        case ProofOp::LT: return "<";
        case ProofOp::LE: return "<=";
    }
    return "?";
}

// Check if a ProofOp comparison is satisfied
static ProofOp negateProofOp(ProofOp op) {
    switch (op) {
        case ProofOp::EQ: return ProofOp::NE;
        case ProofOp::NE: return ProofOp::EQ;
        case ProofOp::GT: return ProofOp::LE;
        case ProofOp::GE: return ProofOp::LT;
        case ProofOp::LT: return ProofOp::GE;
        case ProofOp::LE: return ProofOp::GT;
    }
    llvm_unreachable("all ProofOp cases handled");
}

bool checkProofOp(int64_t actual, ProofOp op, int64_t required) {
    switch (op) {
        case ProofOp::EQ: return actual == required;
        case ProofOp::NE: return actual != required;
        case ProofOp::GT: return actual > required;
        case ProofOp::GE: return actual >= required;
        case ProofOp::LT: return actual < required;
        case ProofOp::LE: return actual <= required;
    }
    return false;
}

// ============================================================
// INFERRED POSTCONDITIONS (for automatic postcondition inference)
// ============================================================
// Stores postconditions inferred from method implementations.
// When DTT sees _size = n in a method body, it infers ensures=_size == argN.
// Persists across functions within the TU for call-site application.

llvm::DenseMap<const clang::FunctionDecl*, llvm::SmallVector<dtt::InferredPostcondition, 4>> InferredFnPostconditions;
llvm::SmallVector<dtt::InferredPostcondition, 4> CurrentInferredPostconditions;
const clang::FunctionDecl* CurrentFnForPostconditions = nullptr;
const clang::CXXRecordDecl* CurrentMethodClass = nullptr;  // For tracking 'this' in methods

void beginFunctionPostconditions(const clang::FunctionDecl* FD) {
    // Use canonical decl for consistent lookup across declarations
    CurrentFnForPostconditions = FD ? FD->getCanonicalDecl() : nullptr;
    CurrentInferredPostconditions.clear();

    // Track if this is a method (has implicit 'this')
    if (FD) {
        if (auto* MD = dyn_cast<clang::CXXMethodDecl>(FD)) {
            if (!MD->isStatic()) {
                CurrentMethodClass = MD->getParent();
            } else {
                CurrentMethodClass = nullptr;
            }
        } else {
            CurrentMethodClass = nullptr;
        }
    }
}

void addInferredPostcondition(const std::string& field, SymValue expr,
                              bool usesOld = false,
                              std::optional<unsigned> exprArgIndex = std::nullopt,
                              std::optional<unsigned> targetArgIndex = std::nullopt) {
    // Avoid duplicates: same target+field gets updated, keep the latest
    for (auto& P : CurrentInferredPostconditions) {
        if (P.field == field && P.targetArgIndex == targetArgIndex) {
            P.expr = expr;
            P.usesOld = usesOld;
            P.exprArgIndex = exprArgIndex;
            return;
        }
    }
    CurrentInferredPostconditions.push_back({field, expr, usesOld, exprArgIndex, targetArgIndex});
}

void endFunctionPostconditions() {
    if (CurrentFnForPostconditions && !CurrentInferredPostconditions.empty()) {
        InferredFnPostconditions[CurrentFnForPostconditions] = std::move(CurrentInferredPostconditions);
    }
    CurrentFnForPostconditions = nullptr;
    CurrentInferredPostconditions.clear();
    CurrentMethodClass = nullptr;
}

// Helper to find parameter index for an expression that refers to a function parameter
std::optional<unsigned> getParameterIndex(const clang::Expr* E, const clang::FunctionDecl* FD) {
    if (!FD || !E) return std::nullopt;
    E = E->IgnoreParenImpCasts();

    // Direct reference to parameter
    if (auto* DRE = dyn_cast<clang::DeclRefExpr>(E)) {
        if (auto* PVD = dyn_cast<clang::ParmVarDecl>(DRE->getDecl())) {
            for (unsigned i = 0; i < FD->getNumParams(); i++) {
                if (FD->getParamDecl(i) == PVD) {
                    return i;
                }
            }
        }
    }

    // Member access on parameter: param->field or param.field
    if (auto* ME = dyn_cast<clang::MemberExpr>(E)) {
        return getParameterIndex(ME->getBase(), FD);
    }

    // Array subscript on parameter: param[i]
    if (auto* ASE = dyn_cast<clang::ArraySubscriptExpr>(E)) {
        return getParameterIndex(ASE->getBase(), FD);
    }

    // Unary operator on parameter: *param
    if (auto* UO = dyn_cast<clang::UnaryOperator>(E)) {
        return getParameterIndex(UO->getSubExpr(), FD);
    }

    // Binary operator: check left side for parameter (e.g., n + 8, n * 2)
    // This allows tracking size expressions that are arithmetic on a parameter
    if (auto* BO = dyn_cast<clang::BinaryOperator>(E)) {
        // Prefer left operand (typically the parameter in n + const patterns)
        if (auto idx = getParameterIndex(BO->getLHS(), FD)) return idx;
        // Fall back to right operand for unusual patterns like const + n
        return getParameterIndex(BO->getRHS(), FD);
    }

    return std::nullopt;
}

void addVarBoundProof(const std::string& var, const std::string& obj, const std::string& field, ProofOp op) {
    State.VarBoundProofs[{var, obj, field}] = op;
}

void removeVarBoundProof(const std::string& var, const std::string& obj, const std::string& field) {
    State.VarBoundProofs.erase({var, obj, field});
}

bool hasVarBoundProof(const std::string& var, const std::string& obj, const std::string& field, ProofOp op) {
    auto it = State.VarBoundProofs.find({var, obj, field});
    if (it == State.VarBoundProofs.end()) return false;
    // Check if existing proof implies the needed one
    if (it->second == op) return true;
    if (it->second == ProofOp::LT && op == ProofOp::LE) return true;  // < implies <=
    return false;
}

llvm::DenseMap<const CallExpr *, bool> CallRequirements;
bool callRequirementsProven(CallExpr *Call, ASTContext &Ctx);
bool inferredCallRequirementsProven(CallExpr *Call, FunctionDecl *Callee, ASTContext &Ctx);

// Clear all per-function analysis state (called at function entry)
void clearPerFunctionState() {
    State = ProgramState();
    CallRequirements.clear();
    WarnedLostTracking.clear();
    NullProvenanceMap.clear();
    ProvenanceChains.clear();
}

// ============================================================
// FIELDPATH-NATIVE API
// ============================================================
// These functions work directly with FieldPath for type-safe path representation.

// The constant c with V == old(Field) + c, when V has that form.
static std::optional<int64_t> offsetFromOld(const SymValue& V, llvm::StringRef Field) {
    auto IsOld = [&](const SymValue* X) {
        llvm::StringRef Name = X ? llvm::StringRef(X->varRef) : "";
        return X && X->kind == SymValue::VAR && Name.consume_front("old:") && Name == Field;
    };
    if (IsOld(&V)) return 0;
    if ((V.kind == SymValue::ADD || V.kind == SymValue::SUB) && IsOld(V.left.get()) && V.right &&
        V.right->kind == SymValue::CONST)
        return V.kind == SymValue::ADD ? V.right->constVal : -V.right->constVal;
    return std::nullopt;
}

// Where Base.Field is stored: a scalar's own value (_value) at its path.
static FieldPath fieldStorage(const FieldPath& Base, llvm::StringRef Field) {
    return Field == "_value" ? Base : Base.withSynthetic(Field);
}

// Set a field value using FieldPath
void setFieldTyped(const FieldPath& basePath, llvm::StringRef field, SymValue v,
                   SourceLocation loc = SourceLocation(),
                   llvm::StringRef description = "") {
    FieldPath fullPath = fieldStorage(basePath, field);
    State.TypedEnv[fullPath] = v;
    // A pointer given a new buffer no longer points into the array it did
    if (field == "_base_length") State.TypedEnv.erase(basePath.withSynthetic("_base"));

    // FieldStates uses string key for tracking state
    std::string baseStr = basePath.toString();
    std::string key = baseStr + "." + field.str();
    State.FieldStates[key] = FieldState::Known(v);
    WarnedLostTracking.erase({baseStr, field.str()});

    // Record provenance chain entry if location is valid
    if (loc.isValid() && !description.empty()) {
        auto entry = std::make_unique<StateProvenance>();
        entry->location = loc;
        entry->description = description;
        entry->value = v;
        // Link to previous chain entry
        auto it = ProvenanceChains.find(key);
        if (it != ProvenanceChains.end()) {
            entry->previous = std::move(it->second);
        }
        ProvenanceChains[key] = std::move(entry);
    }

    // A new value replaces what was proved about the old one, except what it
    // preserves: everything for old(f), lower bounds for old(f) + c (c >= 0),
    // upper bounds for old(f) - c.
    std::optional<int64_t> Delta = offsetFromOld(v, field);
    State.GlobalProofs.invalidateField(basePath, field.str(), [&](const Proof& P) {
        if (!Delta || P.field != field) return false;
        bool Lower = P.op == ProofOp::GT || P.op == ProofOp::GE;
        bool Upper = P.op == ProofOp::LT || P.op == ProofOp::LE;
        return *Delta == 0 || (*Delta > 0 && Lower) || (*Delta < 0 && Upper);
    });

    // Invalidate VarBoundProofs that reference this field
    llvm::SmallVector<VarBoundKey, 4> toRemove;
    for (const auto& [key, op] : State.VarBoundProofs) {
        if (key.objPath == baseStr && key.field == field) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        State.VarBoundProofs.erase(key);
    }

    // Trigger deferred guard evaluation when _nonnull is set to non-zero.
    // This handles cases where inferNonnullFromExpr sets _nonnull directly
    // (e.g., from `new` expressions or `&` operator) without going through addProof.
    if (field == "_nonnull" && v.kind == SymValue::CONST && v.constVal != 0) {
        for (auto& pending : State.PendingGuardedPostconditions) {
            if (pending.applied) continue;
            if (pending.targetPath != baseStr) continue;
            if (llvm::StringRef(pending.guard.field) != "_nonnull") continue;
            if (checkProofOp(v.constVal, pending.guard.op, pending.guard.value)) {
                pending.applied = true;  // Mark BEFORE recursive call to prevent re-entrancy
                setFieldTyped(basePath, pending.field, SymValue::Const(pending.value));
            }
        }
    }
}

// Set a field and propagate to all aliases
// Used when a function modifies ghost state - changes should affect all aliases
void setFieldWithAliasPropagation(const FieldPath& basePath, const std::string& field,
                                  SymValue v) {
    setFieldTyped(basePath, field, v);

    // Find all pointers that alias this path and update them too
    // PointerAliases maps: alias -> source
    // If basePath is the source, update all aliases
    std::string baseStr = basePath.toString();
    for (const auto& entry : State.PointerAliases) {
        if (entry.second == baseStr) {
            FieldPath aliasPath = FieldPath::fromSynthetic(entry.first());
            setFieldTyped(aliasPath, field, v);
        }
    }
}

// Get a field value using FieldPath
SymValue getFieldTyped(const FieldPath& basePath, llvm::StringRef field) {
    FieldPath fullPath = fieldStorage(basePath, field);
    auto it = State.TypedEnv.find(fullPath);
    return (it != State.TypedEnv.end()) ? it->second : SymValue::Unknown();
}

// String-based wrapper for backward compatibility
SymValue getField(llvm::StringRef basePath, llvm::StringRef field) {
    return getFieldTyped(FieldPath::fromSynthetic(basePath), field);
}

// Check if a FieldPath refers to a ghost field (@ prefix).
// Ghost fields like @open, @allocated, @fd are compile-time-only resource trackers.
static bool isGhostField(const FieldPath& path) {
    if (path.elements.empty()) return false;
    const auto& last = path.elements.back();
    return last.kind == PathElement::SyntheticField &&
           llvm::StringRef(last.syntheticName).starts_with("@");
}

// Whether Path is the entry that records a proof (x._proof__value>=0).
static bool isProofEntry(const FieldPath& Path) {
    return !Path.elements.empty() && Path.elements.back().kind == PathElement::SyntheticField &&
           llvm::StringRef(Path.elements.back().syntheticName).starts_with("_proof_");
}

// Merge a value present in only one branch. Absent means the field kept
// whatever value it had before the branch, so the merge is unknown, except
// that a released ghost resource (0) stays released: absent means no active
// resource on that path either.
static void mergeAbsentField(
    llvm::DenseMap<FieldPath, SymValue>& merged,
    const FieldPath& path, const SymValue& val) {
    // A proof entry counts by its presence: one path's proof is no proof.
    if (isProofEntry(path)) return;
    // A variable's own value stays symbolic: its proofs (joined) bound it.
    if (path.elements.empty()) return;
    bool releasedResource = isGhostField(path) && val.kind == SymValue::CONST &&
                            val.constVal == 0;
    merged[path] = releasedResource ? val : SymValue::Unknown();
}

// Compare two SymValues for equality.
static bool symValuesEqual(const SymValue& a, const SymValue& b) {
    return a.equals(b);
}

// Join two values environments from alternative paths: values both paths
// agree on are kept; anything else becomes Unknown, except that absent
// ghost fields follow mergeAbsentField.
static llvm::DenseMap<FieldPath, SymValue> joinEnv(
    const llvm::DenseMap<FieldPath, SymValue>& branchA,
    const llvm::DenseMap<FieldPath, SymValue>& branchB) {
    llvm::DenseMap<FieldPath, SymValue> merged;

    for (const auto& [path, aVal] : branchA) {
        auto bIt = branchB.find(path);
        if (bIt != branchB.end()) {
            if (symValuesEqual(aVal, bIt->second)) merged[path] = aVal;
            else mergeAbsentField(merged, path, SymValue::Unknown());
        } else {
            mergeAbsentField(merged, path, aVal);
        }
    }

    for (const auto& [path, bVal] : branchB) {
        if (branchA.find(path) == branchA.end()) {
            mergeAbsentField(merged, path, bVal);
        }
    }
    return merged;
}

// ============================================================
// JOINING PATHS
// ============================================================
// Joining the program states of paths that meet (see ProgramState). A branch
// starts from a copy of the state before it; where paths meet, their states
// are joined, keeping only what holds on all of them.

// The entries of A that B has too, with a value Same accepts.
template <typename V, typename SameFn>
static llvm::StringMap<V> commonEntries(const llvm::StringMap<V>& A,
                                        const llvm::StringMap<V>& B, SameFn Same) {
    llvm::StringMap<V> Result;
    for (const auto& E : A) {
        auto It = B.find(E.getKey());
        if (It != B.end() && Same(E.getValue(), It->getValue()))
            Result.insert({E.getKey(), E.getValue()});
    }
    return Result;
}

template <typename K, typename V, typename SameFn>
static llvm::DenseMap<K, V> commonEntries(const llvm::DenseMap<K, V>& A,
                                          const llvm::DenseMap<K, V>& B, SameFn Same) {
    llvm::DenseMap<K, V> Result;
    for (const auto& [Key, Value] : A) {
        auto It = B.find(Key);
        if (It != B.end() && Same(Value, It->second)) Result.insert({Key, Value});
    }
    return Result;
}

static bool isFreed(PointerLifetime L) {
    return L == PointerLifetime::Freed || L == PointerLifetime::MaybeFreed;
}

// One pointer's lifetime after two paths meet. Freed on one path and not on
// the other is MaybeFreed; any other difference is Unknown. Other is null when
// the other path does not track the pointer.
static PointerLifetimeState joinLifetime(PointerLifetimeState S,
                                         const PointerLifetimeState* Other) {
    if (!Other) {
        if (isFreed(S.lifetime)) S.lifetime = PointerLifetime::MaybeFreed;
        return S;
    }
    if (S.lifetime != Other->lifetime) {
        S.lifetime = isFreed(S.lifetime) || isFreed(Other->lifetime)
                         ? PointerLifetime::MaybeFreed : PointerLifetime::Unknown;
        if (!S.freeLocation.isValid()) S.freeLocation = Other->freeLocation;
    }
    if (S.allocSource != Other->allocSource) S.allocSource = AllocSource::Unknown;
    return S;
}

static llvm::StringMap<PointerLifetimeState> joinLifetimes(
    const llvm::StringMap<PointerLifetimeState>& A,
    const llvm::StringMap<PointerLifetimeState>& B) {
    llvm::StringMap<PointerLifetimeState> Result;
    for (const auto& E : A) {
        auto It = B.find(E.getKey());
        Result.insert({E.getKey(),
                       joinLifetime(E.getValue(), It == B.end() ? nullptr : &It->getValue())});
    }
    for (const auto& E : B)
        if (!A.count(E.getKey()))
            Result.insert({E.getKey(), joinLifetime(E.getValue(), nullptr)});
    return Result;
}

// A `when`-guarded postcondition (fopen: @open = 1 when nonnull) is waiting
// on a path where its guard is unknown and applied where the guard held; it is
// refuted where the guard failed, leaving no resource. After two paths meet:
//   refuted on one path          ->  the other path's value and status
//   applied | waiting            ->  applied; the value if it is the
//                                    postcondition's own, else unknown
//   otherwise                    ->  the joined value, applied on both paths
// Env is the joined environment; the first two rows rewrite its entry.
static PendingGuardedPostcondition joinPending(
    const PendingGuardedPostcondition& P, const ProgramState& A,
    const PendingGuardedPostcondition& Q, const ProgramState& B,
    llvm::DenseMap<FieldPath, SymValue>& Env) {
    FieldPath Field = FieldPath::fromSynthetic(P.targetPath).withSynthetic(P.field);
    auto ValueOn = [&](const ProgramState& S) -> const SymValue* {
        auto It = S.TypedEnv.find(Field);
        return It == S.TypedEnv.end() ? nullptr : &It->second;
    };
    if (P.refuted != Q.refuted) {
        const ProgramState& Live = P.refuted ? B : A;
        if (const SymValue* V = ValueOn(Live)) Env[Field] = *V;
        else Env.erase(Field);
        return P.refuted ? Q : P;
    }
    PendingGuardedPostcondition R = P;
    R.applied = P.applied && Q.applied;
    if (P.applied == Q.applied) return R;
    R.applied = true;
    const SymValue* V = ValueOn(P.applied ? A : B);
    bool OwnValue = V && V->kind == SymValue::CONST && V->constVal == P.value;
    Env[Field] = OwnValue ? *V : SymValue::Unknown();
    return R;
}

// The constant value path S holds for Base.Field, if any. A variable's own
// value (_value) is stored at its path, a field's value under the field.
static std::optional<int64_t> constantOn(const ProgramState& S, const FieldPath& Base,
                                         llvm::StringRef Field) {
    auto It = S.TypedEnv.find(fieldStorage(Base, Field));
    if (It == S.TypedEnv.end() || It->second.kind != SymValue::CONST) return std::nullopt;
    return It->second.constVal;
}

// Whether path S already meets C, as checkSingleConstraint would judge it: a
// guard that fails on S, or an unset field when C is guarded and no
// acquisition waits on its guard, means no resource; an unset field means
// released when C requires 0.
static bool meetsScopeExit(const ProgramState& S, const ScopeExitConstraint& C) {
    FieldPath Base = FieldPath::fromSynthetic(C.varPath);
    if (C.guard) {
        Proof Failed{C.guard->field, negateProofOp(C.guard->op), C.guard->value};
        auto V = constantOn(S, Base, Failed.field);
        if (V ? checkProofOp(*V, Failed.op, Failed.value)
              : ProofContext(S.GlobalProofs).canProve(Base, Failed))
            return true;
    }
    auto It = S.TypedEnv.find(Base.withSynthetic(C.field));
    if (It == S.TypedEnv.end()) {
        // An acquisition still waiting on its guard may hold a resource
        bool Waiting = C.guard && llvm::any_of(S.PendingGuardedPostconditions, [&](const auto& P) {
            return P.targetPath == C.varPath && P.field == C.field && !P.refuted;
        });
        return !Waiting && (C.guard || (C.op == ProofOp::EQ && C.requiredValue == 0));
    }
    auto V = It->second.eval();
    return V && checkProofOp(*V, C.op, C.requiredValue);
}

// A range of integer values; an absent end is unbounded.
struct ValueRange {
    std::optional<int64_t> Lo, Hi;
};

// The range path S gives Base.Field from its bound facts.
static ValueRange proofRange(const ProgramState& S, const FieldPath& Base,
                             llvm::StringRef Field = "_value") {
    ValueRange R;
    if (auto *Proofs = S.GlobalProofs.getProofs(Base))
        for (const Proof& P : *Proofs) {
            if (P.field != Field) continue;
            auto tighten = [](std::optional<int64_t>& End, int64_t V, bool Lower) {
                if (!End || (Lower ? V > *End : V < *End)) End = V;
            };
            int64_t Adjacent;
            switch (P.op) {
            case ProofOp::GE: tighten(R.Lo, P.value, true); break;
            case ProofOp::GT:
                if (!__builtin_add_overflow(P.value, 1, &Adjacent)) tighten(R.Lo, Adjacent, true);
                break;
            case ProofOp::LE: tighten(R.Hi, P.value, false); break;
            case ProofOp::LT:
                if (!__builtin_sub_overflow(P.value, 1, &Adjacent)) tighten(R.Hi, Adjacent, false);
                break;
            case ProofOp::EQ: tighten(R.Lo, P.value, true); tighten(R.Hi, P.value, false); break;
            case ProofOp::NE: break;
            }
        }
    return R;
}

// The range of value V on path S: constants, variables by their value or
// bound facts, and +, -, % by a positive constant, & with a mask.
static ValueRange valueRange(const ProgramState& S, const SymValue& V, unsigned Depth = 0) {
    if (Depth > 8) return {};
    auto sub = [&](const std::shared_ptr<SymValue>& X) {
        return X ? valueRange(S, *X, Depth + 1) : ValueRange{};
    };
    switch (V.kind) {
    case SymValue::CONST: return {V.constVal, V.constVal};
    case SymValue::VAR: {
        FieldPath Var = FieldPath::fromSynthetic(V.varRef);
        auto It = S.TypedEnv.find(Var);
        if (It != S.TypedEnv.end()) return valueRange(S, It->second, Depth + 1);
        return proofRange(S, FieldPath::fromSynthetic(Var.toString()));
    }
    case SymValue::ADD: case SymValue::SUB: {
        ValueRange L = sub(V.left), R = sub(V.right);
        auto negate = [](std::optional<int64_t> X) -> std::optional<int64_t> {
            if (!X || *X == std::numeric_limits<int64_t>::min()) return std::nullopt;
            return -*X;
        };
        if (V.kind == SymValue::SUB) R = {negate(R.Hi), negate(R.Lo)};
        auto add = [](std::optional<int64_t> X, std::optional<int64_t> Y) -> std::optional<int64_t> {
            int64_t Out;
            if (!X || !Y || __builtin_add_overflow(*X, *Y, &Out)) return std::nullopt;
            return Out;
        };
        return {add(L.Lo, R.Lo), add(L.Hi, R.Hi)};
    }
    case SymValue::MOD: {
        if (!V.right || V.right->kind != SymValue::CONST || V.right->constVal <= 0) return {};
        int64_t C = V.right->constVal;
        ValueRange L = sub(V.left);
        // C's % truncates: a non-negative dividend gives [0, C-1]
        return {L.Lo && *L.Lo >= 0 ? 0 : -(C - 1), C - 1};
    }
    case SymValue::BITAND: {
        if (V.right && V.right->kind == SymValue::CONST && V.right->constVal >= 0)
            return {0, V.right->constVal};
        return {};
    }
    default: return {};
    }
}

// The range of scalar Var on path S: its value's range, or its bound facts.
static ValueRange scalarRange(const ProgramState& S, const FieldPath& Var) {
    auto It = S.TypedEnv.find(Var);
    if (It != S.TypedEnv.end() && It->second.kind != SymValue::UNKNOWN)
        return valueRange(S, It->second);
    return proofRange(S, FieldPath::fromSynthetic(Var.toString()));
}

// Whether two values are the same, unknown parts included.
static bool sameValue(const SymValue& A, const SymValue& B) {
    if (A.kind != B.kind) return false;
    if (A.kind == SymValue::UNKNOWN) return true;
    auto same = [](const std::shared_ptr<SymValue>& X, const std::shared_ptr<SymValue>& Y) {
        return (!X && !Y) || (X && Y && sameValue(*X, *Y));
    };
    switch (A.kind) {
    case SymValue::CONST: return A.constVal == B.constVal;
    case SymValue::VAR: return A.varRef == B.varRef;
    case SymValue::STRING: return A.strVal == B.strVal;
    default:
        return same(A.cond, B.cond) && same(A.left, B.left) && same(A.right, B.right);
    }
}

// Whether two stores hold the same entries, by Same on the values.
template <typename MapT, typename SameFn>
static bool sameEntries(const MapT& A, const MapT& B, SameFn Same) {
    if (A.size() != B.size()) return false;
    for (const auto& E : A) {
        auto It = B.find(E.first);
        if (It == B.end() || !Same(E.second, It->second)) return false;
    }
    return true;
}
template <typename V, typename SameFn>
static bool sameEntries(const llvm::StringMap<V>& A, const llvm::StringMap<V>& B, SameFn Same) {
    if (A.size() != B.size()) return false;
    for (const auto& E : A) {
        auto It = B.find(E.getKey());
        if (It == B.end() || !Same(E.getValue(), It->getValue())) return false;
    }
    return true;
}

// Whether two program states are the same: a loop head has reached its
// fixpoint when another pass leaves it unchanged.
static bool sameState(const ProgramState& A, const ProgramState& B) {
    if (A.Unreachable != B.Unreachable) return false;
    if (A.Unreachable) return true;
    auto Eq = [](const auto& X, const auto& Y) { return X == Y; };
    auto SameLifetime = [](const PointerLifetimeState& X, const PointerLifetimeState& Y) {
        return X.lifetime == Y.lifetime && X.allocSource == Y.allocSource;
    };
    auto SameConstraints = [](const auto& X, const auto& Y) {
        if (X.size() != Y.size()) return false;
        return llvm::all_of(X, [&](const ScopeExitConstraint& C) {
            return llvm::any_of(Y, [&](const ScopeExitConstraint& D) {
                return C.resourceId == D.resourceId && C.field == D.field && C.op == D.op &&
                       C.requiredValue == D.requiredValue && C.varPath == D.varPath &&
                       C.scopeDepth == D.scopeDepth;
            });
        });
    };
    auto SamePending = [](const auto& X, const auto& Y) {
        if (X.size() != Y.size()) return false;
        return llvm::all_of(X, [&](const PendingGuardedPostcondition& P) {
            return llvm::any_of(Y, [&](const PendingGuardedPostcondition& Q) {
                return P.callLoc == Q.callLoc && P.targetPath == Q.targetPath &&
                       P.field == Q.field && P.applied == Q.applied && P.refuted == Q.refuted;
            });
        });
    };
    return sameEntries(A.TypedEnv, B.TypedEnv, sameValue) && A.GlobalProofs == B.GlobalProofs &&
           sameEntries(A.VarBoundProofs, B.VarBoundProofs, Eq) &&
           sameEntries(A.FieldStates, B.FieldStates,
                       [](const FieldState& X, const FieldState& Y) {
                           return X.state == Y.state && sameValue(X.value, Y.value);
                       }) &&
           sameEntries(A.AnyContainedTypes, B.AnyContainedTypes, Eq) &&
           sameEntries(A.PointerLifetimes, B.PointerLifetimes, SameLifetime) &&
           SameConstraints(A.CurrentScopeExitConstraints, B.CurrentScopeExitConstraints) &&
           SamePending(A.PendingGuardedPostconditions, B.PendingGuardedPostconditions) &&
           sameEntries(A.VarToResource, B.VarToResource, Eq) &&
           sameEntries(A.HeapObjectStates, B.HeapObjectStates,
                       [](const HeapObjectState& X, const HeapObjectState& Y) {
                           return X.initialized == Y.initialized;
                       }) &&
           sameEntries(A.StackVarPaths, B.StackVarPaths, Eq) &&
           sameEntries(A.PointerAliases, B.PointerAliases, Eq) &&
           sameEntries(A.ArrayElementFacts, B.ArrayElementFacts, Eq) &&
           sameEntries(A.PointerBaseOrigins, B.PointerBaseOrigins,
                       [](const BaseOriginInfo& X, const BaseOriginInfo& Y) {
                           return X.basePath == Y.basePath && sameValue(X.offsetBytes, Y.offsetBytes) &&
                                  X.baseParamIdx == Y.baseParamIdx &&
                                  X.offsetParamIdx == Y.offsetParamIdx &&
                                  X.offsetElemSize == Y.offsetElemSize &&
                                  X.offsetNegated == Y.offsetNegated;
                       });
}

// Join the states of two paths that meet. With WidenRanges (a loop head, A
// the previous head), only facts of A that B also proves stay, and a range
// that grows beyond A's becomes unbounded on that side, so that iterating
// the loop reaches a fixpoint.
ProgramState joinStates(const ProgramState& A, const ProgramState& B, bool WidenRanges = false) {
    if (A.Unreachable) return B;
    if (B.Unreachable) return A;
    ProgramState J;
    J.TypedEnv = joinEnv(A.TypedEnv, B.TypedEnv);
    // A proof is stored twice, as a proof and as its _proof_ entry (which
    // invalidation finds by path).
    auto AddProof = [&J](const FieldPath& Base, const Proof& P) {
        J.GlobalProofs.add(Base, P);
        J.TypedEnv[Base.withSynthetic("_proof_" + P.toKey())] = SymValue::Const(1);
    };
    // A variable whose value differs between the paths keeps the hull of
    // their ranges (i == 0 on one path, 1 <= i <= 9 on the other: 0 <= i <= 9).
    auto addHull = [&](const FieldPath& Var) {
        if (!Var.elements.empty() || J.TypedEnv.count(Var)) return;
        ValueRange RA = scalarRange(A, Var), RB = scalarRange(B, Var);
        FieldPath Base = FieldPath::fromSynthetic(Var.toString());
        if (RA.Lo && RB.Lo && (!WidenRanges || *RB.Lo >= *RA.Lo))
            AddProof(Base, Proof{"_value", ProofOp::GE, std::min(*RA.Lo, *RB.Lo)});
        if (RA.Hi && RB.Hi && (!WidenRanges || *RB.Hi <= *RA.Hi))
            AddProof(Base, Proof{"_value", ProofOp::LE, std::max(*RA.Hi, *RB.Hi)});
    };
    for (const auto* Side : {&A.TypedEnv, &B.TypedEnv})
        for (const auto& [Path, Value] : *Side) addHull(Path);
    // A fact holds after the join when the other path proves it too: by a
    // proof, perhaps of a stronger form (i > 3 on one path, i >= 0 on the
    // other: i >= 0), or by a constant value (i == 0 on entry to a loop whose
    // other paths prove i < n).
    ProofContext InA(A.GlobalProofs), InB(B.GlobalProofs);
    auto Proves = [](const ProgramState& S, const ProofContext& In, const FieldPath& Base,
                     const Proof& P) {
        if (In.canProve(Base, P)) return true;
        auto V = constantOn(S, Base, P.field);
        return V && checkProofOp(*V, P.op, P.value);
    };
    // Widening keeps a bound of A only while B keeps A's tightest bound on
    // that side; once it moves, every bound of A on that side goes.
    auto Settled = [&](const FieldPath& Base, const Proof& P) {
        bool Lower = P.op == ProofOp::GE || P.op == ProofOp::GT;
        if (!Lower && P.op != ProofOp::LE && P.op != ProofOp::LT) return true;
        ValueRange R = proofRange(A, Base, P.field);
        std::optional<int64_t> Tightest = Lower ? R.Lo : R.Hi;
        return Tightest &&
               Proves(B, InB, Base, Proof{P.field, Lower ? ProofOp::GE : ProofOp::LE, *Tightest});
    };
    A.GlobalProofs.forEach([&](const FieldPath& Base, const Proof& P) {
        if (Proves(B, InB, Base, P) && (!WidenRanges || Settled(Base, P))) AddProof(Base, P);
    });
    if (!WidenRanges)
        B.GlobalProofs.forEach([&](const FieldPath& Base, const Proof& P) {
            if (Proves(A, InA, Base, P)) AddProof(Base, P);
        });
    // Var op Obj.Field, kept the same way
    auto ProvesBound = [](const ProgramState& S, const VarBoundKey& K, ProofOp Op) {
        auto It = S.VarBoundProofs.find(K);
        if (It != S.VarBoundProofs.end()) return It->second == Op;
        auto Var = constantOn(S, FieldPath::fromSynthetic(K.varName), "_value");
        auto Bound = K.objPath.empty() ? constantOn(S, FieldPath::fromSynthetic(K.field), "_value")
                                       : constantOn(S, FieldPath::fromSynthetic(K.objPath), K.field);
        return Var && Bound && checkProofOp(*Var, Op, *Bound);
    };
    for (const auto* From : {&A, &B}) {
        if (WidenRanges && From == &B) break;
        const ProgramState& Other = From == &A ? B : A;
        for (const auto& [Key, Op] : From->VarBoundProofs)
            if (ProvesBound(Other, Key, Op)) J.VarBoundProofs.insert({Key, Op});
    }
    // A field lost on either path stays lost; otherwise both must agree.
    J.FieldStates = commonEntries(A.FieldStates, B.FieldStates, [](const FieldState& X, const FieldState& Y) {
        return X.state == Y.state && X.value.equals(Y.value);
    });
    for (const auto* M : {&A.FieldStates, &B.FieldStates})
        for (const auto& E : *M)
            if (E.getValue().state == TrackingState::UnknownLost)
                J.FieldStates.insert({E.getKey(), E.getValue()});
    J.AnyContainedTypes = commonEntries(A.AnyContainedTypes, B.AnyContainedTypes, std::equal_to<QualType>());
    J.PointerLifetimes = joinLifetimes(A.PointerLifetimes, B.PointerLifetimes);
    // A release obligation stays after the join unless it exists on one path
    // only and that path already met it: the other path released it or handed
    // it on (to a returned pointer or another handle).
    auto SameConstraint = [](const ScopeExitConstraint& X, const ScopeExitConstraint& Y) {
        return X.resourceId == Y.resourceId && X.field == Y.field && X.op == Y.op &&
               X.requiredValue == Y.requiredValue && X.varPath == Y.varPath;
    };
    auto JoinConstraints = [&](const ProgramState& From, const ProgramState& Other) {
        for (const auto& C : From.CurrentScopeExitConstraints) {
            if (llvm::any_of(J.CurrentScopeExitConstraints, [&](const auto& X) { return SameConstraint(X, C); }))
                continue;
            bool OnBoth = llvm::any_of(Other.CurrentScopeExitConstraints,
                                       [&](const auto& X) { return SameConstraint(X, C); });
            if (OnBoth || !meetsScopeExit(From, C)) J.CurrentScopeExitConstraints.push_back(C);
        }
    };
    JoinConstraints(A, B);
    JoinConstraints(B, A);
    auto SamePending = [](const PendingGuardedPostcondition& X,
                          const PendingGuardedPostcondition& Y) {
        return X.callLoc == Y.callLoc && X.targetPath == Y.targetPath && X.field == Y.field;
    };
    for (const auto& P : A.PendingGuardedPostconditions) {
        auto It = llvm::find_if(B.PendingGuardedPostconditions, [&](const auto& X) { return SamePending(X, P); });
        J.PendingGuardedPostconditions.push_back(It == B.PendingGuardedPostconditions.end()
                                       ? P : joinPending(P, A, *It, B, J.TypedEnv));
    }
    for (const auto& P : B.PendingGuardedPostconditions)
        if (llvm::none_of(A.PendingGuardedPostconditions, [&](const auto& X) { return SamePending(X, P); }))
            J.PendingGuardedPostconditions.push_back(P);
    J.VarToResource = commonEntries(A.VarToResource, B.VarToResource, std::equal_to<ResourceId>());
    J.HeapObjectStates = commonEntries(A.HeapObjectStates, B.HeapObjectStates,
        [](const HeapObjectState& X, const HeapObjectState& Y) {
            return X.initialized == Y.initialized;
        });
    J.StackVarPaths = commonEntries(A.StackVarPaths, B.StackVarPaths, std::equal_to<llvm::SmallString<32>>());
    J.PointerAliases = commonEntries(A.PointerAliases, B.PointerAliases, std::equal_to<std::string>());
    J.PointerBaseOrigins = commonEntries(A.PointerBaseOrigins, B.PointerBaseOrigins,
        [](const BaseOriginInfo& X, const BaseOriginInfo& Y) {
            return X.basePath == Y.basePath && X.offsetBytes.equals(Y.offsetBytes) &&
                   X.baseParamIdx == Y.baseParamIdx && X.offsetParamIdx == Y.offsetParamIdx &&
                   X.offsetElemSize == Y.offsetElemSize && X.offsetNegated == Y.offsetNegated;
        });
    J.ArrayElementFacts = commonEntries(A.ArrayElementFacts, B.ArrayElementFacts,
        [](const llvm::SmallVector<int64_t, 16>& X, const llvm::SmallVector<int64_t, 16>& Y) {
            return X == Y;
        });
    return J;
}

// Set environment value using FieldPath
void setEnvValueTyped(const FieldPath& path, SymValue v) {
    State.TypedEnv[path] = v;

    // Handle proof keys - add to GlobalProofs
    const auto& elems = path.elements;
    if (!elems.empty() && elems.back().kind == PathElement::SyntheticField) {
        llvm::StringRef synth = elems.back().syntheticName;
        if (synth.starts_with("_proof_")) {
            std::string proofKey = synth.drop_front(7).str();  // skip "_proof_"
            if (auto proof = Proof::fromKey(proofKey)) {
                FieldPath basePath = path.base();
                State.GlobalProofs.add(basePath, *proof);
            }
        }
    }
}

// Erase environment value using FieldPath
void eraseEnvValueTyped(const FieldPath& path) {
    State.TypedEnv.erase(path);

    // Handle proof keys - remove from GlobalProofs
    const auto& elems = path.elements;
    if (!elems.empty() && elems.back().kind == PathElement::SyntheticField) {
        llvm::StringRef synth = elems.back().syntheticName;
        if (synth.starts_with("_proof_")) {
            std::string proofKey = synth.drop_front(7).str();  // skip "_proof_"
            if (auto proof = Proof::fromKey(proofKey)) {
                FieldPath basePath = path.base();
                State.GlobalProofs.remove(basePath, *proof);
            }
        }
    }
}

// Add a proof using FieldPath
void addProofTyped(const FieldPath& basePath, const Proof& proof) {
    State.GlobalProofs.add(basePath, proof);
    State.TypedEnv[basePath.withSynthetic("_proof_" + proof.toKey())] = SymValue::Const(1);
}

// Check if a proof exists using FieldPath
bool hasProofTyped(const FieldPath& basePath, const Proof& proof) {
    return GlobalProofContext.canProve(basePath, proof);
}

// Read environment value using FieldPath
std::optional<SymValue> getEnvValueTyped(const FieldPath& path) {
    auto it = State.TypedEnv.find(path);
    if (it != State.TypedEnv.end()) {
        return it->second;
    }
    return std::nullopt;
}

// String-based wrappers (delegate to typed versions)
void setEnvValue(const std::string& key, SymValue v) {
    setEnvValueTyped(FieldPath::fromSynthetic(key), v);
}

void eraseEnvValue(const std::string& key) {
    eraseEnvValueTyped(FieldPath::fromSynthetic(key));
}

std::optional<SymValue> getEnvValue(const std::string& key) {
    return getEnvValueTyped(FieldPath::fromSynthetic(key));
}

// Collect all TypedEnv paths matching a string prefix
void collectPathsWithPrefix(llvm::StringRef prefix, llvm::SmallVectorImpl<FieldPath>& result) {
    for (const auto& [fieldPath, value] : State.TypedEnv) {
        if (fieldPath.toStringRef().starts_with(prefix)) {
            result.push_back(fieldPath);
        }
    }
}

// Collect paths matching prefix OR exact match
void collectPathsMatching(llvm::StringRef path, llvm::SmallVectorImpl<FieldPath>& result) {
    for (const auto& [fieldPath, value] : State.TypedEnv) {
        llvm::StringRef pathStr = fieldPath.toStringRef();
        // Match exact path or path followed by "." (sub-path prefix)
        if (pathStr == path ||
            (pathStr.starts_with(path) && pathStr.size() > path.size() && pathStr[path.size()] == '.')) {
            result.push_back(fieldPath);
        }
    }
}

// Evaluate a ConstraintExpr to a concrete boolean by substituting values
// Returns: true=satisfied, false=violated, nullopt=cannot evaluate
std::optional<bool> evaluateConstraintBool(
    const ConstraintExprPtr& expr,
    const std::string& objPath,
    const std::map<std::string, SymValue>& argVals) {

    if (!expr) return std::nullopt;

    switch (expr->kind) {
        case ConstraintExpr::LogicalAnd: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            auto left = evaluateConstraintBool(bin->left, objPath, argVals);
            if (!left) return std::nullopt;
            if (!*left) return false;  // Short-circuit: left is false
            return evaluateConstraintBool(bin->right, objPath, argVals);
        }

        case ConstraintExpr::LogicalOr: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            auto left = evaluateConstraintBool(bin->left, objPath, argVals);
            if (!left) return std::nullopt;
            if (*left) return true;  // Short-circuit: left is true
            return evaluateConstraintBool(bin->right, objPath, argVals);
        }

        case ConstraintExpr::LogicalNot: {
            auto* un = static_cast<UnaryExpr*>(expr.get());
            auto inner = evaluateConstraintBool(un->operand, objPath, argVals);
            if (!inner) return std::nullopt;
            return !*inner;
        }

        case ConstraintExpr::CmpGT:
        case ConstraintExpr::CmpGE:
        case ConstraintExpr::CmpLT:
        case ConstraintExpr::CmpLE:
        case ConstraintExpr::CmpEQ:
        case ConstraintExpr::CmpNE: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            auto lhsSym = bin->left->toSymValue();
            auto rhsSym = bin->right->toSymValue();
            if (!lhsSym || !rhsSym) return std::nullopt;

            // Build substitution map: args + object fields
            std::map<std::string, SymValue> subst = argVals;

            // Add field values from TypedEnv
            for (const auto& [fieldPath, value] : State.TypedEnv) {
                llvm::StringRef pathStr = fieldPath.toStringRef();
                if (pathStr.find("._proof_") != llvm::StringRef::npos) continue;
                if (startsWithDotPrefix(pathStr, objPath)) {
                    std::string field = pathStr.drop_front(objPath.size() + 1).str();
                    if (!field.empty()) {
                        subst[field] = value;
                        // Fields without _ or @ prefix need underscore alias
                        // for constraint expressions that use _fieldName syntax
                        if (field[0] != '_' && field[0] != '@') {
                            llvm::SmallString<64> prefixed("_");
                            prefixed += field;
                            subst[std::string(prefixed)] = value;
                        }
                    }
                }
            }

            // Evaluate after substitution
            SymValue lhsEval = lhsSym->substitute(subst);
            SymValue rhsEval = rhsSym->substitute(subst);
            auto lhs = lhsEval.eval();
            auto rhs = rhsEval.eval();

            if (!lhs || !rhs) return std::nullopt;

            switch (expr->kind) {
                case ConstraintExpr::CmpGT: return *lhs > *rhs;
                case ConstraintExpr::CmpGE: return *lhs >= *rhs;
                case ConstraintExpr::CmpLT: return *lhs < *rhs;
                case ConstraintExpr::CmpLE: return *lhs <= *rhs;
                case ConstraintExpr::CmpEQ: return *lhs == *rhs;
                case ConstraintExpr::CmpNE: return *lhs != *rhs;
                default: return std::nullopt;
            }
        }

        default:
            return std::nullopt;
    }
}

// Get the tracking state for a field (for Category C diagnostics)
TrackingState getFieldTrackingState(const std::string& basePath, const std::string& field) {
    std::string key = basePath + "." + field;
    auto it = State.FieldStates.find(key);
    if (it != State.FieldStates.end()) {
        return it->second.state;
    }
    // Check if field exists in TypedEnv but not in FieldStates
    FieldPath typedPath = fieldStorage(FieldPath::fromSynthetic(basePath), field);
    if (getEnvValueTyped(typedPath).has_value()) {
        return TrackingState::Known;
    }
    return TrackingState::Untracked;
}

// Get the loss provenance for a field (for Category C note)
std::optional<LossProvenance> getFieldLossProvenance(const std::string& basePath, const std::string& field) {
    std::string key = basePath + "." + field;
    auto it = State.FieldStates.find(key);
    if (it != State.FieldStates.end() && it->second.state == TrackingState::UnknownLost) {
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

void addProof(llvm::StringRef basePath, llvm::StringRef proofKey) {
    if (proofKey.empty()) return;
    setEnvValue((llvm::Twine(basePath) + "._proof_" + proofKey).str(), SymValue::Const(1));

    // Deferred guard evaluation: check if this proof satisfies any pending
    // `when` guards. Also resolve through the alias chain so that a null check
    // on alias `g` (where g = f) triggers pending postconditions on `f`.
    if (State.PendingGuardedPostconditions.empty()) return;

    llvm::StringRef resolvedBase = basePath;
    auto aliasIt = State.PointerAliases.find(basePath);
    if (aliasIt != State.PointerAliases.end())
        resolvedBase = aliasIt->second;

    for (auto& pending : State.PendingGuardedPostconditions) {
        if (pending.targetPath != basePath && pending.targetPath != resolvedBase)
            continue;
        // Check proof on the pending target itself and on basePath (for alias case)
        auto holds = [&](const Proof& P) {
            if (hasProofTyped(FieldPath::fromSynthetic(pending.targetPath), P)) return true;
            return pending.targetPath != basePath &&
                   hasProofTyped(FieldPath::fromSynthetic(basePath), P);
        };
        const auto& g = pending.guard;
        if (holds(Proof{g.field, negateProofOp(g.op), g.value})) {
            pending.refuted = true;
        } else if (!pending.applied && holds(Proof{g.field, g.op, g.value})) {
            setFieldTyped(FieldPath::fromSynthetic(pending.targetPath), pending.field,
                          SymValue::Const(pending.value));
            pending.applied = true;
        }
    }
}

void removeProof(llvm::StringRef basePath, llvm::StringRef proofKey) {
    // eraseEnvValueTyped handles both TypedEnv and GlobalProofs
    eraseEnvValue((llvm::Twine(basePath) + "._proof_" + proofKey).str());
}

// Whether bound fact Key relates variable Var: as the bounded variable, or
// as the bound (i < n).
static bool boundRelates(const VarBoundKey& Key, llvm::StringRef Var) {
    return Key.varName == Var || (Key.objPath.empty() && Key.field == Var);
}

// Forget the bounds relating variable VarName (i < v._size, i < n).
void invalidateVarBounds(llvm::StringRef VarName) {
    llvm::SmallVector<VarBoundKey, 4> Stale;
    for (const auto& [Key, Op] : State.VarBoundProofs)
        if (boundRelates(Key, VarName)) Stale.push_back(Key);
    for (const auto& Key : Stale) State.VarBoundProofs.erase(Key);
}

void invalidateProofs(llvm::StringRef basePath, llvm::StringRef field) {
    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsWithPrefix((llvm::Twine(basePath) + "._proof_" + field).str(), paths);
    for (const auto& fp : paths) {
        eraseEnvValueTyped(fp);
    }
    FieldPath typedBase = FieldPath::fromSynthetic(basePath.str());
    State.GlobalProofs.invalidateField(typedBase, field.str());
}

// Whether V names variable Var (or a field of it).
static bool namesVariable(const SymValue& V, llvm::StringRef Var) {
    if (V.kind == SymValue::VAR) return startsWithDotPrefix(V.varRef, Var) || V.varRef == Var;
    for (const auto* Sub : {&V.left, &V.right, &V.cond})
        if (*Sub && namesVariable(**Sub, Var)) return true;
    return false;
}

// The constant c with V == Var + c (Var - c gives -c), if V has that form.
static std::optional<int64_t> stepOf(const SymValue& V, llvm::StringRef Var) {
    if ((V.kind != SymValue::ADD && V.kind != SymValue::SUB) || !V.left || !V.right ||
        V.left->kind != SymValue::VAR || V.left->varRef != Var || V.right->kind != SymValue::CONST)
        return std::nullopt;
    return V.kind == SymValue::ADD ? V.right->constVal : -V.right->constVal;
}

// Values that name variable Var (j = i stores VAR(i)) mean its value before
// a write to it: rewrite them with Old, that value in terms of the new one,
// or make them unknown when Old is.
void rebindValuesNaming(llvm::StringRef Var, const SymValue& Old) {
    std::optional<llvm::StringMap<SymValue>> Subst;
    for (auto& [Path, Value] : State.TypedEnv) {
        if (Value.kind == SymValue::CONST || !namesVariable(Value, Var)) continue;
        if (Old.kind == SymValue::UNKNOWN) {
            Value = SymValue::Unknown();
            continue;
        }
        if (!Subst) Subst.emplace().try_emplace(Var, Old);
        Value = Value.substitute(*Subst);
    }
}

// Forget everything known about scalar Path's value (a compound assignment
// the value model does not track).
void forgetScalarValue(const FieldPath& Path) {
    rebindValuesNaming(Path.toStringRef(), SymValue::Unknown());
    eraseEnvValueTyped(Path);
    invalidateProofs(Path.toStringRef(), "_value");
    invalidateVarBounds(Path.toStringRef());
}

// ++, --, x += c and x -= c move scalar Path by Delta: its value and the
// bounds on it shift; of its bounds against fields only those the move
// preserves stay: lower bounds when it grows, upper bounds when it shrinks.
// (An unsigned value wrapping below zero is the caller's, stepScalar;
// wrapping above its maximum is not modeled yet, step 4.)
void shiftScalarValue(const FieldPath& Path, int64_t Delta) {
    if (Delta == std::numeric_limits<int64_t>::min()) return forgetScalarValue(Path);
    llvm::StringRef Name = Path.toStringRef();
    SymValue Now = SymValue::Var(Name.str());
    rebindValuesNaming(Name, Delta >= 0 ? SymValue::Sub(Now, SymValue::Const(Delta))
                                        : SymValue::Add(Now, SymValue::Const(-Delta)));
    if (auto V = getEnvValueTyped(Path)) {
        int64_t Moved;
        if (V->kind == SymValue::CONST && __builtin_add_overflow(V->constVal, Delta, &Moved))
            return forgetScalarValue(Path);
        if (V->kind == SymValue::CONST)
            setEnvValueTyped(Path, SymValue::Const(Moved));
        else if (V->kind != SymValue::UNKNOWN)
            setEnvValueTyped(Path, SymValue::Add(*V, SymValue::Const(Delta)));
    }
    FieldPath Base = FieldPath::fromSynthetic(Name.str());
    llvm::SmallVector<Proof, 4> Shifted;
    if (auto* Proofs = State.GlobalProofs.getProofs(Base))
        for (const Proof& P : *Proofs) {
            int64_t Moved;
            if (P.field == "_value" && !__builtin_add_overflow(P.value, Delta, &Moved))
                Shifted.push_back(Proof{P.field, P.op, Moved});
        }
    llvm::SmallVector<FieldPath, 4> Stale;
    llvm::SmallString<64> ProofPrefix(Name);
    ProofPrefix += "._proof__value";
    collectPathsWithPrefix(ProofPrefix, Stale);
    for (const auto& Key : Stale) eraseEnvValueTyped(Key);
    State.GlobalProofs.invalidateField(Base, "_value");
    for (const Proof& P : Shifted) addProofTyped(Base, P);
    auto Preserved = [Delta](ProofOp Op) {
        bool Lower = Op == ProofOp::GT || Op == ProofOp::GE;
        bool Upper = Op == ProofOp::LT || Op == ProofOp::LE;
        return (Delta >= 0 && Lower) || (Delta <= 0 && Upper);
    };
    // As the bound (i < n), Path moves the other way
    auto BoundPreserved = [Delta](ProofOp Op) {
        bool Upper = Op == ProofOp::LT || Op == ProofOp::LE;
        bool Lower = Op == ProofOp::GT || Op == ProofOp::GE;
        return (Delta >= 0 && Upper) || (Delta <= 0 && Lower);
    };
    llvm::SmallVector<VarBoundKey, 4> StaleBounds;
    for (const auto& [Key, Op] : State.VarBoundProofs)
        if ((Key.varName == Name && !Preserved(Op)) ||
            (Key.objPath.empty() && Key.field == Name && !BoundPreserved(Op)))
            StaleBounds.push_back(Key);
    for (const auto& Key : StaleBounds) State.VarBoundProofs.erase(Key);
}

// Check if there are any proofs about a field (even without a concrete value)
bool hasAnyProofs(const std::string& basePath, const std::string& field) {
    FieldPath typedBase = FieldPath::fromSynthetic(basePath);
    if (auto* proofs = State.GlobalProofs.getProofs(typedBase)) {
        for (const auto& p : *proofs) {
            if (p.field == field || p.field.find(field) == 0) return true;
        }
    }
    return false;
}

// Check if a specific proof exists (uses GlobalProofs as primary)
bool hasProof(llvm::StringRef basePath, llvm::StringRef proofKey) {
    FieldPath typedBase = FieldPath::fromSynthetic(basePath.str());
    if (auto proof = Proof::fromKey(proofKey.str())) {
        return State.GlobalProofs.has(typedBase, *proof);
    }
    // Fallback for proofs that can't be parsed structurally
    return getEnvValueTyped(typedBase.withSynthetic(("_proof_" + proofKey).str())).has_value();
}

// Invalidate all nullability information for a pointer
// This clears the _nonnull value and associated proofs
void invalidateNullability(const std::string& ptrPath) {
    if (ptrPath.empty()) return;
    FieldPath typedBase = FieldPath::fromSynthetic(ptrPath);
    eraseEnvValueTyped(typedBase.withSynthetic("_nonnull"));
    removeProof(ptrPath, "_nonnull!=0");
    removeProof(ptrPath, "_nonnull==0");
    State.GlobalProofs.invalidateField(typedBase, "_nonnull");
}

void invalidateArrayElements(const std::string& arrayBase, const std::string& field) {
    // Iterate over TypedEnv to find and remove array element entries
    llvm::SmallVector<FieldPath, 8> toRemove;
    std::string pattern = arrayBase + "[";
    for (const auto& [fieldPath, value] : State.TypedEnv) {
        std::string pathStr = fieldPath.toString();
        if (llvm::StringRef(pathStr).starts_with(pattern)) {
            if (pathStr.find("." + field) != std::string::npos ||
                pathStr.find("._proof_" + field) != std::string::npos) {
                toRemove.push_back(fieldPath);
            }
        }
    }
    for (const auto& fp : toRemove) eraseEnvValueTyped(fp);
}

// Invalidate all tracked properties for a given variable path with provenance
// Called when variable is passed by non-const reference to function without postcondition
// When provenance is provided, tracking state becomes UnknownLost (Category C)
void invalidatePropertiesWithProvenance(const std::string& path, SourceLocation loc, const std::string& reason) {
    if (path.empty()) return;
    rebindValuesNaming(path, SymValue::Unknown());

    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsMatching(path, paths);
    for (const auto& fp : paths) {
        std::string key = fp.toString();
        if (key.find("._proof_") == std::string::npos) {
            State.FieldStates[key] = FieldState::Lost(loc, reason);
        }
        eraseEnvValueTyped(fp);
    }

    // Also remove any VarBoundProofs involving this path
    llvm::SmallVector<VarBoundKey, 4> toRemove;
    for (const auto& [key, op] : State.VarBoundProofs) {
        if (key.objPath == path || boundRelates(key, path)) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        State.VarBoundProofs.erase(key);
    }
}

// Invalidate only fields that the function writes (frame-aware invalidation)
// Fields NOT written by the function are preserved (in the "frame")
// If function is unknown or writes are unknown, falls back to full invalidation
void invalidateWrittenFieldsOnly(StringRef path, const FunctionDecl* F,
                                  unsigned paramIdx, SourceLocation loc,
                                  StringRef reason) {
    if (path.empty()) return;

    // Check if we have frame information for this function
    std::string funcKey = getFunctionKey(F);
    auto analysisIt = FunctionAnalysis.find(funcKey);

    // Convert to std::string once for APIs that need it
    std::string pathStr = path.str();
    std::string reasonStr = reason.str();

    // If function is unknown or has no analysis, conservatively invalidate everything
    if (analysisIt == FunctionAnalysis.end()) {
        invalidatePropertiesWithProvenance(pathStr, loc, reasonStr);
        return;
    }

    // Functions without bodies (forward declarations, external) may be in the map
    // from call site recording, but weren't analyzed. Conservatively invalidate.
    const auto& data = analysisIt->second;
    if (!data.decl || !data.decl->hasBody()) {
        invalidatePropertiesWithProvenance(pathStr, loc, reasonStr);
        return;
    }

    auto writeIt = data.paramWrites.find(paramIdx);

    // If we have no write info for this param, check if function is pure
    if (writeIt == data.paramWrites.end()) {
        if (data.isPure) {
            // Pure function - preserve everything (don't invalidate)
            return;
        }
        // Unknown writes - conservatively invalidate everything
        invalidatePropertiesWithProvenance(pathStr, loc, reasonStr);
        return;
    }

    // Selectively invalidate only the fields that are written
    const llvm::StringSet<>& writtenFields = writeIt->second;

    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsMatching(path, paths);

    for (const auto& fp : paths) {
        std::string key = fp.toString();
        StringRef keyRef(key);

        // Check if any field component in the path was written.
        // For nested paths like "arg0._data._size", if the function writes
        // "_data", then "_data._size" must also be invalidated.
        bool shouldInvalidate = false;
        StringRef remaining = keyRef;
        while (!remaining.empty()) {
            auto [prefix, suffix] = remaining.split('.');
            if (!prefix.empty() && prefix.starts_with("_")) {
                // This is a field (starts with underscore per DTT convention)
                if (writtenFields.contains(prefix)) {
                    shouldInvalidate = true;
                    break;
                }
            }
            remaining = suffix;
        }

        if (shouldInvalidate) {
            if (!keyRef.contains("._proof_")) {
                State.FieldStates[key] = FieldState::Lost(loc, reasonStr);
            }
            eraseEnvValueTyped(fp);
        }
        // Fields not in writtenFields are preserved (frame)
    }

    // Also handle VarBoundProofs - only remove proofs for written fields
    llvm::SmallVector<VarBoundKey, 4> toRemove;
    for (const auto& [key, op] : State.VarBoundProofs) {
        if (key.objPath == pathStr && writtenFields.contains(key.field)) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        State.VarBoundProofs.erase(key);
    }
}

// Backward-compatible version without provenance
void invalidateProperties(llvm::StringRef path) {
    if (path.empty()) return;
    rebindValuesNaming(path, SymValue::Unknown());

    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsMatching(path, paths);
    for (const auto& fp : paths) {
        eraseEnvValueTyped(fp);
        State.FieldStates.erase(fp.toString());
    }

    // Also remove any VarBoundProofs involving this path
    llvm::SmallVector<VarBoundKey, 4> toRemove;
    for (const auto& [key, op] : State.VarBoundProofs) {
        if (key.objPath == path || boundRelates(key, path)) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        State.VarBoundProofs.erase(key);
    }
}

// A declaration starts a new variable: nothing known under its key (from
// an earlier block's variable of that name, or a previous loop pass) holds.
void forgetVariable(llvm::StringRef Key) {
    invalidateProperties(Key);
    invalidateProofs(Key, "_value");
    State.PointerLifetimes.erase(Key);
    State.VarToResource.erase(Key);
    State.PointerAliases.erase(Key);
    State.ArrayElementFacts.erase(Key);
    llvm::erase_if(State.PendingGuardedPostconditions,
                   [&](const PendingGuardedPostcondition& P) { return P.targetPath == Key; });
}

// Check if a function has ANY DTT annotations (requires, ensures, etc.)
// If a function has any annotations, assume the user knows what it does - don't invalidate
bool hasDTTAnnotations(FunctionDecl* FD) {
    if (!FD) return false;

    // Check for C++ dtt: attributes
    for (auto *A : FD->attrs()) {
        if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
            if (Ann->getAnnotation().starts_with("dtt:")) {
                return true;
            }
        }
    }

    // Check for .dtt file annotations (FreeFunctionAnnotations)
    if (FD->getIdentifier()) {
        if (FreeFunctionAnnotatedNames.contains(FD->getName()))
            return true;
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
            if (post.field == argPrefix || startsWithDotPrefix(post.field, argPrefix)) {
                return true;
            }
        }
    }

    return false;
}

// Check if a parameter type can be used to modify the argument variable itself
// Returns true for references (T&, T&&) which can rebind/modify the argument.
// Returns false for pointers (T*) - see shouldInvalidateForPointerParam for that case.
// Visit each argument of Call with the type of the parameter it binds to; a
// member operator's object (its first argument) binds to none (null type).
static void forEachCallArgument(const CallExpr *Call, const FunctionDecl *Callee,
                                llvm::function_ref<void(const Expr *Arg, QualType Param)> Fn) {
    unsigned Offset = isa<CXXOperatorCallExpr>(Call) && isa<CXXMethodDecl>(Callee) ? 1 : 0;
    if (Offset && Call->getNumArgs()) Fn(Call->getArg(0), QualType());
    for (unsigned I = 0; I < Callee->getNumParams() && I + Offset < Call->getNumArgs(); ++I)
        Fn(Call->getArg(I + Offset), Callee->getParamDecl(I)->getType());
}

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

// Convert a simple comparison ConstraintExpr to a proof key string
// For "_size > 0" returns "_size>0", for compound/complex returns ""
std::string constraintExprToProofKey(const ConstraintExprPtr& expr) {
    if (!expr) return "";

    // Only handle comparison expressions
    if (expr->kind < ConstraintExpr::CmpGT || expr->kind > ConstraintExpr::CmpNE) return "";

    auto* bin = static_cast<BinaryExpr*>(expr.get());

    // LHS must be a field reference
    if (bin->left->kind != ConstraintExpr::FieldRef) return "";
    auto* fieldRef = static_cast<FieldRefExpr*>(bin->left.get());

    // RHS must be a literal
    if (bin->right->kind != ConstraintExpr::Literal) return "";
    auto* lit = static_cast<LiteralExpr*>(bin->right.get());

    int opIdx = expr->kind - ConstraintExpr::CmpGT;
    return fieldRef->fieldName + ProofOpStrings[opIdx] + std::to_string(lit->value);
}

// Convert ConstraintExpr::Kind comparison to ProofOp
std::optional<ProofOp> constraintExprKindToProofOp(ConstraintExpr::Kind kind) {
    switch (kind) {
        case ConstraintExpr::CmpGT: return ProofOp::GT;
        case ConstraintExpr::CmpGE: return ProofOp::GE;
        case ConstraintExpr::CmpLT: return ProofOp::LT;
        case ConstraintExpr::CmpLE: return ProofOp::LE;
        case ConstraintExpr::CmpEQ: return ProofOp::EQ;
        case ConstraintExpr::CmpNE: return ProofOp::NE;
        default: return std::nullopt;
    }
}

// Inject a requires constraint as an assumption inside the function body
// This implements "contract propagation" - preconditions become assumptions
void injectRequiresAsAssumption(FunctionDecl *F, const ConstraintExprPtr& expr) {
    if (!expr) return;

    switch (expr->kind) {
        case ConstraintExpr::LogicalAnd: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            injectRequiresAsAssumption(F, bin->left);
            injectRequiresAsAssumption(F, bin->right);
            return;
        }

        case ConstraintExpr::LogicalOr:
            // OR constraints require case-split reasoning - skip for now
            return;

        case ConstraintExpr::CmpGT:
        case ConstraintExpr::CmpGE:
        case ConstraintExpr::CmpLT:
        case ConstraintExpr::CmpLE:
        case ConstraintExpr::CmpEQ:
        case ConstraintExpr::CmpNE: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());

            // Determine base path
            std::string basePath;
            if (auto *M = dyn_cast<CXXMethodDecl>(F)) {
                if (!M->isStatic()) basePath = "this";
            }
            if (basePath.empty() && F->getNumParams() > 0) {
                basePath = F->getParamDecl(0)->getName().str();
            }
            if (basePath.empty()) return;

            // Check for arg vs field pattern: arg0 < _size
            if (bin->left->kind == ConstraintExpr::ArgRef &&
                bin->right->kind == ConstraintExpr::FieldRef) {
                auto* argRef = static_cast<ArgRefExpr*>(bin->left.get());
                auto* fieldRef = static_cast<FieldRefExpr*>(bin->right.get());
                if ((int)argRef->argIndex < (int)F->getNumParams()) {
                    std::string argName = F->getParamDecl(argRef->argIndex)->getName().str();
                    std::string objPath = basePath;
                    if (!isa<CXXMethodDecl>(F) && F->getNumParams() > 1) {
                        objPath = F->getParamDecl(1)->getName().str();
                    }
                    std::string fieldName = fieldRef->fieldName;
                    if (!fieldName.empty() && fieldName[0] != '_' && fieldName[0] != '@') {
                        fieldName = "_" + fieldName;
                    }
                    if (auto proofOp = constraintExprKindToProofOp(expr->kind)) {
                        State.VarBoundProofs[{argName, objPath, fieldName}] = *proofOp;
                    }
                }
                return;
            }

            // Simple field constraint: _size > 0
            std::string proofKey = constraintExprToProofKey(expr);
            if (!proofKey.empty()) {
                addProof(basePath, proofKey);
            }
            return;
        }

        default:
            return;
    }
}

void copyProperties(const std::string& toPath, const std::string& fromPath) {
    llvm::SmallVector<std::pair<FieldPath, SymValue>, 8> toCopy;
    std::string fromPrefix = fromPath + ".";
    for (const auto& [fieldPath, value] : State.TypedEnv) {
        std::string pathStr = fieldPath.toString();
        // Match paths that start with "fromPath." (e.g., "d._nonnull" for fromPath="d")
        if (llvm::StringRef(pathStr).starts_with(fromPrefix) && !fieldPath.elements.empty()) {
            toCopy.push_back({fieldPath.rebase(toPath), value});
        }
    }
    for (const auto& [fp, v] : toCopy) setEnvValueTyped(fp, v);
}

// Clear all properties for a path (for assignment LHS)
void clearProperties(llvm::StringRef path) {
    std::string prefix = path.str() + ".";
    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsWithPrefix(prefix, paths);
    for (const auto& fp : paths) {
        eraseEnvValueTyped(fp);
    }
}

// Exchange properties between two paths (for swap)
void swapProperties(const std::string& path1, const std::string& path2) {
    llvm::SmallVector<std::pair<FieldPath, SymValue>, 8> props1, props2;
    std::string prefix1 = path1 + ".";
    std::string prefix2 = path2 + ".";

    for (const auto& [fieldPath, value] : State.TypedEnv) {
        std::string pathStr = fieldPath.toString();
        // Match paths using canonical string representation
        if (llvm::StringRef(pathStr).starts_with(prefix1) && !fieldPath.elements.empty()) {
            props1.push_back({fieldPath, value});
        }
        if (llvm::StringRef(pathStr).starts_with(prefix2) && !fieldPath.elements.empty()) {
            props2.push_back({fieldPath, value});
        }
    }

    clearProperties(path1);
    clearProperties(path2);

    // Swap: path1 gets path2's props, path2 gets path1's props
    for (const auto& [fp, v] : props2) setEnvValueTyped(fp.rebase(path1), v);
    for (const auto& [fp, v] : props1) setEnvValueTyped(fp.rebase(path2), v);

    // Also swap AnyContainedTypes for std::any tracking
    auto it1 = State.AnyContainedTypes.find(path1);
    auto it2 = State.AnyContainedTypes.find(path2);
    bool has1 = (it1 != State.AnyContainedTypes.end());
    bool has2 = (it2 != State.AnyContainedTypes.end());
    QualType type1 = has1 ? it1->second : QualType();
    QualType type2 = has2 ? it2->second : QualType();

    if (has1) State.AnyContainedTypes.erase(path1);
    if (has2) State.AnyContainedTypes.erase(path2);
    if (has2) State.AnyContainedTypes[path1] = type2;
    if (has1) State.AnyContainedTypes[path2] = type1;
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
    std::string className = Spec->getName().str();
    bool inStd = false;

    // Walk up the DeclContext chain to find if we're in std::
    const DeclContext* DC = Spec->getDeclContext();
    while (DC) {
        if (auto* NS = dyn_cast<NamespaceDecl>(DC)) {
            if (NS->getName() == "std") {
                inStd = true;
                break;
            }
        }
        DC = DC->getParent();
    }

    if (inStd) {
        qualName = "std::" + className;
    } else if (auto* NS = dyn_cast<NamespaceDecl>(Spec->getDeclContext())) {
        qualName = NS->getName().str() + "::" + className;
    } else {
        qualName = className;
    }

    // Create typed path for setting template parameters
    FieldPath varTypedPath = FieldPath::fromSynthetic(varPath);

    // Extract all integral template parameters as _tparam0, _tparam1, etc.
    for (unsigned i = 0; i < Args.size(); i++) {
        const TemplateArgument& Arg = Args[i];
        if (Arg.getKind() == TemplateArgument::Integral) {
            llvm::APSInt Val = Arg.getAsIntegral();
            if (Val.isRepresentableByInt64()) {
                int64_t value = Val.getExtValue();
                std::string paramName = "_tparam" + std::to_string(i);
                setFieldTyped(varTypedPath, paramName, SymValue::Const(value));
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
                        setFieldTyped(varTypedPath, alias.propertyName, SymValue::Const(value));
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
        std::string varName = variableKey(DRE->getDecl()).str();
        // Check if we have a tracked value for this variable
        if (auto val = getEnvValue(varName)) return *val;
        return SymValue::Var(varName);
    }

    // Member expression: a.len, v._size, bitfield, etc.
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        // Bitfield: bounded to 0..(2^width - 1)
        if (auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
            if (FD->isBitField()) {
                unsigned width = FD->getBitWidthValue(Ctx);
                int64_t maxVal = (1LL << width) - 1;
                FieldPath typedPath = getTypedPath(E);
                std::string varName = typedPath.isValid() ? typedPath.toString() : "";
                // Encode as BitAnd(var, maxVal) — upperBound() returns maxVal
                return SymValue::BitAnd(SymValue::Var(varName), SymValue::Const(maxVal));
            }
        }
        FieldPath typedPath = getTypedPath(E);
        if (typedPath.isValid() && !typedPath.hasRuntimeIndex()) {
            if (auto val = getEnvValueTyped(typedPath)) return *val;
            return SymValue::Var(typedPath.toString());
        }
    }
    
    // Binary operator (+, -, *, /, &, |, ^, %, <<, >>)
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
            case BO_Or:  return SymValue::BitOr(left, right);
            case BO_Xor: return SymValue::BitXor(left, right);
            case BO_Shl: return SymValue::Shl(left, right);
            case BO_Shr: return SymValue::Shr(left, right);
            default: break;
        }
    }

    // Ternary conditional: cond ? a : b
    if (auto *CO = dyn_cast<ConditionalOperator>(E)) {
        SymValue condVal = exprToSymValueImpl(CO->getCond(), Ctx);
        SymValue trueVal = exprToSymValueImpl(CO->getTrueExpr(), Ctx);
        SymValue falseVal = exprToSymValueImpl(CO->getFalseExpr(), Ctx);
        return SymValue::Ternary(condVal, trueVal, falseVal);
    }

    // Unary operator: ++i, i++, --i, i--, -x, ~x
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
            case UO_Minus:    // -x - negation
                return SymValue::Neg(operand);
            case UO_Not:      // ~x - bitwise complement
                return SymValue::BitNot(operand);
            default: break;
        }
    }

    // Explicit casts: (int)expr, static_cast<int>(expr) — recurse through
    if (auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
        return exprToSymValueImpl(CE->getSubExpr(), Ctx);
    }

    // Method call: v.size() -> lookup return value
    if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        auto *Method = MCE->getMethodDecl();
        StringRef classNameRef = Method
            ? resolveAnnotationClassName(Method, MCE->getImplicitObjectArgument())
            : StringRef();
        if (Method && !classNameRef.empty()) {
            MethodKey key{classNameRef.str(), Method->getNameAsString(), MCE->getNumArgs()};

            auto it = MethodAnnotations.find(key);
            if (it != MethodAnnotations.end() && !it->second.returns.empty() &&
                callRequirementsProven(const_cast<CXXMemberCallExpr *>(MCE), Ctx)) {
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

// Parse an expression for postcondition inference, handling self-references as old() values.
// For example, in _size = _size + 1, the RHS _size becomes old(_size).
// Returns a pair: (SymValue expr, bool usesOld, optional<unsigned> argIdx)
struct PostcondParseResult {
    SymValue expr;
    bool usesOld = false;
    std::optional<unsigned> argIdx;
};

PostcondParseResult parsePostconditionExpr(const Expr *E, ASTContext &Ctx,
                                           const std::string& selfField,
                                           const CXXRecordDecl* methodClass,
                                           const FunctionDecl* fnDecl) {
    PostcondParseResult result;
    result.expr = SymValue::Unknown();
    result.usesOld = false;
    result.argIdx = std::nullopt;

    if (!E) return result;
    E = E->IgnoreParenImpCasts();

    // Constant integer - try evaluation first (handles complex const exprs)
    Expr::EvalResult R;
    if (safeEvaluateAsInt(E, R, const_cast<ASTContext&>(Ctx))) {
        const llvm::APSInt &Val = R.Val.getInt();
        if (Val.isRepresentableByInt64()) {
            result.expr = SymValue::Const(Val.getExtValue());
            return result;
        }
    }

    // Binary operator: recursively parse both sides (check BEFORE parameter reference!)
    // This ensures complex expressions like (a + b) * 2 are fully parsed
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        auto leftRes = parsePostconditionExpr(BO->getLHS(), Ctx, selfField, methodClass, fnDecl);
        auto rightRes = parsePostconditionExpr(BO->getRHS(), Ctx, selfField, methodClass, fnDecl);

        result.usesOld = leftRes.usesOld || rightRes.usesOld;
        if (!result.argIdx) result.argIdx = leftRes.argIdx;
        if (!result.argIdx) result.argIdx = rightRes.argIdx;

        switch (BO->getOpcode()) {
            case BO_Add: result.expr = SymValue::Add(leftRes.expr, rightRes.expr); break;
            case BO_Sub: result.expr = SymValue::Sub(leftRes.expr, rightRes.expr); break;
            case BO_Mul: result.expr = SymValue::Mul(leftRes.expr, rightRes.expr); break;
            case BO_Div: result.expr = SymValue::Div(leftRes.expr, rightRes.expr); break;
            case BO_Rem: result.expr = SymValue::Mod(leftRes.expr, rightRes.expr); break;
            case BO_And: result.expr = SymValue::BitAnd(leftRes.expr, rightRes.expr); break;
            case BO_Or:  result.expr = SymValue::BitOr(leftRes.expr, rightRes.expr); break;
            case BO_Xor: result.expr = SymValue::BitXor(leftRes.expr, rightRes.expr); break;
            case BO_Shl: result.expr = SymValue::Shl(leftRes.expr, rightRes.expr); break;
            case BO_Shr: result.expr = SymValue::Shr(leftRes.expr, rightRes.expr); break;
            default: break;
        }
        return result;
    }

    // Unary operator
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
        auto subRes = parsePostconditionExpr(UO->getSubExpr(), Ctx, selfField, methodClass, fnDecl);
        result.usesOld = subRes.usesOld;
        result.argIdx = subRes.argIdx;

        switch (UO->getOpcode()) {
            case UO_Minus: result.expr = SymValue::Neg(subRes.expr); break;
            case UO_Not: result.expr = SymValue::BitNot(subRes.expr); break;
            case UO_PreInc: result.expr = SymValue::Add(subRes.expr, SymValue::Const(1)); break;
            case UO_PreDec: result.expr = SymValue::Sub(subRes.expr, SymValue::Const(1)); break;
            case UO_PostInc:
            case UO_PostDec:
                result.expr = subRes.expr;
                break;
            default: break;
        }
        return result;
    }

    // Ternary conditional
    if (auto *CO = dyn_cast<ConditionalOperator>(E)) {
        auto condRes = parsePostconditionExpr(CO->getCond(), Ctx, selfField, methodClass, fnDecl);
        auto trueRes = parsePostconditionExpr(CO->getTrueExpr(), Ctx, selfField, methodClass, fnDecl);
        auto falseRes = parsePostconditionExpr(CO->getFalseExpr(), Ctx, selfField, methodClass, fnDecl);

        result.usesOld = condRes.usesOld || trueRes.usesOld || falseRes.usesOld;
        if (!result.argIdx) result.argIdx = trueRes.argIdx;
        if (!result.argIdx) result.argIdx = falseRes.argIdx;
        result.expr = SymValue::Ternary(condRes.expr, trueRes.expr, falseRes.expr);
        return result;
    }

    // Member expression on 'this': detect self-reference and field-to-field patterns
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        if (methodClass && isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts())) {
            std::string fieldName = ME->getMemberDecl()->getName().str();
            if (fieldName == selfField) {
                // Self-reference: _size = _size + ... → old(_size)
                result.expr = SymValue::Var("old(" + selfField + ")");
                result.usesOld = true;
                return result;
            }
            // Field-to-field: _size = _capacity → reference to _capacity
            // Use old() wrapper to indicate it's the value at method entry
            if (!fieldName.empty() && fieldName[0] == '_') {
                result.expr = SymValue::Var("old(" + fieldName + ")");
                result.usesOld = true;  // References another field's value at entry
                return result;
            }
            // Non-DTT field on this
            result.expr = SymValue::Var(fieldName);
            return result;
        }
    }

    // Variable reference: could be parameter or local
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        // Check if it's a parameter
        if (auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
            if (fnDecl) {
                for (unsigned i = 0; i < fnDecl->getNumParams(); i++) {
                    if (fnDecl->getParamDecl(i) == PVD) {
                        result.expr = SymValue::Var("arg" + std::to_string(i));
                        result.argIdx = i;
                        return result;
                    }
                }
            }
        }
        // Local variable
        result.expr = SymValue::Var(variableKey(DRE->getDecl()).str());
        return result;
    }

    return result;
}

// ============================================================
// Z3 SMT SOLVER (C++ API)
// ============================================================

std::string sanitizeForSMT(llvm::StringRef s) {
    std::string result = s.str();
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

// C integer division truncates toward zero and the remainder has the sign
// of the dividend. Z3's integer div and mod are Euclidean: the remainder is
// never negative, so -7 div 2 == -4.
z3::expr cDivZ3(const z3::expr& a, const z3::expr& b) {
    return z3::ite(a >= 0, a / b, -((-a) / b));
}

z3::expr cRemZ3(const z3::expr& a, const z3::expr& b) {
    return a - b * cDivZ3(a, b);
}

// A solver constant equal to nothing else: the value of an expression DTT
// does not model. Unmodeled values must never share a symbol, or the solver
// would treat two of them, or a variable and its own update, as equal.
z3::expr freshUnknown(z3::context& ctx, std::map<std::string, z3::expr>& vars) {
    z3::expr unknown(ctx, Z3_mk_fresh_const(ctx, "unknown", ctx.int_sort()));
    vars.emplace(unknown.decl().name().str(), unknown);
    return unknown;
}

// Convert SymValue to Z3 expression
z3::expr symValueToZ3(const SymValue& sv, z3::context& ctx,
                      std::map<std::string, z3::expr>& vars) {
    switch (sv.kind) {
        case SymValue::CONST:
            return ctx.int_val(sv.constVal);
        case SymValue::UNKNOWN:
            return freshUnknown(ctx, vars);
        case SymValue::VAR:
            return getOrCreateVar(ctx, vars, sanitizeForSMT(sv.varRef));
        case SymValue::ADD:
            return symValueToZ3(*sv.left, ctx, vars) +
                   symValueToZ3(*sv.right, ctx, vars);
        case SymValue::SUB:
            return symValueToZ3(*sv.left, ctx, vars) -
                   symValueToZ3(*sv.right, ctx, vars);
        case SymValue::MUL:
            return symValueToZ3(*sv.left, ctx, vars) *
                   symValueToZ3(*sv.right, ctx, vars);
        case SymValue::DIV:
            return cDivZ3(symValueToZ3(*sv.left, ctx, vars),
                          symValueToZ3(*sv.right, ctx, vars));
        case SymValue::MOD:
            return cRemZ3(symValueToZ3(*sv.left, ctx, vars),
                          symValueToZ3(*sv.right, ctx, vars));
        case SymValue::BITAND:
        case SymValue::BITOR:
        case SymValue::BITXOR:
            // Bitwise operations are not modeled.
            return freshUnknown(ctx, vars);
        case SymValue::SHL:
            // Left shift: approximate as multiplication by power of 2
            if (sv.right && sv.right->kind == SymValue::CONST && sv.right->constVal >= 0 && sv.right->constVal < 63) {
                int64_t shiftVal = static_cast<int64_t>(1) << sv.right->constVal;
                return symValueToZ3(*sv.left, ctx, vars) * ctx.int_val(shiftVal);
            }
            return freshUnknown(ctx, vars);
        case SymValue::SHR:
            // Right shift: approximate as division by power of 2
            if (sv.right && sv.right->kind == SymValue::CONST && sv.right->constVal >= 0 && sv.right->constVal < 63) {
                int64_t shiftVal = static_cast<int64_t>(1) << sv.right->constVal;
                return symValueToZ3(*sv.left, ctx, vars) / ctx.int_val(shiftVal);
            }
            return freshUnknown(ctx, vars);
        case SymValue::NEG:
            return -symValueToZ3(*sv.left, ctx, vars);
        case SymValue::BITNOT:
            // Bitwise NOT: ~x = -1 - x in two's complement
            return ctx.int_val(-1) - symValueToZ3(*sv.left, ctx, vars);
        case SymValue::TERNARY:
            // ITE (If-Then-Else): z3::ite(cond != 0, then, else)
            if (sv.cond && sv.left && sv.right) {
                z3::expr condExpr = symValueToZ3(*sv.cond, ctx, vars);
                z3::expr thenExpr = symValueToZ3(*sv.left, ctx, vars);
                z3::expr elseExpr = symValueToZ3(*sv.right, ctx, vars);
                // Condition is true if != 0
                return z3::ite(condExpr != 0, thenExpr, elseExpr);
            }
            return freshUnknown(ctx, vars);
        default:
            return freshUnknown(ctx, vars);
    }
}

// ============================================================
// BOUND STRING PARSER
// ============================================================
// Parses bound strings like "arg1", "(arg1+arg2)", "(arg1+10)", "(arg1/2)"
// back to SymValue for Z3 evaluation.

SymValue parseBoundStr(StringRef str) {
    str = str.trim();

    // Remove outer parentheses
    while (str.starts_with("(") && str.ends_with(")")) {
        str = str.drop_front(1).drop_back(1).trim();
    }

    // Try to parse as integer constant
    int64_t constVal;
    if (!str.getAsInteger(10, constVal)) {
        return SymValue::Const(constVal);
    }

    // Helper lambda: scan right-to-left for operators at given precedence level
    auto findOperatorRTL = [](StringRef s, ArrayRef<char> ops) -> std::optional<std::pair<size_t, char>> {
        int depth = 0;
        for (size_t i = s.size(); i > 0; --i) {
            char c = s[i - 1];
            if (c == ')') depth++;
            else if (c == '(') depth--;
            else if (depth == 0 && llvm::is_contained(ops, c)) {
                return std::make_pair(i - 1, c);
            }
        }
        return std::nullopt;
    };

    // Scan for lower precedence (+, -) first
    if (auto found = findOperatorRTL(str, {'+', '-'})) {
        auto [pos, op] = *found;
        StringRef left = str.substr(0, pos).trim();
        StringRef right = str.substr(pos + 1).trim();
        SymValue lhs = parseBoundStr(left);
        SymValue rhs = parseBoundStr(right);
        return (op == '+') ? SymValue::Add(lhs, rhs) : SymValue::Sub(lhs, rhs);
    }

    // Then scan for higher precedence (*, /)
    if (auto found = findOperatorRTL(str, {'*', '/'})) {
        auto [pos, op] = *found;
        StringRef left = str.substr(0, pos).trim();
        StringRef right = str.substr(pos + 1).trim();
        SymValue lhs = parseBoundStr(left);
        SymValue rhs = parseBoundStr(right);
        return (op == '*') ? SymValue::Mul(lhs, rhs) : SymValue::Div(lhs, rhs);
    }

    // Valid identifier: starts with letter/underscore, contains only alnum/_
    auto isIdentChar = [](char c) { return llvm::isAlnum(c) || c == '_'; };
    if (!str.empty() && (llvm::isAlpha(str[0]) || str[0] == '_') &&
        llvm::all_of(str, isIdentChar)) {
        return SymValue::Var(str.str());
    }

    return SymValue::Unknown();
}

// ============================================================
// Z3 HELPERS FOR TYPEDENV AND GLOBALPROOFS
// ============================================================

// Add environment value constraints to Z3 solver
// Phase 1: Uses TypedEnv as primary source (migration complete)
void addEnvValuesToZ3(z3::context& ctx, z3::solver& solver,
                       std::map<std::string, z3::expr>& vars) {
    for (const auto& [fieldPath, value] : State.TypedEnv) {
        std::string pathStr = fieldPath.toString();
        if (pathStr.find("._proof_") != std::string::npos) continue;
        std::string v = sanitizeForSMT(pathStr);

        if (value.kind == SymValue::CONST) {
            solver.add(getOrCreateVar(ctx, vars, v) == ctx.int_val(value.constVal));
        } else if (value.kind == SymValue::VAR) {
            std::string rhsVar = sanitizeForSMT(value.varRef);
            solver.add(getOrCreateVar(ctx, vars, v) == getOrCreateVar(ctx, vars, rhsVar));
        } else if (value.kind == SymValue::ADD || value.kind == SymValue::SUB ||
                   value.kind == SymValue::MUL || value.kind == SymValue::DIV) {
            solver.add(getOrCreateVar(ctx, vars, v) == symValueToZ3(value, ctx, vars));
        }
    }
}

// Get or create a Z3 array variable (Int -> Int) in the vars map
// Factors out duplicated array variable creation pattern
z3::expr getOrCreateArrayVar(z3::context& ctx,
                              std::map<std::string, z3::expr>& vars,
                              llvm::StringRef name) {
    std::string nameStr = name.str();  // Cache once for lookup and insert
    auto it = vars.find(nameStr);
    if (it != vars.end()) {
        return it->second;
    }
    z3::sort int_sort = ctx.int_sort();
    z3::sort array_sort = ctx.array_sort(int_sort, int_sort);
    z3::expr arr = ctx.constant(nameStr.c_str(), array_sort);
    vars.insert({std::move(nameStr), arr});
    return arr;
}

// Add array element facts to Z3 solver for quantifier verification
// For arrays initialized with literals, we add:
//   (= (select arr 0) val0) (= (select arr 1) val1) ...
void addArrayElementsToZ3(z3::context& ctx, z3::solver& solver,
                           std::map<std::string, z3::expr>& vars) {

#if 0  // DEBUG
    llvm::errs() << "DEBUG addArrayElementsToZ3: " << State.ArrayElementFacts.size() << " arrays\n";
    for (const auto& e : State.ArrayElementFacts) {
        llvm::errs() << "  " << e.getKey() << ": [";
        for (size_t i = 0; i < e.getValue().size(); ++i) {
            if (i > 0) llvm::errs() << ", ";
            llvm::errs() << e.getValue()[i];
        }
        llvm::errs() << "]\n";
    }
#endif

    for (const auto& entry : State.ArrayElementFacts) {
        std::string arrayName = sanitizeForSMT(entry.getKey().str());
        const auto& elements = entry.getValue();

        z3::expr arr = getOrCreateArrayVar(ctx, vars, arrayName);

        // Add element value assertions: arr[i] == val
        for (size_t i = 0; i < elements.size(); ++i) {
            solver.add(z3::select(arr, ctx.int_val(static_cast<int64_t>(i))) ==
                       ctx.int_val(elements[i]));
        }
    }
}

// Add proof constraints to Z3 solver from GlobalProofs (structured store)
// This replaces addEnvProofsToZ3 - uses the structured proof store instead of Env iteration
void addGlobalProofsToZ3(z3::context& ctx, z3::solver& solver,
                          std::map<std::string, z3::expr>& vars) {
    State.GlobalProofs.forEach([&](const FieldPath& path, const Proof& p) {
        std::string base = sanitizeForSMT(path.toString());

        // Special handling for _value proofs: n._proof__value>0 means n > 0
        // The _value field is a marker for the variable itself
        if (p.field == "_value") {
            z3::expr varExpr = getOrCreateVar(ctx, vars, base);
            z3::expr rhsExpr = ctx.int_val(p.value);
            switch (p.op) {
                case ProofOp::GT: solver.add(varExpr > rhsExpr); break;
                case ProofOp::GE: solver.add(varExpr >= rhsExpr); break;
                case ProofOp::LT: solver.add(varExpr < rhsExpr); break;
                case ProofOp::LE: solver.add(varExpr <= rhsExpr); break;
                case ProofOp::EQ: solver.add(varExpr == rhsExpr); break;
                case ProofOp::NE: solver.add(varExpr != rhsExpr); break;
                default: break;
            }
        } else {
            // Field proof: base._field op value
            z3::expr fieldExpr = getOrCreateVar(ctx, vars, base + "_" + p.field);
            z3::expr rhsExpr = ctx.int_val(p.value);
            switch (p.op) {
                case ProofOp::GT: solver.add(fieldExpr > rhsExpr); break;
                case ProofOp::GE: solver.add(fieldExpr >= rhsExpr); break;
                case ProofOp::LT: solver.add(fieldExpr < rhsExpr); break;
                case ProofOp::LE: solver.add(fieldExpr <= rhsExpr); break;
                case ProofOp::EQ: solver.add(fieldExpr == rhsExpr); break;
                case ProofOp::NE: solver.add(fieldExpr != rhsExpr); break;
                default: break;
            }
        }
    });
}

// Add VarBoundProofs constraints to Z3 solver (loop invariants like i < n)
void addVarBoundProofsToZ3(z3::context& ctx, z3::solver& solver,
                            std::map<std::string, z3::expr>& vars) {
    for (const auto& [key, op] : State.VarBoundProofs) {
        // When objPath is empty, field is a plain variable name (e.g., i < n)
        // When objPath is non-empty, it's a field access (e.g., i < obj._size)
        z3::expr varExpr = getOrCreateVar(ctx, vars, sanitizeForSMT(key.varName));
        z3::expr boundExpr = key.objPath.empty()
            ? getOrCreateVar(ctx, vars, sanitizeForSMT(key.field))
            : getOrCreateVar(ctx, vars, sanitizeForSMT(key.objPath + "." + key.field));

        switch (op) {
            case ProofOp::LT: solver.add(varExpr < boundExpr); break;
            case ProofOp::LE: solver.add(varExpr <= boundExpr); break;
            case ProofOp::GT: solver.add(varExpr > boundExpr); break;
            case ProofOp::GE: solver.add(varExpr >= boundExpr); break;
            case ProofOp::EQ: solver.add(varExpr == boundExpr); break;
            case ProofOp::NE: solver.add(varExpr != boundExpr); break;
        }
    }
}

// Convert ConstraintExprPtr AST directly to Z3 expression
z3::expr constraintExprToZ3(const ConstraintExprPtr& expr, z3::context& ctx,
                             std::map<std::string, z3::expr>& vars, const std::string& basePath) {
    if (!expr) throw z3::exception("missing constraint");

    switch (expr->kind) {
        case ConstraintExpr::Literal:
            return ctx.int_val(static_cast<LiteralExpr*>(expr.get())->value);

        case ConstraintExpr::FieldRef: {
            auto* fr = static_cast<FieldRefExpr*>(expr.get());
            // First check if this is a bound variable from a quantifier
            // Bound variables are added to vars without any prefix
            auto it = vars.find(fr->fieldName);
            if (it != vars.end()) {
                return it->second;
            }
            // Not a bound variable - treat as a field reference with basePath prefix
            return getOrCreateVar(ctx, vars, sanitizeForSMT(basePath + "_" + fr->fieldName));
        }

        case ConstraintExpr::ArgRef: {
            auto* ar = static_cast<ArgRefExpr*>(expr.get());
            return getOrCreateVar(ctx, vars, "arg" + std::to_string(ar->argIndex));
        }

        case ConstraintExpr::ArgFieldRef: {
            auto* afr = static_cast<ArgFieldRefExpr*>(expr.get());
            return getOrCreateVar(ctx, vars, "arg" + std::to_string(afr->argIndex) + "_" + std::string(afr->fieldName));
        }

        case ConstraintExpr::OldRef: {
            auto* old = static_cast<OldRefExpr*>(expr.get());
            return getOrCreateVar(ctx, vars, "old_" + sanitizeForSMT(basePath + "_" + old->fieldName));
        }

        case ConstraintExpr::ResultRef:
            return getOrCreateVar(ctx, vars, "_result");

        case ConstraintExpr::LogicalNot: {
            auto* un = static_cast<UnaryExpr*>(expr.get());
            return !constraintExprToZ3(un->operand, ctx, vars, basePath);
        }

        // Binary operations
        case ConstraintExpr::Add: case ConstraintExpr::Sub: case ConstraintExpr::Mul:
        case ConstraintExpr::Div: case ConstraintExpr::Mod: case ConstraintExpr::BitAnd:
        case ConstraintExpr::CmpGT: case ConstraintExpr::CmpGE: case ConstraintExpr::CmpLT:
        case ConstraintExpr::CmpLE: case ConstraintExpr::CmpEQ: case ConstraintExpr::CmpNE:
        case ConstraintExpr::LogicalAnd: case ConstraintExpr::LogicalOr: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            z3::expr left = constraintExprToZ3(bin->left, ctx, vars, basePath);
            z3::expr right = constraintExprToZ3(bin->right, ctx, vars, basePath);

            switch (expr->kind) {
                case ConstraintExpr::Add: return left + right;
                case ConstraintExpr::Sub: return left - right;
                case ConstraintExpr::Mul: return left * right;
                case ConstraintExpr::Div: return cDivZ3(left, right);
                case ConstraintExpr::Mod: return cRemZ3(left, right);
                case ConstraintExpr::BitAnd: return left & right;
                case ConstraintExpr::CmpGT: return left > right;
                case ConstraintExpr::CmpGE: return left >= right;
                case ConstraintExpr::CmpLT: return left < right;
                case ConstraintExpr::CmpLE: return left <= right;
                case ConstraintExpr::CmpEQ: return left == right;
                case ConstraintExpr::CmpNE: return left != right;
                case ConstraintExpr::LogicalAnd: return left && right;
                case ConstraintExpr::LogicalOr: return left || right;
                default: throw z3::exception("unsupported constraint operator");
            }
        }

        case ConstraintExpr::Forall: {
            auto* f = static_cast<ForallExpr*>(expr.get());

            // Create bound variable for Z3 quantifier
            z3::expr boundVar = ctx.int_const(f->boundVar.c_str());

            // Save old binding and add bound variable to scope
            auto oldVars = vars;
            vars.insert_or_assign(f->boundVar, boundVar);

            // Translate bounds and body
            z3::expr lo = constraintExprToZ3(f->lo, ctx, vars, basePath);
            z3::expr hi = constraintExprToZ3(f->hi, ctx, vars, basePath);
            z3::expr body = constraintExprToZ3(f->body, ctx, vars, basePath);

            // Restore variable scope
            vars = oldVars;

            // Build: forall i. (lo <= i < hi) => body
            z3::expr range = (boundVar >= lo) && (boundVar < hi);
            return z3::forall(boundVar, z3::implies(range, body));
        }

        case ConstraintExpr::Exists: {
            auto* e = static_cast<ExistsExpr*>(expr.get());

            z3::expr boundVar = ctx.int_const(e->boundVar.c_str());
            auto oldVars = vars;
            vars.insert_or_assign(e->boundVar, boundVar);

            z3::expr lo = constraintExprToZ3(e->lo, ctx, vars, basePath);
            z3::expr hi = constraintExprToZ3(e->hi, ctx, vars, basePath);
            z3::expr body = constraintExprToZ3(e->body, ctx, vars, basePath);

            vars = oldVars;

            // Build: exists i. (lo <= i < hi) && body
            z3::expr range = (boundVar >= lo) && (boundVar < hi);
            return z3::exists(boundVar, range && body);
        }

        case ConstraintExpr::ArrayIndex: {
            auto* idx = static_cast<IndexExpr*>(expr.get());

            // Create or get Z3 array for this field
            // If basePath is empty, just use arrayName directly (for arg0, arg1 etc)
            std::string arrayName = basePath.empty()
                ? sanitizeForSMT(idx->arrayName)
                : sanitizeForSMT(basePath + "_" + idx->arrayName);

            z3::expr arr = getOrCreateArrayVar(ctx, vars, arrayName);
            z3::expr index = constraintExprToZ3(idx->index, ctx, vars, basePath);
            return z3::select(arr, index);
        }

        default:
            // An unsupported part must not count as satisfied.
            throw z3::exception("unsupported constraint");
    }
}

// Z3 solver context - bundles setup boilerplate for constraint checking
struct Z3SolverContext {
    z3::context ctx;
    z3::solver solver;
    std::map<std::string, z3::expr> vars;

    Z3SolverContext(unsigned timeout_ms = 1000) : solver(ctx) {
        z3::params p(ctx);
        p.set("timeout", timeout_ms);
        solver.set(p);
    }

    void addEnvironmentConstraints(bool includeVarBounds = false) {
        addEnvValuesToZ3(ctx, solver, vars);
        addGlobalProofsToZ3(ctx, solver, vars);
        addArrayElementsToZ3(ctx, solver, vars);  // For forall quantifier verification
        if (includeVarBounds) {
            addVarBoundProofsToZ3(ctx, solver, vars);
        }
    }

    // Does goal follow from the facts? true: proven (the facts are
    // consistent and no model violates goal); false: disproven (no model
    // satisfies goal); std::nullopt: neither, including when the solver
    // gives up. A counterexample alone does not disprove: it shows only
    // that goal may fail.
    std::optional<bool> checkNegated(const z3::expr& goal) {
        auto holdsIn = [&](const z3::expr& extra) {
            solver.push();
            solver.add(extra);
            z3::check_result result = solver.check();
            solver.pop();
            return result;
        };
        z3::check_result fails = holdsIn(!goal);
        if (fails == z3::unsat)
            return solver.check() == z3::sat ? std::optional(true) : std::nullopt;
        if (fails == z3::sat && holdsIn(goal) == z3::unsat) return false;
        return std::nullopt;
    }
};

// Check constraint using Z3 with ConstraintExprPtr
std::optional<bool> checkWithZ3Expr(const std::string& basePath, const ConstraintExprPtr& expr) {
    if (!expr) return std::nullopt;

    try {
        Z3SolverContext z3ctx;
        z3ctx.addEnvironmentConstraints();

        std::string varName = sanitizeForSMT(basePath);
        z3::expr goal = constraintExprToZ3(expr, z3ctx.ctx, z3ctx.vars, varName);
        return z3ctx.checkNegated(goal);

    } catch (const z3::exception& e) {
        return std::nullopt;
    }
}

// Check constraint expression with argument values added to Z3 context
// Used for forall/exists quantifiers where arg0, arg1 etc need to be bound
std::optional<bool> checkWithZ3ExprWithArgs(const ConstraintExprPtr& expr,
                                             const std::map<std::string, SymValue>& argVals) {
    if (!expr) return std::nullopt;

    try {
        Z3SolverContext z3ctx;
        z3ctx.addEnvironmentConstraints();

        // Add argument values to Z3 context
        // This binds arg0, arg1, arg0._size, etc. to their actual values
        for (const auto& [key, val] : argVals) {
            std::string sanitized = sanitizeForSMT(key);
            if (val.kind == SymValue::CONST) {
                z3ctx.solver.add(getOrCreateVar(z3ctx.ctx, z3ctx.vars, sanitized) ==
                                 z3ctx.ctx.int_val(val.constVal));
            }
        }

        z3::expr goal = constraintExprToZ3(expr, z3ctx.ctx, z3ctx.vars, "");

        return z3ctx.checkNegated(goal);

    } catch (const z3::exception& e) {
        return std::nullopt;
    }
}

// Check a comparison between two SymValues using Z3
// Returns: true = comparison proven, false = comparison disproven, nullopt = unknown
// assumeNonNegative: when true, adds var >= 0 constraints for all variables (for size_t comparisons)
std::optional<bool> checkComparisonWithZ3(const std::string& basePath, const SymValue& lhs, const SymValue& rhs,
                                           ConstraintExpr::Kind op, bool assumeNonNegative = false,
                                           bool includeVarBounds = false) {
    try {
        Z3SolverContext z3ctx;
        z3ctx.addEnvironmentConstraints(includeVarBounds);

        z3::expr z3Lhs = symValueToZ3(lhs, z3ctx.ctx, z3ctx.vars);
        z3::expr z3Rhs = symValueToZ3(rhs, z3ctx.ctx, z3ctx.vars);

        // Add non-negativity constraints for all variables (for unsigned types like size_t)
        if (assumeNonNegative) {
            for (const auto& [name, expr] : z3ctx.vars) {
                z3ctx.solver.add(expr >= 0);
            }
        }

        z3::expr comparison = z3ctx.ctx.bool_val(true);
        switch (op) {
            case ConstraintExpr::CmpGT: comparison = z3Lhs > z3Rhs; break;
            case ConstraintExpr::CmpGE: comparison = z3Lhs >= z3Rhs; break;
            case ConstraintExpr::CmpLT: comparison = z3Lhs < z3Rhs; break;
            case ConstraintExpr::CmpLE: comparison = z3Lhs <= z3Rhs; break;
            case ConstraintExpr::CmpEQ: comparison = z3Lhs == z3Rhs; break;
            case ConstraintExpr::CmpNE: comparison = z3Lhs != z3Rhs; break;
            default: return std::nullopt;
        }

        return z3ctx.checkNegated(comparison);

    } catch (const z3::exception& e) {
        return std::nullopt;
    }
}

// Check buffer bounds using Z3: prove that idx >= minIdx && idx < size
// Returns: true = proven safe, false = proven OOB, nullopt = unknown
std::optional<bool> checkBufferBoundsWithZ3(const SymValue& idx, const SymValue& size,
                                             int64_t minIdx, const std::string& ptrPath,
                                             bool isUnsignedIndex = false) {
    try {
        Z3SolverContext z3ctx;
        z3ctx.addEnvironmentConstraints(/*includeVarBounds=*/true);

        z3::expr z3Idx = symValueToZ3(idx, z3ctx.ctx, z3ctx.vars);
        z3::expr z3Size = symValueToZ3(size, z3ctx.ctx, z3ctx.vars);

        // For unsigned index types, add non-negativity constraint
        if (isUnsignedIndex) {
            z3ctx.solver.add(z3Idx >= 0);
        }

        z3::expr boundsCheck = (z3Idx >= z3ctx.ctx.int_val(minIdx)) && (z3Idx < z3Size);
        return z3ctx.checkNegated(boundsCheck);

    } catch (const z3::exception& e) {
        return std::nullopt;
    }
}

// ============================================================
// CONSTRAINT CHECKING
// ============================================================

// Check if a Proof implies a SimpleFieldComparison (works directly with AST types)
bool proofImpliesSfc(const Proof& p, const SimpleFieldComparison& sfc) {
    if (p.field != sfc.field) return false;
    int64_t pVal = p.value, cVal = sfc.value;

    // Convert ConstraintExpr::Kind to comparison logic
    switch (p.op) {
        case ProofOp::GT:
            if ((sfc.op == ConstraintExpr::CmpGT || sfc.op == ConstraintExpr::CmpGE) && pVal >= cVal) return true;
            break;
        case ProofOp::GE:
            if (sfc.op == ConstraintExpr::CmpGE && pVal >= cVal) return true;
            if (sfc.op == ConstraintExpr::CmpGT && pVal > cVal) return true;
            break;
        case ProofOp::LT:
            if ((sfc.op == ConstraintExpr::CmpLT || sfc.op == ConstraintExpr::CmpLE) && pVal <= cVal) return true;
            break;
        case ProofOp::LE:
            if (sfc.op == ConstraintExpr::CmpLE && pVal <= cVal) return true;
            if (sfc.op == ConstraintExpr::CmpLT && pVal < cVal) return true;
            break;
        case ProofOp::EQ:
            if (sfc.op == ConstraintExpr::CmpEQ && pVal == cVal) return true;
            if (sfc.op == ConstraintExpr::CmpGE && pVal >= cVal) return true;
            if (sfc.op == ConstraintExpr::CmpLE && pVal <= cVal) return true;
            if (sfc.op == ConstraintExpr::CmpGT && pVal > cVal) return true;
            if (sfc.op == ConstraintExpr::CmpLT && pVal < cVal) return true;
            break;
        case ProofOp::NE:
            if (sfc.op == ConstraintExpr::CmpNE && pVal == cVal) return true;
            break;
    }
    return false;
}

// Check a simple field comparison with full proof support
std::optional<bool> checkSimpleFieldComparisonWithProofs(const std::string& basePath, const SimpleFieldComparison& sfc) {
    // Check direct proofs from GlobalProofs
    FieldPath typedBase = FieldPath::fromSynthetic(basePath);
    if (auto* proofs = State.GlobalProofs.getProofs(typedBase)) {
        for (const auto& p : *proofs) {
            if (proofImpliesSfc(p, sfc)) return true;
        }
    }

    // Check ProofContext entailment
    auto sfcProofOp = constraintExprKindToProofOp(sfc.op);
    if (sfcProofOp && GlobalProofContext.canProveConstraint(typedBase, sfc.field, static_cast<int>(*sfcProofOp), sfc.value)) {
        return true;
    }

    // Evaluate field value directly
    auto val = getField(basePath, sfc.field);
    auto v = val.eval();

    // Transitive proof support: if field value is a variable, check proofs on that variable
    if (!v && val.kind == SymValue::VAR) {
        std::string varName = val.varRef;
        FieldPath varPath = FieldPath::fromSynthetic(varName);
        if (auto* varProofs = State.GlobalProofs.getProofs(varPath)) {
            for (const auto& p : *varProofs) {
                if (p.field == "_value" || p.field == sfc.field) {
                    if (proofImpliesSfc(p, sfc)) return true;
                }
            }
        }
    }

    if (!v) return std::nullopt;

    // Direct comparison
    switch (sfc.op) {
        case ConstraintExpr::CmpGT: return *v > sfc.value;
        case ConstraintExpr::CmpGE: return *v >= sfc.value;
        case ConstraintExpr::CmpLT: return *v < sfc.value;
        case ConstraintExpr::CmpLE: return *v <= sfc.value;
        case ConstraintExpr::CmpEQ: return *v == sfc.value;
        case ConstraintExpr::CmpNE: return *v != sfc.value;
        default: return std::nullopt;
    }
}

// Evaluate a ConstraintExprPtr to a SymValue (for complex expressions)
std::optional<SymValue> evaluateConstraintExpr(const ConstraintExprPtr& expr, const std::string& basePath,
                                                const std::map<std::string, SymValue>& argVals = {}) {
    if (!expr) return std::nullopt;

    switch (expr->kind) {
        case ConstraintExpr::Literal:
            return SymValue::Const(static_cast<LiteralExpr*>(expr.get())->value);

        case ConstraintExpr::FieldRef:
            return getField(basePath, static_cast<FieldRefExpr*>(expr.get())->fieldName);

        case ConstraintExpr::ArgRef: {
            auto* ar = static_cast<ArgRefExpr*>(expr.get());
            std::string argKey = "arg" + std::to_string(ar->argIndex);
            auto it = argVals.find(argKey);
            if (it != argVals.end()) return it->second;
            return std::nullopt;
        }

        case ConstraintExpr::ArgFieldRef: {
            auto* afr = static_cast<ArgFieldRefExpr*>(expr.get());
            std::string argKey = "arg" + std::to_string(afr->argIndex);
            auto it = argVals.find(argKey);
            if (it != argVals.end() && it->second.kind == SymValue::VAR) {
                return getField(it->second.varRef, afr->fieldName);
            }
            return std::nullopt;
        }

        case ConstraintExpr::Add: case ConstraintExpr::Sub:
        case ConstraintExpr::Mul: case ConstraintExpr::Div: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            auto left = evaluateConstraintExpr(bin->left, basePath, argVals);
            auto right = evaluateConstraintExpr(bin->right, basePath, argVals);
            if (!left || !right) return std::nullopt;

            switch (expr->kind) {
                case ConstraintExpr::Add: return SymValue::Add(*left, *right);
                case ConstraintExpr::Sub: return SymValue::Sub(*left, *right);
                case ConstraintExpr::Mul: return SymValue::Mul(*left, *right);
                case ConstraintExpr::Div: return SymValue::Div(*left, *right);
                default: return std::nullopt;
            }
        }

        default:
            return std::nullopt;
    }
}

// Check comparison operation with ConstraintExpr::Kind
bool checkExprOp(ConstraintExpr::Kind op, int64_t lhs, int64_t rhs) {
    switch (op) {
        case ConstraintExpr::CmpGT: return lhs > rhs;
        case ConstraintExpr::CmpGE: return lhs >= rhs;
        case ConstraintExpr::CmpLT: return lhs < rhs;
        case ConstraintExpr::CmpLE: return lhs <= rhs;
        case ConstraintExpr::CmpEQ: return lhs == rhs;
        case ConstraintExpr::CmpNE: return lhs != rhs;
        default: return false;
    }
}

// Check a ConstraintExprPtr directly
// This is the main entry point for AST-based constraint checking
std::optional<bool> checkConstraintExprSimple(const std::string& basePath, const ConstraintExprPtr& expr,
                                               const std::map<std::string, SymValue>& argVals = {}) {
    if (!expr) return std::nullopt;

    switch (expr->kind) {
        case ConstraintExpr::LogicalAnd: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            auto l = checkConstraintExprSimple(basePath, bin->left, argVals);
            auto r = checkConstraintExprSimple(basePath, bin->right, argVals);
            if (l && r) return *l && *r;
            if ((l && !*l) || (r && !*r)) return false;
            return std::nullopt;
        }

        case ConstraintExpr::LogicalOr: {
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            auto l = checkConstraintExprSimple(basePath, bin->left, argVals);
            auto r = checkConstraintExprSimple(basePath, bin->right, argVals);
            if (l && r) return *l || *r;
            if ((l && *l) || (r && *r)) return true;
            return std::nullopt;
        }

        case ConstraintExpr::LogicalNot: {
            auto* un = static_cast<UnaryExpr*>(expr.get());
            auto result = checkConstraintExprSimple(basePath, un->operand, argVals);
            if (result) return !*result;
            return std::nullopt;
        }

        case ConstraintExpr::CmpGT:
        case ConstraintExpr::CmpGE:
        case ConstraintExpr::CmpLT:
        case ConstraintExpr::CmpLE:
        case ConstraintExpr::CmpEQ:
        case ConstraintExpr::CmpNE: {
            // Try simple field comparison path first
            if (auto sfc = asSimpleFieldComparison(expr)) {
                return checkSimpleFieldComparisonWithProofs(basePath, *sfc);
            }

            // Try evaluating both sides
            auto* bin = static_cast<BinaryExpr*>(expr.get());
            auto lhsVal = evaluateConstraintExpr(bin->left, basePath, argVals);
            auto rhsVal = evaluateConstraintExpr(bin->right, basePath, argVals);

            if (lhsVal && rhsVal) {
                auto lhsEval = lhsVal->eval();
                auto rhsEval = rhsVal->eval();
                if (lhsEval && rhsEval) {
                    return checkExprOp(expr->kind, *lhsEval, *rhsEval);
                }
            }
            return std::nullopt;
        }

        default:
            return std::nullopt;
    }
}

// Entry point for checking ConstraintExprPtr with Z3 fallback
std::optional<bool> checkConstraintExpr(const std::string& basePath, const ConstraintExprPtr& expr,
                                         const std::map<std::string, SymValue>& argVals = {},
                                         bool useZ3 = true) {
    auto simple = checkConstraintExprSimple(basePath, expr, argVals);
    if (simple.has_value()) return simple;
    if (useZ3) return checkWithZ3Expr(basePath, expr);
    return std::nullopt;
}

ConstraintExpr::Kind proofOpToConstraintKind(ProofOp Op) {
    switch (Op) {
    case ProofOp::GT: return ConstraintExpr::CmpGT;
    case ProofOp::GE: return ConstraintExpr::CmpGE;
    case ProofOp::LT: return ConstraintExpr::CmpLT;
    case ProofOp::LE: return ConstraintExpr::CmpLE;
    case ProofOp::EQ: return ConstraintExpr::CmpEQ;
    case ProofOp::NE: return ConstraintExpr::CmpNE;
    }
    llvm_unreachable("all comparison operators handled");
}

bool isSupportedContractValue(const SymValue &Value) {
    switch (Value.kind) {
    case SymValue::CONST: case SymValue::VAR: return true;
    case SymValue::NEG: return Value.left && isSupportedContractValue(*Value.left);
    case SymValue::ADD: case SymValue::SUB: case SymValue::MUL:
        return Value.left && Value.right && isSupportedContractValue(*Value.left) &&
               isSupportedContractValue(*Value.right);
    default: return false;
    }
}

// A call argument's value for contract checks: its tracked value, or else a
// symbol for its path, so that proofs on the variable (guards) apply.
SymValue contractArgValue(const Expr *Arg, ASTContext &Ctx) {
    auto Value = exprToSymValueImpl(Arg, Ctx);
    const auto Path = getTypedPath(Arg);
    if (Value.kind == SymValue::UNKNOWN && Path.isValid() && !Path.hasRuntimeIndex())
        Value = SymValue::Var(Path.toString());
    return Value;
}

// Does Arg satisfy `Arg Op Bound`? True if its value or the proofs on it
// establish it, false if its known value violates it (Known then holds that
// value), std::nullopt otherwise. Unsigned arguments are not given to the
// solver, whose integers do not wrap around.
std::optional<bool> checkArgBound(const Expr *Arg, ProofOp Op, int64_t Bound,
                                  ASTContext &Ctx, std::optional<int64_t> &Known) {
    auto Value = contractArgValue(Arg, Ctx);
    Known = Value.eval();
    if (Known) return checkProofOp(*Known, Op, Bound);
    if (!Arg->getType()->isUnsignedIntegerType() && isSupportedContractValue(Value) &&
        checkComparisonWithZ3("", Value, SymValue::Const(Bound),
                              proofOpToConstraintKind(Op)) == true)
        return true;
    return std::nullopt;
}

// Effects are conditional on every requirement, independently of diagnostic mode.
// Cache the entry state: a postcondition must not prove its own precondition.
bool callRequirementsProven(CallExpr *Call, ASTContext &Ctx) {
    auto Cached = CallRequirements.find(Call);
    if (Cached != CallRequirements.end()) return Cached->second;
    CallRequirements[Call] = false;
    const auto *Callee = Call->getDirectCallee();
    if (!Callee) return false;
    const Expr *Object = nullptr;
    unsigned ArgOffset = 0;
    if (auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
        Object = Member->getImplicitObjectArgument();
    else if (isa<CXXOperatorCallExpr>(Call) && isa<CXXMethodDecl>(Callee) &&
             Call->getNumArgs()) {
        Object = Call->getArg(0);
        ArgOffset = 1;
    }
    const auto ObjectPath = Object ? getExprPath(Object) : std::string();
    llvm::SmallVector<ConstraintExprPtr, 4> Requirements;
    if (const auto *Method = dyn_cast<CXXMethodDecl>(Callee)) {
        if (Object) {
            MethodKey Key{resolveAnnotationClassName(Method, Object).str(),
                          Method->getNameAsString(), Call->getNumArgs() - ArgOffset};
            auto It = MethodAnnotations.find(Key);
            if (It != MethodAnnotations.end()) Requirements.append(It->second.preconditions);
        }
    }
    if (!Object) {
        for (const auto *Ann : Callee->specific_attrs<AnnotateAttr>()) {
            auto Text = Ann->getAnnotation();
            if (Text.consume_front(kRequiresPrefix)) {
                auto Req = parseConstraintExpr(translateParamNames(Text, Callee));
                if (!Req) return false;
                Requirements.push_back(Req);
            }
        }
    }
    auto ActualArg = [&](unsigned Index) -> Expr * {
        Index += ArgOffset;
        if (Index >= Call->getNumArgs()) return nullptr;
        auto *Arg = Call->getArg(Index)->IgnoreParenImpCasts();
        if (auto *Default = dyn_cast<CXXDefaultArgExpr>(Arg))
            Arg = Default->getExpr()->IgnoreParenImpCasts();
        return Arg;
    };
    auto FieldProven = [&](Expr *Arg, StringRef Field, ProofOp Op, int64_t Value) {
        if (!Arg) return false;
        if (Field == "_nonnull") {
            if (auto Nonnull = inferNonnullFromExpr(Arg))
                return checkProofOp(*Nonnull, Op, Value);
        }
        auto Path = getTypedPath(Arg);
        if (!Path.isValid() || Path.hasRuntimeIndex()) return false;
        const SimpleFieldComparison Comparison{Field.str(), proofOpToConstraintKind(Op), Value};
        return checkSimpleFieldComparisonWithProofs(Path.toString(), Comparison) == true;
    };
    auto ArgValue = [&](Expr *Arg) { return contractArgValue(Arg, Ctx); };
    auto ArgBoundProven = [&](Expr *Arg, ProofOp Op, int64_t Bound) {
        std::optional<int64_t> Known;
        return checkArgBound(Arg, Op, Bound, Ctx, Known) == true;
    };
    auto Prove = [&](const auto &Self, const ConstraintExprPtr &Req) -> bool {
        if (Req->kind == ConstraintExpr::LogicalAnd || Req->kind == ConstraintExpr::LogicalOr) {
            const auto *Binary = static_cast<BinaryExpr *>(Req.get());
            if (Req->kind == ConstraintExpr::LogicalAnd)
                return Self(Self, Binary->left) && Self(Self, Binary->right);
            return Self(Self, Binary->left) || Self(Self, Binary->right);
        }
        if (auto ArgComparison = asArgVsConstComparison(Req)) {
            auto *Arg = ActualArg(ArgComparison->argIdx);
            if (!Arg) return false;
            if (!ArgComparison->argField.empty()) {
                auto Op = constraintExprKindToProofOp(ArgComparison->op);
                return Op && FieldProven(Arg, ArgComparison->argField, *Op, ArgComparison->value);
            }
            if (Arg->getType()->isPointerType() || Arg->getType()->isNullPtrType()) {
                if (ArgComparison->value != 0) return false;
                auto Op = constraintExprKindToProofOp(ArgComparison->op);
                return Op && FieldProven(Arg, "_nonnull", *Op, 0);
            }
            auto Op = constraintExprKindToProofOp(ArgComparison->op);
            return Op && ArgBoundProven(Arg, *Op, ArgComparison->value);
        }
        if (auto Bound = asArgVsFieldComparison(Req)) {
            auto *Arg = ActualArg(Bound->argIdx);
            if (!Arg) return false;
            auto Op = constraintExprKindToProofOp(Bound->op);
            const auto Path = getTypedPath(Arg);
            if (Op && Path.isValid() &&
                hasVarBoundProof(Path.toString(), ObjectPath, Bound->rhsField, *Op)) return true;
            auto Left = ArgValue(Arg);
            auto Right = getField(ObjectPath, Bound->rhsField);
            if (auto L = Left.eval()) {
                if (Bound->op == ConstraintExpr::CmpLT &&
                    checkSimpleFieldComparisonWithProofs(ObjectPath,
                        {Bound->rhsField, ConstraintExpr::CmpGT, *L}) == true) return true;
                if (auto R = Right.eval()) return checkExprOp(Bound->op, *L, *R);
            }
            return isSupportedContractValue(Left) && isSupportedContractValue(Right) &&
                checkComparisonWithZ3(ObjectPath, Left, Right, Bound->op) == true;
        }
        std::map<std::string, SymValue> Args;
        for (unsigned I = 0; I + ArgOffset < Call->getNumArgs(); ++I) {
            llvm::SmallString<16> Key("arg");
            Key += llvm::utostr(I);
            auto *Arg = ActualArg(I);
            Args[Key.str().str()] = ArgValue(Arg);
            auto Path = getTypedPath(Arg);
            if (!Path.isValid()) continue;
            for (const auto &[Field, Value] : State.TypedEnv) {
                auto Name = Field.toStringRef();
                if (startsWithDotPrefix(Name, Path.toStringRef()))
                    Args[(Key + "." + Name.drop_front(Path.toStringRef().size() + 1)).str()] = Value;
            }
        }
        return checkConstraintExprSimple(ObjectPath, Req, Args) == true ||
               evaluateConstraintBool(Req, ObjectPath, Args) == true;
    };
    for (const auto &Req : Requirements)
        if (!Req || !Prove(Prove, Req)) return false;
    if (!Object && Callee->getIdentifier()) {
        auto It = FreeFunctionAnnotations.find({Callee->getName().data(), Call->getNumArgs()});
        if (It != FreeFunctionAnnotations.end()) {
            for (const auto &Req : It->second.preconditions) {
                auto *Arg = ActualArg(Req.argIndex);
                if (!Arg) return false;
                if (Req.isRawArg) {
                    if (!ArgBoundProven(Arg, Req.checkOp, Req.forbiddenValue)) return false;
                } else if (!FieldProven(Arg, Req.field, Req.checkOp, Req.forbiddenValue)) return false;
            }
        }
    }
    if (!inferredCallRequirementsProven(Call, const_cast<FunctionDecl *>(Callee), Ctx)) return false;
    CallRequirements[Call] = true;
    return true;
}

// Discard call effects when the contract could not be established. The pointer
// itself is unchanged by a by-value pointer argument; its pointee facts are not.
void invalidateFailedCallPath(StringRef Path, SourceLocation Loc, bool PreservePointer = false) {
    if (Path.empty()) return;
    llvm::SmallVector<llvm::SmallString<64>, 4> Paths;
    Paths.emplace_back(Path);
    for (unsigned I = 0; I < Paths.size(); ++I) {
        for (const auto &Alias : State.PointerAliases) {
            StringRef Other;
            if (Alias.first() == Paths[I]) Other = Alias.second;
            else if (Alias.second == Paths[I]) Other = Alias.first();
            if (!Other.empty() && !llvm::is_contained(Paths, Other)) Paths.emplace_back(Other);
        }
    }
    for (const auto &Target : Paths) {
        const auto Typed = FieldPath::fromSynthetic(Target.str().str());
        auto Nonnull = getEnvValueTyped(Typed.withSynthetic("_nonnull"));
        auto Initialized = getEnvValueTyped(Typed.withSynthetic("_initialized"));
        llvm::erase_if(State.PendingGuardedPostconditions, [&](const auto &Pending) {
            return Pending.targetPath == Target || startsWithDotPrefix(Pending.targetPath, Target);
        });
        invalidatePropertiesWithProvenance(Target.str().str(), Loc, "call preconditions not proved");
        State.GlobalProofs.invalidateAll(Typed);
        if (PreservePointer && Nonnull) setFieldTyped(Typed, "_nonnull", *Nonnull);
        if (Initialized) setFieldTyped(Typed, "_initialized", *Initialized);
    }
}

void recoverUnprovedCall(CallExpr *Call) {
    auto *Callee = Call->getDirectCallee();
    if (!Callee) return;
    bool HasContract = hasDTTAnnotations(Callee);
    bool HasMutations = false;
    if (auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
        auto *Method = Member->getMethodDecl();
        MethodKey Key{resolveAnnotationClassName(Method, Member->getImplicitObjectArgument()).str(),
                      Method->getNameAsString(), Member->getNumArgs()};
        auto It = MethodAnnotations.find(Key);
        HasContract |= It != MethodAnnotations.end();
        if (It != MethodAnnotations.end() && !It->second.ensures.empty()) {
            HasMutations = true;
            invalidateFailedCallPath(getExprPath(Member->getImplicitObjectArgument()),
                                     Call->getBeginLoc(), Member->getImplicitObjectArgument()->getType()->isPointerType());
        }
        if (!HasContract && !Method->isConst()) {
            auto *Object = Member->getImplicitObjectArgument();
            invalidateFailedCallPath(getExprPath(Object), Call->getBeginLoc(), Object->getType()->isPointerType());
        }
    } else if (auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call)) {
        const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
        if (Method && Operator->getNumArgs()) {
            auto *Object = Operator->getArg(0);
            MethodKey Key{resolveAnnotationClassName(Method, Object).str(),
                          Method->getNameAsString(), Operator->getNumArgs() - 1};
            auto It = MethodAnnotations.find(Key);
            HasContract |= It != MethodAnnotations.end();
            if (It != MethodAnnotations.end() && !It->second.ensures.empty()) {
                HasMutations = true;
                invalidateFailedCallPath(getExprPath(Object), Call->getBeginLoc());
            }
        }
    } else if (Callee->getIdentifier()) {
        auto InvalidateArg = [&](unsigned Index) {
            HasMutations = true;
            if (Index >= Call->getNumArgs()) return;
            auto *Arg = Call->getArg(Index)->IgnoreParenImpCasts();
            invalidateFailedCallPath(getExprPath(Arg), Call->getBeginLoc(), Arg->getType()->isPointerType());
        };
        auto Posts = FnPostconditions.find(Callee->getName());
        if (Posts != FnPostconditions.end())
            for (const auto &Post : Posts->second)
                if (Post.target == PostconditionTarget::Arg) InvalidateArg(Post.argIndex);
        auto Info = FreeFunctionAnnotations.find({Callee->getName().data(), Call->getNumArgs()});
        if (Info != FreeFunctionAnnotations.end())
            for (const auto &Post : Info->second.postconditions) InvalidateArg(Post.argIndex);
    }
    // Honor the same annotation frame as successful calls: absent declared
    // writes, a query preserves its receiver/arguments even when its requires
    // fails. Result guarantees remain blocked by callRequirementsProven().
    // Declared mutations above still invalidate state rather than inventing
    // either successful postconditions or unchanged values for a failed write.
    if (HasContract && !HasMutations) return;
    forEachCallArgument(Call, Callee, [&](const Expr *Arg, QualType Type) {
        if (Type.isNull()) {
            if (!cast<CXXMethodDecl>(Callee)->isConst())
                invalidateFailedCallPath(getExprPath(Arg), Call->getBeginLoc());
            return;
        }
        bool Pointer = Type->isPointerType() && !Type->getPointeeType().isConstQualified();
        if (!Pointer && !isModifiableRef(Type)) return;
        Arg = Arg->IgnoreParenImpCasts();
        invalidateFailedCallPath(getExprPath(Arg), Call->getBeginLoc(), Pointer && !isa<UnaryOperator>(Arg));
    });
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

    // Visit template instantiations so call sites within expanded fold
    // expressions and pack expansions are collected in Pass 1.
    bool shouldVisitTemplateInstantiations() const { return true; }

    bool TraverseFunctionDecl(FunctionDecl* F) {
        if (!F || !F->hasBody()) return true;
        // Skip function template patterns — their instantiations are visited instead
        if (F->getDescribedFunctionTemplate()) return true;
        // Skip system header instantiations (e.g., vector<int>::push_back)
        if (Context.getSourceManager().isInSystemHeader(F->getLocation()))
            return true;

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
        std::string varName = variableKey(VD).str();

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
        std::string varName = variableKey(DRE->getDecl()).str();
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
                                        if (!data.paramDereferenced.contains(callerParamIdx)) {
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
// EXPRESSION DECOMPOSITION FOR TRANSITIVE PROPAGATION
// ============================================================
// Decomposes an argument expression to find the base parameter and offset.
// For `buf + offset`, extracts base param (buf) and offset expression.
// For `s->member`, extracts base param (s) with member path.
//
// This enables transitive propagation through expressions, not just
// direct parameter pass-through.

struct ArgDecomposition {
    std::optional<unsigned> baseParamIdx;  // Caller param that forms the base
    SymValue offsetExpr;                   // Offset expression (Const(0) if none)
    std::string memberPath;                // Member access path (e.g., "._buf")
    bool valid = false;

    ArgDecomposition() : offsetExpr(SymValue::Const(0)) {}
};

// Find which caller parameter a declaration refers to
std::optional<unsigned> findCallerParamIndex(const ValueDecl* decl,
                                              const FunctionDecl* caller) {
    if (auto* PVD = dyn_cast<ParmVarDecl>(decl)) {
        for (unsigned i = 0; i < caller->getNumParams(); ++i) {
            if (caller->getParamDecl(i) == PVD) {
                return i;
            }
        }
    }
    return std::nullopt;
}

// Convert expression to SymValue in terms of caller parameters
// Returns SymValue::Unknown() if expression can't be represented
SymValue exprToParamSymValue(const Expr* E, const FunctionDecl* caller,
                             ASTContext& Ctx) {
    if (!E) return SymValue::Unknown();
    E = E->IgnoreParenImpCasts();

    // Constant integer
    Expr::EvalResult R;
    if (safeEvaluateAsInt(E, R, Ctx)) {
        const llvm::APSInt& Val = R.Val.getInt();
        if (Val.isRepresentableByInt64()) {
            return SymValue::Const(Val.getExtValue());
        }
        return SymValue::Unknown();
    }

    // Parameter reference
    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        if (auto paramIdx = findCallerParamIndex(DRE->getDecl(), caller)) {
            return SymValue::Var((Twine("arg") + Twine(*paramIdx)).str());
        }
        return SymValue::Unknown();
    }

    // Binary operator
    if (auto* BO = dyn_cast<BinaryOperator>(E)) {
        SymValue left = exprToParamSymValue(BO->getLHS(), caller, Ctx);
        SymValue right = exprToParamSymValue(BO->getRHS(), caller, Ctx);
        if (left.kind == SymValue::UNKNOWN || right.kind == SymValue::UNKNOWN) {
            return SymValue::Unknown();
        }
        switch (BO->getOpcode()) {
            case BO_Add: return SymValue::Add(left, right);
            case BO_Sub: return SymValue::Sub(left, right);
            case BO_Mul: return SymValue::Mul(left, right);
            case BO_Div: return SymValue::Div(left, right);
            default: return SymValue::Unknown();
        }
    }

    // sizeof
    if (auto* UETT = dyn_cast<UnaryExprOrTypeTraitExpr>(E)) {
        if (UETT->getKind() == UETT_SizeOf) {
            CharUnits sz = Ctx.getTypeSizeInChars(UETT->getTypeOfArgument());
            return SymValue::Const(sz.getQuantity());
        }
    }

    return SymValue::Unknown();
}

// Decompose a pointer argument expression
// For `buf + offset`: base=buf, offset=offset
// For `s->member`: base=s, memberPath="._member"
ArgDecomposition decomposePointerArg(const Expr* arg, const FunctionDecl* caller,
                                      ASTContext& Ctx) {
    ArgDecomposition result;
    if (!arg) return result;
    arg = arg->IgnoreParenImpCasts();

    // Direct parameter reference
    if (auto* DRE = dyn_cast<DeclRefExpr>(arg)) {
        if (auto idx = findCallerParamIndex(DRE->getDecl(), caller)) {
            result.baseParamIdx = idx;
            result.valid = true;
            return result;
        }
    }

    // Pointer arithmetic: ptr + offset or ptr - offset
    if (auto* BO = dyn_cast<BinaryOperator>(arg)) {
        if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
            const Expr* lhs = BO->getLHS()->IgnoreParenImpCasts();
            const Expr* rhs = BO->getRHS()->IgnoreParenImpCasts();

            // Check if LHS is a pointer/array (base) and RHS is the offset
            // Arrays decay to pointers in arithmetic, so handle both
            if (lhs->getType()->isPointerType() || lhs->getType()->isArrayType()) {
                ArgDecomposition baseDecomp = decomposePointerArg(lhs, caller, Ctx);
                if (baseDecomp.valid) {
                    SymValue offsetVal = exprToParamSymValue(rhs, caller, Ctx);
                    if (offsetVal.kind != SymValue::UNKNOWN) {
                        result = baseDecomp;
                        if (BO->getOpcode() == BO_Add) {
                            result.offsetExpr = SymValue::Add(baseDecomp.offsetExpr, offsetVal);
                        } else {
                            result.offsetExpr = SymValue::Sub(baseDecomp.offsetExpr, offsetVal);
                        }
                        return result;
                    }
                }
            }
        }
    }

    // Member access: s->member or s.member
    if (auto* ME = dyn_cast<MemberExpr>(arg)) {
        const Expr* base = ME->getBase()->IgnoreParenImpCasts();
        ArgDecomposition baseDecomp = decomposePointerArg(base, caller, Ctx);
        if (baseDecomp.valid) {
            result = baseDecomp;
            result.memberPath = baseDecomp.memberPath + "._" +
                               ME->getMemberDecl()->getNameAsString();
            return result;
        }
    }

    // Array subscript: arr[i] is similar to arr + i
    if (auto* ASE = dyn_cast<ArraySubscriptExpr>(arg)) {
        const Expr* base = ASE->getBase()->IgnoreParenImpCasts();
        const Expr* idx = ASE->getIdx()->IgnoreParenImpCasts();

        ArgDecomposition baseDecomp = decomposePointerArg(base, caller, Ctx);
        if (baseDecomp.valid) {
            SymValue idxVal = exprToParamSymValue(idx, caller, Ctx);
            if (idxVal.kind != SymValue::UNKNOWN) {
                result = baseDecomp;
                // arr[i] means offset of i * element_size
                QualType elemType = base->getType()->getPointeeType();
                if (!elemType.isNull()) {
                    int64_t elemSize = Ctx.getTypeSizeInChars(elemType).getQuantity();
                    SymValue scaledIdx = SymValue::Mul(idxVal, SymValue::Const(elemSize));
                    result.offsetExpr = SymValue::Add(baseDecomp.offsetExpr, scaledIdx);
                    return result;
                }
            }
        }
    }

    return result;
}

// ============================================================
// TRANSITIVE PRECONDITION PROPAGATION
// ============================================================
// Computes the transitive closure of inferred preconditions.
//
// When function F calls G, and G has a precondition on parameter Q,
// F inherits that precondition (with remapped indices) if F passes
// one of its own parameters to G's parameter Q.
//
// Now supports expressions: buf+offset, s->member, size/2, etc.
//
// Uses fixpoint iteration: repeat until no new preconditions are added.
//
// ============================================================
// DATALOG-STYLE FIXPOINT ENGINE
// ============================================================
// This implements semi-naive evaluation of the following Datalog program:
//
//   % Generic property propagation rule:
//   requires(Caller, K, Field, Op, Val) :-
//       requires(Callee, J, Field, Op, Val),
//       calls(Caller, Callee, _, K, J),
//       internal(Caller),
//       constraint_kind(Callee, J, generic).
//
//   % Buffer-specific propagation rule (with pointer arithmetic):
//   requires(Caller, BaseK, "_size_bytes", GE, bound + offset) :-
//       requires(Callee, J, "_size_bytes", GE, bound),
//       calls(Caller, Callee, _, K, J),
//       pointer_arith(K, BaseK, offset),
//       internal(Caller),
//       constraint_kind(Callee, J, buffer).
//
// The two rules handle distinct constraint kinds: generic properties
// (null, resource state, optional) use simple parameter index mapping,
// while buffer sizes require pointer arithmetic decomposition.
//
// Monotonicity: each iteration only adds facts (never removes).
// Termination: finite functions x params x distinct constraints = finite lattice.
// Bounded by kMaxFixpointIterations as safety net.

void propagatePreconditionsTransitively() {
    bool changed = true;
    int iterations = 0;

    while (changed && iterations < kMaxFixpointIterations) {
        changed = false;
        ++iterations;

        // Semi-naive: collect new facts derived this iteration
        llvm::DenseMap<const FunctionDecl*,
                       llvm::SmallVector<dtt::InferredPrecondition, 4>> toAdd;

        for (const auto& [calleeDecl, preconditions] : FnPreconditions) {
            if (preconditions.empty()) continue;

            std::string calleeKey = getFunctionKey(calleeDecl);
            auto analysisIt = FunctionAnalysis.find(calleeKey);
            if (analysisIt == FunctionAnalysis.end()) continue;

            for (const CallSiteInfo& site : analysisIt->second.callSites) {
                const FunctionDecl* caller = site.callerFunc;
                if (!caller || !hasInternalLinkage(caller)) continue;

                const FunctionDecl* callerCanonical = caller->getCanonicalDecl();

                for (const auto& pre : preconditions) {
                    if (!pre.argIndex) continue;

                    unsigned calleeParamIdx = *pre.argIndex;
                    if (calleeParamIdx >= site.callExpr->getNumArgs()) continue;

                    const Expr* arg = site.callExpr->getArg(calleeParamIdx);
                    ASTContext& Ctx = const_cast<ASTContext&>(caller->getASTContext());

                    dtt::InferredPrecondition propagated;

                    if (pre.isGeneric()) {
                        // ====================================================
                        // GENERIC PROPERTY PROPAGATION (Datalog-style)
                        // ====================================================
                        // Simple parameter index mapping — no pointer
                        // arithmetic decomposition needed.
                        // Rule: requires(Caller, K, Field, Op, Val) :-
                        //          requires(Callee, J, Field, Op, Val),
                        //          calls(Caller, Callee, _, K, J),
                        //          internal(Caller).
                        auto callerParamIdx = getParameterIndex(arg, caller);
                        if (!callerParamIdx) continue;

                        propagated.path =
                            (Twine("arg") + Twine(*callerParamIdx)).str();
                        propagated.kind = dtt::PreconditionKind::Generic;
                        propagated.argIndex = *callerParamIdx;
                        propagated.field = pre.field;
                        propagated.constraintOp = pre.constraintOp;
                        propagated.constraintValue = pre.constraintValue;
                        propagated.errorReason = pre.errorReason;
                    } else {
                        // ====================================================
                        // BUFFER-SPECIFIC PROPAGATION (pointer arithmetic)
                        // ====================================================
                        // Decompose the buffer argument (supports ptr+offset, s->member)
                        ArgDecomposition bufDecomp = decomposePointerArg(arg, caller, Ctx);
                        if (!bufDecomp.valid || !bufDecomp.baseParamIdx) continue;

                        unsigned callerBufIdx = *bufDecomp.baseParamIdx;

                        // Convert bound argument to SymValue expression
                        SymValue boundExpr = SymValue::Unknown();
                        std::optional<unsigned> callerBoundIdx;
                        if (pre.boundArgIndex && *pre.boundArgIndex < site.callExpr->getNumArgs()) {
                            const Expr* boundArg = site.callExpr->getArg(*pre.boundArgIndex);
                            boundExpr = exprToParamSymValue(boundArg, caller, Ctx);

                            StringRef varRefStr(boundExpr.varRef);
                            if (boundExpr.kind == SymValue::VAR && varRefStr.starts_with("arg")) {
                                unsigned idx;
                                if (!varRefStr.drop_front(3).getAsInteger(10, idx)) {
                                    callerBoundIdx = idx;
                                }
                            }
                        } else if (pre.boundConstant != 0) {
                            boundExpr = SymValue::Const(pre.boundConstant);
                        }

                        // Convert offset argument to SymValue expression
                        SymValue offsetExpr = SymValue::Const(0);
                        std::optional<unsigned> callerOffsetIdx;
                        if (pre.offsetArgIndex && *pre.offsetArgIndex < site.callExpr->getNumArgs()) {
                            const Expr* offArg = site.callExpr->getArg(*pre.offsetArgIndex);
                            offsetExpr = exprToParamSymValue(offArg, caller, Ctx);

                            StringRef offVarRefStr(offsetExpr.varRef);
                            if (offsetExpr.kind == SymValue::VAR && offVarRefStr.starts_with("arg")) {
                                unsigned idx;
                                if (!offVarRefStr.drop_front(3).getAsInteger(10, idx)) {
                                    callerOffsetIdx = idx;
                                }
                            }
                        } else if (pre.offsetConstant != 0) {
                            offsetExpr = SymValue::Const(pre.offsetConstant);
                        }

                        // Compose the effective offset: original offset + pointer arithmetic offset
                        SymValue effectiveOffset = offsetExpr;
                        if (bufDecomp.offsetExpr.kind != SymValue::CONST ||
                            bufDecomp.offsetExpr.constVal != 0) {
                            effectiveOffset = SymValue::Add(offsetExpr, bufDecomp.offsetExpr);
                        }

                        // Build path including member access if present
                        std::string callerPath = (Twine("arg") + Twine(callerBufIdx)).str();
                        if (!bufDecomp.memberPath.empty()) {
                            callerPath += bufDecomp.memberPath;
                        }

                        propagated.path = callerPath;
                        propagated.kind = pre.kind;

                        // Build bound string incorporating all expressions
                        if (boundExpr.kind != SymValue::UNKNOWN) {
                            if (effectiveOffset.kind == SymValue::CONST && effectiveOffset.constVal == 0) {
                                propagated.boundStr = boundExpr.str();
                            } else {
                                propagated.boundStr = (Twine("(") + boundExpr.str() + "+" +
                                                       effectiveOffset.str() + ")").str();
                            }
                        } else {
                            propagated.boundStr = pre.boundStr;
                        }
                        propagated.argIndex = callerBufIdx;
                        propagated.boundArgIndex = callerBoundIdx;
                        propagated.boundConstant = pre.boundConstant;
                        propagated.offsetArgIndex = callerOffsetIdx;
                        propagated.offsetConstant = pre.offsetConstant;
                        propagated.offsetElemSize = pre.offsetElemSize;
                        propagated.offsetNegated = pre.offsetNegated;
                    }

                    // Check for duplicate (check in both existing and pending)
                    auto existsIn = [&](const llvm::SmallVectorImpl<dtt::InferredPrecondition>& vec) {
                        if (propagated.isGeneric()) {
                            return llvm::any_of(vec, [&](const auto& existing) {
                                return existing.isGeneric() &&
                                       existing.argIndex == propagated.argIndex &&
                                       existing.field == propagated.field &&
                                       existing.constraintOp == propagated.constraintOp &&
                                       existing.constraintValue == propagated.constraintValue;
                            });
                        }
                        return llvm::any_of(vec, [&](const auto& existing) {
                            return existing.path == propagated.path &&
                                   existing.kind == propagated.kind &&
                                   existing.boundStr == propagated.boundStr;
                        });
                    };

                    auto existingIt = FnPreconditions.find(callerCanonical);
                    bool inExisting = (existingIt != FnPreconditions.end()) &&
                                      existsIn(existingIt->second);
                    bool inPending = existsIn(toAdd[callerCanonical]);

                    if (!inExisting && !inPending) {
                        toAdd[callerCanonical].push_back(std::move(propagated));
                        changed = true;
                    }
                }
            }
        }

        // Apply collected preconditions after iteration completes
        for (auto& [caller, newPreconditions] : toAdd) {
            auto& existing = FnPreconditions[caller];
            existing.append(newPreconditions.begin(), newPreconditions.end());
        }
    }
}

// ============================================================
// TRANSITIVE FRAME EFFECT PROPAGATION
// ============================================================
// Computes the transitive closure of field write effects.
//
// If F calls G, passing F's parameter P to G's parameter Q, and G writes
// field X through Q, then F also effectively writes field X through P.
// Fields NOT in the write set are "framed" (preserved across the call).
//
void propagateFrameEffectsTransitively() {
    bool changed = true;
    int iterations = 0;

    while (changed && iterations < kMaxFixpointIterations) {
        changed = false;
        ++iterations;

        // Iterate over callees and their call sites (O(call sites), not O(functions²))
        for (auto& [calleeKey, calleeData] : FunctionAnalysis) {
            if (calleeData.paramWrites.empty() && calleeData.isPure) continue;

            for (const CallSiteInfo& site : calleeData.callSites) {
                const FunctionDecl* caller = site.callerFunc;
                if (!caller) continue;

                // Look up caller's analysis data
                std::string callerKey = getFunctionKey(caller);
                auto callerIt = FunctionAnalysis.find(callerKey);
                if (callerIt == FunctionAnalysis.end()) continue;
                auto& callerData = callerIt->second;

                for (const auto& [calleeParamIdx, writtenFields] : calleeData.paramWrites) {
                    if (calleeParamIdx >= site.callExpr->getNumArgs()) continue;

                    const Expr* arg = site.callExpr->getArg(calleeParamIdx)->IgnoreParenImpCasts();
                    auto* DRE = dyn_cast<DeclRefExpr>(arg);
                    if (!DRE) continue;
                    auto* PVD = dyn_cast<ParmVarDecl>(DRE->getDecl());
                    if (!PVD) continue;

                    // Find which caller parameter this corresponds to
                    for (unsigned callerParamIdx = 0; callerParamIdx < caller->getNumParams(); ++callerParamIdx) {
                        if (caller->getParamDecl(callerParamIdx) == PVD) {
                            auto& callerWrites = callerData.paramWrites[callerParamIdx];
                            for (const auto& field : writtenFields) {
                                if (!callerWrites.contains(field.getKey())) {
                                    callerWrites.insert(field.getKey());
                                    callerData.isPure = false;
                                    changed = true;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
}

/// Returns true if function F writes to the given field of parameter paramIdx.
/// Returns true (conservative) if F is unknown or not analyzed.
bool functionWritesField(const FunctionDecl* F, unsigned paramIdx, llvm::StringRef field) {
    if (!F) return true;

    std::string funcKey = getFunctionKey(F);
    auto it = FunctionAnalysis.find(funcKey);
    if (it == FunctionAnalysis.end()) return true;

    const auto& data = it->second;
    auto writeIt = data.paramWrites.find(paramIdx);
    if (writeIt == data.paramWrites.end()) {
        // No writes to this parameter - field is preserved (in frame)
        return false;
    }

    // Check if this specific field is written
    return writeIt->second.contains(field);
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
    if (GlobalSarifConsumer) {
        GlobalSarifConsumer->registerDiag(ErrCallSiteNull, &dtt::RuleNullDeref);
        GlobalSarifConsumer->registerNote(NoteCallSiteGuard);
    }

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
                        argStr = variableKey(DRE->getDecl()).str();
                    } else {
                        llvm::raw_string_ostream OS(argStr);
                        arg->printPretty(OS, nullptr, Ctx.getPrintingPolicy());
                    }

                    Diag.Report(site.loc, ErrCallSiteNull)
                        << argStr
                        << param->getName()
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
// TRANSITIVE PRECONDITION CALL-SITE CHECKING
// ============================================================
// After propagating preconditions transitively, check call sites where
// concrete buffer sizes are passed to functions with propagated preconditions.
// This catches cases like: level0_violation() passes small[4] to level1_wrapper()
// which transitively requires buf._size_bytes >= n.

void emitTransitivePreconditionWarnings(ASTContext& Ctx) {
    DiagnosticsEngine& Diag = Ctx.getDiagnostics();

    unsigned WarnPrecondViolation = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
        "DTT: buffer '%0' (size %1) may be too small for '%2' which requires %3 bytes [-Wdtt-transitive-precondition]");
    unsigned NotePrecondSource = Diag.getCustomDiagID(DiagnosticsEngine::Note,
        "precondition propagated through call chain from '%0'");
    if (GlobalSarifConsumer) {
        GlobalSarifConsumer->registerDiag(WarnPrecondViolation, &dtt::RuleUnverifiedPrecondition);
        GlobalSarifConsumer->registerNote(NotePrecondSource);
    }

    // For each function with propagated preconditions
    for (const auto& [calleeDecl, preconditions] : FnPreconditions) {
        if (preconditions.empty()) continue;

        std::string calleeKey = getFunctionKey(calleeDecl);
        auto analysisIt = FunctionAnalysis.find(calleeKey);
        if (analysisIt == FunctionAnalysis.end()) continue;

        // Cache callee name for diagnostics
        StringRef calleeName = safeGetName(calleeDecl);
        if (calleeName.empty()) continue;

        // Check each call site
        for (const CallSiteInfo& site : analysisIt->second.callSites) {
            for (const auto& pre : preconditions) {
                if (pre.kind != dtt::PreconditionKind::SizeGE) continue;
                if (!pre.argIndex || *pre.argIndex >= site.callExpr->getNumArgs()) continue;

                const Expr* bufArg = site.callExpr->getArg(*pre.argIndex)->IgnoreParenImpCasts();

                // Get concrete buffer size if available
                std::optional<int64_t> bufSizeBytes;
                SmallString<64> bufNameStr;

                // Check if path contains member access
                // Handles both formats: "arg0._buf" (propagated) and "s.data" (direct)
                StringRef pathRef(pre.path);
                size_t memberPos = pathRef.find("._");
                bool hasUnderscorePrefix = (memberPos != StringRef::npos);
                if (!hasUnderscorePrefix) {
                    // Try finding bare dot (but skip synthetic properties like ._size)
                    memberPos = pathRef.find('.');
                }
                bool hasMemberPath = (memberPos != StringRef::npos);

                if (hasMemberPath) {
                    // Member path: need to resolve struct.member size
                    // Get the struct type from the argument
                    QualType argType = bufArg->getType();

                    // Handle pointer to struct (s->buf case)
                    if (argType->isPointerType()) {
                        argType = argType->getPointeeType();
                    }
                    // Handle reference to struct
                    if (argType->isReferenceType()) {
                        argType = argType.getNonReferenceType();
                    }

                    // Get the record (struct/class) declaration
                    if (const RecordType* RT = argType->getAs<RecordType>()) {
                        const RecordDecl* RD = RT->getDecl();

                        // Extract member path from precondition path
                        // For "arg0._buf": memberPath="_buf"
                        // For "arg0._inner._buf": memberPath="_inner._buf" (nested)
                        StringRef memberPath = pathRef.substr(memberPos + 1); // Skip the dot

                        // Navigate through nested struct members to find the final array
                        SmallString<64> displayPath;
                        if (auto* DRE = dyn_cast<DeclRefExpr>(bufArg)) {
                            displayPath = variableKey(DRE->getDecl());
                        }

                        while (!memberPath.empty() && RD) {
                            // Get next segment
                            size_t nextDot = memberPath.find('.');
                            StringRef segment = (nextDot != StringRef::npos)
                                ? memberPath.substr(0, nextDot)
                                : memberPath;

                            // Strip leading underscore if present
                            StringRef fieldToMatch = segment;
                            if (fieldToMatch.starts_with("_")) {
                                fieldToMatch = fieldToMatch.drop_front(1);
                            }

                            // Find the field in current record
                            const FieldDecl* foundField = nullptr;
                            for (const FieldDecl* FD : RD->fields()) {
                                if (FD->getName() == fieldToMatch) {
                                    foundField = FD;
                                    break;
                                }
                            }

                            if (!foundField) break;

                            QualType fieldType = foundField->getType();
                            displayPath += ".";
                            displayPath += foundField->getName();

                            // If this is an array, we found our target
                            if (auto* CAT = dyn_cast<ConstantArrayType>(fieldType.getTypePtr())) {
                                uint64_t numElems = CAT->getSize().getZExtValue();
                                if (auto elemSize = safeGetTypeSizeInChars(Ctx, CAT->getElementType())) {
                                    bufSizeBytes = static_cast<int64_t>(numElems * (*elemSize));
                                    bufNameStr = displayPath;
                                }
                                break;
                            }

                            // If this is a nested struct, continue navigation
                            if (const RecordType* nestedRT = fieldType->getAs<RecordType>()) {
                                RD = nestedRT->getDecl();
                                // Move to next segment
                                if (nextDot != StringRef::npos) {
                                    memberPath = memberPath.substr(nextDot + 1);
                                } else {
                                    break;  // No more segments
                                }
                            } else {
                                break;  // Not a struct, can't continue
                            }
                        }
                    }
                } else {
                    // Direct array argument: char small[4]
                    if (auto* DRE = dyn_cast<DeclRefExpr>(bufArg)) {
                        bufNameStr = variableKey(DRE->getDecl());
                        if (auto* VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                            QualType T = VD->getType();
                            if (auto* CAT = dyn_cast<ConstantArrayType>(T.getTypePtr())) {
                                uint64_t numElems = CAT->getSize().getZExtValue();
                                if (auto elemSize = safeGetTypeSizeInChars(Ctx, CAT->getElementType())) {
                                    bufSizeBytes = static_cast<int64_t>(numElems * (*elemSize));
                                }
                            }
                        }
                    }
                }

                StringRef bufName(bufNameStr);
                if (!bufSizeBytes) continue;  // Can only check concrete sizes

                // Use Z3 to evaluate if bufSize < boundExpr is satisfiable
                bool shouldWarn = false;
                StringRef requiredStr = pre.boundStr;

                // Parse bound expression to SymValue
                SymValue boundSym = parseBoundStr(pre.boundStr);

                // Collect actual argument values for substitution
                // Map both argN format and actual parameter names
                llvm::StringMap<SymValue> argSubst;
                SmallString<16> argKey;
                for (unsigned i = 0; i < site.callExpr->getNumArgs(); ++i) {
                    const Expr* arg = site.callExpr->getArg(i)->IgnoreParenImpCasts();
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(arg, R, const_cast<ASTContext&>(Ctx))) {
                        SymValue val = SymValue::Const(R.Val.getInt().getExtValue());
                        // Add argN format
                        argKey.clear();
                        (Twine("arg") + Twine(i)).toVector(argKey);
                        argSubst[argKey] = val;
                        // Add actual parameter name if available
                        if (i < calleeDecl->getNumParams()) {
                            StringRef paramName = calleeDecl->getParamDecl(i)->getName();
                            if (!paramName.empty()) {
                                argSubst[paramName] = val;
                            }
                        }
                    }
                }

                // Substitute actual values into the bound expression
                SymValue boundWithValues = boundSym.substitute(argSubst);

                // If we can evaluate to a constant, do simple comparison
                if (auto constBound = boundWithValues.eval()) {
                    if (*bufSizeBytes < *constBound) {
                        shouldWarn = true;
                    }
                    // If bufSize >= constBound, no warning needed
                } else if (boundSym.kind != SymValue::UNKNOWN) {
                    // Use Z3 to check if bufSize < bound is satisfiable
                    try {
                        z3::context z3ctx;
                        z3::solver solver(z3ctx);
                        std::map<std::string, z3::expr> vars;

                        // Add known argument values as constraints
                        // Note: getOrCreateVar requires std::string for Z3 int_const stability
                        for (const auto& entry : argSubst) {
                            if (entry.second.kind == SymValue::CONST) {
                                z3::expr var = getOrCreateVar(z3ctx, vars, entry.first().str());
                                solver.add(var == z3ctx.int_val(entry.second.constVal));
                            }
                        }

                        // Convert bound expression to Z3
                        z3::expr boundZ3 = symValueToZ3(boundSym, z3ctx, vars);

                        // Check: is bufSize < bound satisfiable?
                        z3::expr bufSizeZ3 = z3ctx.int_val(*bufSizeBytes);
                        solver.add(bufSizeZ3 < boundZ3);

                        z3::check_result result = solver.check();
                        if (result == z3::sat) {
                            // Buffer might be too small - warn
                            shouldWarn = true;
                        }
                        // If unsat: buffer is always large enough, no warning
                        // If unknown: be conservative, warn
                        if (result == z3::unknown) {
                            shouldWarn = true;
                        }
                    } catch (...) {
                        // Z3 error - be conservative, warn
                        shouldWarn = true;
                    }
                } else if (!pre.boundStr.empty() && pre.boundStr != "0") {
                    // Couldn't parse - be conservative, warn
                    shouldWarn = true;
                }

                if (shouldWarn) {
                    Diag.Report(site.loc, WarnPrecondViolation)
                        << bufName << *bufSizeBytes
                        << calleeName << requiredStr;
                    Diag.Report(site.loc, NotePrecondSource)
                        << calleeName;
                }
            }
        }
    }

    // ========================================================
    // GENERIC PROPERTY CONSTRAINT CHECKING (Datalog-style)
    // ========================================================
    // After transitive propagation, check generic constraints at call sites
    // where concrete values are known (e.g., passing nullptr to a function
    // that transitively requires _nonnull != 0).

    unsigned ErrGenericPrecond = Diag.getCustomDiagID(DiagnosticsEngine::Error,
        "DTT: '%0' requires %1.%2 %3 %4, but %5 %6 [-Wdtt-transitive-generic]");
    unsigned NoteGenericPrecondSource = Diag.getCustomDiagID(DiagnosticsEngine::Note,
        "precondition propagated transitively from '%0'");
    if (GlobalSarifConsumer) {
        GlobalSarifConsumer->registerDiag(ErrGenericPrecond, &dtt::RulePrecondition);
        GlobalSarifConsumer->registerNote(NoteGenericPrecondSource);
    }

    for (const auto& [calleeDecl, preconditions] : FnPreconditions) {
        if (preconditions.empty()) continue;

        // Check if there are any generic preconditions
        bool hasGeneric = false;
        for (const auto& pre : preconditions) {
            if (pre.isGeneric()) { hasGeneric = true; break; }
        }
        if (!hasGeneric) continue;

        std::string calleeKey = getFunctionKey(calleeDecl);
        auto analysisIt = FunctionAnalysis.find(calleeKey);
        if (analysisIt == FunctionAnalysis.end()) continue;

        StringRef calleeName = safeGetName(calleeDecl);
        if (calleeName.empty()) continue;

        for (const CallSiteInfo& site : analysisIt->second.callSites) {
            for (const auto& pre : preconditions) {
                if (!pre.isGeneric()) continue;
                if (!pre.argIndex || *pre.argIndex >= site.callExpr->getNumArgs()) continue;

                const Expr* arg = site.callExpr->getArg(*pre.argIndex)->IgnoreParenImpCasts();

                // Try to evaluate the argument to a known value
                // For null pointer checks: nullptr has _nonnull = 0
                SmallString<64> argNameStr;
                if (auto* DRE = dyn_cast<DeclRefExpr>(arg)) {
                    argNameStr = variableKey(DRE->getDecl());
                } else {
                    llvm::raw_svector_ostream OS(argNameStr);
                    arg->printPretty(OS, nullptr, Ctx.getPrintingPolicy());
                }

                // For _nonnull checks, see if the argument is provably null
                if (pre.field == "_nonnull" && pre.constraintOp == dtt::ProofOp::NE &&
                    pre.constraintValue == 0) {
                    // Check if argument is a null pointer literal
                    bool isNull = false;
                    if (auto* IL = dyn_cast<IntegerLiteral>(arg)) {
                        isNull = (IL->getValue() == 0);
                    }
                    if (arg->isNullPointerConstant(const_cast<ASTContext&>(Ctx),
                            Expr::NPC_ValueDependentIsNotNull)) {
                        isNull = true;
                    }
                    // Check if argument is a variable known to be null
                    if (auto* DRE = dyn_cast<DeclRefExpr>(arg)) {
                        if (auto* VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                            if (VD->hasInit()) {
                                const Expr* init = VD->getInit()->IgnoreParenImpCasts();
                                if (init->isNullPointerConstant(const_cast<ASTContext&>(Ctx),
                                        Expr::NPC_ValueDependentIsNotNull)) {
                                    isNull = true;
                                }
                            }
                        }
                    }

                    if (isNull) {
                        SmallString<8> opStr(" != ");
                        Diag.Report(site.callExpr->getBeginLoc(), ErrGenericPrecond)
                            << calleeName
                            << (llvm::Twine("arg") + llvm::Twine(*pre.argIndex)).str()
                            << pre.field.str()
                            << opStr.str()
                            << std::to_string(pre.constraintValue)
                            << argNameStr.str()
                            << "is null";
                        Diag.Report(site.callExpr->getBeginLoc(), NoteGenericPrecondSource)
                            << calleeName;
                    }
                }
            }
        }
    }
}

// ============================================================
// VISITOR
// ============================================================

// Resolve the byte capacity and required byte count used by inferred contracts.
// A missing expression cannot establish a contract, including an unknown offset.
std::optional<std::pair<SymValue, SymValue>> getInferredCallBounds(
    CallExpr *Call, const dtt::InferredPrecondition &Pre, ASTContext &Ctx) {
    if (!Pre.argIndex || *Pre.argIndex >= Call->getNumArgs())
        return std::nullopt;
    Expr *ActualBufArg = Call->getArg(*Pre.argIndex)->IgnoreParenImpCasts();
    FieldPath BufPath = getTypedPath(ActualBufArg);
    SymValue BufferSize = SymValue::Unknown();
    if (BufPath.isValid()) {
        BufferSize = getFieldTyped(BufPath, "_size_bytes");
        if (BufferSize.kind == SymValue::UNKNOWN) {
            SymValue Remaining = getFieldTyped(BufPath, "_size");
            QualType Type = ActualBufArg->getType();
            QualType ElementType;
            if (Type->isPointerType())
                ElementType = Type->getPointeeType();
            else if (const auto *Array = Ctx.getAsArrayType(Type))
                ElementType = Array->getElementType();
            if (Remaining.kind != SymValue::UNKNOWN && !ElementType.isNull()) {
                if (auto Size = safeGetTypeSizeInChars(Ctx, ElementType))
                    BufferSize = SymValue::Mul(Remaining, SymValue::Const(*Size));
            }
        }
    }

    SymValue Bound = SymValue::Unknown();
    if (Pre.boundArgIndex) {
        if (*Pre.boundArgIndex >= Call->getNumArgs()) return std::nullopt;
        auto Value = buildSymValueFromExpr(Call->getArg(*Pre.boundArgIndex));
        if (!Value) return std::nullopt;
        Bound = SymValue::Add(*Value, SymValue::Const(Pre.boundConstant));
    } else if (Pre.boundConstant != 0) {
        Bound = SymValue::Const(Pre.boundConstant);
    }
    if (Bound.kind == SymValue::UNKNOWN) return std::nullopt;

    if (Pre.offsetArgIndex) {
        if (*Pre.offsetArgIndex >= Call->getNumArgs()) return std::nullopt;
        auto Offset = buildSymValueFromExpr(Call->getArg(*Pre.offsetArgIndex));
        if (!Offset) return std::nullopt;
        SymValue Scaled = SymValue::Mul(*Offset, SymValue::Const(Pre.offsetElemSize));
        Bound = Pre.offsetNegated ? SymValue::Sub(Bound, Scaled)
                                  : SymValue::Add(Bound, Scaled);
    }
    if (Pre.offsetConstant != 0)
        Bound = SymValue::Add(Bound, SymValue::Const(Pre.offsetConstant));
    return std::make_pair(BufferSize, Bound);
}

bool inferredCallRequirementsProven(CallExpr *Call, FunctionDecl *Callee,
                                   ASTContext &Ctx) {
    auto It = FnPreconditions.find(Callee->getCanonicalDecl());
    if (It == FnPreconditions.end()) return true;
    for (const auto &Pre : It->second) {
        if (!Pre.argIndex || *Pre.argIndex >= Call->getNumArgs()) return false;
        Expr *Arg = Call->getArg(*Pre.argIndex)->IgnoreParenImpCasts();
        FieldPath Path = getTypedPath(Arg);
        if (Pre.kind == dtt::PreconditionKind::SizeGE) {
            auto Bounds = getInferredCallBounds(Call, Pre, Ctx);
            if (!Bounds || Bounds->first.kind == SymValue::UNKNOWN) return false;
            const auto &[Capacity, Required] = *Bounds;
            if (Capacity.kind == SymValue::CONST && Required.kind == SymValue::CONST) {
                if (Capacity.constVal < Required.constVal) return false;
            } else if (checkComparisonWithZ3("_callsite", Capacity, Required,
                         ConstraintExpr::CmpGE, /*assumeNonNegative=*/true) != true) {
                return false;
            }
            continue;
        }
        if (Pre.kind == dtt::PreconditionKind::NonNull) {
            auto Nonnull = inferNonnullFromExpr(Arg);
            if (Nonnull) {
                if (*Nonnull == 0) return false;
            } else if (!Path.isValid() ||
                       checkSimpleFieldComparisonWithProofs(
                           Path.toString(), {"_nonnull", ConstraintExpr::CmpNE, 0}) != true) {
                return false;
            }
            continue;
        }
        if (!Pre.isGeneric() || !Path.isValid()) return false;
        ConstraintExpr::Kind Op;
        switch (Pre.constraintOp) {
        case ProofOp::EQ: Op = ConstraintExpr::CmpEQ; break;
        case ProofOp::NE: Op = ConstraintExpr::CmpNE; break;
        case ProofOp::LT: Op = ConstraintExpr::CmpLT; break;
        case ProofOp::LE: Op = ConstraintExpr::CmpLE; break;
        case ProofOp::GT: Op = ConstraintExpr::CmpGT; break;
        case ProofOp::GE: Op = ConstraintExpr::CmpGE; break;
        }
        SimpleFieldComparison Comparison{Pre.field.str().str(), Op, Pre.constraintValue};
        if (checkSimpleFieldComparisonWithProofs(Path.toString(), Comparison) != true)
            return false;
    }
    return true;
}

class DTTVisitor : public RecursiveASTVisitor<DTTVisitor> {
    ASTContext &Ctx;
    DiagnosticsEngine &Diag;
    // Category F diagnostics (always errors - proven unsafe)
    unsigned ErrFail, ErrUnk, NoteGuard, ErrMultiArg, ErrNotTotal, NoteCase, ErrMethodReq, ErrIndexBounds, ErrNegativeIndex;
    unsigned ErrNullDeref, ErrNullDerefUnk, NoteNullGuard, NoteNullProvenance;
    // Buffer pointer tracking diagnostics
    unsigned ErrBufferOOB;      // Array subscript out of bounds on pointer
    unsigned NoteBufferSize;    // Note showing buffer size info
    unsigned NoteBufferGuard;   // Note suggesting bounds guard
    unsigned ErrPointerArithOOB; // Pointer arithmetic creates invalid pointer
    unsigned ErrNullPtrArith;    // Pointer arithmetic on null pointer
    unsigned ErrMemcpyOverread;  // memcpy reads beyond source buffer
    unsigned ErrMemcpyOverflow;  // memcpy writes beyond destination buffer
    unsigned ErrMemsetOverflow;  // memset writes beyond buffer
    unsigned ErrMemcmpOverread;  // memcmp reads beyond buffer
    unsigned ErrMemchrOverread;  // memchr reads beyond buffer
    unsigned ErrStrncpyOverflow; // strncpy writes beyond buffer
    unsigned ErrSnprintfOverflow; // snprintf size exceeds buffer
    unsigned ErrStrcpyOverflow;  // strcpy writes beyond buffer
    unsigned ErrStrcatOverflow;  // strcat writes beyond buffer
    unsigned ErrStrncatOverflow; // strncat writes beyond buffer
    unsigned WarnSprintfUnsafe;  // sprintf is unbounded
    unsigned NoteAddBoundsCheck;    // Note suggesting bounds check
    unsigned NoteUseSnprintf;       // Note suggesting snprintf
    unsigned NoteUseStrncpy;        // Note suggesting strncpy for strcpy
    unsigned NoteUseStrncat;        // Note suggesting strncat for strcat
    unsigned NoteCheckSrcLen;       // Note suggesting strlen check
    unsigned NoteAddSymbolicBoundsCheck;  // Note for symbolic buffer sizes
    unsigned NoteInferredPrecondition;    // Note showing inferred precondition
    unsigned NoteCallSitePreconditionSource;  // Note pointing to where precondition was inferred
    ModeDependentDiag DiagCallSitePreconditionUnverified;  // Call-site precondition cannot be verified
    // Category C & D diagnostics (mode-dependent severity)
    ModeDependentDiag DiagStrncpyUnverified; // strncpy size cannot be verified
    ModeDependentDiag DiagSnprintfUnverified; // snprintf size cannot be verified
    ModeDependentDiag DiagStrcpyUnverified;  // strcpy source length cannot be verified
    ModeDependentDiag DiagStrcatUnverified;  // strcat source length cannot be verified
    ModeDependentDiag DiagStrncatUnverified; // strncat size cannot be verified
    ModeDependentDiag DiagMemcpyUnverified;  // memcpy size cannot be verified (destination)
    ModeDependentDiag DiagMemcpySrcUnverified;  // memcpy source over-read cannot be verified
    ModeDependentDiag DiagMemcpySymbolicUnverified;  // memcpy symbolic destination size cannot be verified
    ModeDependentDiag DiagMemcpySrcSymbolicUnverified;  // memcpy symbolic source size cannot be verified
    ModeDependentDiag DiagMemsetUnverified;  // memset size cannot be verified
    ModeDependentDiag DiagMemsetSymbolicUnverified;  // memset symbolic size cannot be verified
    ModeDependentDiag DiagMemcmpUnverified;  // memcmp size cannot be verified
    ModeDependentDiag DiagMemcmpSymbolicUnverified;  // memcmp symbolic size cannot be verified
    ModeDependentDiag DiagMemchrUnverified;  // memchr size cannot be verified
    ModeDependentDiag DiagMemchrSymbolicUnverified;  // memchr symbolic size cannot be verified
    ModeDependentDiag DiagLostTracking;   // Category C: tracking was lost
    ModeDependentDiag DiagSolverFailed;   // Category D: solver couldn't resolve
    ModeDependentDiag DiagMaybeFreed;     // Pointer freed on some paths only
    unsigned NoteLostAt;                   // Note pointing to where tracking was lost
    // Category G diagnostics (always errors - annotation conflicts)
    unsigned ErrAnnotationConflict;
    // Lifetime tracking diagnostics (Category H - memory safety)
    unsigned ErrDoubleFree;         // Pointer was already freed
    unsigned NoteFirstFree;         // Location of first free
    unsigned ErrMismatchedDealloc;  // malloc/delete or new/free mismatch
    unsigned ErrUseAfterFree;       // Pointer was freed before use
    unsigned ErrUninitializedPtr;   // Using uninitialized pointer is UB
    unsigned ErrUninitializedVar;   // Using uninitialized variable is UB
    unsigned ErrUninitializedHeap;  // Reading uninitialized heap memory is UB
    unsigned NoteHeapAllocatedHere; // Note: allocated here without initialization
    // Scope-exit constraint diagnostics (linear type tracking)
    unsigned ErrScopeExitConstraint;  // Resource not consumed at scope exit
    unsigned NoteScopeExitAcquired;   // Note where resource was acquired
    unsigned ErrOverwriteLeak;        // Overwriting pointer with uncleared constraint
    unsigned WarnUncheckedResource;   // Resource pointer never null-checked
    // Postcondition annotation diagnostics
    unsigned WarnInvalidArgIndex;     // argN.@field where N >= num params
    // Scope tracking for linear type constraints
    unsigned CurrentScopeDepth = 0;   // Current block scope nesting depth
    // Refutation diagnostics (counterexample generation)
    unsigned NoteCounterexample;    // Note showing concrete values that violate constraint
    unsigned NoteUntracked;         // Note explaining why a value is untracked
    // Union member tracking diagnostics
    unsigned ErrUnionInactiveMember;  // Reading inactive union member (C++ UB)
    unsigned WarnUnionTypePunning;    // Reading inactive union member (C type punning)
    unsigned NoteUnionActiveSet;      // Note showing where active member was set
    ModeDependentDiag DiagArityMismatch; // Function name matches annotation but arity differs
    // Provenance chain diagnostics
    unsigned NoteProvenanceEntry;    // "note: %0.%1 set to %2 by %3"
    // FixItHint suggestion diagnostics
    unsigned NoteFixItGuard;         // "note: add guard: %0"
    // Backward search diagnostics
    unsigned NoteEstablisher;        // "note: could establish %0: %1"
    unsigned NoteEstablisherGuard;   // "note: could guard with: %0"
    FunctionDecl *CurrentFunctionDecl = nullptr;

    // Check if a location is in a system header
    bool isInSystemHeader(SourceLocation Loc) {
        if (Loc.isInvalid()) return false;
        return Ctx.getSourceManager().isInSystemHeader(Loc);
    }

    // Emit inferred precondition note and store for call-site checking
    void emitInferredPrecondition(SourceLocation Loc, Expr* BufferArg, Expr* SizeArg,
                                   const SymValue& opSize) {
        FieldPath bufFieldPath = getTypedPath(BufferArg);
        std::string bufPath = bufFieldPath.toString();
        auto bufArgIdx = getParameterIndex(BufferArg, CurrentFunctionDecl);
        auto sizeArgIdx = getParameterIndex(SizeArg, CurrentFunctionDecl);
        // Extract constant part from expressions like (n + 8) for proper call-site checking
        auto constantOffset = opSize.extractConstantOffset();
        if (!constantOffset) return;
        int64_t constPart = *constantOffset;

        // Phase 3: Check for base origin - rewrite precondition in terms of base
        std::optional<unsigned> offsetArgIdx = std::nullopt;
        int64_t offsetConstant = 0;

        if (bufFieldPath.isValid()) {
            auto originIt = State.PointerBaseOrigins.find(bufFieldPath);
            if (originIt != State.PointerBaseOrigins.end() && originIt->second.baseParamIdx) {
                // Rewrite to base parameter
                bufPath = originIt->second.basePath.toString();
                bufArgIdx = originIt->second.baseParamIdx;
                offsetArgIdx = originIt->second.offsetParamIdx;

                // Extract offset info and build bound string
                const SymValue& offsetBytes = originIt->second.offsetBytes;
                std::string boundStr = opSize.str();
                // Extract constant part from offset expression (e.g., off+4 -> 4)
                auto offset = offsetBytes.extractConstantOffset();
                if (!offset) return;
                offsetConstant = *offset;

                if (offsetBytes.kind == SymValue::CONST) {
                    if (offsetConstant > 0) {
                        boundStr = opSize.str() + "+" + std::to_string(offsetConstant);
                    } else if (offsetConstant < 0) {
                        boundStr = opSize.str() + std::to_string(offsetConstant);
                    }
                } else {
                    // Symbolic offset - include the expression in the bound string
                    std::string offsetStr = offsetBytes.str();
                    if (!offsetStr.empty() && offsetStr != "0") {
                        boundStr = opSize.str() + "+(" + offsetStr + ")";
                    }
                }

                Diag.Report(Loc, NoteInferredPrecondition)
                    << (bufPath + "._size_bytes >= " + boundStr);

                dtt::InferredPrecondition pre;
                pre.path = bufPath;
                pre.kind = dtt::PreconditionKind::SizeGE;
                pre.boundStr = boundStr;
                pre.argIndex = bufArgIdx;
                pre.boundArgIndex = sizeArgIdx;
                pre.boundConstant = constPart;
                pre.offsetArgIndex = offsetArgIdx;
                pre.offsetConstant = offsetConstant;
                pre.offsetElemSize = originIt->second.offsetElemSize;
                pre.offsetNegated = originIt->second.offsetNegated;

                // Avoid duplicates: require same path, kind, AND bound expression to dedupe
                for (const auto& P : CurrentFnPreconditions) {
                    if (P.path == pre.path && P.kind == pre.kind &&
                        P.boundStr == pre.boundStr) return;
                }
                CurrentFnPreconditions.push_back(std::move(pre));
                return;
            }
        }

        // No base origin - use original path
        Diag.Report(Loc, NoteInferredPrecondition)
            << (bufPath + "._size_bytes >= " + opSize.str());
        addInferredPrecondition(bufPath, dtt::PreconditionKind::SizeGE, opSize.str(),
                                bufArgIdx, sizeArgIdx, constPart);
    }

    // Phase 2: Check inferred preconditions at call site
    void checkCallSitePreconditions(CallExpr* E, FunctionDecl* Callee) {
        auto precondIt = FnPreconditions.find(Callee->getCanonicalDecl());
        if (precondIt == FnPreconditions.end()) return;

        for (const auto& Pre : precondIt->second) {
            if (Pre.kind != dtt::PreconditionKind::SizeGE) continue;
            if (!Pre.argIndex || *Pre.argIndex >= E->getNumArgs()) continue;

            Expr* ActualBufArg = E->getArg(*Pre.argIndex)->IgnoreParenImpCasts();
            FieldPath bufPath = getTypedPath(ActualBufArg);

            // Build argPath once for diagnostics
            llvm::SmallString<64> argPath;
            if (bufPath.isValid())
                argPath = bufPath.toString();
            else
                argPath = getExprPath(ActualBufArg);

            // Helper to emit unverified precondition diagnostic
            auto emitUnverified = [&]() {
                if (!DiagCallSitePreconditionUnverified.shouldEmit(GlobalMode)) return;

                // Phase 3: Rewrite precondition using base origin if available
                std::string precondStr = Pre.toString();
                llvm::SmallString<64> displayArgPath = argPath;

                if (bufPath.isValid()) {
                    auto originIt = State.PointerBaseOrigins.find(bufPath);
                    if (originIt != State.PointerBaseOrigins.end()) {
                        const auto& origin = originIt->second;
                        // Rewrite to show base pointer with accumulated offset
                        std::string basePath = origin.basePath.toString();
                        std::string boundWithOffset = Pre.boundStr;

                        // Add offset to bound expression
                        if (origin.offsetBytes.kind == SymValue::CONST) {
                            if (origin.offsetBytes.constVal != 0) {
                                boundWithOffset += "+(" + std::to_string(origin.offsetBytes.constVal) + ")";
                            }
                        } else if (origin.offsetBytes.kind != SymValue::UNKNOWN) {
                            boundWithOffset += "+(" + origin.offsetBytes.str() + ")";
                        }

                        precondStr = basePath + "._size_bytes >= " + boundWithOffset;
                        displayArgPath = argPath;  // Keep original arg name for clarity
                    }
                }

                Diag.Report(E->getBeginLoc(), DiagCallSitePreconditionUnverified.get(GlobalMode))
                    << precondStr << displayArgPath;
                Diag.Report(E->getBeginLoc(), NoteCallSitePreconditionSource)
                    << safeGetName(Callee);
            };

            auto Bounds = getInferredCallBounds(E, Pre, Ctx);
            if (!Bounds) continue;
            const auto &[bufSize, boundVal] = *Bounds;

            // Untracked buffer - emit warning
            if (bufSize.kind == SymValue::UNKNOWN) {
                emitUnverified();
                continue;
            }

            // Check if bufSize >= boundVal is provable
            auto proven = checkComparisonWithZ3("_callsite", bufSize, boundVal,
                                                 ConstraintExpr::CmpGE, /*assumeNonNegative=*/true);
            if (proven && *proven) continue;

            // Check constant case
            if (bufSize.kind == SymValue::CONST && boundVal.kind == SymValue::CONST) {
                if (bufSize.constVal >= boundVal.constVal) continue;
            }

            // Cannot prove precondition
            emitUnverified();
        }

        // Check generic (property-based) preconditions from .dtt annotations
        for (const auto& Pre : precondIt->second) {
            if (!Pre.isGeneric()) continue;
            if (!Pre.argIndex || *Pre.argIndex >= E->getNumArgs()) continue;

            Expr* ActualArg = E->getArg(*Pre.argIndex)->IgnoreParenImpCasts();
            FieldPath argPath = getTypedPath(ActualArg);
            if (!argPath.isValid()) continue;

            std::string pathStr = argPath.toString();

            // Check if proof already exists for this constraint
            llvm::SmallString<32> proofKey;
            proofKey = Pre.field;
            proofKey += proofOpToString(Pre.constraintOp);
            proofKey += std::to_string(Pre.constraintValue);
            if (hasProof(pathStr, proofKey.str()))
                continue;
            // Also check stronger proof that entails != 0 (e.g., == 1)
            if (Pre.constraintOp == dtt::ProofOp::NE && Pre.constraintValue == 0) {
                llvm::SmallString<32> proofKey2;
                proofKey2 = Pre.field;
                proofKey2 += "==1";
                if (hasProof(pathStr, proofKey2.str()))
                    continue;
            }

            // Get field value at call site
            SymValue fieldVal = getFieldTyped(argPath, Pre.field);
            auto fieldEval = fieldVal.eval();

            if (fieldEval) {
                // Value known — check if constraint is satisfied
                bool satisfied = false;
                switch (Pre.constraintOp) {
                case dtt::ProofOp::NE: satisfied = *fieldEval != Pre.constraintValue; break;
                case dtt::ProofOp::EQ: satisfied = *fieldEval == Pre.constraintValue; break;
                case dtt::ProofOp::GT: satisfied = *fieldEval > Pre.constraintValue; break;
                case dtt::ProofOp::GE: satisfied = *fieldEval >= Pre.constraintValue; break;
                case dtt::ProofOp::LT: satisfied = *fieldEval < Pre.constraintValue; break;
                case dtt::ProofOp::LE: satisfied = *fieldEval <= Pre.constraintValue; break;
                }
                if (satisfied) continue;

                // Definite violation — emit error
                llvm::SmallString<64> reason;
                if (Pre.errorReason[0] != '\0') {
                    reason = Pre.errorReason;
                } else {
                    reason = Pre.field;
                    reason += " is ";
                    reason += std::to_string(*fieldEval);
                }
                llvm::SmallString<64> constraint;
                constraint = Pre.field;
                switch (Pre.constraintOp) {
                case dtt::ProofOp::NE: constraint += " != "; break;
                case dtt::ProofOp::EQ: constraint += " == "; break;
                case dtt::ProofOp::GT: constraint += " > "; break;
                case dtt::ProofOp::GE: constraint += " >= "; break;
                case dtt::ProofOp::LT: constraint += " < "; break;
                case dtt::ProofOp::LE: constraint += " <= "; break;
                }
                constraint += std::to_string(Pre.constraintValue);
                Diag.Report(E->getBeginLoc(), ErrMethodReq)
                    << "" << safeGetName(Callee) << constraint.str()
                    << pathStr << Pre.field.str() << reason.str();
            } else {
                // Value unknown — emit category C/D warning
                llvm::SmallString<64> constraint;
                constraint = Pre.field;
                constraint += " ";
                constraint += proofOpToString(Pre.constraintOp);
                constraint += " ";
                constraint += std::to_string(Pre.constraintValue);
                if (!emitCategoryC(E->getBeginLoc(), pathStr, Pre.field.str().str(),
                                   constraint.str().str())) {
                    if (GlobalMode != DTTMode::Quiet) {
                        emitCategoryD(E->getBeginLoc(), pathStr, Pre.field.str().str(),
                                      constraint.str().str(), Pre.errorReason);
                    }
                }
            }
        }
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
        ErrNegativeIndex = Diag.getCustomDiagID(DiagnosticsEngine::Error, "DTT: %0::%1 negative index %2 is always out of bounds");
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
        ErrMemcpyOverread = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: memcpy reads %0 bytes from %1-byte source buffer (CWE-126: buffer over-read)");
        ErrMemcpyOverflow = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: memcpy writes %0 bytes to %1-byte buffer (CWE-787: out-of-bounds write)");
        ErrMemsetOverflow = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: memset writes %0 bytes to %1-byte buffer (CWE-787: out-of-bounds write)");
        ErrMemcmpOverread = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: memcmp reads %0 bytes from %1-byte buffer (CWE-126: buffer over-read)");
        ErrMemchrOverread = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: memchr reads %0 bytes from %1-byte buffer (CWE-126: buffer over-read)");
        ErrStrncpyOverflow = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: strncpy writes %0 bytes to %1-byte buffer (CWE-120: buffer overflow)");
        ErrSnprintfOverflow = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: snprintf size %0 exceeds %1-byte buffer (CWE-120)");
        ErrStrcpyOverflow = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: strcpy writes %0 bytes to %1-byte buffer (CWE-120: buffer overflow)");
        ErrStrcatOverflow = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: strcat appends %0 bytes to %1-byte buffer (CWE-120: buffer overflow)");
        ErrStrncatOverflow = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: strncat may append %0+1 bytes to %1-byte buffer (CWE-120: buffer overflow)");
        WarnSprintfUnsafe = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: sprintf is unsafe - buffer overflow risk (CWE-120) [-Wdtt-sprintf-unsafe]");
        NoteAddBoundsCheck = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "add bounds check: if (size <= %0) { ... }");
        NoteUseSnprintf = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "use snprintf(buf, sizeof(buf), ...) instead");
        NoteUseStrncpy = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "use strncpy(dest, src, sizeof(dest)-1) and null-terminate");
        NoteUseStrncat = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "use strncat(dest, src, sizeof(dest)-strlen(dest)-1)");
        NoteCheckSrcLen = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "check source length first: if (strlen(src) < %0) { ... }");
        NoteAddSymbolicBoundsCheck = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "add bounds check: if (copy_size <= buffer_size) { ... }");
        NoteInferredPrecondition = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "inferred precondition: %0");
        NoteCallSitePreconditionSource = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "precondition inferred from callee '%0'");
        DiagCallSitePreconditionUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify call-site precondition '%0' for argument '%1' [-Wdtt-callsite-precondition]");
        DiagCallSitePreconditionUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify call-site precondition '%0' for argument '%1'");
        // Category C: Unverified bounds (mode-dependent)
        DiagStrncpyUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify strncpy size fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagStrncpyUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify strncpy size fits in %0-byte buffer");
        DiagSnprintfUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify snprintf size fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagSnprintfUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify snprintf size fits in %0-byte buffer");
        DiagStrcpyUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify strcpy source length fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagStrcpyUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify strcpy source length fits in %0-byte buffer");
        DiagStrcatUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify strcat source length fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagStrcatUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify strcat source length fits in %0-byte buffer");
        DiagStrncatUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify strncat size fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagStrncatUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify strncat size fits in %0-byte buffer");
        DiagMemcpyUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memcpy size fits in %0-byte destination buffer [-Wdtt-unverified-bounds]");
        DiagMemcpyUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memcpy size fits in %0-byte destination buffer");
        DiagMemcpySrcUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memcpy does not over-read %0-byte source buffer (CWE-126) [-Wdtt-unverified-bounds]");
        DiagMemcpySrcUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memcpy does not over-read %0-byte source buffer (CWE-126)");
        DiagMemcpySymbolicUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memcpy size fits in symbolic-sized destination buffer [-Wdtt-unverified-bounds]");
        DiagMemcpySymbolicUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memcpy size fits in symbolic-sized destination buffer");
        DiagMemcpySrcSymbolicUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memcpy does not over-read symbolic-sized source buffer (CWE-126) [-Wdtt-unverified-bounds]");
        DiagMemcpySrcSymbolicUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memcpy does not over-read symbolic-sized source buffer (CWE-126)");
        DiagMemsetUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memset size fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagMemsetUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memset size fits in %0-byte buffer");
        DiagMemsetSymbolicUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memset size fits in symbolic-sized buffer [-Wdtt-unverified-bounds]");
        DiagMemsetSymbolicUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memset size fits in symbolic-sized buffer");
        DiagMemcmpUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memcmp size fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagMemcmpUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memcmp size fits in %0-byte buffer");
        DiagMemcmpSymbolicUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memcmp size fits in symbolic-sized buffer [-Wdtt-unverified-bounds]");
        DiagMemcmpSymbolicUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memcmp size fits in symbolic-sized buffer");
        DiagMemchrUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memchr size fits in %0-byte buffer [-Wdtt-unverified-bounds]");
        DiagMemchrUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memchr size fits in %0-byte buffer");
        DiagMemchrSymbolicUnverified.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify memchr size fits in symbolic-sized buffer [-Wdtt-unverified-bounds]");
        DiagMemchrSymbolicUnverified.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify memchr size fits in symbolic-sized buffer");
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
        DiagMaybeFreed.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: cannot verify '%0' is not freed (freed on some paths) [-Wdtt-maybe-freed]");
        DiagMaybeFreed.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: cannot verify '%0' is not freed (freed on some paths)");
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
        ErrUninitializedVar = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: use of uninitialized variable '%0' is undefined behavior");
        ErrUninitializedHeap = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: reading uninitialized heap memory via '%0' is undefined behavior");
        NoteHeapAllocatedHere = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "allocated here with 'new' (use 'new T()' or 'new T{}' for zero-initialization)");
        // Scope-exit constraint diagnostics (linear type tracking)
        ErrScopeExitConstraint = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: scope-exit constraint violated for '%0': %1 %2 %3, but %1 = %4 (resource not properly released)");
        NoteScopeExitAcquired = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "resource acquired here");
        ErrOverwriteLeak = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: overwriting '%0' leaks resource (%1 %2 %3, but %1 = %4)");
        WarnUncheckedResource = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: '%0' from %1() is a resource pointer that is never null-checked (potential leak)");
        // Postcondition annotation diagnostics
        WarnInvalidArgIndex = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: postcondition references arg%0 but function has only %1 parameter(s); ignored");
        // Refutation diagnostics
        NoteCounterexample = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "counterexample: %0");
        NoteUntracked = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "'%0' is untracked (no size information available)");
        // Union member tracking diagnostics
        ErrUnionInactiveMember = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: reading inactive union member '%0' when '%1' is active (undefined behavior)");
        WarnUnionTypePunning = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: reading inactive union member '%0' when '%1' is active (type punning)");
        NoteUnionActiveSet = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "union member '%0' was last written here");
        DiagArityMismatch.warn = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
            "DTT: function '%0' called with %1 args but annotated for %2 args (annotations not applied) [-Wdtt-arity-mismatch]");
        DiagArityMismatch.err = Diag.getCustomDiagID(DiagnosticsEngine::Error,
            "DTT: function '%0' called with %1 args but annotated for %2 args (annotations not applied)");
        // Provenance chain notes
        NoteProvenanceEntry = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "%0.%1 set to %2 (%3)");
        // FixItHint suggestion notes
        NoteFixItGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "add guard: %0");
        // Backward search notes
        NoteEstablisher = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "could establish %0: %1");
        NoteEstablisherGuard = Diag.getCustomDiagID(DiagnosticsEngine::Note,
            "could guard with: %0");

        // Register all diagnostic IDs with SARIF consumer
        if (GlobalSarifConsumer) {
            // P0: Bounds overflow
            GlobalSarifConsumer->registerDiag(ErrBufferOOB, &dtt::RuleBoundsOverflow);
            GlobalSarifConsumer->registerDiag(ErrIndexBounds, &dtt::RuleBoundsOverflow);
            GlobalSarifConsumer->registerDiag(ErrNegativeIndex, &dtt::RuleBoundsOverflow);
            // P0: Null deref
            GlobalSarifConsumer->registerDiag(ErrNullDeref, &dtt::RuleNullDeref);
            GlobalSarifConsumer->registerDiag(ErrNullDerefUnk, &dtt::RuleNullDeref);
            // P0: Memory safety
            GlobalSarifConsumer->registerDiag(ErrUseAfterFree, &dtt::RuleUseAfterFree);
            GlobalSarifConsumer->registerDiag(ErrDoubleFree, &dtt::RuleDoubleFree);
            GlobalSarifConsumer->registerDiag(ErrUninitializedPtr, &dtt::RuleUninitialized);
            GlobalSarifConsumer->registerDiag(ErrUninitializedVar, &dtt::RuleUninitialized);
            GlobalSarifConsumer->registerDiag(ErrUninitializedHeap, &dtt::RuleUninitialized);
            GlobalSarifConsumer->registerDiag(ErrMismatchedDealloc, &dtt::RuleMismatchedDealloc);
            // P0: Memory operation overflow
            GlobalSarifConsumer->registerDiag(ErrMemcpyOverflow, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrMemcpyOverread, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrMemsetOverflow, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrMemcmpOverread, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrMemchrOverread, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrStrncpyOverflow, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrSnprintfOverflow, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrStrcpyOverflow, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrStrcatOverflow, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(ErrStrncatOverflow, &dtt::RuleMemopOverflow);
            GlobalSarifConsumer->registerDiag(WarnSprintfUnsafe, &dtt::RuleSprintfUnsafe);
            // P0: Pointer arithmetic
            GlobalSarifConsumer->registerDiag(ErrPointerArithOOB, &dtt::RulePointerArith);
            GlobalSarifConsumer->registerDiag(ErrNullPtrArith, &dtt::RulePointerArith);
            // P1: Resource leaks
            GlobalSarifConsumer->registerDiag(ErrScopeExitConstraint, &dtt::RuleResourceLeak);
            GlobalSarifConsumer->registerDiag(ErrOverwriteLeak, &dtt::RuleResourceLeak);
            GlobalSarifConsumer->registerDiag(WarnUncheckedResource, &dtt::RuleResourceLeak);
            // P1: Unverified bounds (mode-dependent — register both warn and err)
            GlobalSarifConsumer->registerDiag(DiagMemcpyUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcpyUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcpySrcUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcpySrcUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcpySymbolicUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcpySymbolicUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcpySrcSymbolicUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcpySrcSymbolicUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemsetUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemsetUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemsetSymbolicUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemsetSymbolicUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcmpUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcmpUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcmpSymbolicUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemcmpSymbolicUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemchrUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemchrUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemchrSymbolicUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagMemchrSymbolicUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrncpyUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrncpyUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagSnprintfUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagSnprintfUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrcpyUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrcpyUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrcatUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrcatUnverified.err, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrncatUnverified.warn, &dtt::RuleUnverifiedBounds);
            GlobalSarifConsumer->registerDiag(DiagStrncatUnverified.err, &dtt::RuleUnverifiedBounds);
            // P1: Unverified preconditions
            GlobalSarifConsumer->registerDiag(DiagCallSitePreconditionUnverified.warn, &dtt::RuleUnverifiedPrecondition);
            GlobalSarifConsumer->registerDiag(DiagCallSitePreconditionUnverified.err, &dtt::RuleUnverifiedPrecondition);
            // P2: Precondition violations
            GlobalSarifConsumer->registerDiag(ErrFail, &dtt::RulePrecondition);
            GlobalSarifConsumer->registerDiag(ErrUnk, &dtt::RulePrecondition);
            GlobalSarifConsumer->registerDiag(ErrMultiArg, &dtt::RulePrecondition);
            GlobalSarifConsumer->registerDiag(ErrMethodReq, &dtt::RulePrecondition);
            // P2: Enum exhaustiveness
            GlobalSarifConsumer->registerDiag(ErrNotTotal, &dtt::RuleEnumExhaustive);
            // P2: Annotation conflicts
            GlobalSarifConsumer->registerDiag(ErrAnnotationConflict, &dtt::RuleAnnotationConflict);
            GlobalSarifConsumer->registerDiag(WarnInvalidArgIndex, &dtt::RuleAnnotationConflict);
            GlobalSarifConsumer->registerDiag(DiagArityMismatch.warn, &dtt::RuleAnnotationConflict);
            GlobalSarifConsumer->registerDiag(DiagArityMismatch.err, &dtt::RuleAnnotationConflict);
            // P2: Union access
            GlobalSarifConsumer->registerDiag(ErrUnionInactiveMember, &dtt::RuleUnionAccess);
            GlobalSarifConsumer->registerDiag(WarnUnionTypePunning, &dtt::RuleUnionTypePun);
            // P2: Lost tracking / solver failed
            GlobalSarifConsumer->registerDiag(DiagLostTracking.warn, &dtt::RuleLostTracking);
            GlobalSarifConsumer->registerDiag(DiagLostTracking.err, &dtt::RuleLostTracking);
            GlobalSarifConsumer->registerDiag(DiagSolverFailed.warn, &dtt::RuleSolverFailed);
            GlobalSarifConsumer->registerDiag(DiagSolverFailed.err, &dtt::RuleSolverFailed);
            GlobalSarifConsumer->registerDiag(DiagMaybeFreed.warn, &dtt::RuleMaybeFreed);
            GlobalSarifConsumer->registerDiag(DiagMaybeFreed.err, &dtt::RuleMaybeFreed);
            // Notes (all phases)
            GlobalSarifConsumer->registerNote(NoteGuard);
            GlobalSarifConsumer->registerNote(NoteCase);
            GlobalSarifConsumer->registerNote(NoteNullGuard);
            GlobalSarifConsumer->registerNote(NoteNullProvenance);
            GlobalSarifConsumer->registerNote(NoteBufferSize);
            GlobalSarifConsumer->registerNote(NoteBufferGuard);
            GlobalSarifConsumer->registerNote(NoteAddBoundsCheck);
            GlobalSarifConsumer->registerNote(NoteAddSymbolicBoundsCheck);
            GlobalSarifConsumer->registerNote(NoteUseSnprintf);
            GlobalSarifConsumer->registerNote(NoteUseStrncpy);
            GlobalSarifConsumer->registerNote(NoteUseStrncat);
            GlobalSarifConsumer->registerNote(NoteCheckSrcLen);
            GlobalSarifConsumer->registerNote(NoteInferredPrecondition);
            GlobalSarifConsumer->registerNote(NoteCallSitePreconditionSource);
            GlobalSarifConsumer->registerNote(NoteFirstFree);
            GlobalSarifConsumer->registerNote(NoteHeapAllocatedHere);
            GlobalSarifConsumer->registerNote(NoteScopeExitAcquired);
            GlobalSarifConsumer->registerNote(NoteLostAt);
            GlobalSarifConsumer->registerNote(NoteCounterexample);
            GlobalSarifConsumer->registerNote(NoteUntracked);
            GlobalSarifConsumer->registerNote(NoteUnionActiveSet);
            GlobalSarifConsumer->registerNote(NoteProvenanceEntry);
            GlobalSarifConsumer->registerNote(NoteFixItGuard);
            GlobalSarifConsumer->registerNote(NoteEstablisher);
            GlobalSarifConsumer->registerNote(NoteEstablisherGuard);
        }
    }

    /// Emit provenance chain notes for a failed precondition.
    /// Walks the linked list backwards and emits notes in chronological order.
    void emitProvenanceChain(llvm::StringRef basePath, llvm::StringRef field) {
        llvm::SmallString<128> key;
        key += basePath;
        key += ".";
        key += field;
        auto it = ProvenanceChains.find(key);
        if (it == ProvenanceChains.end() || !it->second) return;

        // Collect entries in reverse (linked list is newest-first)
        llvm::SmallVector<const StateProvenance*, 8> entries;
        for (auto *p = it->second.get(); p; p = p->previous.get()) {
            entries.push_back(p);
        }
        // Emit in chronological order (oldest first)
        for (auto entryIt = entries.rbegin(); entryIt != entries.rend(); ++entryIt) {
            auto *entry = *entryIt;
            if (entry->location.isValid()) {
                Diag.Report(entry->location, NoteProvenanceEntry)
                    << basePath << field << entry->value.str() << entry->description;
            }
        }
    }

    /// Generate a FixItHint guard suggestion for a failed precondition.
    /// Returns true if a suggestion was emitted.
    /// Pattern table:
    ///   _size > 0           → if (!OBJ.empty()) { ... }
    ///   _has_value != 0     → if (OBJ.has_value()) { ... }
    ///   _has_value == 1     → if (OBJ.has_value()) { ... }
    ///   _nonnull != 0       → if (PTR) { ... }
    ///   arg0 < _size        → if (IDX < OBJ.size()) { ... }
    bool emitFixItSuggestion(SourceLocation loc, llvm::StringRef objPath,
                             llvm::StringRef field, llvm::StringRef constraint,
                             const Stmt *stmt = nullptr) {
        llvm::SmallString<128> suggestion;
        llvm::raw_svector_ostream os(suggestion);

        // Parse the constraint to identify the pattern
        if (field == "_size") {
            // _size > 0 or _size >= 1 → if (!obj.empty())
            if (constraint.contains("> 0") || constraint.contains(">= 1") ||
                constraint.contains("!= 0")) {
                os << "if (!" << objPath << ".empty()) { ... }";
            } else {
                return false;
            }
        } else if (field == "_has_value") {
            // _has_value != 0 or _has_value == 1 → if (obj.has_value())
            os << "if (" << objPath << ".has_value()) { ... }";
        } else if (field == "_nonnull") {
            // _nonnull != 0 → if (ptr)
            os << "if (" << objPath << ") { ... }";
        } else {
            return false;
        }

        // Emit the note with the suggestion
        Diag.Report(loc, NoteFixItGuard) << suggestion;

        // Emit FixItHint to insert guard before the statement
        if (stmt) {
            auto &SM = Ctx.getSourceManager();
            SourceLocation insertLoc = stmt->getBeginLoc();
            if (insertLoc.isValid() && !SM.isInSystemHeader(insertLoc)) {
                llvm::SmallString<128> guardCode;
                llvm::raw_svector_ostream guardOS(guardCode);
                if (field == "_size") {
                    guardOS << "if (!" << objPath << ".empty()) ";
                } else if (field == "_has_value") {
                    guardOS << "if (" << objPath << ".has_value()) ";
                } else if (field == "_nonnull") {
                    guardOS << "if (" << objPath << ") ";
                }
                Diag.Report(insertLoc, Diag.getCustomDiagID(
                    DiagnosticsEngine::Note, "insert guard here"))
                    << FixItHint::CreateInsertion(insertLoc, guardCode);
            }
        }
        return true;
    }

    /// Emit FixItHint for index bounds violation.
    /// Pattern: arg0 < _size → if (idx < obj.size())
    bool emitIndexFixItSuggestion(SourceLocation loc, llvm::StringRef objPath,
                                  llvm::StringRef indexExpr,
                                  const Stmt *stmt = nullptr) {
        llvm::SmallString<128> suggestion;
        llvm::raw_svector_ostream os(suggestion);
        os << "if (" << indexExpr << " < " << objPath << ".size()) { ... }";
        Diag.Report(loc, NoteFixItGuard) << suggestion;

        if (stmt) {
            auto &SM = Ctx.getSourceManager();
            SourceLocation insertLoc = stmt->getBeginLoc();
            if (insertLoc.isValid() && !SM.isInSystemHeader(insertLoc)) {
                llvm::SmallString<128> guardCode;
                llvm::raw_svector_ostream guardOS(guardCode);
                guardOS << "if (" << indexExpr << " < " << objPath << ".size()) ";
                Diag.Report(insertLoc, Diag.getCustomDiagID(
                    DiagnosticsEngine::Note, "insert guard here"))
                    << FixItHint::CreateInsertion(insertLoc, guardCode);
            }
        }
        return true;
    }

    /// Backward search: given a failed precondition on an object of type
    /// `className`, search the annotation registry for operations that could
    /// establish the required property. This is a dynamic search over ALL
    /// registered annotations — not a hardcoded pattern table.
    ///
    /// For each postcondition in the registry that sets `field` to a value
    /// satisfying (goalOp, goalValue), emit a note suggesting that operation.
    /// Also searches boolean postconditions for guard suggestions.
    void emitBackwardSearchSuggestions(SourceLocation loc,
                                       llvm::StringRef className,
                                       llvm::StringRef field,
                                       ProofOp goalOp, int64_t goalValue) {
        // Collect method-based establishers for this type
        llvm::SmallVector<llvm::SmallString<96>, 8> establishers;
        llvm::SmallVector<llvm::SmallString<64>, 4> guards;
        llvm::StringSet<> seenMethods;   // Deduplicate by method name
        llvm::StringSet<> seenGuards;

        for (const auto &[key, info] : MethodAnnotations) {
            if (key.className != className) continue;

            // Search postconditions that set the target field
            for (const auto &post : info.ensures) {
                if (post.field != field) continue;
                // Skip constructors (less actionable as suggestions)
                llvm::StringRef mname(key.methodName);
                if (mname == className) continue;
                // Deduplicate by method name
                if (!seenMethods.insert(mname).second) continue;

                // Check if postcondition value satisfies the goal
                if (postconditionSatisfiesGoal(post.expr, goalOp, goalValue)) {
                    llvm::SmallString<96> desc;
                    llvm::raw_svector_ostream os(desc);
                    os << mname << "()";

                    // Add condition note if value depends on args
                    if (post.expr.kind == SymValue::VAR &&
                        llvm::StringRef(post.expr.varRef).starts_with("arg")) {
                        if (goalOp == ProofOp::GT && goalValue == 0)
                            os << " with " << post.expr.str() << " > 0";
                        else if (goalOp == ProofOp::NE && goalValue == 0)
                            os << " with " << post.expr.str() << " != 0";
                    }
                    establishers.push_back(desc);
                }
            }

            // Search boolean postconditions for guard patterns
            for (const auto &bp : info.boolPostconds) {
                if (bp.field != field) continue;
                // Skip operator bool (already covered by FixItHint)
                llvm::StringRef mname(key.methodName);
                if (mname.starts_with("operator")) continue;
                if (!seenGuards.insert(mname).second) continue;

                // Check if the boolean postcondition would establish our goal
                if (boolPostcondSatisfiesGoal(bp, goalOp, goalValue)) {
                    llvm::SmallString<64> desc;
                    llvm::raw_svector_ostream os(desc);
                    if (bp.onTrue) {
                        os << "if (obj." << mname << "()) { ... }";
                    } else {
                        os << "if (!obj." << mname << "()) { ... }";
                    }
                    guards.push_back(desc);
                }
            }
        }

        // Emit results
        if (establishers.empty() && guards.empty()) return;
        emitEstablisherList(loc, field, establishers);
        for (const auto &g : guards) {
            Diag.Report(loc, NoteEstablisherGuard) << g;
        }
    }

    /// Emit a capped, comma-separated list of establishers as a single note.
    void emitEstablisherList(
            SourceLocation loc, llvm::StringRef field,
            llvm::ArrayRef<llvm::SmallString<96>> establishers) {
        if (establishers.empty()) return;
        llvm::SmallString<256> combined;
        llvm::raw_svector_ostream os(combined);
        for (unsigned i = 0, n = std::min<unsigned>(establishers.size(), 5);
             i < n; ++i) {
            if (i > 0) os << ", ";
            os << establishers[i];
        }
        if (establishers.size() > 5) os << ", ...";
        Diag.Report(loc, NoteEstablisher) << field << combined;
    }

    /// Check if a postcondition expression establishes a value that satisfies
    /// the goal constraint (goalOp, goalValue).
    /// e.g., goal is _size > 0:
    ///   - expr = old(_size) + 1 → yes (if old >= 0, result >= 1 > 0)
    ///   - expr = 0 → no
    ///   - expr = arg0 → maybe (depends on arg0, report as conditional)
    static bool postconditionSatisfiesGoal(const SymValue &expr,
                                           ProofOp goalOp, int64_t goalValue) {
        // Constant postcondition: check directly
        if (auto val = expr.eval())
            return checkProofOp(*val, goalOp, goalValue);

        // old(_size) + 1: if goal is > 0, this satisfies when old >= 0
        // old(_size) - 1: if goal is > 0, not guaranteed
        if (expr.kind == SymValue::ADD && expr.right &&
            expr.right->kind == SymValue::CONST && expr.right->constVal > 0) {
            // Adding a positive constant — likely satisfies > 0 or != 0
            if ((goalOp == ProofOp::GT && goalValue == 0) ||
                (goalOp == ProofOp::NE && goalValue == 0) ||
                (goalOp == ProofOp::GE && goalValue == 1)) {
                return true;
            }
        }

        // Variable expression (arg0, arg0 + 1, etc.) — conditionally satisfies
        if (expr.kind == SymValue::VAR) {
            // old(field) = identity postcondition — doesn't establish anything new
            if (llvm::StringRef(expr.varRef).starts_with("old:"))
                return false;
            // Setting to an argument means it CAN satisfy the goal with right input
            return true;
        }

        return false;
    }

    /// Check if a boolean postcondition would establish the goal.
    /// e.g., empty() has ensures_if_false: _size > 0
    ///   goal: _size > 0 → !empty() satisfies it
    static bool boolPostcondSatisfiesGoal(const BoolPostcondition &bp,
                                          ProofOp goalOp, int64_t goalValue) {
        return entails(bp.op, bp.rhs, goalOp, goalValue);
    }

    /// Backward search for free function precondition failures.
    /// For pointer properties like _nonnull, searches the registry for
    /// free functions whose postconditions set the field on their result.
    void emitBackwardSearchForFreeFunc(SourceLocation loc,
                                       llvm::StringRef argPath,
                                       llvm::StringRef field,
                                       ProofOp goalOp, int64_t goalValue) {
        // For _nonnull failures on free function args, the main suggestion
        // is a null check guard — already handled by emitFixItSuggestion.
        // Backward search adds: "what functions could establish _nonnull?"
        // This is mainly useful for resource types (FILE*, etc.)

        llvm::SmallVector<llvm::SmallString<96>, 4> establishers;

        for (const auto &[key, info] : FreeFunctionAnnotations) {
            // Search result postconditions
            for (const auto &rp : info.resultPostconditions) {
                if (llvm::StringRef(rp.field) != field) continue;
                if (postconditionSatisfiesGoal(SymValue::Const(rp.value),
                                               goalOp, goalValue)) {
                    llvm::SmallString<96> desc;
                    llvm::raw_svector_ostream os(desc);
                    os << key.functionName << "(...)";
                    if (rp.guard) {
                        os << " [when result is non-null]";
                    }
                    establishers.push_back(desc);
                    if (establishers.size() >= 5) break;
                }
            }
            // Search argument postconditions
            for (const auto &ap : info.postconditions) {
                if (llvm::StringRef(ap.field) != field) continue;
                if (postconditionSatisfiesGoal(SymValue::Const(ap.value),
                                               goalOp, goalValue)) {
                    llvm::SmallString<96> desc;
                    llvm::raw_svector_ostream os(desc);
                    os << key.functionName << "(...)";
                    establishers.push_back(desc);
                    if (establishers.size() >= 5) break;
                }
            }
            if (establishers.size() >= 5) break;
        }

        emitEstablisherList(loc, field, establishers);
    }

    /// Build a counterexample string showing concrete values that violate a
    /// bounds constraint. Used to help users understand verification failures.
    std::string buildCounterexample(llvm::StringRef objPath,
                                    llvm::StringRef /*constraint*/,
                                    const SymValue& bufSize,
                                    const SymValue& requiredSize) {
        llvm::SmallString<128> result;
        llvm::raw_svector_ostream os(result);

        if (bufSize.kind == SymValue::CONST) {
            os << objPath << "._size_bytes = " << bufSize.constVal;
        } else if (bufSize.kind != SymValue::UNKNOWN) {
            os << objPath << "._size_bytes = " << bufSize.str();
        }

        if (requiredSize.kind == SymValue::CONST) {
            if (!result.empty()) os << ", ";
            os << "required = " << requiredSize.constVal;
        } else if (requiredSize.kind != SymValue::UNKNOWN) {
            if (!result.empty()) os << ", ";
            os << "required = " << requiredSize.str();
        }

        if (bufSize.kind == SymValue::CONST && requiredSize.kind == SymValue::CONST) {
            os << " (" << bufSize.constVal << " >= " << requiredSize.constVal
               << " is " << (bufSize.constVal >= requiredSize.constVal ? "true" : "false") << ")";
        }

        return std::string(result);
    }

    // Skip traversing into system headers (std::span internals, etc.)
    bool TraverseDecl(Decl *D) {
        if (!D) return true;
        if (isInSystemHeader(D->getLocation())) return true;  // Skip system headers
        return RecursiveASTVisitor<DTTVisitor>::TraverseDecl(D);
    }

    // A lambda's body runs when the lambda is called, not where it is
    // written: it is analyzed on its own, like a function, from an empty
    // state. Only init captures ([k = f()]) are evaluated here. A generic
    // lambda's body is a template pattern, not analyzed.
    bool TraverseLambdaExpr(LambdaExpr *LE) {
        for (const LambdaCapture &C : LE->captures())
            if (C.capturesVariable())
                if (auto *VD = dyn_cast<VarDecl>(C.getCapturedVar()); VD && VD->isInitCapture())
                    TraverseStmt(VD->getInit());
        CXXMethodDecl *Op = LE->getCallOperator();
        if (!Op || LE->isGenericLambda() || Op->isDependentContext()) return true;
        const LambdaExpr *Outer = std::exchange(AnalyzedLambda, LE);
        bool Result = TraverseFunctionDecl(Op);
        AnalyzedLambda = Outer;
        return Result;
    }

    // The lambda whose body is being analyzed: a non-mutable lambda's init
    // captures hold their initial values in the body.
    const LambdaExpr *AnalyzedLambda = nullptr;

    // Skip traversing into sizeof/alignof/etc. operands (unevaluated contexts).
    // sizeof does NOT evaluate its operand — only the type matters.
    // Without this, DTT falsely flags dereferences inside sizeof as null-deref
    // or use-of-uninitialized (e.g., sizeof(*uninit_ptr) for allocation sizing).
    // The sizeof constant value is already handled in tryEvalInt/decomposeSymValue.
    bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E) {
        // Do NOT traverse into the operand — it is unevaluated.
        return true;
    }

    bool TraverseCallExpr(CallExpr *Call) {
        noteMayThrow(Call->getDirectCallee());
        bool Proven = callRequirementsProven(Call, Ctx);
        bool Result = RecursiveASTVisitor<DTTVisitor>::TraverseCallExpr(Call);
        if (!Proven) recoverUnprovedCall(Call);
        invalidateLambdaCaptures(Call);
        noteNoReturn(Call);
        return Result;
    }

    // A call given a lambda (the object of l() included) may run its body:
    // what the lambda captures by reference may have been written.
    void invalidateLambdaCaptures(const CallExpr *Call) {
        llvm::SmallVector<const Expr *, 4> Args(Call->arguments());
        if (auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) Args.push_back(Member->getImplicitObjectArgument());
        for (const Expr *Arg : Args) {
            auto *Closure = Arg ? Arg->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
            if (!Closure || !Closure->isLambda()) continue;
            for (const LambdaCapture &C : Closure->captures()) {
                if (C.capturesThis())
                    invalidatePropertiesWithProvenance("this", Call->getBeginLoc(),
                                                       "captured by a lambda this call may run");
                else if (C.capturesVariable() && C.getCaptureKind() == LCK_ByRef)
                    invalidatePropertiesWithProvenance(variableKey(C.getCapturedVar()).str(), Call->getBeginLoc(),
                                                       "captured by reference by a lambda this call may run");
            }
        }
    }

    bool TraverseCXXMemberCallExpr(CXXMemberCallExpr *Call) {
        noteMayThrow(Call->getDirectCallee());
        bool Proven = callRequirementsProven(Call, Ctx);
        bool Result = RecursiveASTVisitor<DTTVisitor>::TraverseCXXMemberCallExpr(Call);
        if (!Proven) recoverUnprovedCall(Call);
        invalidateLambdaCaptures(Call);
        noteNoReturn(Call);
        return Result;
    }

    bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
        noteMayThrow(Call->getDirectCallee());
        bool Proven = callRequirementsProven(Call, Ctx);
        bool Result = RecursiveASTVisitor<DTTVisitor>::TraverseCXXOperatorCallExpr(Call);
        if (!Proven) recoverUnprovedCall(Call);
        invalidateLambdaCaptures(Call);
        noteNoReturn(Call);
        return Result;
    }

    // A call to a function that does not return (abort, exit) ends the path.
    void noteNoReturn(const CallExpr *Call) {
        if (const FunctionDecl *FD = Call->getDirectCallee(); FD && FD->isNoReturn())
            State.Unreachable = true;
    }

    // Inside a try block, a call, constructor or allocation that may throw
    // sends the state before it to the handlers. Callee is null for an
    // indirect call or a new expression.
    void noteMayThrow(const FunctionDecl *Callee) {
        if (HandlerTargets.empty() || State.Unreachable) return;
        auto *FPT = Callee ? Callee->getType()->getAs<FunctionProtoType>() : nullptr;
        if (FPT && FPT->isNothrow()) return;
        *HandlerTargets.back() = joinStates(*HandlerTargets.back(), State);
    }

    bool TraverseCXXConstructExpr(CXXConstructExpr *E) {
        noteMayThrow(E->getConstructor());
        return RecursiveASTVisitor::TraverseCXXConstructExpr(E);
    }

    bool TraverseCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *E) {
        noteMayThrow(E->getConstructor());
        return RecursiveASTVisitor::TraverseCXXTemporaryObjectExpr(E);
    }

    bool TraverseCXXNewExpr(CXXNewExpr *E) {
        noteMayThrow(nullptr);
        return RecursiveASTVisitor::TraverseCXXNewExpr(E);
    }

    bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
    bool TraverseRequiresExpr(RequiresExpr *) { return true; }
    bool TraverseDecltypeTypeLoc(DecltypeTypeLoc) { return true; }

    bool TraverseCXXTypeidExpr(CXXTypeidExpr *E) {
        return !E->isPotentiallyEvaluated() ||
               RecursiveASTVisitor<DTTVisitor>::TraverseCXXTypeidExpr(E);
    }

    // Check if function is free() for deallocation detection
    bool isDeallocationFunction(const FunctionDecl *FD) {
        if (!FD) return false;
        return FD->getNameAsString() == "free";
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
            std::string fieldKey = post.field;
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
                llvm::StringSet<> uniqueValues;
                for (const auto& v : kv.second) uniqueValues.insert(v);
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
            std::string valueStr = condStr + "=" + bp.field + " " + ProofOpStrings[static_cast<int>(bp.op)] + " " + std::to_string(bp.rhs);
            boolEnsures[key].push_back(valueStr);
        }

        for (const auto& kv : boolEnsures) {
            if (kv.second.size() > 1) {
                llvm::StringSet<> uniqueValues;
                for (const auto& v : kv.second) uniqueValues.insert(v);
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

    // Register user-defined concepts from dtt:define= annotations
    // Must be called BEFORE processing requires/ensures to ensure concepts are available
    void registerConceptDefinitions(Decl *D) {
        for (auto *A : D->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                llvm::StringRef s = Ann->getAnnotation();
                if (s.starts_with(kDefinePrefix)) {
                    llvm::StringRef def = s.drop_front(kDefinePrefix.size());
                    size_t colonPos = def.find(":=");
                    if (colonPos != llvm::StringRef::npos) {
                        llvm::StringRef name = def.substr(0, colonPos).trim();
                        llvm::StringRef expr = def.substr(colonPos + 2).trim();
                        if (!name.empty() && !expr.empty()) {
                            if (!isValidIdentifier(name)) {
                                auto &DE = Ctx.getDiagnostics();
                                unsigned ID = DE.getCustomDiagID(
                                    DiagnosticsEngine::Error,
                                    "DTT: invalid concept name '%0' - must be a valid identifier");
                                DE.Report(Ann->getLocation(), ID) << name;
                            } else {
                                ConceptRegistry[name] = expr.str();
                            }
                        }
                    }
                }
            }
        }
    }

    // Collect method annotations
    bool VisitCXXMethodDecl(CXXMethodDecl *M) {
        if (!M->getParent()) return true;
        StringRef classNameRef = safeGetName(M->getParent());
        if (classNameRef.empty()) return true;
        std::string className = classNameRef.str();
        std::string methodName = M->getNameAsString();
        // Strip template parameters for constructors
        auto anglePos = methodName.find('<');
        if (anglePos != std::string::npos) methodName = methodName.substr(0, anglePos);
        // Include parameter count to distinguish overloads
        MethodKey key{className, methodName, M->getNumParams()};

        // Register concepts before processing other annotations
        registerConceptDefinitions(M);

        // Process requires/ensures/returns annotations
        MethodInfo info;
        for (auto *A : M->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                llvm::StringRef s = Ann->getAnnotation();
                if (s.starts_with(kRequiresPrefix)) {
                    std::string translated = translateParamNames(s.drop_front(kRequiresPrefix.size()), M);
                    auto expr = parseConstraintExpr(translated);
                    if (expr) info.preconditions.push_back(expr);
                }
                if (s.starts_with(kEnsuresPrefix)) {
                    auto p = parsePostcondition(s.drop_front(kEnsuresPrefix.size()).str(), true);
                    if (p) info.ensures.push_back(*p);
                }
                if (s.starts_with(kReturnsPrefix)) {
                    info.returns = s.drop_front(kReturnsPrefix.size()).str();
                }
                if (s.starts_with(kEnsuresIfTruePrefix)) {
                    auto bp = parseBoolPostcondition(s.drop_front(kEnsuresIfTruePrefix.size()).str(), true);
                    if (bp) info.boolPostconds.push_back(*bp);
                }
                if (s.starts_with(kEnsuresIfFalsePrefix)) {
                    auto bp = parseBoolPostcondition(s.drop_front(kEnsuresIfFalsePrefix.size()).str(), false);
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

    // RecursiveASTVisitor dispatches each kind of function declaration to its
    // own Traverse method; route them all through TraverseFunctionDecl.
    bool TraverseCXXMethodDecl(CXXMethodDecl *M) {
        // First, call VisitCXXMethodDecl to populate MethodAnnotations
        if (!VisitCXXMethodDecl(M)) return false;
        // Then call TraverseFunctionDecl for body analysis and contract propagation
        return TraverseFunctionDecl(M);
    }
    bool TraverseCXXConstructorDecl(CXXConstructorDecl *D) { return TraverseCXXMethodDecl(D); }
    bool TraverseCXXDestructorDecl(CXXDestructorDecl *D) { return TraverseCXXMethodDecl(D); }
    bool TraverseCXXConversionDecl(CXXConversionDecl *D) { return TraverseCXXMethodDecl(D); }

    bool TraverseFunctionDecl(FunctionDecl *F) {
        // Skip function template patterns — folds/packs are unexpanded here.
        // Pass 2b traverses instantiations where folds are expanded into calls.
        if (F && F->getDescribedFunctionTemplate()) {
            return true;
        }

        // A function defined inside another (a local class member) is analyzed
        // on its own; the enclosing function then continues where it was.
        if (CurrentFunctionDecl) {
            FunctionContext Outer = saveFunctionContext();
            bool Result = TraverseFunctionDeclBody(F);
            restoreFunctionContext(std::move(Outer));
            return Result;
        }
        return TraverseFunctionDeclBody(F);
    }

    bool TraverseFunctionDeclBody(FunctionDecl *F) {
        std::string fn = F->getNameAsString();
        CurrentFunctionDecl = F;
        beginFunctionPreconditions(F);  // Start collecting inferred preconditions
        beginFunctionPostconditions(F); // Start collecting inferred postconditions

        // Register concepts before processing other annotations
        registerConceptDefinitions(F);

        // Process ensures/ensures_if_* annotations.
        // For template specializations, skip if already registered —
        // all specializations share identical attrs from the pattern.
        bool newAnnotations = !F->getPrimaryTemplate() ||
            (!FnPostconditions.count(fn) &&
             !FnBoolPostconditions.count(fn) &&
             !FnScopeExitRequires.count(fn));
        if (newAnnotations) {
            for (auto *A : F->attrs()) {
                if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                    llvm::StringRef s = Ann->getAnnotation();
                    if (s.starts_with(kEnsuresPrefix)) {
                        std::string translated = translateParamNames(s.drop_front(kEnsuresPrefix.size()).str(), F);
                        auto post = parsePostcondition(translated);
                        if (post) FnPostconditions[fn].push_back(*post);
                    }
                    if (s.starts_with(kEnsuresIfTruePrefix)) {
                        auto bp = parseBoolPostcondition(s.drop_front(kEnsuresIfTruePrefix.size()).str(), true);
                        if (bp) FnBoolPostconditions[fn].push_back(*bp);
                    }
                    if (s.starts_with(kEnsuresIfFalsePrefix)) {
                        auto bp = parseBoolPostcondition(s.drop_front(kEnsuresIfFalsePrefix.size()).str(), false);
                        if (bp) FnBoolPostconditions[fn].push_back(*bp);
                    }
                    if (s == "dtt:total") TotalFunctions.insert(fn);
                    // Parse scope-exit requirements: dtt:~requires=result.@field == value
                    constexpr llvm::StringLiteral kScopeExitPrefix("dtt:~requires=");
                    if (s.starts_with(kScopeExitPrefix)) {
                        llvm::StringRef constraint = s.drop_front(kScopeExitPrefix.size());
                        // Parse "result.@field == value" format
                        // Expected format: result.@fieldname op value
                        if (constraint.starts_with("result.")) {
                            llvm::StringRef rest = constraint.drop_front(7);  // "result."
                            // Find the operator
                            size_t opPos = rest.find("==");
                            ProofOp op = ProofOp::EQ;
                            size_t opLen = 2;
                            if (opPos == llvm::StringRef::npos) {
                                opPos = rest.find("!=");
                                op = ProofOp::NE;
                            }
                            if (opPos == llvm::StringRef::npos) {
                                opPos = rest.find(">=");
                                op = ProofOp::GE;
                            }
                            if (opPos == llvm::StringRef::npos) {
                                opPos = rest.find("<=");
                                op = ProofOp::LE;
                            }
                            if (opPos == llvm::StringRef::npos) {
                                opPos = rest.find('>');
                                op = ProofOp::GT;
                                opLen = 1;
                            }
                            if (opPos == llvm::StringRef::npos) {
                                opPos = rest.find('<');
                                op = ProofOp::LT;
                                opLen = 1;
                            }
                            if (opPos != llvm::StringRef::npos) {
                                llvm::StringRef field = rest.substr(0, opPos).trim();
                                llvm::StringRef valueStr = rest.substr(opPos + opLen).trim();
                                int64_t value = 0;
                                if (!valueStr.getAsInteger(10, value)) {
                                    ScopeExitRequirement req;
                                    req.field = field.str();
                                    req.op = op;
                                    req.requiredValue = value;
                                    FnScopeExitRequires[fn].push_back(req);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Clear per-function state when entering a new function body
        // This prevents variable names from one function polluting another function's analysis
        if (F->hasBody() && F->isThisDeclarationADefinition()) {
            clearPerFunctionState();
            GotoStates.clear();
            DeclDepth.clear();
            CurrentScopeDepth = 0;  // Reset scope depth for new function

            // Per C++ standard, main's first parameter (argc) is guaranteed >= 1
            // argv[0] always contains the program name (or empty string)
            if (fn == "main" && F->getNumParams() >= 1) {
                ParmVarDecl *firstParam = F->getParamDecl(0);
                if (firstParam->getType()->isIntegerType() && firstParam->getIdentifier()) {
                    StringRef paramName = variableKey(firstParam);
                    FieldPath paramPath = FieldPath::fromSynthetic(paramName);
                    State.GlobalProofs.add(paramPath, Proof{"_value", ProofOp::GE, 1});
                }
            }

            // Contract propagation — inject dtt:requires as assumptions inside function body
            // This is sound because the caller MUST prove the requires at the call site.
            // If the caller cannot prove it, the call is rejected. Therefore, inside the
            // function body, we can safely assume the precondition holds.
            for (auto *A : F->attrs()) {
                if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                    llvm::StringRef s = Ann->getAnnotation();
                    if (s.starts_with(kRequiresPrefix)) {
                        auto expr = parseConstraintExpr(s.drop_front(kRequiresPrefix.size()).str());
                        if (expr) {
                            injectRequiresAsAssumption(F, expr);
                        }
                    }
                }
            }

            // For methods, also check MethodAnnotations
            if (auto *M = dyn_cast<CXXMethodDecl>(F)) {
                StringRef classNameRef = safeGetName(M->getParent());
                if (!classNameRef.empty()) {
                    std::string className = classNameRef.str();
                    std::string methodName = M->getNameAsString();
                    auto anglePos = methodName.find('<');
                    if (anglePos != std::string::npos) methodName = methodName.substr(0, anglePos);
                    MethodKey key{className, methodName, M->getNumParams()};

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
                if (!param->getIdentifier()) continue;

                StringRef paramName = param->getName();
                FieldPath paramPath = FieldPath::fromDecl(param);
                bool isProvenNonnull = false;

                if (funcIt != FunctionAnalysis.end()) {
                    auto paramIt = funcIt->second.paramNonnull.find(p);
                    if (paramIt != funcIt->second.paramNonnull.end() &&
                        paramIt->second.value_or(false)) {
                        // All callers pass non-null for this param - set _nonnull = 1
                        setFieldTyped(paramPath, "_nonnull", SymValue::Const(1));
                        isProvenNonnull = true;
                    }
                }

                // Track provenance for unproven pointer parameters
                if (!isProvenNonnull) {
                    SmallString<128> reason("parameter '");
                    reason += paramName;
                    reason += "' may be NULL (callers could pass NULL)";
                    NullProvenanceMap[paramName] = NullProvenance{
                        param->getLocation(),
                        std::string(reason.str())
                    };
                }
            }
        }

        if (AnalyzedLambda && AnalyzedLambda->getCallOperator() == F && !AnalyzedLambda->isMutable())
            for (const LambdaCapture &C : AnalyzedLambda->captures())
                if (C.capturesVariable())
                    if (auto *VD = dyn_cast<VarDecl>(C.getCapturedVar()); VD && VD->isInitCapture())
                        TraverseDecl(VD);
        bool result = RecursiveASTVisitor::TraverseFunctionDecl(F);
        bool IsDefinition = F->hasBody() && F->isThisDeclarationADefinition();
        if (IsDefinition && !State.Unreachable)
            checkScopeExitConstraints();  // Check linear type constraints at scope exit
        endFunctionPreconditions();  // Save collected preconditions for call-site checking
        endFunctionPostconditions(); // Save collected postconditions for call-site application
        CurrentFunctionDecl = nullptr;
        return result;
    }

    // Track compound statement (block scope) entry/exit for linear type constraints
    // When exiting a scope, check constraints for variables declared in that scope
    bool TraverseCompoundStmt(CompoundStmt *S) {
        if (!S) return true;
        if (ScannedForGotoLoops.insert(S).second) findGotoLoops(S);
        if (auto It = GotoLoops.find(S); It != GotoLoops.end() && !IteratingGotoLoops.count(S))
            return traverseGotoLoop(S, llvm::SmallVector<const LabelDecl *, 2>(It->second));

        // Entering a new scope
        CurrentScopeDepth++;

        // Traverse the body. A statement no path reaches is skipped unless a
        // jump could enter it through a label.
        bool result = WalkUpFromCompoundStmt(S);
        for (Stmt *Child : S->body()) {
            if (!result) break;
            result = traverseIfReachable(Child);
        }

        // Exiting scope - check constraints at this level and remove them
        if (!State.Unreachable) checkAndRemoveConstraintsAtScope(CurrentScopeDepth);
        CurrentScopeDepth--;

        return result;
    }

    // Note: VisitReturnStmt is defined later with auto-postcondition inference logic
    // Scope-exit constraint checking is done there as well

    std::map<std::string, SymValue> captureOldValues(CallExpr *Call) {
        std::map<std::string, SymValue> oldValues;
        for (unsigned i = 0; i < Call->getNumArgs(); i++) {
            auto *Arg = Call->getArg(i)->IgnoreParenImpCasts();
            FieldPath argTypedPath = getTypedPath(Arg);
            std::string argPath = argTypedPath.toString();
            if (!argTypedPath.isValid() || hasRuntimeIndex(argPath)) continue;
            std::string argPrefix = "arg" + std::to_string(i) + ".";
            // Iterate over TypedEnv
            for (const auto& [fieldPath, value] : State.TypedEnv) {
                llvm::StringRef pathStr = fieldPath.toStringRef();
                if (startsWithDotPrefix(pathStr, argPath) && pathStr.find("._proof_") == llvm::StringRef::npos) {
                    std::string field = pathStr.drop_front(argPath.size() + 1).str();
                    oldValues[argPrefix + field] = value;
                }
            }
        }
        return oldValues;
    }

    // Capture old values for method calls (this-based)
    std::map<std::string, SymValue> captureOldValuesMethod(const std::string& objPath) {
        std::map<std::string, SymValue> oldValues;
        // Iterate over TypedEnv
        for (const auto& [fieldPath, value] : State.TypedEnv) {
            llvm::StringRef pathStr = fieldPath.toStringRef();
            if (startsWithDotPrefix(pathStr, objPath) && pathStr.find("._proof_") == llvm::StringRef::npos) {
                std::string field = pathStr.drop_front(objPath.size() + 1).str();
                oldValues[field] = value;
            }
        }
        return oldValues;
    }

    // The block depth at which the variable a path starts with was declared:
    // its release obligations end with that block. Unknown variables
    // (globals, parameters) use the current depth.
    unsigned declDepthOf(llvm::StringRef Path) {
        auto It = DeclDepth.find(Path.take_until([](char C) { return C == '.' || C == '['; }));
        return It != DeclDepth.end() ? It->second : CurrentScopeDepth;
    }

    // Register scope-exit constraints for a function call.
    // Result-targeted requirements apply to resultPath; argument-targeted
    // requirements resolve the argument expression path.
    void registerScopeExitConstraints(const std::string& resultPath, CallExpr *Call) {
        auto *Callee = Call->getDirectCallee();
        if (!Callee) return;
        std::string fn = Callee->getNameAsString();

        auto it = FnScopeExitRequires.find(fn);
        if (it == FnScopeExitRequires.end()) return;

        ResourceId rid = Call->getBeginLoc();

        for (const auto& req : it->second) {
            // Resolve which variable the constraint applies to
            std::string targetPath;
            if (req.isResult) {
                targetPath = resultPath;
            } else if (static_cast<unsigned>(req.argIndex) < Call->getNumArgs()) {
                targetPath = getExprPath(Call->getArg(req.argIndex));
            }
            if (targetPath.empty()) continue;
            // One evaluation is one resource, even when several contracts
            // declare the same obligation (libc.dtt: open(2) and open(3)).
            if (llvm::any_of(State.CurrentScopeExitConstraints, [&](const ScopeExitConstraint& c) {
                    return c.callVisit == CallVisit && c.resourceId == rid && c.varPath == targetPath &&
                           c.field == req.field;
                }))
                continue;

            // Check existing constraints before overwriting with new resource
            if (req.isResult && hasConstraints(targetPath)) {
                checkConstraintsBeforeOverwrite(targetPath, Call->getBeginLoc());
            }

            // Map variable to resource
            State.VarToResource[targetPath] = rid;

            ScopeExitConstraint constraint;
            constraint.varPath = targetPath;
            constraint.resourceId = rid;
            constraint.field = req.field;
            constraint.op = req.op;
            constraint.requiredValue = req.requiredValue;
            constraint.acquireLoc = Call->getBeginLoc();
            constraint.scopeDepth = declDepthOf(targetPath);
            constraint.guard = req.guard;
            constraint.callVisit = CallVisit;
            State.CurrentScopeExitConstraints.push_back(constraint);
        }
    }

    // Whether C's guard fails on this path (fopen returned null): the path
    // holds no resource. A handle overwritten after acquiring one was checked
    // at the overwrite, so the guard's field still describes the acquisition.
    bool guardFails(const ScopeExitConstraint &C) {
        if (!C.guard) return false;
        dtt::PostconditionGuard Failed = *C.guard;
        Failed.op = negateProofOp(Failed.op);
        return checkGuardField(C.varPath.str(), Failed);
    }

    // Check a single scope-exit constraint and report error if violated
    // errorLoc: where to report the error (return stmt or scope exit)
    // If invalid, falls back to acquireLoc
    void checkSingleConstraint(const ScopeExitConstraint& constraint,
                               SourceLocation errorLoc = SourceLocation()) {
        // If there's a `when` guard, skip this constraint if the guard isn't satisfied.
        // e.g., `scope_exit result.@open == 0 when result._nonnull != 0`
        // — only check leak if the resource was actually acquired (nonnull).
        if (constraint.guard) {
            // For scope_exit guards, isResult=true means the constraint's own varPath
            StringRef basePath = constraint.varPath.str();
            if (!checkGuardField(basePath, *constraint.guard)) {
                if (guardFails(constraint)) return;
                // Guard not proven. But if the ghost field was set via deferred
                // evaluation (e.g., inside `if (f)`), the resource was acquired
                // on some path — still check the constraint.
                FieldPath vp = FieldPath::fromSynthetic(constraint.varPath);
                FieldPath fp = vp.withSynthetic(constraint.field);
                if (State.TypedEnv.find(fp) == State.TypedEnv.end())
                    return;  // Field never set → no resource → skip silently
                // Field IS set → resource was acquired → fall through to check
            }
        }

        FieldPath varPath = FieldPath::fromSynthetic(constraint.varPath);
        FieldPath fieldPath = varPath.withSynthetic(constraint.field);

        // Use provided errorLoc or fall back to acquireLoc
        SourceLocation reportLoc = errorLoc.isValid() ? errorLoc : constraint.acquireLoc;

        auto it = State.TypedEnv.find(fieldPath);
        if (it == State.TypedEnv.end()) {
            // Field not found - constraint may be violated if required value is specific
            // For now, assume unknown means not satisfied
            if (constraint.requiredValue != 0 || constraint.op != ProofOp::EQ) {
                Diag.Report(reportLoc, ErrScopeExitConstraint)
                    << constraint.varPath
                    << constraint.field
                    << proofOpToString(constraint.op)
                    << constraint.requiredValue
                    << "unknown";
                if (constraint.acquireLoc.isValid()) {
                    Diag.Report(constraint.acquireLoc, NoteScopeExitAcquired);
                }
            }
            return;
        }

        auto evalResult = it->second.eval();
        if (!evalResult) {
            // Cannot evaluate - report error
            Diag.Report(reportLoc, ErrScopeExitConstraint)
                << constraint.varPath
                << constraint.field
                << proofOpToString(constraint.op)
                << constraint.requiredValue
                << it->second.str();
            if (constraint.acquireLoc.isValid()) {
                Diag.Report(constraint.acquireLoc, NoteScopeExitAcquired);
            }
            return;
        }

        if (!checkProofOp(*evalResult, constraint.op, constraint.requiredValue)) {
            Diag.Report(reportLoc, ErrScopeExitConstraint)
                << constraint.varPath
                << constraint.field
                << proofOpToString(constraint.op)
                << constraint.requiredValue
                << *evalResult;
            if (constraint.acquireLoc.isValid()) {
                Diag.Report(constraint.acquireLoc, NoteScopeExitAcquired);
            }
        }
    }

    // Check constraints at a specific scope level and remove them
    // Called when exiting a block scope (CompoundStmt)
    void checkAndRemoveConstraintsAtScope(unsigned scopeDepth) {
        auto it = State.CurrentScopeExitConstraints.begin();
        while (it != State.CurrentScopeExitConstraints.end()) {
            if (it->scopeDepth >= scopeDepth) {
                checkSingleConstraint(*it);
                it = State.CurrentScopeExitConstraints.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Check ALL scope-exit constraints (for early return or function exit)
    // Reports errors for any linear resources that were not properly consumed
    // errorLoc: where to report errors (e.g., return statement location)
    // Skips constraints for pointers proven to be NULL (open failed, no resource)
    void checkScopeExitConstraints(SourceLocation errorLoc = SourceLocation()) {
        for (const auto& constraint : State.CurrentScopeExitConstraints) {
            // Skip constraint if pointer is proven NULL (e.g., fopen failed)
            // Pattern: FILE* f = fopen(...); if (!f) return; // f is NULL here
            auto nonnull = checkNonnull(constraint.varPath);
            if (nonnull.has_value() && !*nonnull) {
                // Pointer is proven NULL - no resource to release
                continue;
            }
            checkSingleConstraint(constraint, errorLoc);
        }
        // Check for unchecked resource pointers: pending guarded postconditions
        // that were never applied (the pointer was never null-checked).
        // Only fire in enforce mode (this is a coverage gap, not a proven bug).
        if (GlobalMode == DTTMode::Enforce) {
            llvm::StringSet<> warned;
            for (const auto& pending : State.PendingGuardedPostconditions) {
                if (pending.applied || pending.refuted) continue;
                // Check if the ghost field was already set (via another path)
                FieldPath fp = FieldPath::fromSynthetic(pending.targetPath);
                FieldPath fieldPath = fp.withSynthetic(pending.field);
                if (State.TypedEnv.find(fieldPath) != State.TypedEnv.end()) continue;
                // A proven failed acquisition has no resource to release.
                // Honor the actual guard, including integer descriptors.
                auto failedGuard = pending.guard;
                failedGuard.op = negateProofOp(failedGuard.op);
                if (checkGuardField(pending.targetPath, failedGuard)) continue;
                // Skip if pointer was returned (ownership transferred) or empty
                if (pending.targetPath.empty()) continue;
                llvm::StringRef key = pending.targetPath;
                if (!warned.insert(key).second) continue;
                Diag.Report(pending.callLoc, WarnUncheckedResource)
                    << pending.targetPath << pending.calleeName;
            }
        }
    }

    // Add alias: destination variable points to same resource as source
    // With resource-centric tracking, both variables share the resource.
    // Constraints stay on the resource, not moved between variables.
    void transferConstraints(llvm::StringRef srcPath, llvm::StringRef dstPath,
                            SourceLocation transferLoc) {
        // Look up which resource the source points to
        auto srcIt = State.VarToResource.find(srcPath);
        if (srcIt != State.VarToResource.end()) {
            // Destination now points to the same resource
            State.VarToResource[dstPath] = srcIt->second;
        }

        // For backward compatibility: also duplicate constraints for destination
        // This ensures constraint checking works for both variables
        llvm::SmallVector<ScopeExitConstraint, 4> newConstraints;
        for (const auto& constraint : State.CurrentScopeExitConstraints) {
            if (constraint.varPath == srcPath) {
                ScopeExitConstraint newConstraint = constraint;
                newConstraint.varPath = dstPath;
                newConstraint.acquireLoc = transferLoc;
                newConstraints.push_back(std::move(newConstraint));
            }
        }
        State.CurrentScopeExitConstraints.append(newConstraints.begin(), newConstraints.end());
        // Note: We do NOT clear source constraints - both have constraints now
    }

    // Clear all scope-exit constraints for a variable
    // Used when variable is set to nullptr (ownership released/transferred)
    void clearConstraintsForVar(llvm::StringRef varPath) {
        llvm::erase_if(State.CurrentScopeExitConstraints,
            [&](const ScopeExitConstraint& c) { return c.varPath == varPath; });
    }

    // Check if a variable has any scope-exit constraints
    bool hasConstraints(llvm::StringRef varPath) {
        return llvm::any_of(State.CurrentScopeExitConstraints,
            [&](const ScopeExitConstraint& c) { return c.varPath == varPath; });
    }

    // Check constraints before overwriting a variable, report leak if unsatisfied
    // With resource-centric tracking: if other handles to the resource exist,
    // releasing this handle is OK (other handles can still close the resource).
    void checkConstraintsBeforeOverwrite(llvm::StringRef varPath,
                                         SourceLocation overwriteLoc) {
        // Check if this variable points to a tracked resource
        auto resourceIt = State.VarToResource.find(varPath);
        if (resourceIt != State.VarToResource.end()) {
            ResourceId rid = resourceIt->second;

            // If other handles exist, it's safe to release this one
            if (countOtherHandles(rid, varPath) > 0) {
                State.VarToResource.erase(varPath);
                clearConstraintsForVar(varPath);
                return;
            }
        }

        FieldPath varTypedPath = FieldPath::fromSynthetic(varPath);

        // No other handles - check if constraint is satisfied before releasing
        for (const auto& constraint : State.CurrentScopeExitConstraints) {
            if (constraint.varPath != varPath || guardFails(constraint)) continue;

            FieldPath fieldPath = varTypedPath.withSynthetic(constraint.field);
            auto it = State.TypedEnv.find(fieldPath);

            // Get current value
            llvm::SmallString<16> currentValStr;
            std::optional<int64_t> currentVal;
            if (it != State.TypedEnv.end()) {
                currentVal = it->second.eval();
                if (currentVal) {
                    llvm::raw_svector_ostream(currentValStr) << *currentVal;
                } else {
                    currentValStr = it->second.str();
                }
            } else {
                currentValStr = "unknown";
            }

            // Check if constraint is satisfied
            bool satisfied = currentVal &&
                checkProofOp(*currentVal, constraint.op, constraint.requiredValue);

            if (!satisfied) {
                Diag.Report(overwriteLoc, ErrOverwriteLeak)
                    << varPath
                    << constraint.field
                    << proofOpToString(constraint.op)
                    << constraint.requiredValue
                    << currentValStr;
                if (constraint.acquireLoc.isValid()) {
                    Diag.Report(constraint.acquireLoc, NoteScopeExitAcquired);
                }
            }
        }

        // Remove variable from resource tracking
        State.VarToResource.erase(varPath);
        clearConstraintsForVar(varPath);
    }

    // Evaluate a `when` guard against current state (TypedEnv + proof system).
    // Returns true if the guard is satisfied (postcondition should apply).
    // Core guard check: given a resolved base path, check guard.FIELD OP VALUE.
    // Checks both field values in TypedEnv and proofs from branch analysis.
    bool checkGuardField(StringRef basePath, const dtt::PostconditionGuard &g) {
        if (basePath.empty()) return false;
        FieldPath guardPath = FieldPath::fromSynthetic(basePath);
        FieldPath guardField = fieldStorage(guardPath, g.field);
        auto it = State.TypedEnv.find(guardField);
        if (it != State.TypedEnv.end()) {
            auto val = it->second.eval();
            if (val) return checkProofOp(*val, g.op, g.value);
        }
        // Fall back to proof system — branch analysis may have established
        // proofs (e.g., `if (!ptr) return;` proves _nonnull!=0, fd == -1
        // proves _value < 0) that aren't reflected in TypedEnv field values.
        return hasProofTyped(guardPath, Proof{g.field, g.op, g.value});
    }

    // Unified guard evaluation for ALL contexts.
    //   basePath: the "this" / "result" path (used when guard.isResult == true)
    //   Call:     optional CallExpr for resolving argN (nullptr if unavailable)
    bool isGuardSatisfied(const dtt::PostconditionGuard &g,
                          StringRef basePath,
                          CallExpr *Call = nullptr) {
        if (g.isResult) {
            return checkGuardField(basePath, g);
        }
        // argN: resolve from CallExpr if available
        if (Call && static_cast<unsigned>(g.argIndex) < Call->getNumArgs()) {
            std::string argPath = getExprPath(Call->getArg(g.argIndex));
            return checkGuardField(argPath, g);
        }
        return false;
    }

    // Unified postcondition application - works for ALL functions (void and non-void)
    // For void functions, pass empty resultPath - result.@field postconditions are ignored
    // For non-void functions, pass the variable receiving the result
    void applyPostconditions(const std::string& resultPath, CallExpr *Call,
                             const std::map<std::string, SymValue>& oldValues) {
        ++CallVisit;
        if (!callRequirementsProven(Call, Ctx)) {
            if (!resultPath.empty())
                invalidateFailedCallPath(resultPath, Call->getBeginLoc());
            return;
        }
        auto *Callee = Call->getDirectCallee();
        if (!Callee || !Callee->getIdentifier()) return;
        StringRef fn = Callee->getName();

        // Single lookup for free function annotations
        FreeFunctionKey freeKey{fn.data(), Call->getNumArgs()};
        auto freeIt = FreeFunctionAnnotations.find(freeKey);

        // Apply arg postconditions BEFORE scope_exit registration.
        // Handles freopen(path, mode, fp) where arg2.@open = CLOSED
        // must be applied before checking if fp's old scope_exit is satisfied.
        if (resultPath.empty() && freeIt != FreeFunctionAnnotations.end()) {
            for (const auto& post : freeIt->second.postconditions) {
                if (post.argIndex < 0 ||
                    static_cast<unsigned>(post.argIndex) >= Call->getNumArgs())
                    continue;
                if (post.guard && !isGuardSatisfied(*post.guard, "", Call))
                    continue;
                Expr *ModArg = Call->getArg(post.argIndex)->IgnoreParenImpCasts();
                FieldPath modPath = getTypedPath(ModArg);
                if (!modPath.isValid()) continue;
                StringRef fieldRef(post.field);
                if (fieldRef.starts_with("deref."))
                    fieldRef = fieldRef.drop_front(6);
                setFieldTyped(modPath, fieldRef, SymValue::Const(post.value));
            }
        }

        // Register scope-exit constraints and result postconditions (non-void only)
        if (!resultPath.empty()) {
            registerScopeExitConstraints(resultPath, Call);

            if (freeIt != FreeFunctionAnnotations.end()) {
                FieldPath resultTypedPath = FieldPath::fromSynthetic(resultPath);
                for (const auto& rpost : freeIt->second.resultPostconditions) {
                    if (rpost.guard && !isGuardSatisfied(*rpost.guard, resultPath, Call)) {
                        deferPostcondition(resultPath, rpost.field, rpost.value,
                                           *rpost.guard, fn, Call->getBeginLoc());
                        continue;
                    }
                    setFieldTyped(resultTypedPath, rpost.field, SymValue::Const(rpost.value));
                }
            }
        }

        auto it = FnPostconditions.find(fn);
        if (it == FnPostconditions.end()) return;

        // Build substitution map for all arguments
        std::map<std::string, SymValue> subst;
        for (unsigned i = 0; i < Call->getNumArgs(); i++) {
            auto *Arg = Call->getArg(i)->IgnoreParenImpCasts();
            FieldPath argTypedPath = getTypedPath(Arg);
            std::string argPath = argTypedPath.toString();
            std::string argKey = "arg" + std::to_string(i);

            // Try to evaluate as constant first
            Expr::EvalResult R;
            if (safeEvaluateAsInt(Arg, R, Ctx)) {
                auto val = safeGetInt64(R.Val.getInt());
                if (val) subst[argKey] = SymValue::Const(*val);
            } else if (argTypedPath.isValid() && !hasRuntimeIndex(argPath)) {
                subst[argKey] = SymValue::Var(argPath);
            }

            // Add arg0._field, arg1._field etc. for object fields
            if (argTypedPath.isValid() && !hasRuntimeIndex(argPath)) {
                std::string argPrefix = argKey + ".";
                for (const auto& [fieldPath, value] : State.TypedEnv) {
                    llvm::StringRef pathStr = fieldPath.toStringRef();
                    if (startsWithDotPrefix(pathStr, argPath) &&
                        pathStr.find("._proof_") == llvm::StringRef::npos) {
                        std::string field = pathStr.drop_front(argPath.size() + 1).str();
                        subst[argPrefix + field] = value;
                    }
                }
            }
        }
        for (auto& kv : oldValues) subst["old:" + kv.first] = kv.second;

        // Apply each postcondition
        for (const auto& post : it->second) {
            // Defer if `when` guard not satisfied (instead of silently skipping)
            if (post.guard && !isGuardSatisfied(*post.guard, resultPath, Call)) {
                // Determine target path for deferred evaluation
                std::string targetPath;
                if (post.target == PostconditionTarget::Result && !resultPath.empty()) {
                    targetPath = resultPath;
                } else if (post.target == PostconditionTarget::Arg && resultPath.empty()) {
                    if (static_cast<unsigned>(post.argIndex) < Call->getNumArgs()) {
                        auto *Arg = Call->getArg(post.argIndex)->IgnoreParenImpCasts();
                        FieldPath argTypedPath = getTypedPath(Arg);
                        if (argTypedPath.isValid() && !hasRuntimeIndex(argTypedPath.toString())) {
                            targetPath = argTypedPath.toString();
                        }
                    }
                }
                if (!targetPath.empty()) {
                    SymValue guardedResult = post.expr.substitute(subst);
                    auto evalResult = guardedResult.eval();
                    if (evalResult) {
                        deferPostcondition(targetPath, post.field, *evalResult,
                                           *post.guard, fn, Call->getBeginLoc());
                    }
                }
                continue;
            }

            SymValue result = post.expr.substitute(subst);

            if (post.target == PostconditionTarget::Result && !resultPath.empty()) {
                // Apply to return value (only for non-void functions)
                FieldPath resultTypedPath = FieldPath::fromSynthetic(resultPath);
                setFieldTyped(resultTypedPath, post.field, result);
            } else if (post.target == PostconditionTarget::Arg && resultPath.empty()) {
                // Apply Arg postconditions ONLY when resultPath is empty (called from VisitCallExpr)
                // When resultPath is non-empty (called from VisitVarDecl), skip Arg postconditions
                // because preconditions haven't been checked yet - VisitCallExpr will handle them
                if (static_cast<unsigned>(post.argIndex) < Call->getNumArgs()) {
                    auto *Arg = Call->getArg(post.argIndex)->IgnoreParenImpCasts();
                    FieldPath argTypedPath = getTypedPath(Arg);
                    if (argTypedPath.isValid() && !hasRuntimeIndex(argTypedPath.toString())) {
                        // Propagate to aliases: fclose(f) should update g if g aliases f
                        setFieldWithAliasPropagation(argTypedPath, post.field, result);
                    }
                } else {
                    // Invalid arg index - warn user about annotation error
                    Diag.Report(Call->getBeginLoc(), WarnInvalidArgIndex)
                        << post.argIndex << Call->getNumArgs();
                }
            }
            // Note: result.@field on void functions is silently ignored (no legacy fallback)
        }
    }

    // Apply method postconditions
    void applyMethodPostconditions(const FieldPath& objTypedPath, const MethodInfo& info,
                                   const std::map<std::string, SymValue>& oldValues,
                                   const std::map<std::string, SymValue>& argValues = {},
                                   SourceLocation loc = SourceLocation(),
                                   llvm::StringRef methodDesc = "") {
        std::string objPath = objTypedPath.toString();
        std::map<std::string, SymValue> subst;

        // Substitute old(_field) with captured values
        for (auto& kv : oldValues) {
            subst["old:" + kv.first] = kv.second;  // old(_size) -> oldValues["_size"]
        }

        // Substitute arg0, arg1, etc. with argument values
        for (auto& kv : argValues) {
            subst[kv.first] = kv.second;  // arg0 -> value
        }

        // Current values - Phase 1: Iterate over TypedEnv
        for (const auto& [fieldPath, value] : State.TypedEnv) {
            llvm::StringRef pathStr = fieldPath.toStringRef();
            if (startsWithDotPrefix(pathStr, objPath) && pathStr.find("._proof_") == llvm::StringRef::npos) {
                std::string field = pathStr.drop_front(objPath.size() + 1).str();
                // If field already starts with _ or @ (ghost state), use as-is
                if (!field.empty() && (field[0] == '_' || field[0] == '@')) {
                    subst[field] = value;
                } else {
                    subst["_" + field] = value;
                }
            }
        }

        for (const auto& post : info.ensures) {
            // Skip if `when` guard not satisfied
            if (post.guard && !isGuardSatisfied(*post.guard, objPath))
                continue;
            SymValue result = post.expr.substitute(subst);
            setFieldTyped(objTypedPath, post.field, result, loc, methodDesc);
        }
    }

    // Track buffer properties for pointers derived from arrays
    // Takes VarDecl* to enable type-safe FieldPath tracking
    void trackBufferPointerInit(const VarDecl* VD, QualType varType, Expr* Init) {
        if (!varType->isPointerType()) return;

        // Create typed path for the variable
        FieldPath varPath = FieldPath::fromDecl(VD);
        auto RecordBase = llvm::make_scope_exit([&] { recordPointerBase(varPath, Init); });

        QualType dstPointee = varType->getPointeeType();
        bool dstIsVoid = dstPointee->isVoidType();

        // Helper to set buffer properties using typed path
        auto setBufferProps = [&](int64_t baseLen, SymValue offset) {
            setFieldTyped(varPath, "_base_length", SymValue::Const(baseLen));
            setFieldTyped(varPath, "_offset", offset);
            // Compute and store _size for constraint checking
            SymValue size = SymValue::Sub(SymValue::Const(baseLen), offset);
            setFieldTyped(varPath, "_size", size);
        };

        // Helper to set void* properties (bytes only)
        auto setVoidBufferProps = [&](int64_t sizeBytes) {
            setFieldTyped(varPath, "_size_bytes", SymValue::Const(sizeBytes));
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
                    setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                    return;
                }
                // Typed pointer: store element count
                auto maybeDstElemSize = safeGetTypeSizeInChars(Ctx, dstPointee);
                if (!maybeDstElemSize) return;  // Can't determine size for dependent/incomplete types
                int64_t dstElemSize = *maybeDstElemSize;
                int64_t dstCount = (dstElemSize > 0) ? totalBytes / dstElemSize : count;
                setBufferProps(dstCount, SymValue::Const(0));
                setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                return;
            }
            // Copying from another tracked pointer - use typed paths
            FieldPath srcPath = FieldPath::fromDecl(DRE->getDecl());
            QualType srcPtrType = srcType;
            bool srcIsVoid = srcPtrType->isPointerType() &&
                             srcPtrType->getPointeeType()->isVoidType();

            // Source is void*: has _size_bytes
            SymValue srcSizeBytes = getFieldTyped(srcPath, "_size_bytes");
            if (srcIsVoid && srcSizeBytes.kind != SymValue::UNKNOWN) {
                if (dstIsVoid) {
                    // void* to void*: copy _size_bytes
                    setFieldTyped(varPath, "_size_bytes", srcSizeBytes);
                } else {
                    // void* to typed*: convert bytes to elements
                    auto maybeDstElemSize = safeGetTypeSizeInChars(Ctx, dstPointee);
                    if (maybeDstElemSize && srcSizeBytes.kind == SymValue::CONST && *maybeDstElemSize > 0) {
                        int64_t dstCount = srcSizeBytes.constVal / *maybeDstElemSize;
                        setBufferProps(dstCount, SymValue::Const(0));
                    }
                }
                setFieldTyped(varPath, "_nonnull", getFieldTyped(srcPath, "_nonnull"));
                return;
            }

            // Source is typed*: has _base_length/_offset
            SymValue srcBaseLen = getFieldTyped(srcPath, "_base_length");
            if (srcBaseLen.kind != SymValue::UNKNOWN) {
                if (dstIsVoid) {
                    // typed* to void*: convert elements to bytes
                    SymValue srcOffset = getFieldTyped(srcPath, "_offset");
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
                    setFieldTyped(varPath, "_base_length", srcBaseLen);
                    setFieldTyped(varPath, "_offset", getFieldTyped(srcPath, "_offset"));
                }
                setFieldTyped(varPath, "_nonnull", getFieldTyped(srcPath, "_nonnull"));

                // Phase 3: Propagate base origin for pointer copy
                auto srcOriginIt = State.PointerBaseOrigins.find(srcPath);
                if (srcOriginIt != State.PointerBaseOrigins.end()) {
                    State.PointerBaseOrigins[varPath] = srcOriginIt->second;
                }
                return;
            }

            // Phase 3: Track base origin even for untracked pointers (e.g., raw parameters)
            // Check if source already has a base origin (from prior arithmetic)
            auto srcOriginIt = State.PointerBaseOrigins.find(srcPath);
            if (srcOriginIt != State.PointerBaseOrigins.end()) {
                State.PointerBaseOrigins[varPath] = srcOriginIt->second;
            } else {
                // Check if source is a parameter - establish base origin with zero offset
                auto srcParamIdx = getParameterIndex(Init, CurrentFunctionDecl);
                if (srcParamIdx) {
                    State.PointerBaseOrigins[varPath] = BaseOriginInfo(srcPath, SymValue::Const(0), srcParamIdx);
                }
            }
            // Propagate nullability
            setFieldTyped(varPath, "_nonnull", getFieldTyped(srcPath, "_nonnull"));
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
                setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
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
                setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                return;
            }
        }

        // Case 4: Address-of expressions
        if (auto *UO = dyn_cast<UnaryOperator>(Init)) {
            if (UO->getOpcode() == UO_AddrOf) {
                Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();

                // TIER 5.1: Stack aliasing tracking
                // When we see int* p = &x, record that p points to stack variable x
                // This enables: *p = 42; to mark x as initialized
                // Special case: int* p = &r where r is a reference - p points to what r refers to
                if (auto *DRE = dyn_cast<DeclRefExpr>(Sub)) {
                    if (auto *TargetVD = dyn_cast<VarDecl>(DRE->getDecl())) {
                        ResourceId rid;
                        llvm::SmallString<32> targetPath;

                        // Check if target is a reference - if so, use its underlying ResourceId
                        if (TargetVD->getType()->isLValueReferenceType()) {
                            // StringMap accepts StringRef for lookup - no .str() needed
                            auto it = State.VarToResource.find(variableKey(TargetVD));
                            if (it != State.VarToResource.end()) {
                                rid = it->second;
                                // Get the original variable name from StackVarPaths
                                auto stackIt = State.StackVarPaths.find(rid);
                                if (stackIt != State.StackVarPaths.end()) {
                                    targetPath = stackIt->second;
                                }
                            }
                        } else {
                            // Direct variable: int* p = &x
                            rid = TargetVD->getLocation();
                            targetPath = variableKey(TargetVD);
                        }

                        if (rid.isValid() && !targetPath.empty()) {
                            State.VarToResource[varPath.toStringRef()] = rid;
                            State.StackVarPaths[rid] = targetPath;
                        }
                    }
                }

                // Case 4a: Address-of array element - int* p = &arr[k];
                if (auto *ASE = dyn_cast<ArraySubscriptExpr>(Sub)) {
                    Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
                    if (auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
                        QualType arrType = DRE->getType();
                        if (const auto *CAT = Ctx.getAsConstantArrayType(arrType)) {
                            int64_t count = CAT->getSize().getZExtValue();
                            SymValue idx = exprToSymValueImpl(ASE->getIdx(), Ctx);
                            setBufferProps(count, idx);
                            setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
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
                        setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
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
                        setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
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
                        setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
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
                        setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                        return;
                    }
                    // Pointer arithmetic from another tracked pointer - use typed paths
                    FieldPath srcPath = FieldPath::fromDecl(DRE->getDecl());
                    SymValue srcBaseLen = getFieldTyped(srcPath, "_base_length");
                    if (srcBaseLen.kind != SymValue::UNKNOWN) {
                        SymValue srcOffset = getFieldTyped(srcPath, "_offset");
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
                        setFieldTyped(varPath, "_base_length", srcBaseLen);
                        setFieldTyped(varPath, "_offset", newOffset);
                        SymValue size = SymValue::Sub(srcBaseLen, newOffset);
                        setFieldTyped(varPath, "_size", size);
                        setFieldTyped(varPath, "_nonnull", getFieldTyped(srcPath, "_nonnull"));

                        // Phase 3: Track base origin for precondition rewriting
                        // Convert element offset to byte offset
                        SymValue offsetBytes = offsetVal;
                        int64_t elemSizeVal = 1;
                        bool isNegated = (BO->getOpcode() == BO_Sub);
                        if (auto elemSize = safeGetTypeSizeInChars(Ctx, VD->getType()->getPointeeType())) {
                            elemSizeVal = *elemSize;
                            // Skip multiplication for byte-sized elements (char, uint8_t, etc.)
                            if (*elemSize != 1) {
                                offsetBytes = SymValue::Mul(offsetVal, SymValue::Const(*elemSize));
                            }
                            if (isNegated) {
                                offsetBytes = SymValue::Sub(SymValue::Const(0), offsetBytes);
                            }
                        }

                        auto srcOriginIt = State.PointerBaseOrigins.find(srcPath);
                        auto offsetParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                        if (srcOriginIt != State.PointerBaseOrigins.end()) {
                            // Propagate existing base origin with accumulated offset
                            BaseOriginInfo newOrigin = srcOriginIt->second;
                            newOrigin.offsetBytes = SymValue::Add(newOrigin.offsetBytes, offsetBytes);
                            // Track offset param if this is the first offset contribution
                            if (!newOrigin.offsetParamIdx && offsetParamIdx) {
                                newOrigin.offsetParamIdx = offsetParamIdx;
                                newOrigin.offsetElemSize = elemSizeVal;
                                newOrigin.offsetNegated = isNegated;
                            }
                            State.PointerBaseOrigins[varPath] = std::move(newOrigin);
                        } else {
                            // Check if source is a parameter - create new base origin
                            auto srcParamIdx = getParameterIndex(LHS, CurrentFunctionDecl);
                            if (srcParamIdx) {
                                State.PointerBaseOrigins[varPath] = BaseOriginInfo(srcPath, offsetBytes, srcParamIdx, offsetParamIdx, elemSizeVal, isNegated);
                            }
                        }
                        return;
                    }
                    // No buffer tracking - check for null pointer arithmetic
                    SymValue srcNonnull = getFieldTyped(srcPath, "_nonnull");
                    if (srcNonnull.kind == SymValue::CONST && srcNonnull.constVal == 0) {
                        // Pointer arithmetic on known null pointer is UB
                        Diag.Report(Init->getBeginLoc(), ErrNullPtrArith);
                        return;
                    }
                    // Propagate nullability even without buffer tracking
                    setFieldTyped(varPath, "_nonnull", srcNonnull);

                    // Phase 3: Track base origin even without buffer size tracking
                    SymValue offsetVal = exprToSymValueImpl(RHS, Ctx);
                    SymValue offsetBytes = offsetVal;
                    int64_t elemSizeVal = 1;
                    bool isNegated = (BO->getOpcode() == BO_Sub);
                    if (auto elemSize = safeGetTypeSizeInChars(Ctx, VD->getType()->getPointeeType())) {
                        elemSizeVal = *elemSize;
                        // Skip multiplication for byte-sized elements (char, uint8_t, etc.)
                        if (*elemSize != 1) {
                            offsetBytes = SymValue::Mul(offsetVal, SymValue::Const(*elemSize));
                        }
                        if (isNegated) {
                            offsetBytes = SymValue::Sub(SymValue::Const(0), offsetBytes);
                        }
                    }

                    auto srcOriginIt = State.PointerBaseOrigins.find(srcPath);
                    auto offsetParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                    if (srcOriginIt != State.PointerBaseOrigins.end()) {
                        BaseOriginInfo newOrigin = srcOriginIt->second;
                        newOrigin.offsetBytes = SymValue::Add(newOrigin.offsetBytes, offsetBytes);
                        if (!newOrigin.offsetParamIdx && offsetParamIdx) {
                            newOrigin.offsetParamIdx = offsetParamIdx;
                            newOrigin.offsetElemSize = elemSizeVal;
                            newOrigin.offsetNegated = isNegated;
                        }
                        State.PointerBaseOrigins[varPath] = std::move(newOrigin);
                    } else {
                        auto srcParamIdx = getParameterIndex(LHS, CurrentFunctionDecl);
                        if (srcParamIdx) {
                            State.PointerBaseOrigins[varPath] = BaseOriginInfo(srcPath, offsetBytes, srcParamIdx, offsetParamIdx, elemSizeVal, isNegated);
                        }
                    }
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
                        setFieldTyped(varPath, "_base_length", sizeVal);
                        setFieldTyped(varPath, "_offset", SymValue::Const(0));
                    }
                    setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                    // Track allocation source for lifetime analysis
                    PointerLifetimeState pls;
                    pls.lifetime = PointerLifetime::Valid;
                    pls.allocLocation = NE->getBeginLoc();
                    if (isPlacementNew(NE)) {
                        pls.allocSource = AllocSource::Placement;
                    } else {
                        pls.allocSource = AllocSource::NewArray;
                    }
                    State.PointerLifetimes[variableKey(VD)] = pls;
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
                setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                return;
            }

            // Check if casting from tracked pointer - use typed paths
            if (auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
                FieldPath srcPath = FieldPath::fromDecl(DRE->getDecl());
                QualType srcPointee = srcType->isPointerType() ? srcType->getPointeeType() : QualType();
                bool srcIsVoid = srcPointee.isNull() ? false : srcPointee->isVoidType();

                // Source is void*: use _size_bytes
                SymValue srcSizeBytes = getFieldTyped(srcPath, "_size_bytes");
                if (srcIsVoid && srcSizeBytes.kind != SymValue::UNKNOWN) {
                    if (dstIsVoid) {
                        // void* to void*: copy _size_bytes
                        setFieldTyped(varPath, "_size_bytes", srcSizeBytes);
                    } else {
                        // void* to typed*: convert bytes to elements
                        if (srcSizeBytes.kind == SymValue::CONST && dstElemSize > 0) {
                            int64_t dstCount = srcSizeBytes.constVal / dstElemSize;
                            setBufferProps(dstCount, SymValue::Const(0));
                        }
                    }
                    setFieldTyped(varPath, "_nonnull", getFieldTyped(srcPath, "_nonnull"));
                    return;
                }

                // Source is typed*: use _base_length
                SymValue srcBaseLen = getFieldTyped(srcPath, "_base_length");
                if (srcBaseLen.kind != SymValue::UNKNOWN) {
                    int64_t srcElemSize = 0;
                    if (!srcPointee.isNull()) {
                        auto maybeSrcElemSize = safeGetTypeSizeInChars(Ctx, srcPointee);
                        if (maybeSrcElemSize) srcElemSize = *maybeSrcElemSize;
                    }

                    if (dstIsVoid) {
                        // typed* to void*: convert to bytes
                        if (srcBaseLen.kind == SymValue::CONST && srcElemSize > 0) {
                            SymValue srcOffset = getFieldTyped(srcPath, "_offset");
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
                    setFieldTyped(varPath, "_nonnull", getFieldTyped(srcPath, "_nonnull"));
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
                                setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
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
                            setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
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
                        setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                        return;
                    }
                }
            }
        }
    }

    void trackAllocationSize(const FieldPath& dstPath, QualType ptrType, const CallExpr* CE,
                             const std::string& allocName) {
        if (!CE || allocName.empty() || !ptrType->isPointerType()) return;

        QualType pointeeType = ptrType->getPointeeType();
        bool isVoidPtr = pointeeType->isVoidType();
        auto maybeElemSize = isVoidPtr ? std::optional<int64_t>(1)
                                       : safeGetTypeSizeInChars(Ctx, pointeeType);

        auto setElementCount = [&](const SymValue& countVal) {
            if (isVoidPtr) return;
            setFieldTyped(dstPath, "_size", countVal);
            setFieldTyped(dstPath, "_base_length", countVal);
            setFieldTyped(dstPath, "_offset", SymValue::Const(0));
        };

        if (allocName == "calloc" && CE->getNumArgs() >= 2) {
            // calloc(count, size)
            const Expr *CountArg = CE->getArg(0);
            const Expr *SizeArg = CE->getArg(1);
            auto countVal = buildSymValueFromExpr(CountArg);
            auto sizeVal = buildSymValueFromExpr(SizeArg);
            if (countVal) {
                setElementCount(*countVal);
                if (sizeVal) {
                    SymValue sizeBytes = SymValue::Mul(*countVal, *sizeVal);
                    setFieldTyped(dstPath, "_size_bytes", sizeBytes);
                }
            }
            return;
        }

        if (allocName == "reallocarray" && CE->getNumArgs() >= 3) {
            // reallocarray(ptr, count, size)
            const Expr *CountArg = CE->getArg(1);
            const Expr *SizeArg = CE->getArg(2);
            auto countVal = buildSymValueFromExpr(CountArg);
            auto sizeVal = buildSymValueFromExpr(SizeArg);
            if (countVal) {
                setElementCount(*countVal);
                if (sizeVal) {
                    SymValue sizeBytes = SymValue::Mul(*countVal, *sizeVal);
                    setFieldTyped(dstPath, "_size_bytes", sizeBytes);
                }
            }
            return;
        }

        if ((allocName == "malloc" || allocName == "realloc" || allocName == "aligned_alloc") &&
            CE->getNumArgs() >= 1) {
            // malloc(size), realloc(ptr, size), aligned_alloc(align, size)
            unsigned sizeArgIdx = (allocName == "realloc" || allocName == "aligned_alloc") ? 1 : 0;
            if (CE->getNumArgs() > sizeArgIdx) {
                const Expr *SizeArg = CE->getArg(sizeArgIdx);
                Expr::EvalResult R;
                if (safeEvaluateAsInt(SizeArg, R, Ctx)) {
                    if (auto sizeBytes = safeGetInt64(R.Val.getInt())) {
                        setFieldTyped(dstPath, "_size_bytes", SymValue::Const(*sizeBytes));
                        if (!isVoidPtr && maybeElemSize && *maybeElemSize > 0) {
                            int64_t count = *sizeBytes / *maybeElemSize;
                            setElementCount(SymValue::Const(count));
                        }
                    }
                } else {
                    // Track symbolic size for Z3 proof attempts
                    // Use buildSymValueFromExpr to handle compound expressions like len + 10
                    if (auto sizeVal = buildSymValueFromExpr(SizeArg)) {
                        setFieldTyped(dstPath, "_size_bytes", *sizeVal);
                        if (!isVoidPtr && maybeElemSize && *maybeElemSize > 0) {
                            SymValue countVal = SymValue::Div(*sizeVal, SymValue::Const(*maybeElemSize));
                            setElementCount(countVal);
                        }
                    }
                }
            }
        }
    }

    bool VisitVarDecl(VarDecl *D) {
        std::string var = variableKey(D).str();
        FieldPath varPath = FieldPath::fromDecl(D);
        if (D->hasLocalStorage() && !isa<ParmVarDecl>(D)) forgetVariable(var);
        DeclDepth[varPath.toStringRef()] = CurrentScopeDepth;

        // Extract template parameters from type (std::array, std::span, user templates)
        extractTemplateParams(var, D->getType());

        // Track array sizes for requires annotation checking
        QualType varType = D->getType();
        if (const auto *CAT = Ctx.getAsConstantArrayType(varType)) {
            int64_t count = CAT->getSize().getZExtValue();
            setFieldTyped(varPath, "_size", SymValue::Const(count));
            setFieldTyped(varPath, "_base_length", SymValue::Const(count));
            setFieldTyped(varPath, "_offset", SymValue::Const(0));
            // Also compute bytes for void* compatibility
            QualType elemType = CAT->getElementType();
            auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
            if (maybeElemSize) {
                setFieldTyped(varPath, "_size_bytes", SymValue::Const(count * (*maybeElemSize)));
            }

            // Phase 2A: Track array element values for forall quantifier verification
            if (D->hasInit()) {
                Expr *Init = D->getInit()->IgnoreParenImpCasts();
                if (auto *IL = dyn_cast<InitListExpr>(Init)) {
                    llvm::SmallVector<int64_t, 16> elements;
                    bool allConst = true;
                    for (unsigned i = 0; i < IL->getNumInits(); ++i) {
                        Expr::EvalResult R;
                        if (safeEvaluateAsInt(IL->getInit(i), R, Ctx)) {
                            if (auto val = safeGetInt64(R.Val.getInt())) {
                                elements.push_back(*val);
                                continue;
                            }
                        }
                        allConst = false;
                        break;
                    }
                    if (allConst && !elements.empty()) {
                        State.ArrayElementFacts[var] = std::move(elements);
                    }
                }
            }
        }

        // Track pointer nullability on initialization
        if (varType->isPointerType() && D->hasInit()) {
            Expr *Init = D->getInit()->IgnoreParenCasts();

            // Use helper to infer nullability from expression
            if (auto nonnull = inferNonnullFromExpr(Init)) {
                setFieldTyped(varPath, "_nonnull", SymValue::Const(*nonnull));
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
                    State.PointerLifetimes[var] = pls;
                    // Track allocation size for bounds checking
                    if (NewE->isArray()) {
                        if (auto ArraySizeExpr = NewE->getArraySize()) {
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(*ArraySizeExpr, R, Ctx)) {
                                if (auto count = safeGetInt64(R.Val.getInt())) {
                                    setFieldTyped(varPath, "_size", SymValue::Const(*count));
                                    setFieldTyped(varPath, "_base_length", SymValue::Const(*count));
                                    setFieldTyped(varPath, "_offset", SymValue::Const(0));
                                }
                            }
                        }
                    } else {
                        // Scalar new: size is 1 element
                        setFieldTyped(varPath, "_size", SymValue::Const(1));
                        setFieldTyped(varPath, "_base_length", SymValue::Const(1));
                        setFieldTyped(varPath, "_offset", SymValue::Const(0));
                    }

                    // HEAP OBJECT INIT TRACKING (Tier 5.2)
                    // Track whether the heap object is initialized based on new form:
                    // - new T     -> default-init: UNINITIALIZED for scalars/POD
                    // - new T()   -> value-init: zero-initialized
                    // - new T(v)  -> direct-init: initialized
                    // - new T{v}  -> list-init: initialized
                    ResourceId rid = NewE->getBeginLoc();
                    State.VarToResource[var] = rid;

                    // Determine if the heap object is initialized
                    bool isInitialized = false;
                    if (NewE->hasInitializer()) {
                        // new T(), new T(v), new T{}, new T{v} - all initialized
                        isInitialized = true;
                    } else {
                        // new T - default initialization
                        // For class types with constructors, this runs the default ctor
                        // For scalars and POD, this leaves memory uninitialized
                        QualType allocType = NewE->getAllocatedType();
                        if (auto *RT = allocType->getAs<RecordType>()) {
                            // Class/struct with user-declared constructor is initialized
                            if (auto *RD = RT->getDecl()) {
                                // Check if it has a user-declared default constructor
                                if (auto *CRD = dyn_cast<CXXRecordDecl>(RD)) {
                                    isInitialized = CRD->hasUserDeclaredConstructor() ||
                                                    CRD->hasNonTrivialDefaultConstructor();
                                }
                            }
                        }
                        // Scalars and POD structs remain uninitialized
                    }

                    HeapObjectState hos;
                    hos.initialized = isInitialized;
                    hos.allocLoc = rid;
                    State.HeapObjectStates[rid] = hos;
                }
            } else if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
                // Copying from another pointer - copy its nullability and proofs
                FieldPath srcPath = FieldPath::fromDecl(DRE->getDecl());
                std::string srcPathStr = srcPath.toString();
                // Check if source pointer is uninitialized - using it is UB
                SymValue srcInit = getFieldTyped(srcPath, "_initialized");
                if (srcInit.kind == SymValue::CONST && srcInit.constVal == 0) {
                    Diag.Report(D->getInit()->getBeginLoc(), ErrUninitializedPtr) << srcPathStr;
                }
                setFieldTyped(varPath, "_nonnull", getFieldTyped(srcPath, "_nonnull"));
                // Copy proofs for consistency with assignment path
                if (hasProof(srcPathStr, "_nonnull!=0")) {
                    addProof(var, "_nonnull!=0");
                }
                if (hasProof(srcPathStr, "_nonnull==0")) {
                    addProof(var, "_nonnull==0");
                }
                // Track pointer alias for future proof propagation
                State.PointerAliases[var] = srcPathStr;

                // HEAP OBJECT INIT: Propagate ResourceId when copying pointers
                // This enables aliasing: int* q = p; *q = 42; *p is now initialized
                auto srcResIt = State.VarToResource.find(srcPathStr);
                if (srcResIt != State.VarToResource.end()) {
                    State.VarToResource[var] = srcResIt->second;
                }
                // Copy initialization state (to propagate uninitialized through copies)
                SymValue initState = getFieldTyped(srcPath, "_initialized");
                if (initState.kind != SymValue::UNKNOWN) {
                    setFieldTyped(varPath, "_initialized", initState);
                } else {
                    setFieldTyped(varPath, "_initialized", SymValue::Const(1));
                }
                // Transfer scope-exit constraints for move semantics
                // FILE* g = f; transfers f's obligations to g
                if (hasConstraints(srcPathStr)) {
                    transferConstraints(srcPathStr, var, D->getInit()->getBeginLoc());
                    // Copy ghost state (like @open) from source to destination
                    for (const auto& constraint : State.CurrentScopeExitConstraints) {
                        if (constraint.varPath == var) {
                            SymValue fieldVal = getFieldTyped(srcPath, constraint.field);
                            if (fieldVal.kind != SymValue::UNKNOWN) {
                                setFieldTyped(varPath, constraint.field, fieldVal);
                            }
                        }
                    }
                }
            } else if (auto *CE = dyn_cast<CallExpr>(Init)) {
                // Handle std::move - transfer scope-exit constraints from source
                if (isStdMove(Init)) {
                    std::string srcPath = getMovedFromPath(Init);
                    if (!srcPath.empty() && hasConstraints(srcPath)) {
                        transferConstraints(srcPath, var, D->getInit()->getBeginLoc());
                        // Copy ghost state (like @open) from source to destination
                        FieldPath srcTypedPath = FieldPath::fromSynthetic(srcPath);
                        for (const auto& constraint : State.CurrentScopeExitConstraints) {
                            if (constraint.varPath == var) {
                                SymValue fieldVal = getFieldTyped(srcTypedPath, constraint.field);
                                if (fieldVal.kind != SymValue::UNKNOWN) {
                                    setFieldTyped(varPath, constraint.field, fieldVal);
                                }
                            }
                        }
                        // Copy nullability from moved-from pointer
                        setFieldTyped(varPath, "_nonnull", getFieldTyped(srcTypedPath, "_nonnull"));
                    }
                }
                // Check if callee has @returns_nonnull from function ghost state (Pass 1)
                bool gotNonnull = false;
                if (auto *callee = CE->getDirectCallee()) {
                    std::string calleeKey = getFunctionKey(callee);
                    auto funcIt = FunctionAnalysis.find(calleeKey);
                    if (funcIt != FunctionAnalysis.end() &&
                        funcIt->second.returnsNonnull.value_or(false)) {
                        // All return paths return non-null - set _nonnull = 1
                        setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                        gotNonnull = true;
                    }

                    // Handle std::get_if<T>(&v) - returns nullptr if type doesn't match
                    if (callee->getIdentifier() && callee->getName() == "get_if" &&
                        isInStdNamespace(callee->getDeclContext()) &&
                        CE->getNumArgs() == 1) {
                        // Get variant from argument (which is &v)
                        Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
                        if (auto *UO = dyn_cast<UnaryOperator>(Arg)) {
                            if (UO->getOpcode() == UO_AddrOf) {
                                Expr *VariantExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                                FieldPath variantPath = getTypedPath(VariantExpr);
                                QualType variantType = VariantExpr->getType().getNonReferenceType();

                                if (variantPath.isValid()) {
                                    if (auto *TST = variantType->getAs<TemplateSpecializationType>()) {
                                        // Get the template argument using helper
                                        std::optional<int64_t> requestedIndex;
                                        if (auto tplArg = getFirstTemplateArg(CE)) {
                                            if (tplArg->index) {
                                                // std::get_if<I>(&v)
                                                requestedIndex = *tplArg->index;
                                            } else if (tplArg->type) {
                                                // std::get_if<T>(&v)
                                                if (auto idx = getVariantAlternativeIndex(TST, *tplArg->type)) {
                                                    requestedIndex = static_cast<int64_t>(*idx);
                                                }
                                            }
                                        }

                                        if (requestedIndex.has_value()) {
                                            SymValue variantIndex = getFieldTyped(variantPath, "_variant_index");
                                            auto variantIndexEval = variantIndex.eval();
                                            if (variantIndexEval) {
                                                // Variant index known - set _nonnull based on match
                                                if (*variantIndexEval == *requestedIndex) {
                                                    setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                                                } else {
                                                    setFieldTyped(varPath, "_nonnull", SymValue::Const(0));
                                                }
                                                gotNonnull = true;
                                            }
                                            // If unknown, leave _nonnull unknown (handled below)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (!gotNonnull) {
                    setFieldTyped(varPath, "_nonnull", SymValue::Unknown());

                    // Track fopen mode for read/write capability
                    if (auto *callee = CE->getDirectCallee()) {
                        if (auto *II = callee->getIdentifier()) {
                            llvm::StringRef fnName = II->getName();
                            if ((fnName == "fopen" || fnName == "freopen") && CE->getNumArgs() >= 2) {
                                // Get mode string from second argument
                                Expr *ModeArg = CE->getArg(1)->IgnoreParenImpCasts();
                                if (auto *SL = dyn_cast<StringLiteral>(ModeArg)) {
                                    llvm::StringRef mode = SL->getString();
                                    // Parse mode: r=read, w/a=write, +=both, b=binary (ignored)
                                    bool readable = mode.contains('r') || mode.contains('+');
                                    bool writable = mode.contains('w') || mode.contains('a') || mode.contains('+');
                                    setFieldTyped(varPath, "_readable", SymValue::Const(readable ? 1 : 0));
                                    setFieldTyped(varPath, "_writable", SymValue::Const(writable ? 1 : 0));
                                }
                            }
                        }
                    }

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
                        State.PointerLifetimes[var] = pls;
                        // Track allocation size for bounds checking
                        trackAllocationSize(varPath, varType, CE, allocName);
                    }
                }
            } else {
                // Unknown source - explicitly mark as unknown
                // This ensures state is well-defined and any stale values are cleared
                setFieldTyped(varPath, "_nonnull", SymValue::Unknown());
            }
            // Pointer has initializer - mark as initialized
            setFieldTyped(varPath, "_initialized", SymValue::Const(1));

            // Ternary buffer tracking: int* p = cond ? arr1 : arr2
            // Track the minimum buffer size from both branches.
            if (auto *CO = dyn_cast<ConditionalOperator>(
                    Init->IgnoreImpCasts())) {
                auto getArraySizeFromExpr = [&](Expr *E) -> std::optional<int64_t> {
                    E = E->IgnoreParenImpCasts();
                    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
                        QualType qt = DRE->getDecl()->getType();
                        if (auto *CAT = Ctx.getAsConstantArrayType(qt))
                            return CAT->getSize().getZExtValue();
                        // Pointer with tracked size
                        FieldPath srcPath = FieldPath::fromDecl(DRE->getDecl());
                        SymValue sz = getFieldTyped(srcPath, "_size");
                        if (sz.kind == SymValue::CONST)
                            return sz.constVal;
                    }
                    return std::nullopt;
                };
                auto trueSize = getArraySizeFromExpr(CO->getTrueExpr());
                auto falseSize = getArraySizeFromExpr(CO->getFalseExpr());
                if (trueSize && falseSize) {
                    int64_t minSize = std::min(*trueSize, *falseSize);
                    setFieldTyped(varPath, "_size", SymValue::Const(minSize));
                    setFieldTyped(varPath, "_base_length", SymValue::Const(minSize));
                    setFieldTyped(varPath, "_offset", SymValue::Const(0));
                }
            }
        } else if (varType->isPointerType() && !D->hasInit()) {
            // Uninitialized local pointer - using it in any way is UB
            // Only track for local variables, not parameters or extern
            // Function parameters (ParmVarDecl) are initialized by the caller
            if (D->hasLocalStorage() && !D->isExceptionVariable() && !isa<ParmVarDecl>(D)) {
                setFieldTyped(varPath, "_initialized", SymValue::Const(0));
                setFieldTyped(varPath, "_nonnull", SymValue::Unknown());
            }
        }

        // Tier 1: Track initialization state for scalar types (int, double, bool, char, etc.)
        // Reading an uninitialized scalar is undefined behavior (CWE-457)
        if ((varType->isArithmeticType() || varType->isBooleanType()) &&
            !varType->isPointerType()) {
            if (D->hasInit()) {
                // Scalar has initializer - mark as initialized
                setFieldTyped(varPath, "_initialized", SymValue::Const(1));
            } else if (D->hasLocalStorage() && !D->isExceptionVariable() &&
                       !isa<ParmVarDecl>(D) && !D->isStaticLocal()) {
                // Uninitialized local scalar - reading it is UB
                // Exclude: parameters (caller initializes), static locals (zero-init),
                //          exception variables, globals (zero-init)
                setFieldTyped(varPath, "_initialized", SymValue::Const(0));
            }
        }

        // Track uninitialized struct members (POD structs without initializer)
        // Reading any uninitialized member is UB (CWE-457)
        if (auto *RD = varType->getAsRecordDecl()) {
            if (!RD->isUnion() && D->hasLocalStorage() &&
                !D->isExceptionVariable() && !isa<ParmVarDecl>(D)) {
                // Check if it's a POD/trivial type (no user constructors)
                if (auto *CRD = dyn_cast<CXXRecordDecl>(RD)) {
                    // For C++ structs, check if trivially default constructible
                    // hasInit() returns true even for implicit default ctor, so check POD
                    if (CRD->hasTrivialDefaultConstructor() && CRD->isPOD()) {
                        // POD type: "Point p;" leaves members uninitialized
                        // Check if there's a real initializer (brace-init, etc.)
                        // hasInit() returns true even for implicit default ctor
                        bool hasRealInit = false;
                        if (const Expr *Init = D->getInit()) {
                            const Expr *Inner = Init->IgnoreParenImpCasts();
                            // CXXConstructExpr with no args = implicit default ctor (no real init)
                            if (auto *CE = dyn_cast<CXXConstructExpr>(Inner)) {
                                hasRealInit = (CE->getNumArgs() > 0) || CE->isListInitialization();
                            } else {
                                hasRealInit = true;  // InitListExpr, etc.
                            }
                        }
                        if (!hasRealInit) {
                            setStructMembersInitState(varPath, RD, false);
                        }
                    }
                } else {
                    // C-style struct (RecordDecl but not CXXRecordDecl) - check hasInit
                    if (!D->hasInit()) {
                        setStructMembersInitState(varPath, RD, false);
                    }
                }
            }
        }

        // TIER 2.3: Reference binding tracking
        // When int& r = x, track that r refers to x so that r = 42 marks x as initialized
        // References are like non-null, non-resettable pointers
        if (varType->isLValueReferenceType() && D->hasInit()) {
            Expr *Init = D->getInit()->IgnoreParenImpCasts();
            if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
                if (auto *TargetVD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    // Check if target is itself a reference (reference chain)
                    // int& r2 = r1; where r1 refers to x - propagate r1's ResourceId
                    if (TargetVD->getType()->isLValueReferenceType() && TargetVD->getIdentifier()) {
                        // StringMap accepts StringRef for lookup
                        auto it = State.VarToResource.find(variableKey(TargetVD));
                        if (it != State.VarToResource.end()) {
                            State.VarToResource[var] = it->second;
                            // Don't update StackVarPaths - it already has the original variable
                        }
                    } else if (TargetVD->getIdentifier()) {
                        // Direct binding: int& r = x;
                        ResourceId rid = TargetVD->getLocation();
                        State.VarToResource[var] = rid;
                        State.StackVarPaths[rid] = variableKey(TargetVD);
                    }
                    // Struct reference binding: Point& r = p
                    // Members may be written through the reference, so mark initialized
                    QualType targetType = TargetVD->getType().getNonReferenceType();
                    if (auto *RD = targetType->getAsRecordDecl()) {
                        if (!RD->isUnion()) {
                            FieldPath targetPath = FieldPath::fromDecl(TargetVD);
                            setStructMembersInitState(targetPath, RD, true);
                        }
                    }
                }
            }
            // Handle reference to dereference: int& r = *p;
            else if (auto *UO = dyn_cast<UnaryOperator>(Init)) {
                if (UO->getOpcode() == UO_Deref) {
                    Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                    FieldPath ptrPath = getTypedPath(SubExpr);
                    if (ptrPath.isValid()) {
                        // Propagate ResourceId from pointer to reference
                        auto it = State.VarToResource.find(ptrPath.toStringRef());
                        if (it != State.VarToResource.end()) {
                            State.VarToResource[var] = it->second;
                        }
                    }
                }
            }
        }

        // NOTE: bare dtt:field=value on variables was removed (deprecated).
        // Use real C++ assignments instead: v.len = 3;
        // Union initialization tracking
        // Track which member is active after initialization
        if (auto *RD = varType->getAsRecordDecl()) {
            if (RD->isUnion() && D->hasInit()) {
                auto *Init = D->getInit()->IgnoreParenImpCasts();
                StringRef activeMember;

                // Use InitListExpr's getInitializedFieldInUnion() for designated inits
                if (auto *ILE = dyn_cast<InitListExpr>(Init)) {
                    // Get the semantic form if available (has field info)
                    const InitListExpr *SemanticILE = ILE->isSemanticForm()
                        ? ILE : ILE->getSemanticForm();
                    if (SemanticILE) {
                        if (auto *FD = SemanticILE->getInitializedFieldInUnion()) {
                            if (FD->getIdentifier())
                                activeMember = FD->getName();
                        }
                    }
                    // Fallback to first member if no field info
                    if (activeMember.empty() && ILE->getNumInits() >= 1) {
                        for (auto *Field : RD->fields()) {
                            if (Field->getIdentifier()) {
                                activeMember = Field->getName();
                                break;
                            }
                        }
                    }
                }
                // Brace init without InitListExpr - first member is active
                else {
                    for (auto *Field : RD->fields()) {
                        if (Field->getIdentifier()) {
                            activeMember = Field->getName();
                            break;
                        }
                    }
                }

                if (!activeMember.empty()) {
                    setFieldTyped(varPath, "_union_active", SymValue::String(activeMember));
                }
            }
        }

        if (D->hasInit()) {
            auto *Init = D->getInit()->IgnoreParenImpCasts();

            // TIER 5.2: Check for uninitialized heap read in variable initialization
            // Pattern: int x = *p; where p points to uninitialized heap memory
            if (auto *UO = dyn_cast<UnaryOperator>(Init)) {
                if (UO->getOpcode() == UO_Deref) {
                    Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                    FieldPath ptrPath = getTypedPath(SubExpr);
                    if (ptrPath.isValid()) {
                        auto it = State.VarToResource.find(ptrPath.toStringRef());
                        if (it != State.VarToResource.end()) {
                            ResourceId rid = it->second;
                            auto hosIt = State.HeapObjectStates.find(rid);
                            if (hosIt != State.HeapObjectStates.end() && !hosIt->second.initialized) {
                                Diag.Report(Init->getBeginLoc(), ErrUninitializedHeap) << ptrPath.toStringRef();
                                Diag.Report(hosIt->second.allocLoc, NoteHeapAllocatedHere);
                            }
                        }
                    }
                }
            }

            // Unwrap ExprWithCleanups and CXXBindTemporaryExpr to find CallExpr
            Expr *Unwrapped = Init;
            if (auto *EWC = dyn_cast<ExprWithCleanups>(Unwrapped))
                Unwrapped = EWC->getSubExpr()->IgnoreParenImpCasts();
            if (auto *BTE = dyn_cast<CXXBindTemporaryExpr>(Unwrapped))
                Unwrapped = BTE->getSubExpr()->IgnoreParenImpCasts();
            
            if (auto *Call = dyn_cast<CallExpr>(Unwrapped)) {
                auto oldVals = captureOldValues(Call);
                applyPostconditions(var, Call, oldVals);

                // Handle std::make_any<T>(...) - track the contained type
                if (auto *callee = Call->getDirectCallee()) {
                    if (callee->getIdentifier() && callee->getName() == "make_any" &&
                        isInStdNamespace(callee->getDeclContext())) {
                        if (auto tplArg = getFirstTemplateArg(Call)) {
                            if (tplArg->type) {
                                // Store canonical type for proper comparison
                                State.AnyContainedTypes[var] = tplArg->type->getCanonicalType();
                                // Also set _has_value since make_any always creates valid any
                                setFieldTyped(varPath, "_has_value", SymValue::Const(1));
                            }
                        }
                    }
                }

                // Handle std::expected monadic operations:
                // auto result = e.and_then(f) / e.transform(f) / e.or_else(f) / e.transform_error(f)
                if (auto *MCE = dyn_cast<CXXMemberCallExpr>(Call)) {
                    if (auto *Method = MCE->getMethodDecl()) {
                        if (Method->getParent() && Method->getParent()->getIdentifier() &&
                            Method->getParent()->getName() == "expected" &&
                            Method->getIdentifier()) {
                            StringRef methodName = Method->getName();
                            FieldPath srcPath = getTypedPath(MCE->getImplicitObjectArgument());
                            if (srcPath.isValid()) {
                                SymValue srcHasValue = getField(srcPath.toString(), "_has_value");
                                // transform/transform_error: output._has_value = input._has_value
                                if (methodName == "transform" || methodName == "transform_error") {
                                    if (srcHasValue.kind != SymValue::UNKNOWN) {
                                        setFieldTyped(varPath, "_has_value", srcHasValue);
                                    }
                                }
                                // and_then: if input has error, output has error
                                // If input has value, output depends on F (conservative: propagate known error)
                                else if (methodName == "and_then") {
                                    if (srcHasValue.kind == SymValue::CONST && srcHasValue.constVal == 0) {
                                        setFieldTyped(varPath, "_has_value", SymValue::Const(0));
                                    } else {
                                        // Invoking F does not prove that F returns a value.
                                        setFieldTyped(varPath, "_has_value", SymValue::Unknown());
                                    }
                                }
                                // or_else: if input has value, output has value
                                // If input has error, output depends on F (conservative: propagate known value)
                                else if (methodName == "or_else") {
                                    if (srcHasValue.kind == SymValue::CONST && srcHasValue.constVal == 1) {
                                        setFieldTyped(varPath, "_has_value", SymValue::Const(1));
                                    } else if (srcHasValue.kind == SymValue::CONST && srcHasValue.constVal == 0) {
                                        // Input has error - F is called, result depends on F
                                        // Conservative: leave unknown, user should guard
                                    }
                                }
                            }
                        }
                    }

                    // ============================================================
                    // RETURN VALUE POSTCONDITION: Apply _result to receiving variable
                    // ============================================================
                    // If the method has an inferred _result postcondition, apply it
                    // to the variable being initialized: int x = v.method();
                    if (auto *MethodDecl = MCE->getMethodDecl()) {
                        auto inferredIt = InferredFnPostconditions.find(MethodDecl->getCanonicalDecl());
                        if (inferredIt != InferredFnPostconditions.end() && callRequirementsProven(MCE, Ctx)) {
                            // Build argument substitution map
                            llvm::StringMap<SymValue> argSubst;
                            for (unsigned i = 0; i < MCE->getNumArgs(); i++) {
                                llvm::SmallString<8> argKey;
                                llvm::raw_svector_ostream(argKey) << "arg" << i;
                                Expr *ArgExpr = MCE->getArg(i)->IgnoreParenImpCasts();
                                Expr::EvalResult R;
                                if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                                    auto val = safeGetInt64(R.Val.getInt());
                                    if (val) argSubst[argKey] = SymValue::Const(*val);
                                } else {
                                    FieldPath argTypedPath = getTypedPath(ArgExpr);
                                    if (argTypedPath.isValid()) {
                                        argSubst[argKey] = exprToSymValueImpl(ArgExpr, Ctx);
                                    }
                                }
                            }

                            // Get object path for field substitution
                            FieldPath objTypedPath = getTypedPath(MCE->getImplicitObjectArgument());
                            const std::string objPath = objTypedPath.toString();

                            // Helper to check if varRef is old(fieldName) pattern
                            auto isOldRef = [](llvm::StringRef ref) -> bool {
                                return ref.size() > 5 && ref.starts_with("old(") && ref.back() == ')';
                            };
                            // Helper to extract fieldName from old(fieldName)
                            auto extractOldField = [](llvm::StringRef ref) -> llvm::StringRef {
                                return ref.drop_front(4).drop_back(1);
                            };

                            // Reusable struct to capture old() references from SymValue trees
                            struct OldRefCapture {
                                llvm::StringMap<SymValue>& subst;
                                const std::string& objPath;
                                decltype(isOldRef)& isOld;
                                decltype(extractOldField)& extractField;
                                void operator()(const SymValue& sv) {
                                    if (sv.kind == SymValue::VAR && isOld(sv.varRef)) {
                                        if (subst.find(sv.varRef) == subst.end()) {
                                            subst[sv.varRef] = getField(objPath, extractField(sv.varRef).str());
                                        }
                                    }
                                    if (sv.left) (*this)(*sv.left);
                                    if (sv.right) (*this)(*sv.right);
                                    if (sv.cond) (*this)(*sv.cond);
                                }
                            };

                            // FIRST: Apply object's field postconditions (non-_result)
                            // This ensures _result can reference the post-call field values
                            static constexpr llvm::StringLiteral kResultField("_result");
                            for (const auto& post : inferredIt->second) {
                                if (post.field != kResultField && !post.targetArgIndex) {
                                    // This is a field postcondition like _size = old(_size) - 1
                                    // Capture old values and apply
                                    llvm::StringMap<SymValue> fieldArgSubst;
                                    for (const auto& kv : argSubst) fieldArgSubst[kv.first()] = kv.second;

                                    if (post.usesOld) {
                                        OldRefCapture{fieldArgSubst, objPath, isOldRef, extractOldField}(post.expr);
                                    }
                                    SymValue newVal = post.expr.substitute(fieldArgSubst);
                                    setFieldTyped(objTypedPath, post.field, newVal);
                                }
                            }

                            // SECOND: Look for _result postcondition and substitute object fields
                            // Now field values reflect post-call state
                            for (const auto& post : inferredIt->second) {
                                if (post.field == kResultField && !post.targetArgIndex) {
                                    if (post.usesOld) {
                                        OldRefCapture{argSubst, objPath, isOldRef, extractOldField}(post.expr);
                                    }

                                    // Substitute object._field references with actual values
                                    // Convert VAR references like "_size" to their current values
                                    struct FieldResolver {
                                        llvm::StringMap<SymValue>& subst;
                                        const std::string& objPath;
                                        SymValue operator()(const SymValue& sv) {
                                            if (sv.kind == SymValue::VAR) {
                                                auto it = subst.find(sv.varRef);
                                                if (it != subst.end()) return it->second;
                                                if (!sv.varRef.empty() && sv.varRef[0] == '_') {
                                                    SymValue fieldVal = getField(objPath, sv.varRef);
                                                    if (fieldVal.kind != SymValue::UNKNOWN) return fieldVal;
                                                }
                                                return sv;
                                            }
                                            SymValue result = sv;
                                            if (sv.left) result.left = std::make_shared<SymValue>((*this)(*sv.left));
                                            if (sv.right) result.right = std::make_shared<SymValue>((*this)(*sv.right));
                                            if (sv.cond) result.cond = std::make_shared<SymValue>((*this)(*sv.cond));
                                            return result;
                                        }
                                    };

                                    SymValue resultVal = FieldResolver{argSubst, objPath}(post.expr);
                                    resultVal = resultVal.substitute(argSubst);

                                    // The variable holds the result
                                    if (D->getType()->isIntegralOrEnumerationType())
                                        setEnvValueTyped(varPath, resultVal);
                                }
                            }
                        }
                    }
                }
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
                clearProperties(var);
                std::string srcVar = variableKey(DRE->getDecl()).str();
                copyProperties(var, srcVar);
                // Copy VarBoundProofs from source variable to this new variable
                // e.g., if j = i and i has proof "i < v._size", add "j < v._size"
                llvm::SmallVector<std::pair<VarBoundKey, ProofOp>, 4> newProofs;
                for (const auto& [key, op] : State.VarBoundProofs) {
                    if (key.varName == srcVar) {
                        newProofs.push_back({{var, key.objPath, key.field}, op});
                    }
                }
                for (const auto& [key, op] : newProofs) {
                    State.VarBoundProofs[key] = op;
                }
            }
            // Also unwrap for CXXConstructExpr (reuse Unwrapped from above)
            if (auto *CE = dyn_cast<CXXConstructExpr>(Unwrapped)) {
                // Apply constructor postconditions
                auto *Ctor = CE->getConstructor();
                StringRef classNameRef = safeGetName(Ctor ? Ctor->getParent() : nullptr);
                if (Ctor && !classNameRef.empty()) {
                    std::string className = classNameRef.str();

                    // Special case: string/string_view from string literal
                    // std::string_view sv = "hello" -> _size = literal length
                    // std::string s = "hello" -> _size = literal length (2 args: ptr, allocator)
                    // Note: Don't set _size=0 for empty literals - leave unknown so guards work
                    if (className == "basic_string_view" || className == "basic_string") {
                        // string_view: 1 arg (const char*)
                        // string: 2 args (const char*, allocator - default)
                        if (CE->getNumArgs() >= 1) {
                            Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
                            if (auto *SL = dyn_cast<StringLiteral>(Arg)) {
                                if (SL->getLength() > 0) {
                                    setFieldTyped(varPath, "_size", SymValue::Const(SL->getLength()));
                                }
                                // Empty literal: don't set _size, let guards establish proofs
                            }
                        }
                    }

                    // Special case: std::variant construction with value
                    // std::variant<int, string> v = 42 -> _variant_index = 0
                    // std::variant<int, string> v = "hi" -> _variant_index = 1
                    // std::variant<int, string> v(in_place_index<1>, "hi") -> _variant_index = 1
                    // std::variant<int, string> v(in_place_type<string>, "hi") -> _variant_index = 1
                    if (className == "variant" && CE->getNumArgs() >= 1) {
                        QualType variantType = D->getType().getNonReferenceType();
                        if (auto *TST = variantType->getAs<TemplateSpecializationType>()) {
                            Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
                            QualType argType = Arg->getType();
                            bool handled = false;

                            // Check for std::in_place_index_t<I> or std::in_place_type_t<T>
                            if (auto tag = getInPlaceTagInfo(argType)) {
                                if (tag->index) {
                                    // in_place_index<I> - use index directly
                                    setFieldTyped(varPath, "_variant_index",
                                                 SymValue::Const(*tag->index));
                                    handled = true;
                                } else if (tag->type) {
                                    // in_place_type<T> - map T to index
                                    if (auto idx = getVariantAlternativeIndex(TST, *tag->type)) {
                                        setFieldTyped(varPath, "_variant_index",
                                                     SymValue::Const(static_cast<int64_t>(*idx)));
                                        handled = true;
                                    }
                                }
                            }

                            // Fall back to type-based inference
                            if (!handled) {
                                if (auto idx = getVariantAlternativeIndex(TST, argType)) {
                                    setFieldTyped(varPath, "_variant_index", SymValue::Const(*idx));
                                }
                            }
                        }
                    }

                    // Special case: std::any construction with value
                    // std::any a = 42 -> track contained type as int
                    // std::any b = a  -> copy contained type from a (not "std::any")
                    // std::any a(in_place_type<int>, 42) -> track int
                    if (className == "any" && CE->getNumArgs() >= 1) {
                        Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
                        QualType argType = Arg->getType().getCanonicalType();
                        QualType anyType = D->getType().getNonReferenceType().getCanonicalType();
                        bool handled = false;

                        // Check for std::in_place_type_t<T>
                        if (auto tag = getInPlaceTagInfo(argType)) {
                            if (tag->type) {
                                State.AnyContainedTypes[var] = tag->type->getCanonicalType();
                                handled = true;
                            }
                        }

                        if (!handled) {
                            // Check if constructing from another std::any (copy/move)
                            if (Ctx.hasSameUnqualifiedType(argType.getNonReferenceType(), anyType)) {
                                // Copy construction from another std::any
                                FieldPath srcPath = getTypedPath(Arg);
                                if (srcPath.isValid()) {
                                    auto srcIt = State.AnyContainedTypes.find(srcPath.toString());
                                    if (srcIt != State.AnyContainedTypes.end()) {
                                        State.AnyContainedTypes[var] = srcIt->second;
                                    }
                                    // If source unknown, don't set - leave untracked
                                }
                            } else {
                                // Constructing with a value - track the value's type
                                State.AnyContainedTypes[var] = argType;
                            }
                        }
                    }

                    MethodKey key{className, className, CE->getNumArgs()};
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
                                // Phase 1 Migration: Use typed path
                                FieldPath argTypedPath = getTypedPath(ArgExpr);
                                std::string argPath = argTypedPath.toString();
                                if (argTypedPath.isValid() && !hasRuntimeIndex(argPath)) {
                                    argVals[argKey] = SymValue::Var(argPath);
                                } else {
                                    argVals[argKey] = SymValue::Unknown();
                                }
                            }
                        }

                        llvm::SmallString<64> ctorDesc;
                        ctorDesc += className;
                        ctorDesc += " constructor";
                        for (const auto& post : it->second.ensures) {
                            SymValue result = post.expr.substitute(argVals);
                            setFieldTyped(varPath, post.field, result,
                                         CE->getBeginLoc(), ctorDesc);
                        }
                    }

                    // The initializer-list overload counts elements; its first
                    // argument is not the numeric count used by the arity-only
                    // vector constructor contract. Allocator arguments may be defaulted.
                    if (classNameRef == "vector" &&
                        isInStdNamespace(Ctor->getParent()->getDeclContext()) &&
                        CE->getNumArgs() >= 1) {
                        const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
                        if (const auto *List = dyn_cast<CXXStdInitializerListExpr>(Arg)) {
                            const Expr *Backing = List->getSubExpr()->IgnoreParenImpCasts();
                            if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Backing))
                                Backing = Temporary->getSubExpr()->IgnoreParenImpCasts();
                            if (const auto *Elements = dyn_cast<InitListExpr>(Backing))
                                setFieldTyped(varPath, "_size",
                                              SymValue::Const(Elements->getNumInits()),
                                              CE->getBeginLoc(), "vector initializer-list constructor");
                        } else if (const auto *EmptyList = dyn_cast<CXXConstructExpr>(Arg)) {
                            const auto *ListCtor = EmptyList->getConstructor();
                            if (ListCtor->isDefaultConstructor() &&
                                safeGetName(ListCtor->getParent()) == "initializer_list" &&
                                isInStdNamespace(ListCtor->getParent()->getDeclContext()))
                                setFieldTyped(varPath, "_size", SymValue::Const(0),
                                              CE->getBeginLoc(), "vector empty initializer-list constructor");
                        }
                    }

                    // Special case: std::expected construction from std::unexpected
                    // Must happen AFTER annotation to override _has_value = 1
                    // std::expected<T,E> e{std::unexpected{err}} -> _has_value = 0
                    if (className == "expected" && CE->getNumArgs() >= 1) {
                        Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
                        QualType argType = Arg->getType().getCanonicalType();
                        // Check if argument is std::unexpected<E>
                        if (auto *RD = argType->getAsCXXRecordDecl()) {
                            if (RD->getIdentifier() &&
                                RD->getName() == "unexpected" &&
                                isInStdNamespace(RD->getDeclContext())) {
                                // Constructed from unexpected - no value
                                setFieldTyped(varPath, "_has_value", SymValue::Const(0));
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
                    if (auto *DRE = dyn_cast<DeclRefExpr>(Arg)) {
                        std::string srcPath = variableKey(DRE->getDecl()).str();
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
                            setFieldTyped(varPath, "len", SymValue::Const(IL->getNumInits()));
                            setFieldTyped(varPath, "size", SymValue::Const(IL->getNumInits()));
                        }
                    }
                }
            }
            if (auto *IL = dyn_cast<InitListExpr>(Init)) {
                setFieldTyped(varPath, "len", SymValue::Const(IL->getNumInits()));
                setFieldTyped(varPath, "size", SymValue::Const(IL->getNumInits()));
            }
            
            // Import actual initialized member values, not declaration defaults:
            // aggregate arguments and constructor bodies can override those defaults.
            // Let Clang evaluate the complete initialization and leave runtime values
            // alone. Run after synthetic list properties so they cannot overwrite real fields.
            if (const auto *RD = D->getType()->getAsCXXRecordDecl();
                RD && RD->isCompleteDefinition() && !RD->isUnion() &&
                !Ctx.getSourceManager().isInSystemHeader(RD->getLocation()) &&
                !Init->isInstantiationDependent() && !Init->isValueDependent()) {
                Expr::EvalResult Result;
                if (Init->EvaluateAsRValue(Result, Ctx) &&
                    !Result.HasSideEffects && !Result.HasUndefinedBehavior &&
                    Result.Val.isStruct()) {
                    for (const FieldDecl *Field : RD->fields()) {
                        if (!Field->getIdentifier() || !Field->getType()->isIntegerType())
                            continue;
                        const auto &Value = Result.Val.getStructField(Field->getFieldIndex());
                        if (Value.isInt()) {
                            if (auto Integer = safeGetInt64(Value.getInt()))
                                setFieldTyped(varPath, Field->getName(), SymValue::Const(*Integer),
                                              Init->getBeginLoc(), "member initialization");
                        }
                    }
                }
            }

            // Track integer variable assignments for index arithmetic
            // e.g., int mid = v.size() / 2;
            QualType type = D->getType().getCanonicalType();
            if (type->isIntegerType()) {
                SymValue val = exprToSymValueImpl(Init, Ctx);
                if (val.kind != SymValue::UNKNOWN) {
                    setEnvValue(var, val);
                }
            }
        }

        // Buffer pointer tracking: track bounds for pointers derived from arrays
        // This must run AFTER the hasInit block which may call clearProperties
        if (D->getType()->isPointerType() && D->hasInit()) {
            trackBufferPointerInit(D, D->getType(), D->getInit()->IgnoreParenImpCasts());
        }

        return true;
    }

    bool VisitBinaryOperator(BinaryOperator *B) {
        auto *LHS = B->getLHS()->IgnoreParenImpCasts();
        auto *RHS = B->getRHS()->IgnoreParenImpCasts();
        // Phase 1 Migration: Use typed path
        FieldPath lhsTypedPath = getTypedPath(LHS);
        std::string lhsPath = lhsTypedPath.toString();  // For backward compat during migration

        // TIER 1: Check for uninitialized scalar operands in expressions
        // Reading an uninitialized variable in an expression is UB
        auto checkOperand = [this](const Expr *E, SourceLocation Loc) {
            if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
                if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    QualType varType = VD->getType();
                    if ((varType->isArithmeticType() || varType->isBooleanType()) &&
                        !varType->isPointerType()) {
                        FieldPath varPath = FieldPath::fromDecl(VD);
                        checkUninitializedVar(varPath.toStringRef(), Loc);
                    }
                }
            }
        };

        // For assignment, only check RHS (LHS is being written)
        // For compound assignment (+=, -=, etc.), LHS is read then written
        if (B->isAssignmentOp()) {
            checkOperand(RHS, RHS->getBeginLoc());
            if (B->isCompoundAssignmentOp()) {
                // Compound assignment reads LHS first
                checkOperand(LHS, LHS->getBeginLoc());
            }
        } else {
            // Non-assignment: check both operands
            checkOperand(LHS, LHS->getBeginLoc());
            checkOperand(RHS, RHS->getBeginLoc());
        }

        // Overwriting a handle that holds a resource (f = nullptr, fd = -1,
        // fd = other()) leaks it unless another handle keeps it; checked
        // before anything forgets what the old value said about its
        // acquisition. A call that is given the handle may release it
        // (freopen), and a call that acquires checks where it registers.
        if (B->getOpcode() == BO_Assign && isa<DeclRefExpr>(LHS) && hasConstraints(lhsPath)) {
            llvm::erase_if(State.CurrentScopeExitConstraints, [&](const ScopeExitConstraint &C) {
                return C.varPath == lhsPath && guardFails(C);
            });
            bool CallDecides = false;
            if (auto *Call = dyn_cast<CallExpr>(RHS)) {
                auto *Callee = Call->getDirectCallee();
                CallDecides = (Callee && Callee->getIdentifier() && FnScopeExitRequires.count(Callee->getName())) ||
                              llvm::any_of(Call->arguments(), [&](const Expr *Arg) {
                                  return refersToVariable(Arg, lhsPath);
                              });
            }
            if (!CallDecides) checkConstraintsBeforeOverwrite(lhsPath, B->getBeginLoc());
        }

        // TIER 5.2: Heap object initialization tracking
        // Helper to check if reading uninitialized heap memory through dereference
        auto checkHeapDeref = [this](const Expr *E, SourceLocation Loc) {
            if (auto *UO = dyn_cast<UnaryOperator>(E)) {
                if (UO->getOpcode() == UO_Deref) {
                    Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                    FieldPath ptrPath = getTypedPath(SubExpr);
                    if (ptrPath.isValid()) {
                        auto it = State.VarToResource.find(ptrPath.toStringRef());
                        if (it != State.VarToResource.end()) {
                            ResourceId rid = it->second;
                            auto hosIt = State.HeapObjectStates.find(rid);
                            if (hosIt != State.HeapObjectStates.end() && !hosIt->second.initialized) {
                                Diag.Report(Loc, ErrUninitializedHeap) << ptrPath.toStringRef();
                                Diag.Report(hosIt->second.allocLoc, NoteHeapAllocatedHere);
                            }
                        }
                    }
                }
            }
        };

        // Helper to mark heap/stack object as initialized when written through dereference or reference
        auto markHeapInitialized = [this](const Expr *E) {
            ResourceId rid;
            bool found = false;
            llvm::SmallString<64> memberSuffix;  // For struct member access: ".x", ".y", etc.

            // Case 1: Dereference (*p = value)
            if (auto *UO = dyn_cast<UnaryOperator>(E)) {
                if (UO->getOpcode() == UO_Deref) {
                    Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                    FieldPath ptrPath = getTypedPath(SubExpr);
                    if (ptrPath.isValid()) {
                        auto it = State.VarToResource.find(ptrPath.toStringRef());
                        if (it != State.VarToResource.end()) {
                            rid = it->second;
                            found = true;
                        }
                    }
                }
            }
            // Case 2: Reference assignment (r = value where r is a reference)
            else if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
                // Note: DRE->getType() gives the referred-to type (e.g., int), not the reference type
                // We need to check the declaration's type to see if it was declared as a reference
                if (DRE->getDecl()->getType()->isLValueReferenceType()) {
                    // StringMap accepts StringRef - no .str() allocation needed
                    auto it = State.VarToResource.find(variableKey(DRE->getDecl()));
                    if (it != State.VarToResource.end()) {
                        rid = it->second;
                        found = true;
                    }
                }
            }
            // Case 3: Member access via pointer (p->x, p->p2.x, etc.) - TIER 2.1 & 2.4
            else if (auto *ME = dyn_cast<MemberExpr>(E)) {
                // Walk through nested member expressions to find the pointer base
                // and collect member suffixes along the way
                // For ptr->p.x: ME represents .x, we need to collect [x, p] and reverse to ".p.x"
                llvm::SmallVector<const FieldDecl*, 4> fields;
                const MemberExpr *currentME = ME;
                Expr *ultimateBase = nullptr;
                bool foundArrow = false;
                const MemberExpr *arrowME = nullptr;  // Track which ME has the arrow for union check

                while (currentME) {
                    if (auto *FD = dyn_cast<FieldDecl>(currentME->getMemberDecl())) {
                        fields.push_back(FD);
                    }
                    Expr *Base = currentME->getBase()->IgnoreParenImpCasts();
                    if (currentME->isArrow()) {
                        // Found the arrow: p->x or p->p2.x
                        ultimateBase = Base;
                        foundArrow = true;
                        arrowME = currentME;
                        break;
                    }
                    // Check if base is (*p).x
                    if (auto *UO = dyn_cast<UnaryOperator>(Base)) {
                        if (UO->getOpcode() == UO_Deref) {
                            ultimateBase = UO->getSubExpr()->IgnoreParenImpCasts();
                            foundArrow = true;
                            break;
                        }
                    }
                    // Check if base is another MemberExpr (nested: p->p2.x)
                    currentME = dyn_cast<MemberExpr>(Base);
                }

                if (foundArrow && ultimateBase) {
                    FieldPath basePath = getTypedPath(ultimateBase);
                    if (basePath.isValid()) {
                        // Use StringRef for lookup - no allocation
                        auto it = State.VarToResource.find(basePath.toStringRef());
                        if (it != State.VarToResource.end()) {
                            rid = it->second;
                            found = true;
                            // Build member suffix from collected fields (reverse order)
                            for (auto fit = fields.rbegin(); fit != fields.rend(); ++fit) {
                                if ((*fit)->getIdentifier()) {
                                    memberSuffix += '.';
                                    memberSuffix += (*fit)->getName();
                                }
                            }

                            // TIER 2.4: Union member aliasing
                            // Check if the first field after arrow is a union member
                            if (arrowME) {
                                Expr *arrowBase = arrowME->getBase()->IgnoreParenImpCasts();
                                QualType baseType = arrowBase->getType()->getPointeeType();
                                if (auto *RD = baseType->getAsRecordDecl()) {
                                    if (RD->isUnion()) {
                                        // The arrow ME's member is the union member
                                        if (auto *FD = dyn_cast<FieldDecl>(arrowME->getMemberDecl())) {
                                            auto stackIt = State.StackVarPaths.find(rid);
                                            if (stackIt != State.StackVarPaths.end()) {
                                                FieldPath stackPath = FieldPath::fromSynthetic(stackIt->second);
                                                setFieldTyped(stackPath, "_union_active",
                                                             SymValue::String(FD->getName()));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (found) {
                // Check heap objects
                auto hosIt = State.HeapObjectStates.find(rid);
                if (hosIt != State.HeapObjectStates.end()) {
                    hosIt->second.initialized = true;
                }
                // Check stack aliasing - mark stack variable (or member) as initialized
                auto stackIt = State.StackVarPaths.find(rid);
                if (stackIt != State.StackVarPaths.end()) {
                    llvm::SmallString<64> fullPath(stackIt->second);
                    fullPath += memberSuffix;
                    FieldPath stackPath = FieldPath::fromSynthetic(fullPath);
                    setFieldTyped(stackPath, "_initialized", SymValue::Const(1));
                }
            }
        };

        // Check for uninitialized heap read in expressions
        if (B->isAssignmentOp()) {
            // Check RHS for uninitialized heap dereference
            checkHeapDeref(RHS, RHS->getBeginLoc());
            if (B->isCompoundAssignmentOp()) {
                // Compound assignment reads LHS first
                checkHeapDeref(LHS, LHS->getBeginLoc());
            }
            // Mark heap object as initialized when written through dereference
            markHeapInitialized(LHS);
            // x += c and x -= c on an integer move its value by c; other
            // compound assignments leave it unknown
            if (B->isCompoundAssignmentOp() && LHS->getType()->isIntegralOrEnumerationType() &&
                lhsTypedPath.isValid()) {
                Expr::EvalResult Step;
                std::optional<int64_t> C;
                if ((B->getOpcode() == BO_AddAssign || B->getOpcode() == BO_SubAssign) &&
                    safeEvaluateAsInt(B->getRHS(), Step, Ctx))
                    C = safeGetInt64(Step.Val.getInt());
                if (C && *C == std::numeric_limits<int64_t>::min()) C.reset();
                if (C && B->getOpcode() == BO_SubAssign) C = -*C;
                stepScalar(LHS, C);
            }
        } else {
            // Non-assignment: check both operands for uninitialized heap read
            checkHeapDeref(LHS, LHS->getBeginLoc());
            checkHeapDeref(RHS, RHS->getBeginLoc());
        }

        // Handle pointer compound assignment: p += n, p -= n
        if ((B->getOpcode() == BO_AddAssign || B->getOpcode() == BO_SubAssign) &&
            LHS->getType()->isPointerType()) {
            if (lhsTypedPath.isValid()) {
                // Check for null pointer arithmetic (UB)
                SymValue nonnull = getFieldTyped(lhsTypedPath, "_nonnull");
                if (nonnull.kind == SymValue::CONST && nonnull.constVal == 0) {
                    Diag.Report(B->getBeginLoc(), ErrNullPtrArith);
                    return true;
                }

                SymValue baseLen = getFieldTyped(lhsTypedPath, "_base_length");
                SymValue delta = exprToSymValueImpl(RHS, Ctx);

                if (baseLen.kind != SymValue::UNKNOWN) {
                    SymValue offset = getFieldTyped(lhsTypedPath, "_offset");
                    SymValue newOffset = (B->getOpcode() == BO_AddAssign)
                        ? SymValue::Add(offset, delta)
                        : SymValue::Sub(offset, delta);
                    setFieldTyped(lhsTypedPath, "_offset", newOffset);
                }

                // Phase 3: Update base origin offset for compound assignment
                // Works even for untracked pointers (raw parameters)
                auto originIt = State.PointerBaseOrigins.find(lhsTypedPath);
                auto offsetParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                int64_t elemSizeVal = 1;
                bool isNegated = (B->getOpcode() == BO_SubAssign);
                if (auto elemSize = safeGetTypeSizeInChars(Ctx, LHS->getType()->getPointeeType())) {
                    elemSizeVal = *elemSize;
                }
                if (originIt != State.PointerBaseOrigins.end()) {
                    SymValue deltaBytes = delta;
                    if (elemSizeVal != 1) {
                        deltaBytes = SymValue::Mul(delta, SymValue::Const(elemSizeVal));
                    }
                    if (isNegated) {
                        deltaBytes = SymValue::Sub(SymValue::Const(0), deltaBytes);
                    }
                    originIt->second.offsetBytes = SymValue::Add(originIt->second.offsetBytes, deltaBytes);
                    // Track offset param if first contribution
                    if (!originIt->second.offsetParamIdx && offsetParamIdx) {
                        originIt->second.offsetParamIdx = offsetParamIdx;
                        originIt->second.offsetElemSize = elemSizeVal;
                        originIt->second.offsetNegated = isNegated;
                    }
                } else {
                    // Check if LHS is a parameter - establish base origin
                    auto lhsParamIdx = getParameterIndex(LHS, CurrentFunctionDecl);
                    if (lhsParamIdx) {
                        SymValue deltaBytes = delta;
                        if (elemSizeVal != 1) {
                            deltaBytes = SymValue::Mul(delta, SymValue::Const(elemSizeVal));
                        }
                        if (isNegated) {
                            deltaBytes = SymValue::Sub(SymValue::Const(0), deltaBytes);
                        }
                        State.PointerBaseOrigins[lhsTypedPath] = BaseOriginInfo(lhsTypedPath, deltaBytes, lhsParamIdx, offsetParamIdx, elemSizeVal, isNegated);
                    }
                }
            }
            return true;
        }

        // ============================================================
        // AUTO-POSTCONDITION INFERENCE: Compound assignment on this->_field
        // ============================================================
        // Detect _size += n, _size -= n on 'this' and infer postconditions
        if ((B->getOpcode() == BO_AddAssign || B->getOpcode() == BO_SubAssign) &&
            CurrentMethodClass) {
            if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
                if (isa<CXXThisExpr>(Mem->getBase()->IgnoreParenImpCasts())) {
                    std::string field = Mem->getMemberDecl()->getName().str();
                    if (!field.empty() && field[0] == '_') {
                        SymValue oldRef = SymValue::Var("old(" + field + ")");
                        SymValue deltaExpr = SymValue::Unknown();
                        std::optional<unsigned> argIdx = std::nullopt;

                        // Try constant delta
                        Expr::EvalResult R;
                        if (safeEvaluateAsInt(RHS, R, Ctx)) {
                            auto val = safeGetInt64(R.Val.getInt());
                            if (val) deltaExpr = SymValue::Const(*val);
                        }
                        // Try parameter reference
                        else if (auto paramIdx = getParameterIndex(RHS, CurrentFunctionDecl)) {
                            deltaExpr = SymValue::Var("arg" + std::to_string(*paramIdx));
                            argIdx = paramIdx;
                        }

                        if (deltaExpr.kind != SymValue::UNKNOWN) {
                            SymValue inferredExpr = (B->getOpcode() == BO_AddAssign)
                                ? SymValue::Add(oldRef, deltaExpr)
                                : SymValue::Sub(oldRef, deltaExpr);
                            addInferredPostcondition(field, inferredExpr, /*usesOld=*/true, argIdx);
                        }
                    }
                }
            }
        }

        if (B->getOpcode() != BO_Assign) return true;

        // TIER 1: Mark variables as initialized upon assignment
        // After x = value, x is considered initialized
        if (!lhsPath.empty() && !hasRuntimeIndex(lhsPath)) {
            QualType lhsType = B->getLHS()->getType();
            if ((lhsType->isArithmeticType() || lhsType->isBooleanType()) &&
                !lhsType->isPointerType()) {
                setFieldTyped(lhsTypedPath, "_initialized", SymValue::Const(1));
                // Store SymValue for integer assignments (enables modular bounds analysis)
                SymValue val = exprToSymValueImpl(RHS, Ctx);
                std::optional<int64_t> Step = isa<DeclRefExpr>(LHS) ? stepOf(val, lhsPath) : std::nullopt;
                if (Step) {
                    // i = i + c moves i like i += c
                    stepScalar(LHS, Step);
                } else if (isa<MemberExpr>(LHS)) {
                    // Values naming the member keep its old value
                    rebindValuesNaming(lhsPath, SymValue::Unknown());
                } else if (isa<DeclRefExpr>(LHS)) {
                    // Values naming the variable keep its old value; a new
                    // value that names it otherwise (i = 2 * i) is unknown
                    std::optional<SymValue> Old = getEnvValueTyped(lhsTypedPath);
                    rebindValuesNaming(lhsPath, Old && !namesVariable(*Old, lhsPath) ? *Old
                                                                                    : SymValue::Unknown());
                    // Such a value keeps the range it has before the write
                    // (h = (h + 1) % N: 0 <= h <= N - 1)
                    ValueRange Kept;
                    if (namesVariable(val, lhsPath)) {
                        Kept = valueRange(State, val);
                        val = SymValue::Unknown();
                    }
                    // Unknown assignments must discard the old scalar value too.
                    invalidateProofs(lhsPath, "_value");
                    invalidateVarBounds(lhsPath);
                    if (Kept.Lo) addProof(lhsPath, buildProofKey("_value", ProofOp::GE, *Kept.Lo));
                    if (Kept.Hi) addProof(lhsPath, buildProofKey("_value", ProofOp::LE, *Kept.Hi));
                }
                if (!Step) setEnvValueTyped(lhsTypedPath, val);
            }
            // Pointers: p = &x or p = q marks p as initialized
            else if (lhsType->isPointerType()) {
                setFieldTyped(lhsTypedPath, "_initialized", SymValue::Const(1));
            }
        }

        if (!lhsPath.empty() && !hasRuntimeIndex(lhsPath)) {
            // Handle pointer reassignment: p = p + n, p = p - n, p = q + n
            QualType lhsType = B->getLHS()->getType();
            auto RecordBase = llvm::make_scope_exit([&] {
                if (lhsType->isPointerType()) recordPointerBase(lhsTypedPath, RHS);
            });
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
                            // Phase 1 Migration: Use typed path
                            FieldPath srcTypedPath = getTypedPath(ptrExpr);
                            if (srcTypedPath.isValid()) {
                                SymValue delta = exprToSymValueImpl(deltaExpr, Ctx);
                                SymValue baseLen = getFieldTyped(srcTypedPath, "_base_length");

                                if (baseLen.kind != SymValue::UNKNOWN) {
                                    // Copy buffer tracking from source
                                    SymValue srcOffset = getFieldTyped(srcTypedPath, "_offset");
                                    SymValue newOffset = (BinOp->getOpcode() == BO_Add)
                                        ? SymValue::Add(srcOffset, delta)
                                        : SymValue::Sub(srcOffset, delta);

                                    setFieldTyped(lhsTypedPath, "_base_length", baseLen);
                                    setFieldTyped(lhsTypedPath, "_offset", newOffset);
                                    setFieldTyped(lhsTypedPath, "_nonnull", SymValue::Const(1));
                                }

                                // Phase 3: Track base origin for p = q + n reassignment
                                // Works even for untracked pointers
                                SymValue deltaBytes = delta;
                                int64_t elemSizeVal = 1;
                                bool isNegated = (BinOp->getOpcode() == BO_Sub);
                                if (auto elemSize = safeGetTypeSizeInChars(Ctx, lhsType->getPointeeType())) {
                                    elemSizeVal = *elemSize;
                                    if (*elemSize != 1) {
                                        deltaBytes = SymValue::Mul(delta, SymValue::Const(*elemSize));
                                    }
                                    if (isNegated) {
                                        deltaBytes = SymValue::Sub(SymValue::Const(0), deltaBytes);
                                    }
                                }

                                auto srcOriginIt = State.PointerBaseOrigins.find(srcTypedPath);
                                auto offsetParamIdx = getParameterIndex(deltaExpr, CurrentFunctionDecl);
                                if (srcOriginIt != State.PointerBaseOrigins.end()) {
                                    // Propagate existing base origin with accumulated offset
                                    BaseOriginInfo newOrigin = srcOriginIt->second;
                                    newOrigin.offsetBytes = SymValue::Add(newOrigin.offsetBytes, deltaBytes);
                                    // Track offset param if first contribution
                                    if (!newOrigin.offsetParamIdx && offsetParamIdx) {
                                        newOrigin.offsetParamIdx = offsetParamIdx;
                                        newOrigin.offsetElemSize = elemSizeVal;
                                        newOrigin.offsetNegated = isNegated;
                                    }
                                    State.PointerBaseOrigins[lhsTypedPath] = std::move(newOrigin);
                                } else {
                                    // Check if source is a parameter - create new base origin
                                    auto srcParamIdx = getParameterIndex(ptrExpr, CurrentFunctionDecl);
                                    if (srcParamIdx) {
                                        State.PointerBaseOrigins[lhsTypedPath] = BaseOriginInfo(srcTypedPath, deltaBytes, srcParamIdx, offsetParamIdx, elemSizeVal, isNegated);
                                    }
                                }
                                return true;  // Skip default nullability handling
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
                State.PointerAliases.erase(lhsPath);
                // Phase 3: Clear stale base origin - will be updated below if applicable
                State.PointerBaseOrigins.erase(lhsTypedPath);
                // A null pointer has no lifetime: free(NULL) is a no-op.
                if (RHS->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
                    State.PointerLifetimes.erase(lhsPath);
                // Also invalidate aliases that point TO this pointer (they're now stale)
                llvm::SmallVector<llvm::StringRef, 4> toErase;
                for (const auto& entry : State.PointerAliases) {
                    if (entry.second == lhsPath) {
                        toErase.push_back(entry.first());
                    }
                }
                for (const auto& key : toErase) {
                    State.PointerAliases.erase(key);
                }

                // Use helper to infer nullability from expression
                if (auto nonnull = inferNonnullFromExpr(RHS)) {
                    setFieldTyped(lhsTypedPath, "_nonnull", SymValue::Const(*nonnull));
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
                        State.PointerLifetimes[lhsPath] = pls;
                        // Track allocation size for bounds checking
                        if (NewE->isArray()) {
                            if (auto ArraySizeExpr = NewE->getArraySize()) {
                                Expr::EvalResult R;
                                if (safeEvaluateAsInt(*ArraySizeExpr, R, Ctx)) {
                                    if (auto count = safeGetInt64(R.Val.getInt())) {
                                        setFieldTyped(lhsTypedPath, "_size", SymValue::Const(*count));
                                        setFieldTyped(lhsTypedPath, "_base_length", SymValue::Const(*count));
                                        setFieldTyped(lhsTypedPath, "_offset", SymValue::Const(0));
                                    }
                                }
                            }
                        } else {
                            // Scalar new: size is 1 element
                            setFieldTyped(lhsTypedPath, "_size", SymValue::Const(1));
                            setFieldTyped(lhsTypedPath, "_base_length", SymValue::Const(1));
                            setFieldTyped(lhsTypedPath, "_offset", SymValue::Const(0));
                        }

                        // HEAP OBJECT INIT TRACKING (Tier 5.2)
                        // Track whether the heap object is initialized based on new form
                        ResourceId rid = NewE->getBeginLoc();
                        State.VarToResource[lhsPath] = rid;

                        bool isInitialized = false;
                        if (NewE->hasInitializer()) {
                            isInitialized = true;
                        } else {
                            QualType allocType = NewE->getAllocatedType();
                            if (auto *RT = allocType->getAs<RecordType>()) {
                                if (auto *RD = RT->getDecl()) {
                                    if (auto *CRD = dyn_cast<CXXRecordDecl>(RD)) {
                                        isInitialized = CRD->hasUserDeclaredConstructor() ||
                                                        CRD->hasNonTrivialDefaultConstructor();
                                    }
                                }
                            }
                        }

                        HeapObjectState hos;
                        hos.initialized = isInitialized;
                        hos.allocLoc = rid;
                        State.HeapObjectStates[rid] = hos;
                    }
                    // TIER 5.1: Handle address-of assignment: p = &x
                    else if (auto *UO = dyn_cast<UnaryOperator>(RHS)) {
                        if (UO->getOpcode() == UO_AddrOf) {
                            Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();
                            if (auto *TargetDRE = dyn_cast<DeclRefExpr>(Sub)) {
                                if (auto *TargetVD = dyn_cast<VarDecl>(TargetDRE->getDecl())) {
                                    ResourceId rid = TargetVD->getLocation();
                                    State.VarToResource[lhsPath] = rid;
                                    State.StackVarPaths[rid] = variableKey(TargetVD);
                                }
                            }
                        }
                    }
                } else if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                    // Copy nullability from source pointer - Phase 1: use typed path
                    FieldPath srcTypedPath = FieldPath::fromDecl(DRE->getDecl());
                    std::string srcPath = srcTypedPath.toString();
                    setFieldTyped(lhsTypedPath, "_nonnull", getFieldTyped(srcTypedPath, "_nonnull"));
                    // Also copy proofs using typed API
                    if (hasProofTyped(srcTypedPath, Proof{"_nonnull", ProofOp::NE, 0})) {
                        addProofTyped(lhsTypedPath, Proof{"_nonnull", ProofOp::NE, 0});
                    }
                    if (hasProofTyped(srcTypedPath, Proof{"_nonnull", ProofOp::EQ, 0})) {
                        addProofTyped(lhsTypedPath, Proof{"_nonnull", ProofOp::EQ, 0});
                    }
                    // Track pointer alias for future proof propagation
                    State.PointerAliases[lhsPath] = srcPath;

                    // HEAP OBJECT INIT: Propagate ResourceId when copying pointers
                    // This enables aliasing: p = q; *p = 42; *q is now initialized
                    auto srcResIt = State.VarToResource.find(srcPath);
                    if (srcResIt != State.VarToResource.end()) {
                        State.VarToResource[lhsPath] = srcResIt->second;
                    }

                    // Linear type support: transfer scope-exit constraints from src to dst
                    // This enables move semantics: g = f transfers f's obligations to g
                    if (hasConstraints(srcPath)) {
                        transferConstraints(srcPath, lhsPath, B->getBeginLoc());
                        // Copy ghost state (like @open) from source to destination
                        for (const auto& constraint : State.CurrentScopeExitConstraints) {
                            if (constraint.varPath == lhsPath) {  // Constraints now on lhs
                                SymValue fieldVal = getFieldTyped(srcTypedPath, constraint.field);
                                if (fieldVal.kind != SymValue::UNKNOWN) {
                                    setFieldTyped(lhsTypedPath, constraint.field, fieldVal);
                                }
                            }
                        }
                    }

                    // Phase 3: Track base origin for simple pointer copy (p = q)
                    auto srcOriginIt = State.PointerBaseOrigins.find(srcTypedPath);
                    if (srcOriginIt != State.PointerBaseOrigins.end()) {
                        // Propagate existing base origin
                        State.PointerBaseOrigins[lhsTypedPath] = srcOriginIt->second;
                    } else {
                        // Check if source is a parameter - establish base origin with zero offset
                        auto srcParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                        if (srcParamIdx) {
                            State.PointerBaseOrigins[lhsTypedPath] = BaseOriginInfo(srcTypedPath, SymValue::Const(0), srcParamIdx);
                        }
                    }
                } else if (auto *CE = dyn_cast<CallExpr>(RHS->IgnoreParenCasts())) {
                    // Function call - invalidate old proofs and mark as unknown
                    invalidateNullability(lhsPath);
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
                        State.PointerLifetimes[lhsPath] = pls;
                        // Track allocation size for bounds checking
                        trackAllocationSize(lhsTypedPath, lhsType, CE, allocName);
                    }
                } else {
                    // Unknown source - invalidate old proofs and mark as unknown
                    invalidateNullability(lhsPath);
                }
            }

            if (auto *Call = dyn_cast<CallExpr>(RHS)) {
                auto oldVals = captureOldValues(Call);
                applyPostconditions(lhsPath, Call, oldVals);
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                clearProperties(lhsPath);
                copyProperties(lhsPath, variableKey(DRE->getDecl()).str());
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
                    // Phase 1 Migration: Use typed path
                    FieldPath rhsTypedPath = getTypedPath(RHS);
                    std::string rhsPath = rhsTypedPath.toString();
                    if (rhsTypedPath.isValid() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                    }
                }
            }
        }

        if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
            // Phase 1 Migration: Use typed path
            FieldPath baseTypedPath = getTypedPath(Mem->getBase());
            std::string basePath = baseTypedPath.toString();

            // Union member write tracking
            // When writing to a union member, track which member is now active
            auto *MD = Mem->getMemberDecl();
            StringRef fieldNameRef;
            if (MD->getIdentifier()) {
                fieldNameRef = MD->getName();
                QualType baseType = Mem->getBase()->getType();
                if (baseType->isPointerType())
                    baseType = baseType->getPointeeType();
                if (auto *RD = baseType->getAsRecordDecl()) {
                    if (RD->isUnion() && baseTypedPath.isValid()) {
                        setFieldTyped(baseTypedPath, "_union_active",
                                     SymValue::String(fieldNameRef));
                    }
                }
            }

            // Get field name for legacy path-based tracking
            // Reuse fieldNameRef if available, otherwise fall back to getNameAsString for operators
            std::string field = !fieldNameRef.empty() ? fieldNameRef.str()
                                                      : MD->getNameAsString();

            // Frame inference: Record write to parameter field
            // This tracks which fields each function writes for frame preservation
            if (CurrentFunctionDecl && !basePath.empty()) {
                recordParamFieldWrite(CurrentFunctionDecl, basePath, field);
            }

            if (!basePath.empty()) {
                if (hasRuntimeIndex(basePath)) {
                    std::string arrayBase = getArrayBase(basePath);
                    if (!arrayBase.empty()) invalidateArrayElements(arrayBase, field);
                } else {
                    invalidateProofs(basePath, field);
                    FieldPath baseTypedPath = FieldPath::fromSynthetic(basePath);
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(RHS, R, Ctx)) {
                        auto val = safeGetInt64(R.Val.getInt());
                        if (val) setFieldTyped(baseTypedPath, field, SymValue::Const(*val));
                        else setFieldTyped(baseTypedPath, field, SymValue::Unknown());
                    }
                    else if (auto *BinOp = dyn_cast<BinaryOperator>(RHS)) {
                        auto opcode = BinOp->getOpcode();
                        if (opcode == BO_Add || opcode == BO_Sub || opcode == BO_Mul || opcode == BO_Div) {
                            auto *L = BinOp->getLHS()->IgnoreParenImpCasts();
                            auto *Rh = BinOp->getRHS()->IgnoreParenImpCasts();
                            SymValue lVal = SymValue::Unknown(), rVal = SymValue::Unknown();
                            std::string lBase = getBasePath(L), lField = getFieldName(L);
                            if (!lBase.empty() && !lField.empty() && !hasRuntimeIndex(lBase)) {
                                FieldPath lTypedPath = FieldPath::fromSynthetic(lBase);
                                lVal = getFieldTyped(lTypedPath, lField);
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
                                    FieldPath rTypedPath = FieldPath::fromSynthetic(rBase);
                                    rVal = getFieldTyped(rTypedPath, rField);
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
                            setFieldTyped(baseTypedPath, field, result);
                        }
                    }
                    else setFieldTyped(baseTypedPath, field, SymValue::Unknown());
                }
            }

            // ============================================================
            // AUTO-POSTCONDITION INFERENCE (Unified Expression Parser)
            // ============================================================
            // If we're inside a method (CurrentMethodClass != nullptr) and
            // the assignment is to a DTT-tracked field on 'this' (e.g., _size),
            // infer a postcondition from the assignment using the unified parser.
            // Also handles nested fields: this->inner._value or this->stats.count
            if (CurrentMethodClass) {
                // Get the full nested path from 'this' (e.g., "inner._value" or "_size")
                std::string nestedPath = getNestedFieldPathFromThis(LHS);

                // Track if:
                // 1. Direct field starts with '_' (e.g., this->_size)
                // 2. Nested path has any '_' prefixed field
                // 3. Nested path has depth > 1 (e.g., this->stats.count) - track for constraint matching
                bool isNested = !nestedPath.empty() && nestedPath.find('.') != std::string::npos;
                bool shouldTrack = !nestedPath.empty() &&
                    (hasTrackedFieldInPath(nestedPath) ||
                     (!field.empty() && field[0] == '_') ||
                     isNested);  // Track all nested field assignments

                if (shouldTrack) {
                    // Use the full nested path as the field key for postconditions
                    std::string fieldKey = nestedPath.empty() ? field : nestedPath;

                    // Use unified expression parser for all RHS expressions
                    auto parseResult = parsePostconditionExpr(RHS, Ctx, fieldKey,
                                                              CurrentMethodClass, CurrentFunctionDecl);

                    // Record the inferred postcondition if we successfully analyzed the RHS
                    if (parseResult.expr.kind != SymValue::UNKNOWN) {
                        addInferredPostcondition(fieldKey, parseResult.expr,
                                                 parseResult.usesOld, parseResult.argIdx);
                    }
                }
            }
            // ============================================================
            // AUTO-POSTCONDITION INFERENCE: Out-parameter (reference) assignments
            // ============================================================
            // For free functions or methods: detect param._field = value where param is a reference
            // Example: void init(Vec& v, int n) { v._size = n; } → ensures=arg0._size == arg1
            else if (CurrentFunctionDecl) {
                Expr* baseExpr = Mem->getBase()->IgnoreParenImpCasts();
                if (auto targetArgIdx = getParameterIndex(baseExpr, CurrentFunctionDecl)) {
                    // Check if the parameter is a reference type
                    if (*targetArgIdx < CurrentFunctionDecl->getNumParams()) {
                        QualType paramType = CurrentFunctionDecl->getParamDecl(*targetArgIdx)->getType();
                        if (paramType->isReferenceType()) {
                            // Only track fields that start with '_'
                            if (!field.empty() && field[0] == '_') {
                                // Use unified expression parser
                                auto parseResult = parsePostconditionExpr(RHS, Ctx, field,
                                                                          nullptr, CurrentFunctionDecl);

                                if (parseResult.expr.kind != SymValue::UNKNOWN) {
                                    addInferredPostcondition(field, parseResult.expr, /*usesOld=*/false,
                                                            parseResult.argIdx, targetArgIdx);
                                }
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
    
    struct GuardInfo {
        std::string basePath, field, proofKey, negProofKey;
        std::string extraNegProofKey;  // For unsigned cast: (unsigned)n > C implies n < 0 || n > C
                                       // So negation is n >= 0 && n <= C (two proofs needed)
        bool extraNegByConvention = false;  // From the `_` field naming convention
    };

    // Variable guard info for "if (i < v.size())"
    struct VarGuardInfo {
        std::string varName;
        std::string objPath;
        std::string field;
        ProofOp op;
        ProofOp negOp;
        // The bound was weakened (i < v._size - 1 recorded as i < v._size):
        // only an upper bound still holds, on whichever side gives one.
        bool weakened = false;

        static bool isUpper(ProofOp Op) { return Op == ProofOp::LT || Op == ProofOp::LE; }
        std::optional<ProofOp> whenTrue() const {
            if (weakened && !isUpper(op)) return std::nullopt;
            return op;
        }
        std::optional<ProofOp> whenFalse() const {
            if (weakened && !isUpper(negOp)) return std::nullopt;
            return negOp;
        }
    };

    std::optional<VarGuardInfo> extractVarGuard(Expr *Cond) {
        bool weakenedBound = false;
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
                varName = variableKey(DRE->getDecl()).str();
            }

            if (!varName.empty()) {
                // Normal form: LHS is variable, RHS should be field
                if (auto *MCE = dyn_cast<CXXMemberCallExpr>(RHS)) {
                    auto *Method = MCE->getMethodDecl();
                    StringRef classNameRef = safeGetName(Method ? Method->getParent() : nullptr);
                    if (Method && !classNameRef.empty()) {
                        MethodKey key{classNameRef.str(), Method->getNameAsString(), MCE->getNumArgs()};

                        auto it = MethodAnnotations.find(key);
                        if (it != MethodAnnotations.end() && !it->second.returns.empty() &&
                            callRequirementsProven(MCE, Ctx)) {
                            // Phase 1 Migration: Use typed path
                            objPath = getTypedPath(MCE->getImplicitObjectArgument()).toString();
                            field = it->second.returns;
                        }
                    }
                } else if (auto *ME = dyn_cast<MemberExpr>(RHS)) {
                    // Phase 1 Migration: Use typed path
                    objPath = getTypedPath(ME->getBase()).toString();
                    field = ME->getMemberDecl()->getName().str();
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
                                // i < _size - N (N > 0) implies i < _size, unless
                                // an unsigned _size - N wraps around
                                if (val && *val > 0 && !ME->getType()->isUnsignedIntegerType()) {
                                    // Phase 1 Migration: Use typed path
                                    objPath = getTypedPath(ME->getBase()).toString();
                                    field = ME->getMemberDecl()->getName().str();
                                    weakenedBound = true;
                                }
                            }
                        }
                    }
                }
            } else {
                // Try reversed form: field op variable (e.g., v._size > i)
                if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                    varName = variableKey(DRE->getDecl()).str();
                }
                if (varName.empty()) return std::nullopt;

                reversed = true;
                if (auto *MCE = dyn_cast<CXXMemberCallExpr>(LHS)) {
                    auto *Method = MCE->getMethodDecl();
                    StringRef classNameRef = safeGetName(Method ? Method->getParent() : nullptr);
                    if (Method && !classNameRef.empty()) {
                        MethodKey key{classNameRef.str(),
                                     Method->getNameAsString(),
                                     MCE->getNumArgs()};

                        auto it = MethodAnnotations.find(key);
                        if (it != MethodAnnotations.end() && !it->second.returns.empty() &&
                            callRequirementsProven(MCE, Ctx)) {
                            // Phase 1 Migration: Use typed path
                            objPath = getTypedPath(MCE->getImplicitObjectArgument()).toString();
                            field = it->second.returns;
                        }
                    }
                } else if (auto *ME = dyn_cast<MemberExpr>(LHS)) {
                    // Phase 1 Migration: Use typed path
                    objPath = getTypedPath(ME->getBase()).toString();
                    field = ME->getMemberDecl()->getName().str();
                }
            }

            if (objPath.empty() || field.empty()) return std::nullopt;

            ProofOp op, negOp;
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
                case BO_LT:  op = ProofOp::LT;  negOp = ProofOp::GE; break;
                case BO_LE:  op = ProofOp::LE;  negOp = ProofOp::GT; break;
                case BO_GT:  op = ProofOp::GT;  negOp = ProofOp::LE; break;
                case BO_GE:  op = ProofOp::GE;  negOp = ProofOp::LT; break;
                default: return std::nullopt;
            }

            return VarGuardInfo{varName, objPath, field, op, negOp, weakenedBound};
        }

        return std::nullopt;
    }

    // Extract multiple guards from compound conditions
    llvm::SmallVector<GuardInfo, 4> extractAllGuards(Expr *Cond) {
        llvm::SmallVector<GuardInfo, 4> result;
        Cond = Cond->IgnoreParenImpCasts();

        // Handle && for compound guards like "k >= 0 && k < 10"
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            if (BO->getOpcode() == BO_LAnd) {
                auto leftGuards = extractAllGuards(BO->getLHS());
                auto rightGuards = extractAllGuards(BO->getRHS());
                result.append(leftGuards.begin(), leftGuards.end());
                result.append(rightGuards.begin(), rightGuards.end());
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
    llvm::SmallVector<GuardInfo, 4> extractOrGuards(Expr *Cond) {
        llvm::SmallVector<GuardInfo, 4> result;
        Cond = Cond->IgnoreParenImpCasts();

        // Handle || for compound guards like "tag < 0 || tag > 30"
        if (auto *BO = dyn_cast<BinaryOperator>(Cond)) {
            if (BO->getOpcode() == BO_LOr) {
                auto leftGuards = extractOrGuards(BO->getLHS());
                auto rightGuards = extractOrGuards(BO->getRHS());
                result.append(leftGuards.begin(), leftGuards.end());
                result.append(rightGuards.begin(), rightGuards.end());
                return result;
            }
        }

        // Try single guard extraction
        auto single = extractGuard(Cond);
        if (single) result.push_back(*single);
        return result;
    }

    // nullptr, NULL, 0, or a cast of one of them (C's NULL is ((void *)0)).
    static bool isNullLiteral(const Expr *E) {
        E = E->IgnoreParenCasts();
        if (isa<CXXNullPtrLiteralExpr>(E) || isa<GNUNullExpr>(E))
            return true;
        if (auto *IL = dyn_cast<IntegerLiteral>(E))
            return IL->getValue() == 0;
        return false;
    }

    // When a method returning a pointer is compared to nullptr,
    // and that method has bool postconditions (ensures_if_true/ensures_if_false),
    // map the null check to a guard on the object's tracked property.
    // e.g., ptr.get() != nullptr → guard on ptr._nonnull
    std::optional<GuardInfo> extractMethodNullCheckGuard(Expr *PtrExpr, Expr *NullExpr,
                                                         BinaryOperatorKind Op) {
        if (!PtrExpr->getType()->isPointerType() || !isNullLiteral(NullExpr))
            return std::nullopt;

        auto *MCE = dyn_cast<CXXMemberCallExpr>(PtrExpr);
        if (!MCE || !callRequirementsProven(MCE, Ctx)) return std::nullopt;

        auto *Method = MCE->getMethodDecl();
        if (!Method) return std::nullopt;

        StringRef classNameRef = resolveAnnotationClassName(Method, MCE->getImplicitObjectArgument());
        if (classNameRef.empty()) return std::nullopt;

        // Method must have identifier (bool postconds come from named methods, not operators)
        if (!Method->getIdentifier()) return std::nullopt;
        MethodKey key{classNameRef.str(), Method->getName().str(), MCE->getNumArgs()};
        auto it = MethodAnnotations.find(key);
        if (it == MethodAnnotations.end() || it->second.boolPostconds.empty())
            return std::nullopt;

        FieldPath objTypedPath = getTypedPath(MCE->getImplicitObjectArgument());
        if (!objTypedPath.isValid()) return std::nullopt;

        // NE → method returned non-null → "true" postconds
        // EQ → method returned null → "false" postconds
        bool trueCase = (Op == BO_NE);
        for (const auto& bp : it->second.boolPostconds) {
            if (bp.onTrue != trueCase) continue;
            GuardInfo gi;
            gi.basePath = objTypedPath.toString();
            gi.field = bp.field;
            auto pk = buildProofKey(bp.field, bp.op, bp.rhs);
            auto npk = buildProofKey(bp.field, negateProofOp(bp.op), bp.rhs);
            gi.proofKey.assign(pk.begin(), pk.end());
            gi.negProofKey.assign(npk.begin(), npk.end());
            return gi;
        }
        return std::nullopt;
    }

    // The value of integer operand E when known on this path: a constant
    // expression, or a variable or field holding a constant that every
    // conversion on the way to E's type represents.
    std::optional<int64_t> knownIntValue(const Expr *E) {
        Expr::EvalResult R;
        if (safeEvaluateAsInt(E, R, Ctx)) return safeGetInt64(R.Val.getInt());
        llvm::SmallVector<QualType, 4> Types{E->getType()};
        const Expr *Inner = E->IgnoreParens();
        while (auto *Cast = dyn_cast<CastExpr>(Inner)) {
            Inner = Cast->getSubExpr()->IgnoreParens();
            Types.push_back(Inner->getType());
        }
        std::optional<SymValue> V;
        if (auto *DRE = dyn_cast<DeclRefExpr>(Inner)) {
            FieldPath Path = getTypedPath(DRE);
            if (Path.isValid()) V = getEnvValueTyped(Path);
        } else if (auto *ME = dyn_cast<MemberExpr>(Inner); ME && ME->getMemberDecl()->getIdentifier()) {
            FieldPath Base = getTypedPath(ME->getBase());
            if (Base.isValid()) V = getFieldTyped(Base, ME->getMemberDecl()->getName());
        }
        if (!V || V->kind != SymValue::CONST) return std::nullopt;
        for (QualType T : Types) {
            if (!T->isIntegralOrEnumerationType()) return std::nullopt;
            unsigned Width = Ctx.getIntWidth(T);
            bool Fits = T->isSignedIntegerOrEnumerationType()
                            ? llvm::isIntN(Width, V->constVal)
                            : V->constVal >= 0 && llvm::isUIntN(Width, V->constVal);
            if (!Fits) return std::nullopt;
        }
        return V->constVal;
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
                if (Method && Method->getIdentifier()) {
                    StringRef classNameRef = resolveAnnotationClassName(Method, MCE->getImplicitObjectArgument());
                    if (!classNameRef.empty()) {
                        MethodKey key{classNameRef.str(),
                                      Method->getName().str(),
                                      MCE->getNumArgs()};

                        auto it = MethodAnnotations.find(key);
                        if (it != MethodAnnotations.end() && !it->second.returns.empty() &&
                            callRequirementsProven(MCE, Ctx)) {
                            GuardInfo gi;
                            FieldPath baseTypedPath = getTypedPath(MCE->getImplicitObjectArgument());
                            gi.basePath = baseTypedPath.toString();
                            gi.field = it->second.returns;  // _size
                            if (!baseTypedPath.isValid()) return std::nullopt;

                            auto Value = knownIntValue(BO->getRHS());
                            if (!Value) return std::nullopt;
                            int64_t rhsVal = *Value;

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
            }

            // Handle field access on LHS: v.len > 0
            // Skip pointer member null checks - let the pointer handler below handle them
            if (auto *Mem = dyn_cast<MemberExpr>(LHS)) {
                // Check if this is a pointer member null check - let specialized handler below handle it
                bool isPointerNullCheck = Mem->getType()->isPointerType() &&
                    (BO->getOpcode() == BO_EQ || BO->getOpcode() == BO_NE) &&
                    isNullLiteral(BO->getRHS()->IgnoreParenImpCasts());

                if (!isPointerNullCheck) {
                    GuardInfo gi;
                    // Phase 1 Migration: Use typed path
                    FieldPath baseTypedPath = getTypedPath(Mem->getBase());
                    gi.basePath = baseTypedPath.toString();
                    gi.field = Mem->getMemberDecl()->getName().str();
                    if (!baseTypedPath.isValid() || hasRuntimeIndex(gi.basePath)) return std::nullopt;
                    auto Value = knownIntValue(BO->getRHS());
                    if (!Value) return std::nullopt;
                    int64_t rhsVal = *Value;
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
                        gi.extraNegByConvention = true;
                    }
                    return gi;
                }
            }

            // Handle pointer null checks: ptr != nullptr, ptr == nullptr, ptr != 0, ptr == 0
            // This MUST come before the generic DeclRefExpr handler
            if (BO->getOpcode() == BO_NE || BO->getOpcode() == BO_EQ) {
                auto *RHS = BO->getRHS()->IgnoreParenImpCasts();

                // Special case: method() != nullptr where method has bool postconditions
                // e.g., ptr.get() != nullptr → propagate guard to the object's _nonnull
                if (auto gi = extractMethodNullCheckGuard(LHS, RHS, BO->getOpcode()))
                    return *gi;

                // ptr == null or null == ptr (nullptr, NULL, 0)
                Expr *PtrSide = isNullLiteral(RHS) ? LHS : isNullLiteral(LHS) ? RHS : nullptr;
                if (PtrSide && PtrSide->getType()->isPointerType()) {
                    std::string ptrPath = getExprPath(PtrSide);
                    if (!ptrPath.empty()) {
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
            Expr *VarExpr = BO->getLHS()->IgnoreParens();
            bool hasUnsignedCast = false;
            // Strip ALL casts and check for signed→unsigned conversion anywhere in the chain.
            // A conversion that can change the value otherwise (narrowing,
            // unsigned to signed of the same width) leaves no fact.
            while (auto *Cast = dyn_cast<CastExpr>(VarExpr)) {
                // Check if this cast or any implicit cast converts signed to unsigned
                QualType destType = Cast->getType();
                // Get the innermost source type (after stripping all implicit casts)
                Expr *innerExpr = Cast->getSubExpr();
                while (auto *innerCast = dyn_cast<ImplicitCastExpr>(innerExpr)) {
                    innerExpr = innerCast->getSubExpr();
                }
                QualType srcType = innerExpr->getType();
                if (destType->isIntegralOrEnumerationType() && srcType->isIntegralOrEnumerationType()) {
                    uint64_t DestWidth = Ctx.getTypeSize(destType), SrcWidth = Ctx.getTypeSize(srcType);
                    bool DestSigned = destType->isSignedIntegerOrEnumerationType();
                    bool SrcSigned = srcType->isSignedIntegerOrEnumerationType();
                    if (DestWidth < SrcWidth || (!SrcSigned && DestSigned && DestWidth == SrcWidth))
                        return std::nullopt;
                }
                if (destType->isUnsignedIntegerType() && srcType->isSignedIntegerType()) {
                    hasUnsignedCast = true;
                }
                VarExpr = Cast->getSubExpr()->IgnoreParenImpCasts();
            }
            // ++n and --n compare n's new value: the fact is about n
            if (auto *U = dyn_cast<UnaryOperator>(VarExpr); U && U->isPrefix() && U->isIncrementDecrementOp())
                VarExpr = U->getSubExpr()->IgnoreParenImpCasts();
            if (auto *DRE = dyn_cast<DeclRefExpr>(VarExpr)) {
                GuardInfo gi;
                gi.basePath = variableKey(DRE->getDecl()).str();
                gi.field = "_value";  // Use special field name for the variable itself

                auto Value = knownIntValue(BO->getRHS());
                if (!Value) return std::nullopt;
                int64_t rhsVal = *Value;

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

                // A negative n converted to unsigned is larger than any C >= 0:
                // (unsigned)n > C means n < 0 || n > C, and its negation
                // (unsigned)n <= C means n >= 0 && n <= C; (unsigned)n < C
                // means 0 <= n < C, and its negation says nothing.
                if (hasUnsignedCast) {
                    if (rhsVal < 0) return std::nullopt;
                    if (BO->getOpcode() == BO_GT || BO->getOpcode() == BO_GE) {
                        gi.proofKey.clear();
                        gi.extraNegProofKey = gi.field + ">=0";
                    } else if (BO->getOpcode() == BO_LT || BO->getOpcode() == BO_LE) {
                        gi.negProofKey.clear();
                    }
                }

                return gi;
            }

            // Handle reversed simple variable guard: 8 > n, 0 < n
            auto *RHS = BO->getRHS()->IgnoreParenImpCasts();
            // A conversion that can change n's value (narrowing, signedness)
            // leaves no fact in the reversed form.
            for (Expr *E = BO->getRHS()->IgnoreParens(); auto *Cast = dyn_cast<CastExpr>(E);
                 E = Cast->getSubExpr()->IgnoreParens()) {
                QualType Dest = Cast->getType(), Src = Cast->getSubExpr()->getType();
                if (Dest->isIntegralOrEnumerationType() && Src->isIntegralOrEnumerationType() &&
                    (Ctx.getTypeSize(Dest) < Ctx.getTypeSize(Src) ||
                     Dest->isSignedIntegerOrEnumerationType() != Src->isSignedIntegerOrEnumerationType()))
                    return std::nullopt;
            }
            if (auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
                auto KnownLHS = knownIntValue(BO->getLHS());
                if (!KnownLHS) return std::nullopt;  // LHS must be known for reversed form
                int64_t lhsVal = *KnownLHS;

                GuardInfo gi;
                gi.basePath = variableKey(DRE->getDecl()).str();
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
        
        if (auto *Call = dyn_cast<CallExpr>(Cond))
            if (!callRequirementsProven(Call, Ctx)) return std::nullopt;

        // Method call: v.empty()
        if (auto *MCE = dyn_cast<CXXMemberCallExpr>(Cond)) {
            auto *Method = MCE->getMethodDecl();
            if (!Method || !Method->getParent()) return std::nullopt;
            StringRef classNameRef = resolveAnnotationClassName(Method, MCE->getImplicitObjectArgument());
            if (classNameRef.empty()) return std::nullopt;

            std::string className = classNameRef.str();
            std::string methodName = Method->getNameAsString();
            MethodKey key{className, methodName, MCE->getNumArgs()};

            auto it = MethodAnnotations.find(key);
            if (it == MethodAnnotations.end() || it->second.boolPostconds.empty()) return std::nullopt;

            // Phase 1 Migration: Use typed path
            FieldPath objTypedPath = getTypedPath(MCE->getImplicitObjectArgument());
            if (!objTypedPath.isValid()) return std::nullopt;

            return BoolCallInfo{methodName, objTypedPath.toString(), negated, true, className, MCE->getNumArgs()};
        }

        // Free function call
        if (auto *Call = dyn_cast<CallExpr>(Cond)) {
            auto *Callee = Call->getDirectCallee();
            if (!Callee || Call->getNumArgs() < 1) return std::nullopt;

            std::string fn = Callee->getNameAsString();
            if (FnBoolPostconditions.find(fn) == FnBoolPostconditions.end()) return std::nullopt;

            auto *Arg = Call->getArg(0)->IgnoreParenImpCasts();
            // Phase 1 Migration: Use typed path
            FieldPath argTypedPath = getTypedPath(Arg);
            if (!argTypedPath.isValid()) return std::nullopt;

            return BoolCallInfo{fn, argTypedPath.toString(), negated, false, "", 0};
        }
        return std::nullopt;
    }

    // Special info for holds_alternative guard extraction
    struct HoldsAlternativeInfo {
        llvm::SmallString<64> variantPath;
        int64_t alternativeIndex;
        bool negated;
    };

    std::optional<HoldsAlternativeInfo> extractHoldsAlternative(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();

        bool negated = false;
        if (auto *UO = dyn_cast<UnaryOperator>(Cond)) {
            if (UO->getOpcode() == UO_LNot) {
                negated = true;
                Cond = UO->getSubExpr()->IgnoreParenImpCasts();
            }
        }

        auto *Call = dyn_cast<CallExpr>(Cond);
        if (!Call || Call->getNumArgs() != 1) return std::nullopt;

        auto *Callee = Call->getDirectCallee();
        if (!Callee) return std::nullopt;

        if (!Callee->getIdentifier() || Callee->getName() != "holds_alternative")
            return std::nullopt;

        // Check if it's std::holds_alternative
        if (!isInStdNamespace(Callee->getDeclContext())) return std::nullopt;

        Expr *Arg = Call->getArg(0)->IgnoreParenImpCasts();
        FieldPath argPath = getTypedPath(Arg);
        if (!argPath.isValid()) return std::nullopt;

        // Get variant type
        QualType variantType = Arg->getType().getNonReferenceType();
        auto *TST = variantType->getAs<TemplateSpecializationType>();
        if (!TST) return std::nullopt;

        // Get template argument (the type T in holds_alternative<T>) using helper
        if (auto tplArg = getFirstTemplateArg(Call)) {
            if (tplArg->type) {
                if (auto idx = getVariantAlternativeIndex(TST, *tplArg->type)) {
                    HoldsAlternativeInfo info;
                    info.variantPath = argPath.toString();
                    info.alternativeIndex = static_cast<int64_t>(*idx);
                    info.negated = negated;
                    return info;
                }
            }
        }
        return std::nullopt;
    }

    // Special info for any type check guard extraction: a.type() == typeid(T)
    struct AnyTypeCheckInfo {
        llvm::SmallString<64> anyPath;
        QualType checkedType;
        bool negated;
    };

    std::optional<AnyTypeCheckInfo> extractAnyTypeCheck(Expr *Cond) {
        Cond = Cond->IgnoreParenImpCasts();

        bool negated = false;
        if (auto *UO = dyn_cast<UnaryOperator>(Cond)) {
            if (UO->getOpcode() == UO_LNot) {
                negated = true;
                Cond = UO->getSubExpr()->IgnoreParenImpCasts();
            }
        }

        // Look for a.type() == typeid(T) or typeid(T) == a.type()
        auto *BO = dyn_cast<BinaryOperator>(Cond);
        if (!BO) {
            // Also check for CXXOperatorCallExpr (operator== overload)
            if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(Cond)) {
                if (OCE->getOperator() == OO_EqualEqual && OCE->getNumArgs() == 2) {
                    // Treat as comparison
                    Expr *LHS = OCE->getArg(0)->IgnoreParenImpCasts();
                    Expr *RHS = OCE->getArg(1)->IgnoreParenImpCasts();
                    return extractAnyTypeCheckFromExprs(LHS, RHS, negated);
                }
            }
            return std::nullopt;
        }

        if (BO->getOpcode() == BO_NE) {
            negated = !negated;
        } else if (BO->getOpcode() != BO_EQ) {
            return std::nullopt;
        }

        Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
        Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
        return extractAnyTypeCheckFromExprs(LHS, RHS, negated);
    }

    std::optional<AnyTypeCheckInfo> extractAnyTypeCheckFromExprs(Expr *LHS, Expr *RHS, bool negated) {
        // Try both orders: a.type() == typeid(T) and typeid(T) == a.type()
        auto result = tryExtractAnyTypeCheckPair(LHS, RHS, negated);
        if (result) return result;
        return tryExtractAnyTypeCheckPair(RHS, LHS, negated);
    }

    std::optional<AnyTypeCheckInfo> tryExtractAnyTypeCheckPair(Expr *TypeCall, Expr *TypeId, bool negated) {
        // TypeCall should be a.type()
        auto *MCE = dyn_cast<CXXMemberCallExpr>(TypeCall);
        if (!MCE) return std::nullopt;

        auto *Method = MCE->getMethodDecl();
        if (!Method || safeGetName(Method) != "type") return std::nullopt;

        // Check if it's on std::any
        auto *Parent = Method->getParent();
        if (!Parent || safeGetName(Parent) != "any") return std::nullopt;
        if (!isInStdNamespace(Parent->getDeclContext())) return std::nullopt;

        // Get the any object path
        Expr *AnyObj = MCE->getImplicitObjectArgument()->IgnoreParenImpCasts();
        FieldPath anyPath = getTypedPath(AnyObj);
        if (!anyPath.isValid()) return std::nullopt;

        // TypeId should be typeid(T)
        auto *TIE = dyn_cast<CXXTypeidExpr>(TypeId);
        if (!TIE) return std::nullopt;
        if (!TIE->isTypeOperand()) return std::nullopt;

        QualType checkedType = TIE->getTypeOperand(Ctx).getNonReferenceType().getCanonicalType();
        AnyTypeCheckInfo info;
        info.anyPath = anyPath.toString();
        info.checkedType = checkedType;
        info.negated = negated;
        return info;
    }

    // Traverse an if statement's branches from the states the condition left
    // when true (Then) and false (Else; it reaches the code after the `if`
    // when there is no else). The state after the `if` joins both branches; a
    // branch that never reaches its end adds nothing. AfterEarlyExit runs when
    // the then branch never reaches its end and there is no else.
    void traverseBranches(IfStmt *If, ProgramState Then, ProgramState Else,
                          llvm::function_ref<void()> AfterEarlyExit = nullptr) {
        State = std::move(Then);
        traverseIfReachable(If->getThen());
        ProgramState AfterThen = std::move(State);

        State = std::move(Else);
        if (If->getElse()) traverseIfReachable(If->getElse());
        else if (AfterThen.Unreachable && AfterEarlyExit && endsInJumpStatement(If->getThen()))
            AfterEarlyExit();
        State = joinStates(AfterThen, State);
    }

    // Whether S's last statement is return, throw, break or continue. The
    // early-exit heuristics (applyEarlyExitConventions) keep this scope.
    static bool endsInJumpStatement(const Stmt *S) {
        if (auto *CS = dyn_cast_or_null<CompoundStmt>(S))
            return !CS->body_empty() && endsInJumpStatement(CS->body_back());
        return isa_and_nonnull<ReturnStmt, CXXThrowExpr, BreakStmt, ContinueStmt>(S);
    }

    // Traverse S unless no path reaches it and no jump can enter it.
    bool traverseIfReachable(Stmt *S) {
        if (State.Unreachable && !canBeJumpedInto(S)) return true;
        return TraverseStmt(S);
    }

    // Whether a jump from outside S can land inside it: S holds a goto label,
    // or a case label of an enclosing switch.
    static bool canBeJumpedInto(const Stmt *S, bool InNestedSwitch = false) {
        if (!S) return false;
        if (isa<LabelStmt>(S)) return true;
        if (!InNestedSwitch && isa<SwitchCase>(S)) return true;
        bool Nested = InNestedSwitch || isa<SwitchStmt>(S);
        return llvm::any_of(S->children(),
                            [&](const Stmt *C) { return canBeJumpedInto(C, Nested); });
    }

    // The current path jumps to Target, which joins the states of all paths
    // that jump there.
    void jumpTo(ProgramState &Target) {
        Target = joinStates(Target, State);
        State.Unreachable = true;
    }

    static ProgramState unreachableState() {
        ProgramState S;
        S.Unreachable = true;
        return S;
    }

    bool TraverseReturnStmt(ReturnStmt *RS) {
        bool Result = RecursiveASTVisitor::TraverseReturnStmt(RS);
        State.Unreachable = true;
        return Result;
    }

    // A throw goes to the handlers of the enclosing try block, or leaves the
    // function, which must release what it holds as a return would.
    bool TraverseCXXThrowExpr(CXXThrowExpr *E) {
        bool Result = RecursiveASTVisitor::TraverseCXXThrowExpr(E);
        if (!HandlerTargets.empty()) {
            jumpTo(*HandlerTargets.back());
        } else {
            if (CurrentFunctionDecl && !State.Unreachable)
                checkScopeExitConstraints(E->getThrowLoc());
            State.Unreachable = true;
        }
        return Result;
    }

    bool TraverseBreakStmt(BreakStmt *) {
        if (BreakTargets.empty()) State.Unreachable = true;
        else jumpTo(*BreakTargets.back());
        return true;
    }

    // continue goes back to the loop condition.
    bool TraverseContinueStmt(ContinueStmt *) {
        if (ContinueTargets.empty()) State.Unreachable = true;
        else jumpTo(*ContinueTargets.back());
        return true;
    }

    // goto sends the state to its label. A label joins the state reaching it
    // with those of the gotos seen before it and, in a goto loop, of the gotos
    // back to it (traverseGotoLoop). A label whose address is taken continues
    // from the current state: computed gotos are not followed.
    bool TraverseGotoStmt(GotoStmt *G) {
        jumpTo(GotoStates.try_emplace(G->getLabel(), unreachableState()).first->second);
        return true;
    }

    bool TraverseIndirectGotoStmt(IndirectGotoStmt *G) {
        bool Result = RecursiveASTVisitor::TraverseIndirectGotoStmt(G);
        State.Unreachable = true;
        return Result;
    }

    bool TraverseLabelStmt(LabelStmt *L) {
        auto It = GotoStates.find(L->getDecl());
        if (It != GotoStates.end()) {
            State = joinStates(State, It->second);
            GotoStates.erase(It);
        }
        if (auto Back = BackEdges.find(L->getDecl()); Back != BackEdges.end())
            State = joinStates(State, Back->second);
        if (AddressTakenLabels.count(L->getDecl())) State.Unreachable = false;
        if (!WalkUpFromLabelStmt(L)) return false;
        return traverseIfReachable(L->getSubStmt());
    }

    // Compound statements holding a label and a later goto back to it (the
    // smallest one holding both), with those labels.
    llvm::DenseMap<const CompoundStmt *, llvm::SmallVector<const LabelDecl *, 2>> GotoLoops;
    llvm::DenseSet<const CompoundStmt *> ScannedForGotoLoops, IteratingGotoLoops;
    llvm::DenseSet<const LabelDecl *> AddressTakenLabels;
    // In a goto loop: the states of the gotos back to each label so far
    llvm::DenseMap<const LabelDecl *, ProgramState> BackEdges;

    // Find the goto loops in Root, in traversal order: a goto after its label
    // jumps back. Lambda and block bodies are their own functions.
    void findGotoLoops(const CompoundStmt *Root) {
        using Enclosing = llvm::SmallVector<const CompoundStmt *, 8>;
        Enclosing Open;
        llvm::DenseMap<const LabelDecl *, Enclosing> LabelEnclosing;
        // Pending (S, Leaving): Leaving closes compound S
        llvm::SmallVector<std::pair<const Stmt *, bool>, 32> Work{{Root, false}};
        while (!Work.empty()) {
            auto [S, Leaving] = Work.pop_back_val();
            if (Leaving) {
                Open.pop_back();
                continue;
            }
            if (!S || isa<LambdaExpr>(S) || isa<BlockExpr>(S)) continue;
            if (auto *C = dyn_cast<CompoundStmt>(S)) {
                ScannedForGotoLoops.insert(C);
                Open.push_back(C);
                Work.push_back({C, true});
            }
            if (auto *L = dyn_cast<LabelStmt>(S)) LabelEnclosing[L->getDecl()] = Open;
            if (auto *A = dyn_cast<AddrLabelExpr>(S)) AddressTakenLabels.insert(A->getLabel());
            if (auto *G = dyn_cast<GotoStmt>(S)) {
                auto It = LabelEnclosing.find(G->getLabel());
                if (It != LabelEnclosing.end()) {
                    const Enclosing &AtLabel = It->second;
                    size_t Common = 0;
                    while (Common < AtLabel.size() && Common < Open.size() &&
                           AtLabel[Common] == Open[Common])
                        ++Common;
                    if (Common) {
                        auto &Labels = GotoLoops[AtLabel[Common - 1]];
                        if (!llvm::is_contained(Labels, G->getLabel())) Labels.push_back(G->getLabel());
                    }
                }
            }
            llvm::SmallVector<const Stmt *, 8> Children(S->children());
            for (const Stmt *Child : llvm::reverse(Children)) Work.push_back({Child, false});
        }
    }

    // A goto loop runs like a loop (traverseLoop): each label's head state
    // joins the state reaching it with those of the gotos back to it, the
    // statement is explored until they stop changing, then traversed once
    // more to report.
    bool traverseGotoLoop(CompoundStmt *S, llvm::ArrayRef<const LabelDecl *> Labels) {
        IteratingGotoLoops.insert(S);
        ProgramState Entry = State;
        for (const LabelDecl *L : Labels) BackEdges[L] = unreachableState();
        // One pass from Entry with the current back edges: each label's
        // gotos back to it, joined into the back edge (Widen) or replacing it
        auto explore = [&](bool Join, bool Widen) {
            bool Changed = false;
            LoopExploration Quiet(*this);
            State = Entry;
            TraverseCompoundStmt(S);
            for (const LabelDecl *L : Labels) {
                auto It = GotoStates.find(L);
                ProgramState Back = It == GotoStates.end() ? unreachableState() : std::move(It->second);
                ProgramState Next = Join ? joinStates(BackEdges[L], Back, Widen) : std::move(Back);
                if (!sameState(Next, BackEdges[L])) Changed = true;
                BackEdges[L] = std::move(Next);
            }
            return Changed;
        };
        bool Changed = true, Widened = false;
        for (unsigned Pass = 0; Pass < MaxLoopPasses && Changed; ++Pass) {
            Widened |= Pass >= 2;
            Changed = explore(/*Join=*/true, /*Widen=*/Pass >= 2);
        }
        if (Changed)
            for (const LabelDecl *L : Labels) forgetValues(BackEdges[L]);
        else if (Widened)
            explore(/*Join=*/false, /*Widen=*/false);  // narrowing, as in traverseLoop
        State = std::move(Entry);
        bool Result = TraverseCompoundStmt(S);
        // The gotos back to the labels were joined in at the labels
        for (const LabelDecl *L : Labels) {
            GotoStates.erase(L);
            BackEdges.erase(L);
        }
        IteratingGotoLoops.erase(S);
        return Result;
    }

    // Each arm of ?: starts from the state the condition left on that arm;
    // the state after the expression joins them.
    bool TraverseConditionalOperator(ConditionalOperator *CO) {
        if (!WalkUpFromConditionalOperator(CO)) return false;
        ConditionStates Outcomes = traverseCondition(CO->getCond());
        State = std::move(Outcomes.True);
        TraverseStmt(CO->getTrueExpr());
        ProgramState AfterTrue = std::move(State);
        State = std::move(Outcomes.False);
        TraverseStmt(CO->getFalseExpr());
        State = joinStates(AfterTrue, State);
        return true;
    }

    // a ?: b evaluates b only when a is false.
    bool TraverseBinaryConditionalOperator(BinaryConditionalOperator *CO) {
        if (!WalkUpFromBinaryConditionalOperator(CO)) return false;
        TraverseStmt(CO->getCommon());
        ProgramState Before = State;
        TraverseStmt(CO->getFalseExpr());
        State = joinStates(Before, State);
        return true;
    }

    // The right operand of && and || runs on one path only, knowing the left
    // operand's value (traverseCondition). Other operators keep
    // RecursiveASTVisitor's data recursion.
    bool TraverseBinaryOperator(BinaryOperator *B, DataRecursionQueue *Queue = nullptr) {
        if (!B->isLogicalOp()) return RecursiveASTVisitor::TraverseBinaryOperator(B, Queue);
        ConditionStates Outcomes = traverseCondition(B);
        State = joinStates(Outcomes.True, Outcomes.False);
        return true;
    }

    // A handler starts from the join of the states at each throw and each
    // call in the try block that may throw.
    bool TraverseCXXTryStmt(CXXTryStmt *Try) {
        ProgramState Thrown = unreachableState();
        HandlerTargets.push_back(&Thrown);
        TraverseStmt(Try->getTryBlock());
        HandlerTargets.pop_back();
        ProgramState After = std::move(State);
        for (unsigned I = 0; I < Try->getNumHandlers(); ++I) {
            State = Thrown;
            TraverseStmt(Try->getHandler(I));
            After = joinStates(After, State);
        }
        State = std::move(After);
        return true;
    }

    // Whether a loop condition is always true (for (;;), while (1)).
    bool isAlwaysTrue(Expr *Cond) {
        bool Value = false;
        return !Cond || (!Cond->isValueDependent() &&
                         Cond->EvaluateAsBooleanCondition(Value, Ctx) && Value);
    }

    // The parts of a loop, in the order one pass evaluates them: the
    // condition first (or after the body, for do), then the per-iteration
    // declaration (range-for), the body and the increment.
    struct LoopParts {
        llvm::SmallVector<Stmt *, 4> Inits;  // run once, before the loop
        Stmt *CondVar = nullptr;             // while (T x = ...): declared each pass
        Expr *Cond = nullptr;
        Stmt *PerPass = nullptr;
        Stmt *Body = nullptr;
        Expr *Inc = nullptr;
        bool BodyFirst = false;
        bool AlwaysTrue = false;
        bool AlwaysFalse = false;
    };

    // Whether a loop condition is always false (do ... while (0)).
    bool isAlwaysFalse(Expr *Cond) {
        bool Value = true;
        return Cond && !Cond->isValueDependent() &&
               Cond->EvaluateAsBooleanCondition(Value, Ctx) && !Value;
    }

    // What a loop's shape guarantees at the start of each pass of its body,
    // beyond what its condition says: the loop variable's range from its
    // initialization, condition and step. It holds only while the body does
    // not write the variable.
    struct LoopInvariant {
        std::string Var;
        llvm::SmallVector<std::pair<ProofOp, int64_t>, 3> VarBounds;  // Var op c
        // Var BoundOp BoundObj.BoundField (BoundObj empty: a variable)
        bool HasFieldBound = false;
        std::string BoundObj, BoundField;
        ProofOp BoundOp = ProofOp::LT;
        const Expr *SyntheticBound = nullptr;  // computes BoundField (__loop_bound_N)
        // Pointers the body steps once per pass, with their offsets at the
        // start of a pass in terms of Var
        llvm::SmallVector<std::pair<std::string, SymValue>, 2> SteppedPointers;
    };

    // While a loop is iterated to its fixpoint its body is traversed several
    // times; only the last pass reports. Exploration passes emit no
    // diagnostics and record nothing that outlives them.
    struct LoopExploration {
        DTTVisitor &V;
        bool WasSuppressed, WasExploring;
        llvm::DenseMap<const CallExpr *, bool> Requirements;
        llvm::DenseSet<WarningKey> WarnedLost;
        llvm::StringMap<NullProvenance> NullProvenances;
        llvm::StringMap<std::unique_ptr<StateProvenance>> Provenance;
        llvm::SmallVector<dtt::InferredPrecondition, 4> Preconditions;
        llvm::SmallVector<dtt::InferredPostcondition, 4> Postconditions;
        llvm::DenseMap<const LabelDecl *, ProgramState> Gotos;

        explicit LoopExploration(DTTVisitor &V)
            : V(V), WasSuppressed(V.Diag.getSuppressAllDiagnostics()),
              WasExploring(std::exchange(V.ExploringLoop, true)),
              Requirements(CallRequirements), WarnedLost(WarnedLostTracking),
              NullProvenances(NullProvenanceMap), Provenance(std::move(ProvenanceChains)),
              Preconditions(CurrentFnPreconditions),
              Postconditions(CurrentInferredPostconditions), Gotos(V.GotoStates) {
            V.Diag.setSuppressAllDiagnostics(true);
        }
        ~LoopExploration() {
            V.Diag.setSuppressAllDiagnostics(WasSuppressed);
            V.ExploringLoop = WasExploring;
            CallRequirements = std::move(Requirements);
            WarnedLostTracking = std::move(WarnedLost);
            NullProvenanceMap = std::move(NullProvenances);
            ProvenanceChains = std::move(Provenance);
            CurrentFnPreconditions = std::move(Preconditions);
            CurrentInferredPostconditions = std::move(Postconditions);
            V.GotoStates = std::move(Gotos);
        }
    };

    // Widening makes a loop head settle; a head still changing after this many
    // passes forgets what it knows (forgetValues).
    static constexpr unsigned MaxLoopPasses = 32;

    // A loop head that did not settle: no value it holds, and no pointer's
    // lifetime, is known to hold on every iteration.
    static void forgetValues(ProgramState &S) {
        llvm::SmallVector<FieldPath, 16> ProofEntries;
        for (auto &[Path, Value] : S.TypedEnv) {
            if (isProofEntry(Path)) ProofEntries.push_back(Path);
            else Value = SymValue::Unknown();
        }
        for (const FieldPath &Path : ProofEntries) S.TypedEnv.erase(Path);
        S.GlobalProofs.clear();
        S.VarBoundProofs.clear();
        for (auto &E : S.PointerLifetimes) E.getValue().lifetime = PointerLifetime::MaybeFreed;
    }
    // In an exploration pass: checks that only report can be skipped.
    bool ExploringLoop = false;


    // A loop runs its body zero or more times. Its head state joins the state
    // on entry with the state at the end of every pass; the body is explored
    // from the head until the head stops changing, then traversed once more
    // to report. The state after the loop joins the paths that leave it: the
    // condition failing, and break.
    void traverseLoop(const LoopParts &L,
                      llvm::function_ref<std::optional<LoopInvariant>()> MakeInvariant) {
        for (Stmt *Init : L.Inits) TraverseStmt(Init);
        std::optional<LoopInvariant> Inv = MakeInvariant();
        const ProgramState Entry = State;
        ProgramState Head = State;
        // Inside an outer loop's exploration nothing is reported, so the
        // exits and gotos of the pass that settles the head are this loop's.
        const bool Reports = !ExploringLoop;
        ProgramState Exits = unreachableState();
        llvm::DenseMap<const LabelDecl *, ProgramState> PassGotos;
        // A body that runs at most once (do ... while (0), while (0)) needs
        // no fixpoint.
        bool Settled = L.AlwaysFalse, Widened = false;
        for (unsigned Pass = 0; Pass < MaxLoopPasses && !Settled; ++Pass) {
            ProgramState Back;
            Exits = unreachableState();
            {
                LoopExploration Quiet(*this);
                Back = loopPass(L, Inv, Head, Reports ? nullptr : &Exits);
                if (!Reports) PassGotos = GotoStates;
            }
            // Ranges widen from the third pass on: two passes let a bounded
            // update (head = (head + 1) % N) settle, a counter's does not.
            bool Widen = Pass >= 2;
            Widened |= Widen;
            ProgramState Next = joinStates(Head, Back, Widen);
            Settled = sameState(Next, Head);
            Head = std::move(Next);
        }
        if (!Reports && Settled && !L.AlwaysFalse) {
            State = std::move(Exits);
            GotoStates = std::move(PassGotos);
            return;
        }
        if (!Settled) {
            forgetValues(Head);
        } else if (Widened) {
            // Narrowing: the settled head passed through the body once more
            // and joined with the entry keeps the bounds every pass
            // re-establishes (a do-while's condition), which widening dropped.
            LoopExploration Quiet(*this);
            Head = joinStates(Entry, loopPass(L, Inv, Head, nullptr));
        }
        Exits = unreachableState();
        loopPass(L, Inv, std::move(Head), &Exits);
        State = std::move(Exits);
    }

    // One pass of loop L from state Head. Returns the state that goes back to
    // the head; joins the states that leave the loop into Exits, if given.
    ProgramState loopPass(const LoopParts &L, const std::optional<LoopInvariant> &Inv,
                          ProgramState Head, ProgramState *Exits) {
        State = std::move(Head);
        ProgramState Breaks = unreachableState(), Continues = unreachableState();
        BreakTargets.push_back(&Breaks);
        ContinueTargets.push_back(&Continues);
        auto evaluateCondition = [&] {
            if (L.CondVar) traverseIfReachable(L.CondVar);
            if (!L.Cond || State.Unreachable) return;
            ConditionStates Outcomes = traverseCondition(L.Cond);
            if (Exits && !L.AlwaysTrue) *Exits = joinStates(*Exits, Outcomes.False);
            State = std::move(Outcomes.True);
            if (L.AlwaysFalse) State.Unreachable = true;
        };
        if (!L.BodyFirst) evaluateCondition();
        if (Inv && !State.Unreachable) applyLoopInvariant(*Inv);
        traverseIfReachable(L.PerPass);
        traverseIfReachable(L.Body);
        State = joinStates(State, Continues);
        if (Inv && !State.Unreachable) dropLoopInvariant(*Inv);
        traverseIfReachable(L.Inc);
        if (L.BodyFirst) evaluateCondition();
        ContinueTargets.pop_back();
        BreakTargets.pop_back();
        if (Exits) *Exits = joinStates(*Exits, Breaks);
        return std::move(State);
    }

    void applyLoopInvariant(const LoopInvariant &I) {
        // Inside the body the variable is symbolic, bounded by the facts
        eraseEnvValue(I.Var);
        FieldPath VarBase = FieldPath::fromSynthetic(I.Var);
        for (const auto &[Op, Value] : I.VarBounds) addProofTyped(VarBase, Proof{"_value", Op, Value});
        // The bound's value as the condition just read it
        if (I.SyntheticBound) setEnvValue(I.BoundField, exprToSymValueImpl(I.SyntheticBound, Ctx));
        if (I.HasFieldBound) addVarBoundProof(I.Var, I.BoundObj, I.BoundField, I.BoundOp);
        for (const auto &[Ptr, Offset] : I.SteppedPointers)
            setFieldTyped(FieldPath::fromSynthetic(Ptr), "_offset", Offset);
    }

    // Before the increment: the facts were about this pass's value.
    void dropLoopInvariant(const LoopInvariant &I) {
        forgetScalarValue(FieldPath::fromSynthetic(I.Var));
        if (I.SyntheticBound) eraseEnvValue(I.BoundField);
    }

    // Operand, an integer variable or field, changes by Delta (by an unknown
    // amount without one): a variable's value and bounds shift; a field, or
    // an unsigned variable that may go below zero and wrap, becomes unknown.
    void stepScalar(const Expr *Operand, std::optional<int64_t> Delta) {
        const Expr *E = Operand->IgnoreParenImpCasts();
        if (auto *ME = dyn_cast<MemberExpr>(E)) {
            FieldPath Base = getTypedPath(ME->getBase());
            if (!Base.isValid() || !ME->getMemberDecl()->getIdentifier()) return;
            rebindValuesNaming(getTypedPath(ME).toStringRef(), SymValue::Unknown());
            setFieldTyped(Base, ME->getMemberDecl()->getName(), SymValue::Unknown());
            return;
        }
        FieldPath Path = getTypedPath(E);
        if (!Path.isValid()) return;
        if (Delta && *Delta < 0 && E->getType()->isUnsignedIntegerType()) {
            ValueRange R = scalarRange(State, Path);
            if (!R.Lo || *R.Lo < -*Delta) Delta.reset();
        }
        if (Delta) shiftScalarValue(Path, *Delta);
        else forgetScalarValue(Path);
    }

    // Whether E names variable Var.
    static bool refersToVariable(const Expr *E, llvm::StringRef Var) {
        auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
        return DRE && variableKey(DRE->getDecl()) == Var;
    }

    // Whether node S itself lets Var be written through a reference to it:
    // &Var, Var bound to a non-const reference (a call argument, the object
    // of a non-const member operator, a reference variable), or a lambda
    // capturing Var by reference.
    static bool bindsReference(const Stmt *S, llvm::StringRef Var) {
        auto isVar = [&](const Expr *E) { return refersToVariable(E, Var); };
        if (auto *U = dyn_cast<UnaryOperator>(S); U && U->getOpcode() == UO_AddrOf && isVar(U->getSubExpr()))
            return true;
        if (auto *Call = dyn_cast<CallExpr>(S)) {
            bool Bound = false;
            if (auto *FD = Call->getDirectCallee())
                forEachCallArgument(Call, FD, [&](const Expr *Arg, QualType Type) {
                    bool ByReference = Type.isNull() ? !cast<CXXMethodDecl>(FD)->isConst()
                                                     : isModifiableRef(Type);
                    Bound |= ByReference && isVar(Arg);
                });
            return Bound;
        }
        if (auto *DS = dyn_cast<DeclStmt>(S))
            return llvm::any_of(DS->decls(), [&](const Decl *D) {
                auto *VD = dyn_cast<VarDecl>(D);
                return VD && VD->getType()->isReferenceType() &&
                       !VD->getType()->getPointeeType().isConstQualified() && VD->getInit() &&
                       isVar(VD->getInit());
            });
        if (auto *L = dyn_cast<LambdaExpr>(S))
            return llvm::any_of(L->captures(), [&](const LambdaCapture &C) {
                return C.capturesVariable() && C.getCaptureKind() == LCK_ByRef &&
                       variableKey(C.getCapturedVar()) == Var;
            });
        return false;
    }

    // Whether S may write variable Var: assignment, ++/--, or a reference
    // to it (bindsReference).
    static bool mayWriteVariable(const Stmt *S, llvm::StringRef Var) {
        if (!S) return false;
        auto isVar = [&](const Expr *E) { return refersToVariable(E, Var); };
        if (auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp() && isVar(B->getLHS()))
            return true;
        if (auto *U = dyn_cast<UnaryOperator>(S); U && U->isIncrementDecrementOp() && isVar(U->getSubExpr()))
            return true;
        if (bindsReference(S, Var)) return true;
        return llvm::any_of(S->children(), [&](const Stmt *C) { return mayWriteVariable(C, Var); });
    }

    // Whether a reference to Var may be taken anywhere in S, so that writes
    // through it are not seen as writes to Var.
    static bool referenceMayEscape(const Stmt *S, llvm::StringRef Var) {
        if (!S) return false;
        return bindsReference(S, Var) ||
               llvm::any_of(S->children(), [&](const Stmt *C) { return referenceMayEscape(C, Var); });
    }

    bool TraverseDoStmt(DoStmt *Do) {
        if (!WalkUpFromDoStmt(Do)) return false;
        LoopParts L;
        L.Cond = Do->getCond();
        L.Body = Do->getBody();
        L.BodyFirst = true;
        L.AlwaysTrue = isAlwaysTrue(Do->getCond());
        L.AlwaysFalse = isAlwaysFalse(Do->getCond());
        traverseLoop(L, [] { return std::optional<LoopInvariant>(); });
        return true;
    }

    bool TraverseCXXForRangeStmt(CXXForRangeStmt *For) {
        if (!WalkUpFromCXXForRangeStmt(For)) return false;
        LoopParts L;
        for (Stmt *S : {For->getInit(), static_cast<Stmt *>(For->getRangeStmt()),
                        static_cast<Stmt *>(For->getBeginStmt()), static_cast<Stmt *>(For->getEndStmt())})
            if (S) L.Inits.push_back(S);
        L.Cond = For->getCond();
        L.PerPass = For->getLoopVarStmt();
        L.Body = For->getBody();
        L.Inc = For->getInc();
        traverseLoop(L, [] { return std::optional<LoopInvariant>(); });
        return true;
    }

    bool TraverseWhileStmt(WhileStmt *While) {
        if (!WalkUpFromWhileStmt(While)) return false;
        LoopParts L;
        L.CondVar = While->getConditionVariableDeclStmt();
        L.Cond = While->getCond();
        L.Body = While->getBody();
        L.AlwaysTrue = isAlwaysTrue(While->getCond());
        L.AlwaysFalse = isAlwaysFalse(While->getCond());
        traverseLoop(L, [] { return std::optional<LoopInvariant>(); });
        return true;
    }

    bool TraverseForStmt(ForStmt *For) {
        if (!WalkUpFromForStmt(For)) return false;
        LoopParts L;
        if (For->getInit()) L.Inits.push_back(For->getInit());
        L.CondVar = For->getConditionVariableDeclStmt();
        L.Cond = For->getCond();
        L.Body = For->getBody();
        L.Inc = For->getInc();
        L.AlwaysTrue = isAlwaysTrue(For->getCond());
        L.AlwaysFalse = isAlwaysFalse(For->getCond());
        traverseLoop(L, [&] { return forLoopInvariant(For); });
        return true;
    }

    // The innermost enclosing switch: the state before its body, for each case
    // label to start from.
    struct SwitchContext {
        SwitchStmt *Switch;
        ProgramState Before;
        // The tracked field the condition reads, if any
        std::optional<std::pair<FieldPath, llvm::StringRef>> Subject;
    };

    // Where jumps send the current state: break to the innermost loop or
    // switch, continue to the innermost loop, throw (and statements that may
    // throw) to the innermost try block's handlers, goto to its label.
    llvm::SmallVector<ProgramState *, 4> BreakTargets, ContinueTargets, HandlerTargets;
    llvm::DenseMap<const LabelDecl *, ProgramState> GotoStates;
    llvm::SmallVector<SwitchContext *, 2> Switches;
    bool InIfCondition = false;
    // Block depth of each local variable's declaration
    llvm::StringMap<unsigned> DeclDepth;

    // The per-function analysis state: the visitor's and the inferred
    // contracts being collected.
    struct FunctionContext {
        FunctionDecl *Function;
        ProgramState State;
        llvm::DenseMap<const LabelDecl *, ProgramState> GotoStates;
        llvm::SmallVector<ProgramState *, 4> BreakTargets, ContinueTargets, HandlerTargets;
        llvm::SmallVector<SwitchContext *, 2> Switches;
        llvm::StringMap<unsigned> DeclDepth;
        unsigned ScopeDepth;
        const FunctionDecl *PreconditionsFn, *PostconditionsFn;
        llvm::SmallVector<dtt::InferredPrecondition, 4> Preconditions;
        llvm::SmallVector<dtt::InferredPostcondition, 4> Postconditions;
        const CXXRecordDecl *MethodClass;
        llvm::DenseMap<const CallExpr *, bool> Requirements;
        llvm::DenseSet<WarningKey> WarnedLost;
        llvm::StringMap<NullProvenance> NullProvenances;
        llvm::StringMap<std::unique_ptr<StateProvenance>> Provenance;
    };

    FunctionContext saveFunctionContext() {
        return {CurrentFunctionDecl, std::move(State), std::move(GotoStates),
                std::move(BreakTargets), std::move(ContinueTargets), std::move(HandlerTargets),
                std::move(Switches), std::move(DeclDepth), CurrentScopeDepth,
                CurrentFnForPreconditions, CurrentFnForPostconditions,
                std::move(CurrentFnPreconditions), std::move(CurrentInferredPostconditions),
                CurrentMethodClass, std::move(CallRequirements), std::move(WarnedLostTracking),
                std::move(NullProvenanceMap), std::move(ProvenanceChains)};
    }

    void restoreFunctionContext(FunctionContext C) {
        CurrentFunctionDecl = C.Function;
        State = std::move(C.State);
        GotoStates = std::move(C.GotoStates);
        BreakTargets = std::move(C.BreakTargets);
        ContinueTargets = std::move(C.ContinueTargets);
        HandlerTargets = std::move(C.HandlerTargets);
        Switches = std::move(C.Switches);
        DeclDepth = std::move(C.DeclDepth);
        CurrentScopeDepth = C.ScopeDepth;
        CurrentFnForPreconditions = C.PreconditionsFn;
        CurrentFnForPostconditions = C.PostconditionsFn;
        CurrentFnPreconditions = std::move(C.Preconditions);
        CurrentInferredPostconditions = std::move(C.Postconditions);
        CurrentMethodClass = C.MethodClass;
        CallRequirements = std::move(C.Requirements);
        WarnedLostTracking = std::move(C.WarnedLost);
        NullProvenanceMap = std::move(C.NullProvenances);
        ProvenanceChains = std::move(C.Provenance);
    }

    bool TraverseSwitchStmt(SwitchStmt *S) {
        if (S->getInit()) TraverseStmt(S->getInit());
        if (S->getConditionVariableDeclStmt()) TraverseStmt(S->getConditionVariableDeclStmt());
        TraverseStmt(S->getCond());
        WalkUpFromSwitchStmt(S);

        SwitchContext Context{S, State, switchSubject(S->getCond())};
        ProgramState Breaks = unreachableState();
        Switches.push_back(&Context);
        BreakTargets.push_back(&Breaks);
        State.Unreachable = true;  // Code before the first case label is not reached
        TraverseStmt(S->getBody());
        BreakTargets.pop_back();
        Switches.pop_back();

        State = joinStates(State, Breaks);
        if (!hasDefaultCase(S)) {
            ProgramState Fallthrough = std::move(State);
            State = std::move(Context.Before);
            assumeSwitchCase(Context, nullptr);
            State = joinStates(Fallthrough, State);
        }
        return true;
    }

    // Entering case Label of the innermost switch: falling through from the
    // case above, or jumping from the switch with the condition matching.
    void enterSwitchCase(SwitchCase *Label) {
        if (Switches.empty()) return;
        ProgramState Fallthrough = std::move(State);
        State = Switches.back()->Before;
        assumeSwitchCase(*Switches.back(), Label);
        State = joinStates(Fallthrough, State);
    }

    bool TraverseCaseStmt(CaseStmt *C) {
        enterSwitchCase(C);
        return traverseIfReachable(C->getSubStmt());
    }

    bool TraverseDefaultStmt(DefaultStmt *D) {
        enterSwitchCase(D);
        return traverseIfReachable(D->getSubStmt());
    }

    // The tracked field a switch condition reads: a variable or field
    // (_value), or v.index() of a std::variant (_variant_index).
    std::optional<std::pair<FieldPath, llvm::StringRef>> switchSubject(Expr *Cond) {
        Expr *E = Cond->IgnoreParenImpCasts();
        if (auto *Call = dyn_cast<CXXMemberCallExpr>(E)) {
            auto *M = Call->getMethodDecl();
            auto *RD = M ? M->getParent() : nullptr;
            if (!M || !M->getIdentifier() || M->getName() != "index" || Call->getNumArgs() ||
                !RD || !RD->getIdentifier() || RD->getName() != "variant" || !isInStdNamespace(RD))
                return std::nullopt;
            FieldPath Path = getTypedPath(Call->getImplicitObjectArgument()->IgnoreParenImpCasts());
            if (!Path.isValid()) return std::nullopt;
            return std::make_pair(Path, llvm::StringRef("_variant_index"));
        }
        if (!isa<DeclRefExpr, MemberExpr>(E) || !E->getType()->isIntegralOrEnumerationType())
            return std::nullopt;
        FieldPath Path = getTypedPath(E);
        if (!Path.isValid()) return std::nullopt;
        return std::make_pair(Path, llvm::StringRef("_value"));
    }

    static bool hasDefaultCase(const SwitchStmt *S) {
        for (const SwitchCase *SC = S->getSwitchCaseList(); SC; SC = SC->getNextSwitchCase())
            if (isa<DefaultStmt>(SC)) return true;
        return false;
    }

    // What reaching Label says about the switch condition: it equals the
    // case value (or lies in the case range); for the default label, or no
    // label when no case matches, it differs from every case value. The
    // condition then holds that value on this path, even where an earlier
    // known value disagreed: values are not exact (loops, aliases), so a
    // disagreement does not prove the path impossible.
    void assumeSwitchCase(const SwitchContext &Context, SwitchCase *Label) {
        if (!Context.Subject) return;
        const FieldPath &Subject = Context.Subject->first;
        llvm::StringRef Field = Context.Subject->second;
        llvm::StringRef Path = Subject.toStringRef();
        auto valueOf = [&](const Expr *E) -> std::optional<int64_t> {
            Expr::EvalResult R;
            if (!E || !safeEvaluateAsInt(E, R, Ctx)) return std::nullopt;
            return safeGetInt64(R.Val.getInt());
        };
        // [Lo, Hi] of a case label
        auto rangeOf = [&](const CaseStmt *Case) -> std::optional<std::pair<int64_t, int64_t>> {
            auto Lo = valueOf(Case->getLHS());
            if (!Lo) return std::nullopt;
            auto Hi = Case->caseStmtIsGNURange() ? valueOf(Case->getRHS()) : Lo;
            if (!Hi) return std::nullopt;
            return std::make_pair(*Lo, *Hi);
        };
        // A scalar's value is stored at its own path, not under _value.
        // Forgetting it leaves the condition symbolic, bounded by its proofs.
        FieldPath ValuePath = fieldStorage(Subject, Field);
        auto setValue = [&](SymValue V) { setEnvValueTyped(ValuePath, V); };
        auto forgetValue = [&] { eraseEnvValueTyped(ValuePath); };

        if (auto *Case = dyn_cast_or_null<CaseStmt>(Label)) {
            auto R = rangeOf(Case);
            if (!R) return;
            if (R->first == R->second) {
                setValue(SymValue::Const(R->first));
                addProof(Path, buildProofKey(Field, ProofOp::EQ, R->first));
                return;
            }
            forgetValue();
            addProof(Path, buildProofKey(Field, ProofOp::GE, R->first));
            addProof(Path, buildProofKey(Field, ProofOp::LE, R->second));
            return;
        }
        SymValue Known = getEnvValueTyped(ValuePath).value_or(SymValue::Unknown());
        for (const SwitchCase *SC = Context.Switch->getSwitchCaseList(); SC;
             SC = SC->getNextSwitchCase()) {
            auto *Case = dyn_cast<CaseStmt>(SC);
            auto R = Case ? rangeOf(Case) : std::nullopt;
            if (!R) continue;
            if (Known.kind == SymValue::CONST && Known.constVal >= R->first &&
                Known.constVal <= R->second)
                forgetValue();
            if (R->first == R->second)
                addProof(Path, buildProofKey(Field, ProofOp::NE, R->first));
        }
    }

    // Undocumented heuristics kept as they were (docs/HEURISTICS.md lists
    // neither): after `if (!arr) return;` every element of arr is non-null,
    // and after `if (len < 1) return;` a pointer named like len is non-null.
    void applyEarlyExitConventions(const GuardInfo &G) {
        // Array element propagation: if arr != NULL is proven (for pointer-to-pointer),
        // also mark arr[?]._nonnull = 1 (trust array elements are valid)
        if (G.negProofKey == "_nonnull!=0") {
            FieldPath elemPath = FieldPath::fromSynthetic(G.basePath + "[?]");
            setFieldTyped(elemPath, "_nonnull", SymValue::Const(1));
        }
        // Size-parameter to pointer-parameter inference:
        // If size >= N (N > 0) is proven, associated pointer is non-null.
        // Common convention: buffer/buffer_len, data/data_size, ptr/ptr_length
        if (G.field == "_value") {
            // Check if negProofKey indicates size >= N for some N > 0
            // negProofKey format: "_value>=N" or "_value>N"
            const std::string& negKey = G.negProofKey;
            bool sizeIsPositive = false;
            if (negKey.find("_value>=") == 0) {
                int64_t n = std::stoll(negKey.substr(8));
                sizeIsPositive = (n > 0);
            } else if (negKey.find("_value>") == 0) {
                int64_t n = std::stoll(negKey.substr(7));
                sizeIsPositive = (n >= 0);  // > 0 means >= 1
            }
            if (sizeIsPositive) {
                // Try to find associated pointer parameter
                std::string sizeName = G.basePath;
                std::string ptrName;
                // Remove common suffixes to get pointer name
                static const char* suffixes[] = {"_len", "_size", "_length", "Len", "Size", "Length", "_sz", "_count"};
                for (const char* suffix : suffixes) {
                    size_t suffixLen = strlen(suffix);
                    if (sizeName.size() > suffixLen &&
                        sizeName.substr(sizeName.size() - suffixLen) == suffix) {
                        ptrName = sizeName.substr(0, sizeName.size() - suffixLen);
                        break;
                    }
                }
                // Also handle standalone "len", "size", "length" paired with common pointer names
                if (ptrName.empty() && (sizeName == "len" || sizeName == "size" || sizeName == "length" ||
                                        sizeName == "n" || sizeName == "count" || sizeName == "sz")) {
                    // Look for common pointer parameter names in current function
                    if (CurrentFunctionDecl) {
                        for (const auto *Param : CurrentFunctionDecl->parameters()) {
                            if (Param->getType()->isPointerType()) {
                                std::string paramName = Param->getNameAsString();
                                // Take first pointer parameter as associated
                                if (paramName == "ptr" || paramName == "data" || paramName == "buffer" ||
                                    paramName == "buf" || paramName == "str" || paramName == "s" ||
                                    paramName == "p" || paramName == "src" || paramName == "dst") {
                                    ptrName = paramName;
                                    break;
                                }
                            }
                        }
                    }
                }
                // Mark associated pointer as non-null
                if (!ptrName.empty()) {
                    FieldPath ptrPath = FieldPath::fromSynthetic(ptrName);
                    setFieldTyped(ptrPath, "_nonnull", SymValue::Const(1));
                    addProof(ptrName, "_nonnull!=0");
                }
            }
        }
    }

    // The states after a condition, when it is true and when it is false.
    struct ConditionStates {
        ProgramState True, False;
    };

    // Traverse condition Cond, which starts from the current state: its
    // operands run in order, each knowing what the operands before it said
    // (a && b runs b only when a is true). Each guard DTT recognizes adds its
    // facts where it is evaluated, so later writes are not overridden.
    ConditionStates traverseCondition(Expr *Cond) {
        Expr *E = Cond->IgnoreParens();
        // Same truth value: full-expressions (temporaries' cleanups),
        // implicit casts, conversions to bool, __builtin_expect (likely,
        // unlikely).
        if (auto *Full = dyn_cast<FullExpr>(E))
            return traverseCondition(Full->getSubExpr());
        if (auto *Cast = dyn_cast<ImplicitCastExpr>(E))
            return traverseCondition(Cast->getSubExpr());
        if (auto *Cast = dyn_cast<ExplicitCastExpr>(E); Cast && Cast->getType()->isBooleanType())
            return traverseCondition(Cast->getSubExpr());
        if (auto *Call = dyn_cast<CallExpr>(E);
            Call && Call->getBuiltinCallee() == Builtin::BI__builtin_expect) {
            WalkUpFromCallExpr(Call);
            TraverseStmt(Call->getArg(1));
            return traverseCondition(Call->getArg(0));
        }
        if (auto *U = dyn_cast<UnaryOperator>(E); U && U->getOpcode() == UO_LNot) {
            WalkUpFromUnaryOperator(U);
            ConditionStates Sub = traverseCondition(U->getSubExpr());
            return {std::move(Sub.False), std::move(Sub.True)};
        }
        if (auto *B = dyn_cast<BinaryOperator>(E)) {
            if (B->getOpcode() == BO_Comma) {
                TraverseStmt(B->getLHS());
                WalkUpFromBinaryOperator(B);
                return traverseCondition(B->getRHS());
            }
            if (B->isLogicalOp()) {
                // a && b && c groups as (a && b) && c: walk the left spine
                // without recursion.
                bool IsAnd = B->getOpcode() == BO_LAnd;
                llvm::SmallVector<BinaryOperator *, 4> Chain{B};
                while (auto *L = dyn_cast<BinaryOperator>(Chain.back()->getLHS()->IgnoreParens())) {
                    if (L->getOpcode() != B->getOpcode()) break;
                    Chain.push_back(L);
                }
                ConditionStates Result = traverseCondition(Chain.back()->getLHS());
                for (BinaryOperator *Op : llvm::reverse(Chain)) {
                    // Short-circuit: false for &&, true for ||
                    ProgramState &Stop = IsAnd ? Result.False : Result.True;
                    State = std::move(IsAnd ? Result.True : Result.False);
                    // Visited once its left operand has run (uninitialized reads)
                    WalkUpFromBinaryOperator(Op);
                    ConditionStates Rhs = traverseCondition(Op->getRHS());
                    if (IsAnd) Result = {std::move(Rhs.True), joinStates(Stop, Rhs.False)};
                    else Result = {joinStates(Stop, Rhs.True), std::move(Rhs.False)};
                }
                return Result;
            }
        }
        // A leaf: evaluate it, then apply its facts on each outcome.
        TraverseStmt(Cond);
        auto Facts = conditionFacts(E);
        ProgramState False = State;
        if (Facts) Facts(true);
        ConditionStates Result{std::move(State), std::move(False)};
        if (Facts) {
            State = std::move(Result.False);
            Facts(false);
            Result.False = std::move(State);
        }
        return Result;
    }

    // What a single guard says when it is true or false, as a function that
    // adds those facts to the current state; empty for an expression DTT does
    // not recognize as a guard. The recognizers run in this order: variable
    // bound against a field, comparison or null check, annotated bool call,
    // holds_alternative, any::type.
    std::function<void(bool)> conditionFacts(Expr *Leaf) {
        // (r = e) as a condition tests r once assigned
        if (auto *A = dyn_cast<BinaryOperator>(Leaf->IgnoreParenImpCasts());
            A && A->getOpcode() == BO_Assign)
            return conditionFacts(A->getLHS()->IgnoreParenImpCasts());
        if (auto VG = extractVarGuard(Leaf))
            return [this, VG = *VG](bool Truth) {
                if (auto Op = Truth ? VG.whenTrue() : VG.whenFalse())
                    addVarBoundProof(VG.varName, VG.objPath, VG.field, *Op);
            };
        if (auto G = extractGuard(Leaf)) {
            // Comparing an uninitialized pointer is UB even with a null check
            if (G->field == "_nonnull") checkUninitializedPointer(G->basePath, Leaf->getBeginLoc());
            return [this, G = *G](bool Truth) {
                if (Truth) {
                    addProof(G.basePath, G.proofKey);
                } else if (!G.extraNegByConvention || InIfCondition) {
                    // The `_` field convention (X._f == 0 false gives _f > 0)
                    // applies to if conditions only, as before; elsewhere the
                    // false side of such a guard stays unknown, as before
                    // (an undocumented heuristic, step 6).
                    addProof(G.basePath, G.negProofKey);
                    addProof(G.basePath, G.extraNegProofKey);
                }
            };
        }
        if (auto boolCall = extractBoolCall(Leaf)) {
            llvm::SmallVectorImpl<BoolPostcondition>* postconds = nullptr;
            if (boolCall->isMethod) {
                MethodKey key{boolCall->className, boolCall->fnName, boolCall->argCount};
                auto it = MethodAnnotations.find(key);
                if (it != MethodAnnotations.end()) postconds = &it->second.boolPostconds;
            } else {
                auto it = FnBoolPostconditions.find(boolCall->fnName);
                if (it != FnBoolPostconditions.end()) postconds = &it->second;
            }
            if (postconds)
                return [this, postconds, Path = boolCall->argPath,
                        Negated = boolCall->negated](bool Truth) {
                    for (const auto& bp : *postconds)
                        if (bp.onTrue == (Truth != Negated))
                            addProof(Path, buildProofKey(bp.field, bp.op, bp.rhs));
                };
        }
        if (auto holdsAlt = extractHoldsAlternative(Leaf))
            return [this, HA = *holdsAlt](bool Truth) {
                addProof(HA.variantPath,
                         buildProofKey("_variant_index", Truth != HA.negated ? ProofOp::EQ : ProofOp::NE,
                                       HA.alternativeIndex));
            };
        if (auto anyTypeCheck = extractAnyTypeCheck(Leaf))
            return [this, AT = *anyTypeCheck](bool Truth) {
                llvm::SmallString<64> Key(Truth != AT.negated ? "_any_type==" : "_any_type!=");
                Key += AT.checkedType.getAsString();
                addProof(AT.anyPath, Key);
            };
        return nullptr;
    }

    bool TraverseIfStmt(IfStmt *If) {
        if (If->getInit()) TraverseStmt(If->getInit());
        if (If->getConditionVariableDeclStmt())
            TraverseStmt(If->getConditionVariableDeclStmt());
        Expr *Cond = If->getCond();
        if (!Cond) {  // if consteval
            ProgramState Else = State;
            traverseBranches(If, State, std::move(Else));
            return true;
        }
        bool OuterIfCondition = std::exchange(InIfCondition, true);
        ConditionStates Outcomes = traverseCondition(Cond);
        InIfCondition = OuterIfCondition;
        // The undocumented early-exit heuristics keep the trigger they had: a
        // single guard from extractAllGuards (even one taken from a longer
        // conjunction), or a disjunction of guards.
        traverseBranches(If, std::move(Outcomes.True), std::move(Outcomes.False), [&] {
            if (extractVarGuard(Cond)) return;
            auto Guards = extractAllGuards(Cond);
            if (Guards.size() == 1) applyEarlyExitConventions(Guards[0]);
            if (!Guards.empty()) return;
            for (const auto& g : extractOrGuards(Cond))
                if (g.negProofKey == "_nonnull!=0")
                    setFieldTyped(FieldPath::fromSynthetic(g.basePath + "[?]"),
                                  "_nonnull", SymValue::Const(1));
        });
        return true;
    }


    // The pointers a loop body steps exactly once per pass: ++p as one of its
    // statements, with no other write to p, and no continue, goto or label
    // that could skip or repeat it.
    llvm::SmallVector<const DeclRefExpr *, 2> pointersSteppedOncePerPass(Stmt *Body) {
        llvm::SmallVector<const DeclRefExpr *, 2> Result;
        auto *Block = dyn_cast_or_null<CompoundStmt>(Body);
        if (!Block || skipsOrRepeats(Block)) return Result;
        for (const Stmt *S : Block->body()) {
            auto *U = dyn_cast<UnaryOperator>(S->IgnoreContainers());
            auto *DRE = U && U->isIncrementOp() ? dyn_cast<DeclRefExpr>(U->getSubExpr()->IgnoreParens())
                                                : nullptr;
            if (!DRE || !DRE->getType()->isPointerType() || !DRE->getDecl()->getIdentifier()) continue;
            llvm::StringRef Name = variableKey(DRE->getDecl());
            bool WrittenElsewhere = llvm::any_of(Block->body(), [&](const Stmt *Other) {
                return Other != S && mayWriteVariable(Other, Name);
            });
            if (!WrittenElsewhere) Result.push_back(DRE);
        }
        return Result;
    }

    // Whether S holds a continue for the enclosing loop, a goto or a label.
    static bool skipsOrRepeats(const Stmt *S) {
        if (!S) return false;
        if (isa<ContinueStmt>(S) || isa<GotoStmt>(S) || isa<IndirectGotoStmt>(S) || isa<LabelStmt>(S))
            return true;
        // A nested loop's continue is its own
        if (isa<ForStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S) || isa<CXXForRangeStmt>(S))
            return canHoldJump(S);
        return llvm::any_of(S->children(), [](const Stmt *C) { return skipsOrRepeats(C); });
    }

    // Whether S holds a goto or a label (a continue inside it is its own).
    static bool canHoldJump(const Stmt *S) {
        if (!S) return false;
        if (isa<GotoStmt>(S) || isa<IndirectGotoStmt>(S) || isa<LabelStmt>(S)) return true;
        return llvm::any_of(S->children(), [](const Stmt *C) { return canHoldJump(C); });
    }

    // The constant step of increment Inc on variable Var: ++Var and Var++
    // give 1, --Var and Var-- give -1, Var += c and Var = Var + c give c,
    // Var -= c and Var = Var - c give -c; anything else gives none.
    std::optional<int64_t> loopStep(Expr *Inc, llvm::StringRef Var) {
        if (!Inc) return std::nullopt;
        Expr *E = Inc->IgnoreParenImpCasts();
        if (auto *U = dyn_cast<UnaryOperator>(E); U && U->isIncrementDecrementOp()) {
            if (!refersToVariable(U->getSubExpr(), Var)) return std::nullopt;
            return U->isIncrementOp() ? 1 : -1;
        }
        auto *B = dyn_cast<BinaryOperator>(E);
        if (!B || !refersToVariable(B->getLHS(), Var)) return std::nullopt;
        const Expr *Amount = B->getRHS();
        bool Negate = B->getOpcode() == BO_SubAssign;
        if (B->getOpcode() == BO_Assign) {
            auto *Sum = dyn_cast<BinaryOperator>(B->getRHS()->IgnoreParenImpCasts());
            if (!Sum || (Sum->getOpcode() != BO_Add && Sum->getOpcode() != BO_Sub) ||
                !refersToVariable(Sum->getLHS(), Var))
                return std::nullopt;
            Amount = Sum->getRHS();
            Negate = Sum->getOpcode() == BO_Sub;
        } else if (B->getOpcode() != BO_AddAssign && B->getOpcode() != BO_SubAssign) {
            return std::nullopt;
        }
        Expr::EvalResult Result;
        if (!safeEvaluateAsInt(Amount, Result, Ctx)) return std::nullopt;
        auto C = safeGetInt64(Result.Val.getInt());
        if (!C || *C == std::numeric_limits<int64_t>::min()) return std::nullopt;
        return Negate ? -*C : *C;
    }

    // Whether a loop variable of Width bits (Signed or not), stepping by
    // Step toward a bound of `i Op Bound`, stays representable: increasing,
    // the last value the condition passes plus the step; decreasing, the
    // smallest value it passes minus the step. A variable bound (i < n, n of
    // the same type: i + 1 <= n) is fine for a step of 1, unless the
    // variable is Narrow (promoted, so n can exceed its range).
    static bool staysInRange(unsigned Width, bool Signed, bool Narrow, bool Increasing, ProofOp Op,
                             std::optional<int64_t> Bound, int64_t Step) {
        using Wide = __int128;
        Wide Max = Signed ? (Wide(1) << (Width - 1)) - 1 : (Wide(1) << Width) - 1;
        Wide Min = Signed ? -(Wide(1) << (Width - 1)) : 0;
        if (!Bound) return Increasing && Op == ProofOp::LT && Step == 1 && !Narrow;
        if (Increasing) {
            Wide Last = Op == ProofOp::LT ? Wide(*Bound) - 1 : Wide(*Bound);
            return Last + Step <= Max;
        }
        Wide Lowest = Op == ProofOp::GE ? Wide(*Bound) : Wide(*Bound) + 1;
        return Lowest - Step >= Min;
    }

    // Whether an implicit conversion on the way to E's type may change an
    // integer's value: narrowing, or a change of signedness that does not
    // widen from unsigned to signed.
    bool conversionMayChangeValue(const Expr *E) {
        for (auto *Cast = dyn_cast<ImplicitCastExpr>(E->IgnoreParens()); Cast;
             Cast = dyn_cast<ImplicitCastExpr>(Cast->getSubExpr()->IgnoreParens())) {
            QualType To = Cast->getType(), From = Cast->getSubExpr()->getType();
            if (!To->isIntegerType() || !From->isIntegerType()) continue;
            unsigned ToWidth = Ctx.getIntWidth(To), FromWidth = Ctx.getIntWidth(From);
            bool ToSigned = To->isSignedIntegerOrEnumerationType();
            bool FromSigned = From->isSignedIntegerOrEnumerationType();
            bool Preserves = ToSigned == FromSigned ? ToWidth >= FromWidth : ToSigned && ToWidth > FromWidth;
            if (!Preserves) return true;
        }
        return false;
    }

    // Whether integer loop variable i starts at most at the bound of
    // `i != bound`: a constant start at most a constant bound, or 0 against
    // an unsigned bound.
    bool startsAtMostAtBound(ForStmt *For, std::optional<int64_t> Start,
                             std::optional<int64_t> ConstBound) {
        if (Start && ConstBound) return *Start <= *ConstBound;
        auto *BO = dyn_cast<BinaryOperator>(For->getCond()->IgnoreParenImpCasts());
        return BO && Start && *Start == 0 &&
               BO->getRHS()->IgnoreParenImpCasts()->getType()->isUnsignedIntegerType();
    }

    // The array pointer expression E points into (arr, arr + k, &arr[k], or
    // a pointer that points into it), named by its path (a string: it is an
    // identity, not a number); unknown otherwise.
    SymValue arrayBaseOf(const Expr *E) {
        E = E->IgnoreParenCasts();
        if (auto *U = dyn_cast<UnaryOperator>(E); U && U->getOpcode() == UO_AddrOf) {
            const Expr *Sub = U->getSubExpr()->IgnoreParenImpCasts();
            if (auto *ASE = dyn_cast<ArraySubscriptExpr>(Sub)) return arrayBaseOf(ASE->getBase());
            return Ctx.getAsConstantArrayType(Sub->getType()) ? arrayBaseOf(Sub) : SymValue::Unknown();
        }
        if (auto *BO = dyn_cast<BinaryOperator>(E);
            BO && (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub))
            return arrayBaseOf(BO->getLHS()->getType()->isPointerType() ? BO->getLHS() : BO->getRHS());
        auto *DRE = dyn_cast<DeclRefExpr>(E);
        FieldPath Path = DRE ? getTypedPath(DRE) : FieldPath();
        if (!Path.isValid()) return SymValue::Unknown();
        if (Ctx.getAsConstantArrayType(DRE->getType())) return SymValue::String(Path.toStringRef());
        if (DRE->getType()->isPointerType()) return getFieldTyped(Path, "_base");
        return SymValue::Unknown();
    }

    // After pointer Path is set from Source: the array it points into, if
    // its buffer is tracked and the array is known.
    void recordPointerBase(const FieldPath &Path, const Expr *Source) {
        FieldPath BaseField = Path.withSynthetic("_base");
        SymValue Base = arrayBaseOf(Source);
        if (Base.kind != SymValue::UNKNOWN && getFieldTyped(Path, "_base_length").kind != SymValue::UNKNOWN)
            State.TypedEnv[BaseField] = Base;
        else
            State.TypedEnv.erase(BaseField);
    }

    // Whether E reads only local variables (and constants) that none of
    // Writers may write: its value is the same on every pass.
    static bool readsOnlyLocalsUnwritten(const Expr *E, std::initializer_list<const Stmt *> Writers) {
        E = E->IgnoreParenCasts();
        if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
            auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
            if (!VD) return isa<EnumConstantDecl>(DRE->getDecl());
            return VD->hasLocalStorage() && VD->getIdentifier() &&
                   llvm::none_of(Writers, [&](const Stmt *W) { return mayWriteVariable(W, variableKey(VD)); });
        }
        if (isa<IntegerLiteral>(E)) return true;
        if (auto *BO = dyn_cast<BinaryOperator>(E))
            return readsOnlyLocalsUnwritten(BO->getLHS(), Writers) &&
                   readsOnlyLocalsUnwritten(BO->getRHS(), Writers);
        return false;
    }

    // A position in an array: the array, its length and an offset into it.
    struct PointerPosition {
        SymValue Base;
        int64_t BaseLength, Offset;
    };

    // The position pointer expression E is known to hold: a tracked pointer
    // into a known array, an array, or either plus or minus a known integer.
    std::optional<PointerPosition> pointerPosition(const Expr *E) {
        E = E->IgnoreParenImpCasts();
        if (auto *BO = dyn_cast<BinaryOperator>(E);
            BO && (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub)) {
            auto Base = pointerPosition(BO->getLHS());
            auto Delta = knownIntValue(BO->getRHS());
            if (!Base || !Delta) return std::nullopt;
            Base->Offset += BO->getOpcode() == BO_Add ? *Delta : -*Delta;
            return Base;
        }
        auto *DRE = dyn_cast<DeclRefExpr>(E);
        if (!DRE) return std::nullopt;
        SymValue Base = arrayBaseOf(DRE);
        if (Base.kind == SymValue::UNKNOWN) return std::nullopt;
        if (auto *Array = Ctx.getAsConstantArrayType(DRE->getType()))
            return PointerPosition{Base, static_cast<int64_t>(Array->getSize().getZExtValue()), 0};
        FieldPath Path = getTypedPath(DRE);
        SymValue Length = getFieldTyped(Path, "_base_length"), Offset = getFieldTyped(Path, "_offset");
        if (Length.kind != SymValue::CONST || Offset.kind != SymValue::CONST) return std::nullopt;
        return PointerPosition{Base, Length.constVal, Offset.constVal};
    }

    // for (p = start; p < end; ++p) over one array (also <=, and != from at
    // most end): in the body p's offset is a symbol, named p, between the
    // start's offset and the end's.
    std::optional<LoopInvariant> pointerLoopInvariant(ForStmt *For, llvm::StringRef Var) {
        auto *Cond = For->getCond() ? dyn_cast<BinaryOperator>(For->getCond()->IgnoreParenImpCasts())
                                    : nullptr;
        auto *Inc = For->getInc() ? dyn_cast<UnaryOperator>(For->getInc()->IgnoreParenImpCasts())
                                  : nullptr;
        if (!Cond || !Inc || !Inc->isIncrementOp() || !refersToVariable(Cond->getLHS(), Var) ||
            !refersToVariable(Inc->getSubExpr(), Var) || mayWriteVariable(For->getBody(), Var))
            return std::nullopt;
        if (!readsOnlyLocalsUnwritten(Cond->getRHS(), {For->getBody(), For->getInc()}))
            return std::nullopt;
        auto Start = pointerPosition(Cond->getLHS()), End = pointerPosition(Cond->getRHS());
        if (!Start || !End || !sameValue(Start->Base, End->Base) ||
            Start->BaseLength != End->BaseLength)
            return std::nullopt;
        LoopInvariant I;
        I.Var = Var.str();
        I.VarBounds.push_back({ProofOp::GE, Start->Offset});
        switch (Cond->getOpcode()) {
        case BO_LE: I.VarBounds.push_back({ProofOp::LE, End->Offset}); break;
        case BO_NE:
            if (Start->Offset > End->Offset) return std::nullopt;
            [[fallthrough]];
        case BO_LT: I.VarBounds.push_back({ProofOp::LT, End->Offset}); break;
        default: return std::nullopt;
        }
        I.SteppedPointers.push_back({I.Var, SymValue::Var(I.Var)});
        return I;
    }

    // for (i = L; i < bound; i += S) and the decreasing forms: the range the
    // initialization, condition and step give i in the body. The bound can be
    // v.size(), a field, a variable, an expression or a constant.
    std::optional<LoopInvariant> forLoopInvariant(ForStmt *For) {
        // A jump into the body skips the initialization and the condition
        if (canBeJumpedInto(For->getBody())) return std::nullopt;
        std::string loopVar;
        std::string objPath;
        std::string boundField;
        std::optional<int64_t> lowerBound;
        std::optional<int64_t> constUpperBound;
        std::optional<int64_t> stepValue;
        bool hasFieldUpperBound = false;
        ProofOp upperOp = ProofOp::LT;
        bool isForwardCondition = true;  // < or <= or != (ascending)
        QualType loopVarType;
        const VarDecl *loopVarDecl = nullptr;
        bool notEqualCondition = false;          // i != bound
        bool loopVarIsPointer = false;
        const Expr *syntheticBound = nullptr;

        // Extract loop variable and lower bound from init
        if (auto *Init = For->getInit()) {
            // Case 1: Declaration (for (int i = 0; ...))
            if (auto *DS = dyn_cast<DeclStmt>(Init)) {
                if (DS->isSingleDecl()) {
                    if (auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl())) {
                        loopVar = variableKey(VD).str();
                        loopVarType = VD->getType();
                        loopVarDecl = VD;
                        loopVarIsPointer = VD->getType()->isPointerType();
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
                            loopVar = variableKey(DRE->getDecl()).str();
                            loopVarType = DRE->getType();
                            loopVarDecl = dyn_cast<VarDecl>(DRE->getDecl());
                            loopVarIsPointer = DRE->getType()->isPointerType();
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(BO->getRHS(), R, Ctx)) {
                                lowerBound = safeGetInt64(R.Val.getInt());
                            }
                        }
                    }
                }
            }
        }

        if (loopVarIsPointer) return pointerLoopInvariant(For, loopVar);

        // Extract upper bound from condition
        if (!loopVar.empty()) {
            if (auto *Cond = For->getCond()) {
                if (auto *BO = dyn_cast<BinaryOperator>(Cond->IgnoreParenImpCasts())) {
                    auto *LHS = BO->getLHS()->IgnoreParenImpCasts();
                    if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                        if (variableKey(DRE->getDecl()).str() == loopVar) {
                            auto *RHS = BO->getRHS()->IgnoreParenImpCasts();

                            // Track the comparison operator
                            if (BO->getOpcode() == BO_LT) {
                                upperOp = ProofOp::LT;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_LE) {
                                upperOp = ProofOp::LE;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_NE) {
                                // != implies < only when stepping by 1 from
                                // at most the bound (checked below)
                                upperOp = ProofOp::LT;
                                isForwardCondition = true;
                                notEqualCondition = true;
                            }
                            else if (BO->getOpcode() == BO_GT) {
                                upperOp = ProofOp::GT;
                                isForwardCondition = false;
                            }
                            else if (BO->getOpcode() == BO_GE) {
                                upperOp = ProofOp::GE;
                                isForwardCondition = false;
                            }
                            else return std::nullopt;

                            // Case 1: Method call (v.size())
                            if (auto *MCE = dyn_cast<CXXMemberCallExpr>(RHS)) {
                                auto *Method = MCE->getMethodDecl();
                                StringRef classNameRef = safeGetName(Method ? Method->getParent() : nullptr);
                                if (Method && !classNameRef.empty()) {
                                    MethodKey key{classNameRef.str(),
                                                  Method->getNameAsString(),
                                                  MCE->getNumArgs()};

                                    auto it = MethodAnnotations.find(key);
                                    if (it != MethodAnnotations.end() && !it->second.returns.empty() &&
                                        callRequirementsProven(MCE, Ctx)) {
                                        // Phase 1 Migration: Use typed path
                                        objPath = getTypedPath(MCE->getImplicitObjectArgument()).toString();
                                        boundField = it->second.returns;
                                        hasFieldUpperBound = true;
                                    }
                                }
                            }
                            // Case 2: Member access (v.len, v._size)
                            else if (auto *ME = dyn_cast<MemberExpr>(RHS)) {
                                // Phase 1 Migration: Use typed path
                                objPath = getTypedPath(ME->getBase()).toString();
                                boundField = ME->getMemberDecl()->getName().str();
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
                                    boundField = variableKey(DRE->getDecl()).str();
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
                                            if (val && *val > 0 && !ME->getType()->isUnsignedIntegerType()) {
                                                // i < _size - N implies i < _size
                                                // Phase 1 Migration: Use typed path
                                                objPath = getTypedPath(ME->getBase()).toString();
                                                boundField = ME->getMemberDecl()->getName().str();
                                                hasFieldUpperBound = true;
                                            }
                                        }
                                    }
                                }
                                // Fallback: create synthetic bound (not of unsigned
                                // arithmetic, which can wrap around)
                                if (!hasFieldUpperBound && !SubBO->getType()->isUnsignedIntegerType()) {
                                    SymValue boundVal = exprToSymValueImpl(SubBO, Ctx);
                                    if (boundVal.kind != SymValue::UNKNOWN) {
                                        static int loopBoundCounter = 0;
                                        boundField = "__loop_bound_" + std::to_string(loopBoundCounter++);
                                        syntheticBound = SubBO;
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

        // The increment steps the variable by a known constant in the
        // condition's direction
        std::optional<int64_t> step = loopStep(For->getInc(), loopVar);
        if (!step || *step == 0 || (*step > 0) != isForwardCondition) return std::nullopt;
        stepValue = *step > 0 ? *step : -*step;

        // A local variable only the loop writes, compared without a
        // conversion that could change either side's value
        auto *CondBO = dyn_cast<BinaryOperator>(For->getCond()->IgnoreParenImpCasts());
        if (loopVar.empty() || !loopVarType->isIntegerType() || !(hasFieldUpperBound || constUpperBound) ||
            !loopVarDecl || !loopVarDecl->hasLocalStorage() || mayWriteVariable(For->getBody(), loopVar) ||
            (CurrentFunctionDecl && referenceMayEscape(CurrentFunctionDecl->getBody(), loopVar)) ||
            !CondBO || conversionMayChangeValue(CondBO->getLHS()) || conversionMayChangeValue(CondBO->getRHS()))
            return std::nullopt;
        // An unsigned or narrow variable must not wrap around past its bound
        unsigned Width = Ctx.getIntWidth(loopVarType);
        bool Narrow = Width < Ctx.getIntWidth(Ctx.IntTy);
        if ((loopVarType->isUnsignedIntegerType() || Narrow) &&
            !staysInRange(Width, loopVarType->isSignedIntegerType(), Narrow, isForwardCondition, upperOp,
                          constUpperBound, *stepValue))
            return std::nullopt;
        // i != bound stays below the bound only when i steps by 1 from at most
        // the bound (after the initialization, which has run).
        if (notEqualCondition && !(stepValue == 1 && startsAtMostAtBound(For, lowerBound, constUpperBound)))
            return std::nullopt;

        LoopInvariant I;
        I.Var = loopVar;
        if (isForwardCondition) {
            // Increasing from L: L <= i, and i below the bound
            if (lowerBound) I.VarBounds.push_back({ProofOp::GE, *lowerBound});
            if (constUpperBound) {
                I.VarBounds.push_back({upperOp == ProofOp::LT ? ProofOp::LT : ProofOp::LE, *constUpperBound});
                // With step S > 1 from L, the largest i below B is L + floor((B-1-L)/S)*S;
                // this proves arr[i+K] safe when K < S.
                if (stepValue && *stepValue > 1 && lowerBound && upperOp == ProofOp::LT) {
                    int64_t L = *lowerBound, B = *constUpperBound, S = *stepValue;
                    if (B > L && S > 0)
                        I.VarBounds.push_back({ProofOp::LE, L + ((B - 1 - L) / S) * S});
                }
            }
        } else {
            // Decreasing from U: i <= U, and i above the bound
            if (lowerBound) I.VarBounds.push_back({ProofOp::LE, *lowerBound});
            if (constUpperBound)
                I.VarBounds.push_back({upperOp == ProofOp::GT ? ProofOp::GT : ProofOp::GE, *constUpperBound});
        }
        if (hasFieldUpperBound && !notEqualCondition) {
            I.HasFieldBound = true;
            I.BoundObj = objPath;
            I.BoundField = boundField;
            I.BoundOp = upperOp;
            I.SyntheticBound = syntheticBound;
        }
        // A pointer the body steps once per pass, while i steps by one from
        // L, is at its entry offset + (i - L) when a pass starts.
        if (isForwardCondition && stepValue == 1 && lowerBound)
            for (const DeclRefExpr *Ptr : pointersSteppedOncePerPass(For->getBody())) {
                FieldPath Path = getTypedPath(Ptr);
                SymValue Entry = getFieldTyped(Path, "_offset");
                if (!Path.isValid() || getFieldTyped(Path, "_base_length").kind == SymValue::UNKNOWN ||
                    Entry.kind == SymValue::UNKNOWN)
                    continue;
                SymValue Passes = *lowerBound == 0
                                      ? SymValue::Var(loopVar)
                                      : SymValue::Sub(SymValue::Var(loopVar), SymValue::Const(*lowerBound));
                bool AtStart = Entry.kind == SymValue::CONST && Entry.constVal == 0;
                I.SteppedPointers.push_back({Path.toString(), AtStart ? Passes : SymValue::Add(Entry, Passes)});
            }
        return I;
    }

    bool VisitSwitchStmt(SwitchStmt *S) {
        if (!CurrentFunctionDecl) return true;
        std::string fn = CurrentFunctionDecl->getNameAsString();
        if (!TotalFunctions.contains(fn)) return true;
        
        auto *Cond = S->getCond();
        if (!Cond) return true;
        QualType condType = Cond->IgnoreParenImpCasts()->getType();
        if (!condType->isEnumeralType()) return true;
        auto *enumType = condType->getAs<EnumType>();
        if (!enumType) return true;
        EnumDecl *ED = enumType->getDecl();
        if (!ED) return true;
        
        llvm::DenseSet<int64_t> allValues;
        std::map<int64_t, std::string> valueNames;
        for (auto *EC : ED->enumerators()) {
            auto val = safeGetInt64(EC->getInitVal());
            if (val) {
                allValues.insert(*val);
                valueNames[*val] = EC->getName().str();
            }
        }

        llvm::DenseSet<int64_t> coveredValues;
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
            if (!coveredValues.contains(val)) {
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
        StringRef classNameRef = resolveAnnotationClassName(Method,
            E->getImplicitObjectArgument());
        if (classNameRef.empty()) return true;

        std::string className = classNameRef.str();
        std::string methodName = Method->getNameAsString();

        // Handle swap method (exchanges properties)
        if (methodName == "swap" && E->getNumArgs() == 1) {
            // Phase 1 Migration: Use typed path
            FieldPath objTypedPath = getTypedPath(E->getImplicitObjectArgument());
            FieldPath argTypedPath = getTypedPath(E->getArg(0));
            std::string objPath = objTypedPath.toString();
            std::string argPath = argTypedPath.toString();
            if (objTypedPath.isValid() && argTypedPath.isValid() && objPath != argPath) {
                swapProperties(objPath, argPath);
            }
            return true;  // swap doesn't need further annotation checking
        }

        // Handle operator= (copy/move assignment) - may appear as CXXMemberCallExpr on some platforms
        if (methodName == "operator=" && E->getNumArgs() == 1) {
            // Phase 1 Migration: Use typed path
            FieldPath lhsTypedPath = getTypedPath(E->getImplicitObjectArgument());
            std::string lhsPath = lhsTypedPath.toString();
            Expr *RHS = E->getArg(0)->IgnoreParenImpCasts();

            if (lhsTypedPath.isValid()) {
                if (isStdMove(RHS)) {
                    std::string rhsPath = getMovedFromPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                        invalidateProperties(rhsPath);
                    }
                } else {
                    // Phase 1 Migration: Use typed path
                    FieldPath rhsTypedPath = getTypedPath(RHS);
                    std::string rhsPath = rhsTypedPath.toString();
                    if (rhsTypedPath.isValid() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                    }
                }
            }
            return true;
        }

        // Include argument count to match correct overload
        MethodKey key{className, methodName, E->getNumArgs()};

        // Get object path early - needed for both annotated and unannotated methods
        // Phase 1 Migration: Use typed path
        FieldPath objTypedPath = getTypedPath(E->getImplicitObjectArgument());
        std::string objPath = objTypedPath.toString();
        if (!objTypedPath.isValid()) return true;

        auto it = MethodAnnotations.find(key);

        // Special case: assign() with initializer_list - detect size at compile time
        if (methodName == "assign" && E->getNumArgs() >= 1) {
            Expr *Arg = E->getArg(0)->IgnoreParenImpCasts();
            if (auto *SIL = dyn_cast<CXXStdInitializerListExpr>(Arg)) {
                if (auto *MTE = dyn_cast<MaterializeTemporaryExpr>(SIL->getSubExpr())) {
                    if (auto *IL = dyn_cast<InitListExpr>(MTE->getSubExpr()->IgnoreParenImpCasts())) {
                        setFieldTyped(objTypedPath, "_size", SymValue::Const(IL->getNumInits()));
                        return true;
                    }
                }
            }
        }

        // Special case: basic_string methods with various argument types
        if (className == "basic_string" && E->getNumArgs() >= 1) {
            Expr *Arg = E->getArg(0)->IgnoreParenImpCasts();

            // Check for string literal argument to append() or operator+=
            if ((methodName == "append" || methodName == "operator+=") &&
                isa<StringLiteral>(Arg)) {
                auto *SL = cast<StringLiteral>(Arg);
                unsigned len = SL->getLength();
                SymValue oldSize = getField(objPath, "_size");
                SymValue newSize = SymValue::Add(oldSize, SymValue::Const(len));
                setFieldTyped(objTypedPath, "_size", newSize);
                return true;
            }

            // Check for tracked string argument (append from another string)
            if ((methodName == "append" || methodName == "operator+=")) {
                FieldPath argTypedPath = getTypedPath(Arg);
                if (argTypedPath.isValid()) {
                    SymValue argSize = getField(argTypedPath.toString(), "_size");
                    if (argSize.kind != SymValue::UNKNOWN) {
                        SymValue oldSize = getField(objPath, "_size");
                        SymValue newSize = SymValue::Add(oldSize, argSize);
                        setFieldTyped(objTypedPath, "_size", newSize);
                        return true;
                    }
                }
            }

            // For pointer arguments (non-literal), invalidate tracking
            if ((methodName == "append" || methodName == "operator+=") &&
                Arg->getType()->isPointerType()) {
                std::string reason = "string pointer argument (unknown length)";
                invalidatePropertiesWithProvenance(objPath, E->getBeginLoc(), reason);
                return true;
            }
        }

        // Special case: std::variant::emplace - update _variant_index
        if (className == "variant" && methodName == "emplace") {
            if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
                if (auto tplArg = getMemberCallTemplateArg(MCE)) {
                    if (tplArg->index) {
                        // emplace<I>(...) - direct index
                        setFieldTyped(objTypedPath, "_variant_index",
                                     SymValue::Const(*tplArg->index));
                        return true;
                    } else if (tplArg->type) {
                        // emplace<T>(...) - map type to index
                        QualType variantType = MCE->getImplicitObjectArgument()
                                                  ->getType().getNonReferenceType();
                        if (auto *TST = variantType->getAs<TemplateSpecializationType>()) {
                            if (auto idx = getVariantAlternativeIndex(TST, *tplArg->type)) {
                                setFieldTyped(objTypedPath, "_variant_index",
                                             SymValue::Const(static_cast<int64_t>(*idx)));
                                return true;
                            }
                        }
                    }
                }
            }
        }

        // Special case: std::any::emplace - track contained type and set _has_value
        if (className == "any" && methodName == "emplace") {
            if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
                if (auto tplArg = getMemberCallTemplateArg(MCE)) {
                    if (tplArg->type) {
                        State.AnyContainedTypes[objPath] = tplArg->type->getCanonicalType();
                        setFieldTyped(objTypedPath, "_has_value", SymValue::Const(1));
                        return true;
                    }
                }
            }
        }

        // Special case: std::any::reset - clears the contained value
        if (className == "any" && methodName == "reset") {
            State.AnyContainedTypes.erase(objPath);
            setFieldTyped(objTypedPath, "_has_value", SymValue::Const(0));
            return true;
        }

        if (it == MethodAnnotations.end()) {
            // No manual annotations - check for auto-inferred postconditions
            auto inferredIt = InferredFnPostconditions.find(Method->getCanonicalDecl());
            if (inferredIt != InferredFnPostconditions.end() && !inferredIt->second.empty()) {
                // Apply inferred postconditions from method implementation analysis
                // First collect argument values for substitution
                std::map<std::string, SymValue> argSubst;
                for (unsigned i = 0; i < E->getNumArgs(); i++) {
                    std::string argKey = "arg" + std::to_string(i);
                    Expr *ArgExpr = E->getArg(i)->IgnoreParenImpCasts();
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                        auto val = safeGetInt64(R.Val.getInt());
                        if (val) argSubst[argKey] = SymValue::Const(*val);
                    } else {
                        FieldPath argTypedPath = getTypedPath(ArgExpr);
                        if (argTypedPath.isValid()) {
                            argSubst[argKey] = exprToSymValueImpl(ArgExpr, Ctx);
                        }
                    }
                }

                // Capture old values for old() substitution
                // Helper to extract all old() references from a SymValue
                std::function<void(const SymValue&)> captureOldRefs = [&](const SymValue& sv) {
                    if (sv.kind == SymValue::VAR &&
                        sv.varRef.size() > 5 &&
                        sv.varRef.compare(0, 4, "old(") == 0 &&
                        sv.varRef.back() == ')') {
                        // Extract field name from old(fieldName)
                        std::string fieldName = sv.varRef.substr(4, sv.varRef.size() - 5);
                        if (argSubst.find(sv.varRef) == argSubst.end()) {
                            // Capture old value from the object
                            SymValue oldVal = getField(objPath, fieldName);
                            argSubst[sv.varRef] = oldVal;
                        }
                    }
                    if (sv.left) captureOldRefs(*sv.left);
                    if (sv.right) captureOldRefs(*sv.right);
                    if (sv.cond) captureOldRefs(*sv.cond);
                };

                for (const auto& post : inferredIt->second) {
                    if (post.usesOld) {
                        // Capture all old() references in the expression
                        captureOldRefs(post.expr);

                        // Also capture old(post.field) for the target field
                        std::string oldKey = "old(" + post.field + ")";
                        if (argSubst.find(oldKey) == argSubst.end()) {
                            if (post.targetArgIndex) {
                                if (*post.targetArgIndex < E->getNumArgs()) {
                                    FieldPath argPath = getTypedPath(E->getArg(*post.targetArgIndex));
                                    if (argPath.isValid()) {
                                        SymValue oldVal = getField(argPath.toString(), post.field);
                                        argSubst[oldKey] = oldVal;
                                    }
                                }
                            } else {
                                SymValue oldVal = getField(objPath, post.field);
                                argSubst[oldKey] = oldVal;
                            }
                        }
                    }
                }

                // Apply each inferred postcondition
                for (const auto& post : inferredIt->second) {
                    SymValue result = post.expr.substitute(argSubst);
                    if (post.targetArgIndex) {
                        // Out-parameter postcondition: apply to argN
                        if (*post.targetArgIndex < E->getNumArgs()) {
                            FieldPath argPath = getTypedPath(E->getArg(*post.targetArgIndex));
                            if (argPath.isValid()) {
                                // Propagate to aliases
                                setFieldWithAliasPropagation(argPath, post.field, result);
                            }
                        }
                    } else {
                        // 'this' postcondition: apply to the object
                        setFieldTyped(objTypedPath, post.field, result);
                    }
                }
                return true;
            }

            // No inferred postconditions - if non-const method, invalidate the object (Category C)
            if (!Method->isConst()) {
                // Check if this is an arrow call (ptr->method())
                // For arrow calls, the method mutates the pointee, not the pointer
                // itself, so preserve the pointer's _nonnull property
                bool isArrowCall = false;
                if (auto *ME = dyn_cast<MemberExpr>(E->getCallee()->IgnoreParens())) {
                    isArrowCall = ME->isArrow();
                }

                // Save _nonnull (value and proofs) before invalidation for arrow calls
                std::optional<SymValue> savedNonnull;
                llvm::SmallVector<Proof, 2> savedNonnullProofs;
                FieldPath objBase = FieldPath::fromSynthetic(objPath);
                if (isArrowCall) {
                    savedNonnull = getEnvValue(objPath + "._nonnull");
                    if (auto *Proofs = State.GlobalProofs.getProofs(objBase))
                        for (const Proof& P : *Proofs)
                            if (P.field == "_nonnull") savedNonnullProofs.push_back(P);
                }

                std::string reason = "call to unannotated method '" + className + "::" + methodName + "()'";
                invalidatePropertiesWithProvenance(objPath, E->getBeginLoc(), reason);

                // Restore _nonnull for arrow calls since pointer nullability is unchanged
                if (isArrowCall && savedNonnull) {
                    setEnvValue(objPath + "._nonnull", *savedNonnull);
                }
                for (const Proof& P : savedNonnullProofs) addProofTyped(objBase, P);
            }
            return true;
        }

        const MethodInfo& info = it->second;
        
        // Capture argument values for constraint checking
        std::map<std::string, SymValue> argVals;
        for (unsigned i = 0; i < E->getNumArgs(); i++) {
            Expr *ArgExpr = E->getArg(i)->IgnoreParenImpCasts();
            if (auto *Default = dyn_cast<CXXDefaultArgExpr>(ArgExpr))
                ArgExpr = Default->getExpr()->IgnoreParenImpCasts();
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
                    // Also add arg0._field for object argument fields
                    std::string argPrefix = argKey + ".";
                    // Iterate over TypedEnv
                    for (const auto& [fieldPath, value] : State.TypedEnv) {
                        llvm::StringRef pathStr = fieldPath.toStringRef();
                        if (startsWithDotPrefix(pathStr, argPath) &&
                            pathStr.find("._proof_") == llvm::StringRef::npos) {
                            std::string field = pathStr.drop_front(argPath.size() + 1).str();
                            argVals[argPrefix + field] = value;
                        }
                    }
                } else {
                    argVals[argKey] = SymValue::Unknown();
                }
            }
            // Pointer argument properties also apply to literals, defaulted
            // arguments and new-expressions, which have no tracked field path.
            if (ArgExpr->getType()->isPointerType() || ArgExpr->getType()->isNullPtrType()) {
                auto nonnull = inferNonnullFromExpr(ArgExpr);
                if (ArgExpr->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
                    nonnull = 0;
                if (!nonnull) {
                    auto argPath = getTypedPath(ArgExpr);
                    if (argPath.isValid()) {
                        if (auto known = checkNonnull(argPath.toString()))
                            nonnull = *known ? 1 : 0;
                    }
                }
                argVals[argKey + "._nonnull"] = nonnull
                    ? SymValue::Const(*nonnull) : SymValue::Unknown();
            }
        }

        // Check preconditions (using ConstraintExpr AST directly)
        for (const auto& req : info.preconditions) {
            if (!req) continue;

            // Try arg vs field: arg0 < _size
            if (auto avf = asArgVsFieldComparison(req)) {
                auto argIt = argVals.find("arg" + std::to_string(avf->argIdx));
                if (argIt == argVals.end()) {
                    if (!emitCategoryC(E->getBeginLoc(), objPath, avf->rhsField, req->toString())) {
                        if (GlobalMode != DTTMode::Quiet) {
                            emitCategoryD(E->getBeginLoc(), objPath, avf->rhsField, req->toString(), "argument not available");
                        }
                    }
                    continue;
                }

                auto argEval = argIt->second.eval();
                SymValue fieldVal = getField(objPath, avf->rhsField);
                auto fieldEval = fieldVal.eval();

                if (argEval && fieldEval) {
                    bool satisfied = false;
                    switch (avf->op) {
                        case ConstraintExpr::CmpGT: satisfied = *argEval > *fieldEval; break;
                        case ConstraintExpr::CmpGE: satisfied = *argEval >= *fieldEval; break;
                        case ConstraintExpr::CmpLT: satisfied = *argEval < *fieldEval; break;
                        case ConstraintExpr::CmpLE: satisfied = *argEval <= *fieldEval; break;
                        case ConstraintExpr::CmpEQ: satisfied = *argEval == *fieldEval; break;
                        case ConstraintExpr::CmpNE: satisfied = *argEval != *fieldEval; break;
                        default: break;
                    }
                    if (!satisfied) {
                        Diag.Report(E->getBeginLoc(), ErrIndexBounds) << className << methodName
                            << std::to_string(*argEval) << std::to_string(*fieldEval)
                            << objPath << avf->rhsField << fieldVal.str();
                        emitProvenanceChain(objPath, avf->rhsField);
                        emitIndexFixItSuggestion(E->getBeginLoc(), objPath, std::to_string(*argEval), E);
                    }
                } else if (!argEval) {
                    // Runtime arg - proofs handled elsewhere
                } else {
                    if (!emitCategoryC(E->getBeginLoc(), objPath, avf->rhsField, req->toString())) {
                        if (GlobalMode != DTTMode::Quiet) {
                            emitCategoryD(E->getBeginLoc(), objPath, avf->rhsField, req->toString(), "could not evaluate field");
                        }
                    }
                }
                continue;
            }

            // Try arg vs const: arg0 >= 0
            if (auto avc = asArgVsConstComparison(req)) {
                if (avc->argIdx < E->getNumArgs()) {
                    const Expr *Arg = E->getArg(avc->argIdx)->IgnoreParenImpCasts();
                    if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(Arg))
                        Arg = Default->getExpr()->IgnoreParenImpCasts();
                    FieldPath ArgPath = getTypedPath(Arg);
                    SymValue Value = exprToSymValueImpl(Arg, Ctx);
                    if (!avc->argField.empty()) {
                        llvm::SmallString<64> Key("arg");
                        Key += llvm::utostr(avc->argIdx);
                        Key += ".";
                        Key += avc->argField;
                        auto It = argVals.find(Key.str().str());
                        Value = It != argVals.end() ? It->second : SymValue::Unknown();
                        ArgPath = ArgPath.withSynthetic(avc->argField);
                    }
                    if (Value.kind == SymValue::UNKNOWN && ArgPath.isValid() &&
                        !ArgPath.hasRuntimeIndex())
                        Value = SymValue::Var(ArgPath.toString());
                    auto argEval = Value.eval();
                    if (argEval) {
                        bool satisfied = false;
                        switch (avc->op) {
                            case ConstraintExpr::CmpGT: satisfied = *argEval > avc->value; break;
                            case ConstraintExpr::CmpGE: satisfied = *argEval >= avc->value; break;
                            case ConstraintExpr::CmpLT: satisfied = *argEval < avc->value; break;
                            case ConstraintExpr::CmpLE: satisfied = *argEval <= avc->value; break;
                            case ConstraintExpr::CmpEQ: satisfied = *argEval == avc->value; break;
                            case ConstraintExpr::CmpNE: satisfied = *argEval != avc->value; break;
                            default: break;
                        }
                        if (!satisfied) {
                            Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName
                                << req->toString() << objPath << "arg" << std::to_string(*argEval);
                        }
                    } else {
                        // A satisfying counterexample is not a proven violation.
                        // Use Z3 only to establish safety; otherwise report uncertainty.
                        // Unknown subexpressions must not share one solver symbol.
                        // Restrict this proof path to integer arithmetic whose solver
                        // encoding agrees with C++ (not signed remainder, shifts, etc.).
                        std::optional<bool> Proven;
                        if (!Arg->getType()->isUnsignedIntegerType() &&
                            isSupportedContractValue(Value))
                            Proven = checkComparisonWithZ3(objPath, Value,
                                                           SymValue::Const(avc->value), avc->op);
                        if (Proven != true && GlobalMode != DTTMode::Quiet) {
                            bool Lost = false;
                            if (!avc->argField.empty())
                                Lost = emitCategoryC(E->getBeginLoc(), getExprPath(Arg),
                                                     avc->argField, req->toString());
                            else if (isa<MemberExpr>(Arg))
                                Lost = emitCategoryC(E->getBeginLoc(), getBasePath(Arg),
                                                     getFieldName(Arg), req->toString());
                            if (!Lost)
                                emitCategoryD(E->getBeginLoc(), objPath, "arg", req->toString(),
                                              "argument constraint could not be proved");
                        }
                    }
                }
                continue;
            }

            // Try compound (AND/OR)
            if (isCompoundConstraint(req)) {
                auto result = evaluateConstraintBool(req, objPath, argVals);
                if (!result.has_value()) {
                    if (GlobalMode != DTTMode::Quiet) {
                        emitCategoryD(E->getBeginLoc(), objPath, "compound", req->toString(), "constraint undecidable");
                    }
                } else if (!*result) {
                    Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName << req->toString() << objPath << "compound" << "violated";
                }
                continue;
            }

            // Try simple field comparison: _size > 0
            if (auto sfc = asSimpleFieldComparison(req)) {
                TrackingState trackState = getFieldTrackingState(objPath, sfc->field);
                if (trackState == TrackingState::UnknownLost) {
                    emitCategoryC(E->getBeginLoc(), objPath, sfc->field, req->toString());
                    continue;
                }
                bool isGhostState = !sfc->field.empty() && sfc->field[0] == '@';
                if (trackState == TrackingState::Untracked && !hasAnyProofs(objPath, sfc->field) && !isGhostState) {
                    continue;  // Category A: silently allow
                }

                auto result = checkSimpleFieldComparisonWithProofs(objPath, *sfc);
                if (!result.has_value()) {
                    if (GlobalMode != DTTMode::Quiet) {
                        emitCategoryD(E->getBeginLoc(), objPath, sfc->field, req->toString(), "constraint undecidable");
                    }
                } else if (!*result) {
                    SymValue fieldVal = getField(objPath, sfc->field);
                    Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName << req->toString() << objPath << sfc->field << fieldVal.str();
                    emitProvenanceChain(objPath, sfc->field);
                    emitFixItSuggestion(E->getBeginLoc(), objPath, sfc->field, req->toString(), E);
                    // Backward search: find operations that could establish this property
                    if (auto goalOp = constraintExprKindToProofOp(sfc->op)) {
                        emitBackwardSearchSuggestions(E->getBeginLoc(), className,
                                                     sfc->field, *goalOp, sfc->value);
                    }
                }
                continue;
            }

            // Complex expression constraint - use general evaluator
            auto result = evaluateConstraintBool(req, objPath, argVals);
            if (!result.has_value()) {
                if (GlobalMode != DTTMode::Quiet) {
                    emitCategoryD(E->getBeginLoc(), objPath, "expr", req->toString(), "could not evaluate expression");
                }
            } else if (!*result) {
                Diag.Report(E->getBeginLoc(), ErrMethodReq) << className << methodName << req->toString() << objPath << "expr" << "violated";
            }
        }
        
        // Apply postconditions if present
        if (!info.ensures.empty() && callRequirementsProven(E, Ctx)) {
            auto oldVals = captureOldValuesMethod(objPath);
            llvm::SmallString<64> desc;
            desc += className;
            desc += "::";
            desc += methodName;
            applyMethodPostconditions(objTypedPath, info, oldVals, argVals,
                                     E->getBeginLoc(), desc);
        }
        // Methods with DTT annotations but no ensures are trusted - the user has
        // explicitly annotated the method and would add ensures if it mutated.
        // This matches the behavior for free functions.

        return true;
    }

    // Helper to check if a DeclContext is in the std namespace (handles inline namespaces)
    bool isInStdNamespace(const DeclContext *DC) {
        for (; DC; DC = DC->getParent()) {
            if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                if (NS->getName() == "std") return true;
            }
        }
        return false;
    }

    // Helper to find the index of a type in a variant's template arguments
    // Returns std::nullopt if type not found
    std::optional<size_t> getVariantAlternativeIndex(
        const TemplateSpecializationType *VariantTST,
        QualType TargetType) {

        TargetType = TargetType.getNonReferenceType().getUnqualifiedType();

        // First pass: exact type match
        size_t idx = 0;
        for (const auto &Arg : VariantTST->template_arguments()) {
            if (Arg.getKind() == TemplateArgument::Type) {
                QualType AltType = Arg.getAsType().getUnqualifiedType();
                if (Ctx.hasSameType(AltType, TargetType)) {
                    return idx;
                }
                idx++;
            }
        }

        // Second pass: try implicit conversions
        idx = 0;
        for (const auto &Arg : VariantTST->template_arguments()) {
            if (Arg.getKind() == TemplateArgument::Type) {
                QualType AltType = Arg.getAsType();

                // Integer types
                if (TargetType->isIntegralOrEnumerationType() &&
                    AltType->isIntegralOrEnumerationType()) {
                    return idx;
                }

                // Float types
                if (TargetType->isFloatingType() && AltType->isFloatingType()) {
                    return idx;
                }

                // const char* -> std::string
                if (TargetType->isPointerType()) {
                    QualType Pointee = TargetType->getPointeeType();
                    if (Pointee->isCharType()) {
                        std::string AltName = AltType.getAsString();
                        if (AltName.find("basic_string") != std::string::npos) {
                            return idx;
                        }
                    }
                }
                idx++;
            }
        }
        return std::nullopt;
    }

    // Helper to extract in_place tag information from a type
    // Returns the contained type for in_place_type_t<T> or index for in_place_index_t<I>
    // Only matches std::in_place_* (verifies namespace)
    struct InPlaceTagInfo {
        std::optional<QualType> type;   // For in_place_type_t<T>
        std::optional<int64_t> index;   // For in_place_index_t<I>
    };
    std::optional<InPlaceTagInfo> getInPlaceTagInfo(QualType argType) {
        auto *TST = argType->getAs<TemplateSpecializationType>();
        if (!TST) return std::nullopt;

        auto *TD = TST->getTemplateName().getAsTemplateDecl();
        if (!TD) return std::nullopt;

        // Verify it's in std namespace
        if (!isInStdNamespace(TD->getDeclContext())) return std::nullopt;

        StringRef tagName = TD->getName();
        if (TST->template_arguments().empty()) return std::nullopt;

        const auto &firstArg = TST->template_arguments()[0];

        if (tagName == "in_place_type_t") {
            if (firstArg.getKind() == TemplateArgument::Type) {
                return InPlaceTagInfo{firstArg.getAsType(), std::nullopt};
            }
        } else if (tagName == "in_place_index_t") {
            if (firstArg.getKind() == TemplateArgument::Integral) {
                return InPlaceTagInfo{std::nullopt, firstArg.getAsIntegral().getExtValue()};
            }
        }
        return std::nullopt;
    }

    // Helper to extract first template argument from a call expression
    // Returns the type or integral value of the first template arg
    struct FirstTemplateArg {
        std::optional<QualType> type;
        std::optional<int64_t> index;
    };

    // Extract type/index from a TemplateArgument
    FirstTemplateArg extractTemplateArgValue(const TemplateArgument &TA) {
        FirstTemplateArg result;
        if (TA.getKind() == TemplateArgument::Type) {
            result.type = TA.getAsType();
        } else if (TA.getKind() == TemplateArgument::Integral) {
            result.index = TA.getAsIntegral().getExtValue();
        } else if (TA.getKind() == TemplateArgument::Expression) {
            Expr *TAExpr = TA.getAsExpr();
            Expr::EvalResult evalResult;
            if (TAExpr->EvaluateAsInt(evalResult, Ctx)) {
                result.index = evalResult.Val.getInt().getExtValue();
            }
        }
        return result;
    }

    std::optional<FirstTemplateArg> getFirstTemplateArg(const CallExpr *CE) {
        auto *CalleeDRE = dyn_cast<DeclRefExpr>(CE->getCallee()->IgnoreParenImpCasts());
        if (!CalleeDRE) return std::nullopt;

        auto TAL = CalleeDRE->getTemplateArgs();
        if (!TAL || CalleeDRE->getNumTemplateArgs() == 0) return std::nullopt;

        return extractTemplateArgValue(TAL[0].getArgument());
    }

    // Helper to extract first template argument from a member call expression
    // For calls like v.emplace<1>() where the callee is a MemberExpr
    std::optional<FirstTemplateArg> getMemberCallTemplateArg(const CXXMemberCallExpr *MCE) {
        auto *ME = dyn_cast<MemberExpr>(MCE->getCallee()->IgnoreParens());
        if (!ME) return std::nullopt;

        auto TAL = ME->getTemplateArgs();
        if (!TAL || ME->getNumTemplateArgs() == 0) return std::nullopt;

        return extractTemplateArgValue(TAL[0].getArgument());
    }

    // Helper to detect if expression is std::move(x)
    bool isStdMove(const Expr *E) {
        auto *Call = dyn_cast<CallExpr>(E->IgnoreParenImpCasts());
        if (!Call) return false;

        auto *Callee = Call->getDirectCallee();
        if (!Callee) return false;
        // Check for identifier name (not an operator)
        if (!Callee->getIdentifier() || Callee->getName() != "move") return false;

        return isInStdNamespace(Callee->getDeclContext());
    }

    // Check if call is std::sort
    bool isStdSort(const CallExpr *Call) {
        if (!Call) return false;
        auto *Callee = Call->getDirectCallee();
        if (!Callee || Callee->getNameAsString() != "sort") return false;

        for (auto *DC = Callee->getDeclContext(); DC; DC = DC->getParent()) {
            if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                if (NS->getName() == "std") return true;
            }
        }
        return false;
    }

    // Get container path from iterator (container.begin() -> container)
    // Handles both v.begin() and begin(v) patterns
    std::string getContainerFromIterator(const Expr *E) {
        E = E->IgnoreParenImpCasts();

        // Pattern 1: v.begin() - member call
        if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
            if (auto *MD = MCE->getMethodDecl()) {
                llvm::StringRef name = safeGetName(MD);
                if (name == "begin" || name == "end" || name == "cbegin" || name == "cend") {
                    return getTypedPath(MCE->getImplicitObjectArgument()).toString();
                }
            }
        }

        // Pattern 2: std::begin(v) - free function (rare but valid)
        if (auto *CE = dyn_cast<CallExpr>(E)) {
            if (auto *Callee = CE->getDirectCallee()) {
                llvm::StringRef name = safeGetName(Callee);
                if ((name == "begin" || name == "end") && CE->getNumArgs() >= 1) {
                    // Check if it's in std namespace
                    for (auto *DC = Callee->getDeclContext(); DC; DC = DC->getParent()) {
                        if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                            if (NS->getName() == "std") {
                                return getTypedPath(CE->getArg(0)).toString();
                            }
                        }
                    }
                }
            }
        }

        return "";
    }
    
    // Get the inner expression from std::move(x)
    std::string getMovedFromPath(const Expr *E) {
        E = E->IgnoreParenImpCasts();
        if (auto *Call = dyn_cast<CallExpr>(E)) {
            if (Call->getNumArgs() >= 1) {
                // Phase 1 Migration: Use typed path
                return getTypedPath(Call->getArg(0)).toString();
            }
        }
        return "";
    }

    // Read presence from a copy/move source, including a temporary constructed
    // by an implicit conversion (e.g. optional<int> = 42).
    SymValue assignmentPresence(Expr *Source, StringRef className, StringRef field) {
        Source = Source->IgnoreCasts()->IgnoreImplicit();
        if (isStdMove(Source))
            Source = cast<CallExpr>(Source->IgnoreParenImpCasts())->getArg(0)->IgnoreImplicit();
        auto sourcePath = getTypedPath(Source);
        if (sourcePath.isValid()) return getFieldTyped(sourcePath, field);

        if (auto *CE = dyn_cast<CXXConstructExpr>(Source)) {
            if (CE->getNumArgs() == 0)
                return SymValue::Const(className == "expected" ? 1 : 0);
            Expr *Arg = CE->getArg(0)->IgnoreImplicit();
            if (CE->getConstructor()->isCopyOrMoveConstructor())
                return assignmentPresence(Arg, className, field);
            if (field == "_nonnull") {
                if (Arg->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
                    return SymValue::Const(0);
                if (auto nonnull = inferNonnullFromExpr(Arg)) return SymValue::Const(*nonnull);
                auto path = getTypedPath(Arg);
                if (path.isValid()) return getFieldTyped(path, field);
                return SymValue::Unknown();
            }
            if (auto *RD = Arg->getType()->getAsCXXRecordDecl()) {
                if (RD->getIdentifier() && isInStdNamespace(RD->getDeclContext())) {
                    if (RD->getName() == "nullopt_t" || RD->getName() == "unexpected")
                        return SymValue::Const(0);
                    // Converting container constructors need overload-specific
                    // reasoning; do not assume their source is engaged.
                    if (RD->getName() == className) return SymValue::Unknown();
                }
            }
            return SymValue::Const(1);
        }
        if (auto *Call = dyn_cast<CallExpr>(Source)) {
            auto *Callee = Call->getDirectCallee();
            if (Callee && Callee->getIdentifier() && isInStdNamespace(Callee->getDeclContext()) &&
                (Callee->getName() == "make_unique" || Callee->getName() == "make_shared"))
                return SymValue::Const(1);
        }
        return SymValue::Unknown();
    }

    // Handle operator[] and operator=
    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
        auto Op = E->getOperator();

        // Handle operator= for copy/move assignment
        if (Op == OO_Equal && E->getNumArgs() >= 2) {
            // Phase 1 Migration: Use typed path
            FieldPath lhsTypedPath = getTypedPath(E->getArg(0));
            std::string lhsPath = lhsTypedPath.toString();
            Expr *RHS = E->getArg(1)->IgnoreParenImpCasts();

            // Handle chain assignment: v3 = (v2 = v1)
            // Process inner assignment first so v2 has correct properties
            if (auto *InnerAssign = dyn_cast<CXXOperatorCallExpr>(RHS)) {
                if (InnerAssign->getOperator() == OO_Equal) {
                    VisitCXXOperatorCallExpr(InnerAssign);  // Recursively process inner
                }
            }

            if (lhsTypedPath.isValid()) {
                // Check if RHS is std::move(x)
                if (isStdMove(RHS)) {
                    std::string rhsPath = getMovedFromPath(RHS);
                    if (!rhsPath.empty() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                        invalidateProperties(rhsPath);  // Moved-from = Unknown
                    }
                } else {
                    // Phase 1 Migration: Use typed path
                    FieldPath rhsTypedPath = getTypedPath(RHS);
                    std::string rhsPath = rhsTypedPath.toString();
                    if (rhsTypedPath.isValid() && lhsPath != rhsPath) {
                        clearProperties(lhsPath);
                        copyProperties(lhsPath, rhsPath);
                    }
                }

                // Check for method annotations (e.g., optional::operator= sets _has_value)
                auto *Method = dyn_cast<CXXMethodDecl>(E->getCalleeDecl());
                StringRef classNameRef = Method
                    ? resolveAnnotationClassName(Method, E->getArg(0))
                    : StringRef();
                if (!classNameRef.empty()) {
                    std::string className = classNameRef.str();
                    auto anglePos = className.find('<');
                    if (anglePos != std::string::npos) className = className.substr(0, anglePos);
                    // Some STLs implement same-type wrapper assignment as a
                    // constrained function template, not a special member.
                    bool sameWrapperAssignment =
                        (className == "unique_ptr" || className == "shared_ptr" ||
                         className == "optional" || className == "expected" || className == "any") &&
                        Ctx.hasSameUnqualifiedType(RHS->getType().getNonReferenceType(),
                                                  E->getArg(0)->getType().getNonReferenceType());
                    bool preserveAssignmentState = Method && isInStdNamespace(Method->getDeclContext()) &&
                        (Method->isCopyAssignmentOperator() || Method->isMoveAssignmentOperator() ||
                         sameWrapperAssignment);

                    // Arity-only STL value-assignment contracts must not
                    // overwrite the state just copied from the source object.
                    if (Method && isInStdNamespace(Method->getDeclContext())) {
                        if (preserveAssignmentState) {
                            StringRef presenceField;
                            if (className == "unique_ptr" || className == "shared_ptr")
                                presenceField = "_nonnull";
                            else if (className == "optional" || className == "expected" || className == "any")
                                presenceField = "_has_value";
                            if (!presenceField.empty() && !isStdMove(RHS))
                                setFieldTyped(lhsTypedPath, presenceField,
                                              assignmentPresence(RHS, className, presenceField));
                        }
                        if ((className == "unique_ptr" || className == "shared_ptr") &&
                            RHS->getType()->isNullPtrType()) {
                            setFieldTyped(lhsTypedPath, "_nonnull", SymValue::Const(0));
                            return true;
                        }
                        if (className == "optional") {
                            auto *RD = RHS->getType()->getAsCXXRecordDecl();
                            if (RD && RD->getIdentifier() && RD->getName() == "nullopt_t" &&
                                isInStdNamespace(RD->getDeclContext())) {
                                setFieldTyped(lhsTypedPath, "_has_value", SymValue::Const(0));
                                return true;
                            }
                        }
                    }

                    // Special handling for std::variant assignment - compute index from RHS type
                    if (className == "variant") {
                        QualType lhsType = E->getArg(0)->getType().getNonReferenceType();
                        if (auto *TST = lhsType->getAs<TemplateSpecializationType>()) {
                            QualType rhsType = RHS->getType();
                            if (auto idx = getVariantAlternativeIndex(TST, rhsType)) {
                                setFieldTyped(lhsTypedPath, "_variant_index", SymValue::Const(*idx));
                            }
                        }
                    }

                    // Special handling for std::any assignment - track contained type
                    if (className == "any") {
                        QualType rhsType = RHS->getType().getCanonicalType();
                        QualType lhsType = E->getArg(0)->getType().getNonReferenceType().getCanonicalType();

                        // Check if RHS is another std::any (copy/move assignment)
                        // by comparing with LHS type (which we know is std::any)
                        if (Ctx.hasSameUnqualifiedType(rhsType.getNonReferenceType(), lhsType)) {
                            // RHS is std::any - copy contained type from source
                            FieldPath rhsPath = getTypedPath(RHS);
                            if (rhsPath.isValid()) {
                                auto srcIt = State.AnyContainedTypes.find(rhsPath.toString());
                                if (srcIt != State.AnyContainedTypes.end()) {
                                    State.AnyContainedTypes[lhsPath] = srcIt->second;
                                } else {
                                    // Source type unknown - clear tracking
                                    State.AnyContainedTypes.erase(lhsPath);
                                }
                            }
                        } else {
                            // RHS is a value type - store as contained type
                            State.AnyContainedTypes[lhsPath] = rhsType;
                        }
                    }

                    // Special handling for std::expected assignment
                    // e = std::unexpected{err} -> _has_value = 0
                    // e = value -> _has_value = 1
                    if (className == "expected" && !preserveAssignmentState) {
                        QualType rhsType = RHS->getType().getCanonicalType();
                        if (auto *RD = rhsType->getAsCXXRecordDecl()) {
                            if (RD->getIdentifier() &&
                                RD->getName() == "unexpected" &&
                                isInStdNamespace(RD->getDeclContext())) {
                                // Assigned from unexpected - no value
                                setFieldTyped(lhsTypedPath, "_has_value", SymValue::Const(0));
                            } else {
                                // Assigned a value
                                setFieldTyped(lhsTypedPath, "_has_value", SymValue::Const(1));
                            }
                        } else {
                            // Non-class type (like int) - has value
                            setFieldTyped(lhsTypedPath, "_has_value", SymValue::Const(1));
                        }
                    }

                    MethodKey key{className, "operator=", E->getNumArgs() - 1};
                    auto it = MethodAnnotations.find(key);
                    if (it != MethodAnnotations.end() && !preserveAssignmentState) {
                        const MethodInfo& info = it->second;
                        std::map<std::string, SymValue> subst;
                        for (unsigned i = 1; i < E->getNumArgs(); i++) {
                            std::string argKey = "arg" + std::to_string(i-1);
                            subst[argKey] = exprToSymValueImpl(E->getArg(i), Ctx);
                        }
                        for (const auto& post : info.ensures) {
                            SymValue result = post.expr.substitute(subst);
                            setFieldTyped(lhsTypedPath, post.field, result);
                        }
                    }
                }
            }
            return true;
        }

        // Handle compound assignment operators (+=, -=, etc.)
        if (Op == OO_PlusEqual || Op == OO_MinusEqual || Op == OO_StarEqual ||
            Op == OO_SlashEqual || Op == OO_PercentEqual) {
            auto *Method = dyn_cast<CXXMethodDecl>(E->getCalleeDecl());
            if (!Method || !Method->getParent()) return true;
            StringRef classNameRef = safeGetName(Method->getParent());
            if (classNameRef.empty()) return true;

            std::string className = classNameRef.str();
            auto anglePos = className.find('<');
            if (anglePos != std::string::npos) className = className.substr(0, anglePos);

            std::string opName;
            switch (Op) {
                case OO_PlusEqual: opName = "operator+="; break;
                case OO_MinusEqual: opName = "operator-="; break;
                case OO_StarEqual: opName = "operator*="; break;
                case OO_SlashEqual: opName = "operator/="; break;
                case OO_PercentEqual: opName = "operator%="; break;
                default: return true;
            }

            // Arguments: arg0 = object (this), arg1 = operand
            MethodKey key{className, opName, E->getNumArgs() - 1};  // -1 because first arg is 'this'

            FieldPath objTypedPath = getTypedPath(E->getArg(0));
            std::string objPath = objTypedPath.toString();
            if (!objTypedPath.isValid()) return true;

            auto it = MethodAnnotations.find(key);

            // Special case: basic_string::operator+= with string literal
            // Detect string literal length and add it to size instead of just 1
            if (className == "basic_string" && Op == OO_PlusEqual && E->getNumArgs() >= 2) {
                Expr *Arg = E->getArg(1)->IgnoreParenImpCasts();
                QualType argType = Arg->getType();

                // Check if argument is a string literal (array of char)
                if (auto *SL = dyn_cast<StringLiteral>(Arg)) {
                    // String literal - we know the exact length
                    unsigned len = SL->getLength();
                    SymValue oldSize = getField(objPath, "_size");
                    SymValue newSize = SymValue::Add(oldSize, SymValue::Const(len));
                    setFieldTyped(objTypedPath, "_size", newSize);
                    return true;
                }

                // Check for tracked string argument (concatenation from another string)
                FieldPath argTypedPath = getTypedPath(Arg);
                if (argTypedPath.isValid()) {
                    SymValue argSize = getField(argTypedPath.toString(), "_size");
                    if (argSize.kind != SymValue::UNKNOWN) {
                        SymValue oldSize = getField(objPath, "_size");
                        SymValue newSize = SymValue::Add(oldSize, argSize);
                        setFieldTyped(objTypedPath, "_size", newSize);
                        return true;
                    }
                }

                // Check for array-to-pointer decay (also string literals, or char arrays)
                if (argType->isPointerType()) {
                    // Could be const char* variable - can't determine length, invalidate
                    std::string reason = "string pointer argument to operator+= (unknown length)";
                    invalidatePropertiesWithProvenance(objPath, E->getBeginLoc(), reason);
                    return true;
                }

                // char argument - fall through to use annotation (size += 1)
            }

            if (it == MethodAnnotations.end()) {
                // No annotation - invalidate tracking if non-const
                if (!Method->isConst()) {
                    std::string reason = "call to unannotated method '" + className + "::" + opName + "()'";
                    invalidatePropertiesWithProvenance(objPath, E->getBeginLoc(), reason);
                }
                return true;
            }

            const MethodInfo& info = it->second;

            // Collect old values for postconditions that use old()
            std::map<std::string, SymValue> subst;
            for (const auto& post : info.ensures) {
                if (post.usesOld) {
                    subst["old:" + post.field] = getField(objPath, post.field);
                }
            }

            // Collect argument values
            for (unsigned i = 1; i < E->getNumArgs(); i++) {
                std::string argKey = "arg" + std::to_string(i-1);
                FieldPath argTypedPath = getTypedPath(E->getArg(i));
                if (argTypedPath.isValid()) {
                    subst[argKey] = SymValue::Var(argTypedPath.toString());
                } else {
                    subst[argKey] = exprToSymValueImpl(E->getArg(i), Ctx);
                }
            }

            // Apply postconditions only under the call's entry contract.
            if (!callRequirementsProven(E, Ctx)) return true;
            for (const auto& post : info.ensures) {
                SymValue result = post.expr.substitute(subst);
                setFieldTyped(objTypedPath, post.field, result);
            }

            return true;
        }

        // Handle operator* and operator-> (dereference operators with preconditions)
        // Used by std::optional, std::unique_ptr, std::shared_ptr, iterators, etc.
        if (Op == OO_Star || Op == OO_Arrow) {
            auto *Method = dyn_cast<CXXMethodDecl>(E->getCalleeDecl());
            if (!Method || !Method->getParent()) return true;
            StringRef classNameRef = (E->getNumArgs() >= 1)
                ? resolveAnnotationClassName(Method, E->getArg(0))
                : safeGetName(Method->getParent());
            if (classNameRef.empty()) return true;

            std::string className = classNameRef.str();
            auto anglePos = className.find('<');
            if (anglePos != std::string::npos) className = className.substr(0, anglePos);

            std::string opName = (Op == OO_Star) ? "operator*" : "operator->";

            // These operators take 0 arguments (just 'this')
            MethodKey key{className, opName, 0};
            auto it = MethodAnnotations.find(key);
            if (it == MethodAnnotations.end()) return true;

            const MethodInfo& info = it->second;

            // Get the object (first argument is 'this' for operator call)
            if (E->getNumArgs() < 1) return true;
            FieldPath objTypedPath = getTypedPath(E->getArg(0));
            std::string objPath = objTypedPath.toString();
            if (!objTypedPath.isValid()) return true;

            // Check preconditions (e.g., _has_value == 1 for optional)
            for (const auto& req : info.preconditions) {
                if (!req) continue;

                // Handle field == constant constraints (e.g., _has_value == 1)
                if (auto sfc = asSimpleFieldComparison(req)) {
                    TrackingState trackState = getFieldTrackingState(objPath, sfc->field);
                    if (trackState == TrackingState::UnknownLost) {
                        emitCategoryC(E->getBeginLoc(), objPath, sfc->field, req->toString());
                        continue;
                    }
                    // No Untracked skip here: operator*/operator-> preconditions
                    // are always checked. The annotation's existence means the
                    // property must be established before dereference.
                    auto result = checkSimpleFieldComparisonWithProofs(objPath, *sfc);
                    if (!result.has_value()) {
                        if (GlobalMode != DTTMode::Quiet) {
                            emitCategoryD(E->getBeginLoc(), objPath, sfc->field, req->toString(), "field value unknown");
                        }
                    } else if (!*result) {
                        SymValue fieldVal = getField(objPath, sfc->field);
                        Diag.Report(E->getBeginLoc(), ErrMethodReq)
                            << className << opName << req->toString()
                            << objPath << sfc->field << fieldVal.str();
                        emitProvenanceChain(objPath, sfc->field);
                        emitFixItSuggestion(E->getBeginLoc(), objPath, sfc->field, req->toString(), E);
                        if (auto goalOp = constraintExprKindToProofOp(sfc->op)) {
                            emitBackwardSearchSuggestions(E->getBeginLoc(), className,
                                                         sfc->field, *goalOp, sfc->value);
                        }
                    }
                }
            }
            return true;
        }

        // Handle operator[]
        if (Op != OO_Subscript) return true;

        auto *Method = dyn_cast<CXXMethodDecl>(E->getCalleeDecl());
        if (!Method || !Method->getParent()) return true;
        StringRef classNameRef = safeGetName(Method->getParent());
        if (classNameRef.empty()) return true;

        std::string className = classNameRef.str();
        // Strip template params
        auto anglePos = className.find('<');
        if (anglePos != std::string::npos) className = className.substr(0, anglePos);

        // Use Method->getNumParams() since operator[] has implicit this
        MethodKey key{className, "operator[]", Method->getNumParams()};

        auto it = MethodAnnotations.find(key);
        if (it == MethodAnnotations.end()) return true;

        const MethodInfo& info = it->second;

        // Get the object (first argument to operator[])
        if (E->getNumArgs() < 2) return true;
        // Phase 1 Migration: Use typed path
        FieldPath objTypedPath = getTypedPath(E->getArg(0));
        std::string objPath = objTypedPath.toString();
        if (!objTypedPath.isValid()) return true;
        
        // Check argVsField constraints (using AST inspection)
        for (const auto& req : info.preconditions) {
            auto avf = asArgVsFieldComparison(req);
            if (!avf) continue;  // Skip non-argVsField constraints

            // Get argument as expression
            Expr *ArgExpr = E->getArg(1)->IgnoreParenImpCasts();

            // Get this.field value (e.g., _size)
            SymValue fieldVal = getField(objPath, avf->rhsField);
            auto fieldEval = fieldVal.eval();

            // Get ProofOp for this comparison
            auto reqProofOp = constraintExprKindToProofOp(avf->op);

            // Check if index is a variable with proofs (loop/guard variable)
            if (auto *DRE = dyn_cast<DeclRefExpr>(ArgExpr)) {
                std::string varName = variableKey(DRE->getDecl()).str();

                if (reqProofOp && hasVarBoundProof(varName, objPath, avf->rhsField, *reqProofOp))
                    continue;  // Matching proof exists

                // Check TypedEnv proofs for constant bounds
                if (fieldEval) {
                    std::string opStr = (avf->op == ConstraintExpr::CmpLT) ? "<" :
                                       (avf->op == ConstraintExpr::CmpLE) ? "<=" : "";
                    if (!opStr.empty()) {
                        std::string proofKey = varName + "._proof__value" + opStr + std::to_string(*fieldEval);
                        if (getEnvValue(proofKey).has_value()) continue;
                        if (avf->op == ConstraintExpr::CmpLE) {
                            std::string ltProofKey = varName + "._proof__value<" + std::to_string(*fieldEval);
                            if (getEnvValue(ltProofKey).has_value()) continue;
                        }
                    }
                }
            }

            // Lambda to check comparison
            auto checkCmp = [&](int64_t lhs, int64_t rhs) -> bool {
                switch (avf->op) {
                    case ConstraintExpr::CmpGT: return lhs > rhs;
                    case ConstraintExpr::CmpGE: return lhs >= rhs;
                    case ConstraintExpr::CmpLT: return lhs < rhs;
                    case ConstraintExpr::CmpLE: return lhs <= rhs;
                    case ConstraintExpr::CmpEQ: return lhs == rhs;
                    case ConstraintExpr::CmpNE: return lhs != rhs;
                    default: return true;
                }
            };

            // Try constant evaluation
            Expr::EvalResult R;
            if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                auto argValOpt = safeGetInt64(R.Val.getInt());
                if (argValOpt) {
                    // Negative index is always out of bounds
                    if (*argValOpt < 0) {
                        Diag.Report(E->getBeginLoc(), ErrNegativeIndex)
                            << className << "operator[]" << std::to_string(*argValOpt);
                        continue;
                    }

                    // For index < _size constraints, check if proofs establish safety
                    if (avf->op == ConstraintExpr::CmpLT) {
                        ProofContext proofCtx(State.GlobalProofs);
                        // _size > index proves index < _size
                        if (proofCtx.canProveGT(objTypedPath, avf->rhsField, *argValOpt))
                            continue;
                        // _size >= index+1 is equivalent for integers
                        if (proofCtx.canProveGE(objTypedPath, avf->rhsField, *argValOpt + 1))
                            continue;
                        // For index 0, _size != 0 implies _size > 0 (non-negative fields)
                        if (*argValOpt == 0 &&
                            proofCtx.canProveNE(objTypedPath, avf->rhsField, 0))
                            continue;
                    }

                    if (fieldEval) {
                        if (!checkCmp(*argValOpt, *fieldEval)) {
                            Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                                << className << "operator[]" << std::to_string(*argValOpt)
                                << std::to_string(*fieldEval) << objPath << avf->rhsField << fieldVal.str();
                            emitProvenanceChain(objPath, avf->rhsField);
                            emitIndexFixItSuggestion(E->getBeginLoc(), objPath, std::to_string(*argValOpt), E);
                        }
                        continue;  // Only continue if we did a complete check
                    }
                }
                // Fall through to Z3 path if fieldEval is symbolic
            }

            // Try symbolic evaluation
            SymValue indexVal = exprToSymValueImpl(ArgExpr, Ctx);
            if (indexVal.kind != SymValue::UNKNOWN) {
                auto indexEval = indexVal.eval();
                if (indexEval && fieldEval) {
                    if (!checkCmp(*indexEval, *fieldEval)) {
                        Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                            << className << "operator[]" << indexVal.str()
                            << std::to_string(*fieldEval) << objPath << avf->rhsField << fieldVal.str();
                    }
                    continue;
                }

                // Try Z3 for symbolic bounds (only to report)
                if (ExploringLoop) continue;
                auto z3Result = checkComparisonWithZ3(objPath, indexVal, fieldVal, avf->op,
                                                      /*assumeNonNegative=*/false,
                                                      /*includeVarBounds=*/true);
                if (z3Result.has_value() && *z3Result) {
                    continue;  // Z3 proved safe
                }
                // No value the facts allow satisfies the bound: proven unsafe
                if (z3Result == false) {
                    Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                        << className << "operator[]" << indexVal.str() << fieldVal.str() << objPath
                        << avf->rhsField << fieldVal.str();
                    continue;
                }
                // Neither proven nor disproven: Category D
                std::string constraint = indexVal.str() + " < " + fieldVal.str();
                emitCategoryD(E->getBeginLoc(), objPath, avf->rhsField, constraint,
                              "bounds could not be verified");
            }
        }
        return true;
    }

    // Check if a pointer dereference is safe (has nonnull proof)
    // Returns: true = safe, false = known null, nullopt = unknown
    std::optional<bool> checkNonnull(llvm::StringRef ptrPath) {
        if (ptrPath.empty()) return std::nullopt;

        // Check for explicit nonnull proof
        if (hasProof(ptrPath, "_nonnull!=0")) return true;

        // Check for known null (proof that _nonnull == 0)
        if (hasProof(ptrPath, "_nonnull==0")) return false;

        // Check direct _nonnull value
        auto val = getField(ptrPath, "_nonnull");
        if (val.kind == SymValue::CONST) {
            return val.constVal != 0;
        }

        // Check alias chain: if ptrPath is an alias of another pointer, check that too
        auto aliasIt = State.PointerAliases.find(ptrPath);
        if (aliasIt != State.PointerAliases.end()) {
            const std::string& srcPath = aliasIt->second;
            // Check if alias source has proof (handles guard on original, use via alias)
            if (hasProof(srcPath, "_nonnull!=0")) return true;
            if (hasProof(srcPath, "_nonnull==0")) return false;
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

    // Report a use or free of a pointer freed on some paths only.
    void reportMaybeFreed(StringRef ptrPath, const PointerLifetimeState &S,
                          SourceLocation useLoc) {
        if (!DiagMaybeFreed.shouldEmit(GlobalMode)) return;
        Diag.Report(useLoc, DiagMaybeFreed.get(GlobalMode)) << ptrPath;
        if (S.freeLocation.isValid()) Diag.Report(S.freeLocation, NoteFirstFree);
    }

    // Check if accessing a freed pointer (use-after-free)
    // Returns true if UAF was detected and reported
    bool checkUseAfterFree(StringRef ptrPath, SourceLocation useLoc) {
        if (ptrPath.empty()) return false;

        auto it = State.PointerLifetimes.find(ptrPath);
        if (it == State.PointerLifetimes.end()) return false;
        if (it->second.lifetime == PointerLifetime::MaybeFreed)
            reportMaybeFreed(ptrPath, it->second, useLoc);
        if (it->second.lifetime == PointerLifetime::Freed) {
            Diag.Report(useLoc, ErrUseAfterFree) << ptrPath;
            if (it->second.freeLocation.isValid()) {
                Diag.Report(it->second.freeLocation, NoteFirstFree);
            }
            return true;
        }
        return false;
    }

    // Check if pointer is uninitialized - using it in any way is UB
    bool checkUninitializedPointer(StringRef ptrPath, SourceLocation useLoc) {
        if (ptrPath.empty()) return false;

        // Check _initialized field
        SymValue initVal = getField(ptrPath, "_initialized");
        if (initVal.kind == SymValue::CONST && initVal.constVal == 0) {
            Diag.Report(useLoc, ErrUninitializedPtr) << ptrPath;
            return true;
        }
        return false;
    }

    // Check if a scalar variable is uninitialized - reading it is UB (Tier 1)
    // Returns true if error was reported
    bool checkUninitializedVar(llvm::StringRef varPath, SourceLocation useLoc) {
        if (varPath.empty()) return false;

        // Check _initialized field
        SymValue initVal = getField(varPath, "_initialized");
        if (initVal.kind == SymValue::CONST && initVal.constVal == 0) {
            Diag.Report(useLoc, ErrUninitializedVar) << varPath;
            return true;
        }
        return false;
    }

    // Recursively set _initialized on all scalar/pointer members of a struct.
    // initialized=false: POD struct declared without initializer (all members UB to read)
    // initialized=true: address escaped via pointer/reference (alias may write)
    void setStructMembersInitState(FieldPath basePath, const RecordDecl *RD,
                                   bool initialized, int depth = 0) {
        if (!RD || depth > 10) return;
        int64_t val = initialized ? 1 : 0;
        for (const auto *FD : RD->fields()) {
            if (!FD->getIdentifier()) continue;
            FieldPath memberPath = basePath.with(PathElement::field(FD));
            QualType fieldType = FD->getType();
            if (fieldType->isArithmeticType() || fieldType->isBooleanType() ||
                fieldType->isPointerType()) {
                setFieldTyped(memberPath, "_initialized", SymValue::Const(val));
            } else if (auto *nestedRD = fieldType->getAsRecordDecl()) {
                if (!nestedRD->isUnion())
                    setStructMembersInitState(memberPath, nestedRD, initialized, depth + 1);
            }
        }
    }

    // Check if any scalar/pointer member of a struct is uninitialized
    // Returns true if any error was reported
    // Takes basePath by value since .with() copies anyway
    bool checkStructMembersInitialized(FieldPath basePath, const RecordDecl *RD,
                                       SourceLocation useLoc, int depth = 0) {
        if (!RD || depth > 10) return false;

        bool reported = false;
        for (const auto *FD : RD->fields()) {
            if (!FD->getIdentifier()) continue;

            FieldPath memberPath = basePath.with(PathElement::field(FD));
            QualType fieldType = FD->getType();

            if (fieldType->isArithmeticType() || fieldType->isBooleanType()) {
                if (checkUninitializedVar(memberPath.toStringRef(), useLoc))
                    reported = true;
            } else if (fieldType->isPointerType()) {
                if (checkUninitializedPointer(memberPath.toStringRef(), useLoc))
                    reported = true;
            } else if (auto *nestedRD = fieldType->getAsRecordDecl()) {
                if (!nestedRD->isUnion()) {
                    if (checkStructMembersInitialized(memberPath, nestedRD, useLoc, depth + 1))
                        reported = true;
                }
            }
        }
        return reported;
    }

    // TIER 5.3: Mark destination of memory functions (memset/memcpy/memmove) as initialized
    void markMemoryFunctionDstInitialized(Expr *DstArg) {
        if (auto *UO = dyn_cast<UnaryOperator>(DstArg)) {
            if (UO->getOpcode() == UO_AddrOf) {
                // &x pattern - mark x as initialized
                Expr *Target = UO->getSubExpr()->IgnoreParenImpCasts();
                FieldPath targetPath = getTypedPath(Target);
                if (targetPath.isValid()) {
                    setFieldTyped(targetPath, "_initialized", SymValue::Const(1));
                }
            }
        } else {
            // Direct pointer - check if it's a heap object
            FieldPath ptrPath = getTypedPath(DstArg);
            if (ptrPath.isValid()) {
                auto it = State.VarToResource.find(ptrPath.toStringRef());
                if (it != State.VarToResource.end()) {
                    ResourceId rid = it->second;
                    auto hosIt = State.HeapObjectStates.find(rid);
                    if (hosIt != State.HeapObjectStates.end()) {
                        hosIt->second.initialized = true;
                    }
                }
            }
        }
    }

    // Helper to emit provenance note for a pointer path
    // Returns the base variable name (stripping array indices) for provenance lookup
    std::string getBaseVarName(llvm::StringRef ptrPath) {
        // Strip array indices and member accesses to get base variable name
        // e.g., "str[i]->field" -> "str", "name" -> "name"
        size_t bracket = ptrPath.find('[');
        size_t arrow = ptrPath.find("->");
        size_t dot = ptrPath.find('.');
        size_t end = ptrPath.size();
        if (bracket != llvm::StringRef::npos) end = std::min(end, bracket);
        if (arrow != llvm::StringRef::npos) end = std::min(end, arrow);
        if (dot != llvm::StringRef::npos) end = std::min(end, dot);
        return ptrPath.substr(0, end).str();
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

        // TIER 1: Check for uninitialized scalar in unary operations
        // Operations like ++x, --x, -x, ~x read the operand
        if (UO->isIncrementDecrementOp() || UO->getOpcode() == UO_Minus ||
            UO->getOpcode() == UO_Plus || UO->getOpcode() == UO_Not ||
            UO->getOpcode() == UO_LNot) {
            if (auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
                if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    QualType varType = VD->getType();
                    if ((varType->isArithmeticType() || varType->isBooleanType()) &&
                        !varType->isPointerType()) {
                        FieldPath varPath = FieldPath::fromDecl(VD);
                        checkUninitializedVar(varPath.toStringRef(), SubExpr->getBeginLoc());
                    }
                }
            }
        }

        // ++/-- on an integer moves its value by one
        if (UO->isIncrementDecrementOp() && SubExpr->getType()->isIntegralOrEnumerationType())
            stepScalar(SubExpr, UO->isIncrementOp() ? 1 : -1);

        // Address-of a struct/struct-member: mark members as initialized
        // (address escape means member may be written through the pointer)
        if (UO->getOpcode() == UO_AddrOf) {
            QualType subType = SubExpr->getType();
            if (auto *RD = subType->getAsRecordDecl()) {
                if (!RD->isUnion()) {
                    FieldPath basePath = getTypedPath(SubExpr);
                    if (basePath.isValid()) {
                        setStructMembersInitState(basePath, RD, true);
                    }
                }
            }
        }

        // ============================================================
        // AUTO-POSTCONDITION INFERENCE: Unary increment/decrement on this->_field
        // ============================================================
        // Detect _size++, ++_size, _size--, --_size on 'this' and infer postconditions
        // Also handles nested fields: this->stats.count++
        if (UO->isIncrementDecrementOp() && CurrentMethodClass) {
            // Get the full nested path from 'this' (e.g., "stats.count" or "_size")
            std::string nestedPath = getNestedFieldPathFromThis(SubExpr);

            if (!nestedPath.empty()) {
                // Check if we should track this field
                bool isNested = nestedPath.find('.') != std::string::npos;
                bool shouldTrack = hasTrackedFieldInPath(nestedPath) ||
                    (!nestedPath.empty() && nestedPath[0] == '_') ||
                    isNested;

                if (shouldTrack) {
                    // field++ or ++field → ensures=field == old(field) + 1
                    // field-- or --field → ensures=field == old(field) - 1
                    SymValue oldRef = SymValue::Var("old(" + nestedPath + ")");
                    SymValue delta = SymValue::Const(1);
                    SymValue inferredExpr = UO->isIncrementOp()
                        ? SymValue::Add(oldRef, delta)
                        : SymValue::Sub(oldRef, delta);
                    addInferredPostcondition(nestedPath, inferredExpr, /*usesOld=*/true);
                }
            }
        }

        // Handle pointer increment/decrement: p++, ++p, p--, --p
        // Update tracked offset when pointer is modified
        if (UO->isIncrementDecrementOp() && SubExpr->getType()->isPointerType()) {
            // Phase 1 Migration: Use typed path
            FieldPath ptrTypedPath = getTypedPath(SubExpr);
            std::string ptrPath = ptrTypedPath.toString();
            if (ptrTypedPath.isValid()) {
                // Check for null pointer arithmetic (UB)
                SymValue nonnull = getFieldTyped(ptrTypedPath, "_nonnull");
                if (nonnull.kind == SymValue::CONST && nonnull.constVal == 0) {
                    Diag.Report(UO->getBeginLoc(), ErrNullPtrArith);
                    return true;
                }

                int64_t delta = UO->isIncrementOp() ? 1 : -1;

                // Phase 3: Update base origin offset for unary increment/decrement
                // This runs even for untracked pointers (raw parameters)
                auto originIt = State.PointerBaseOrigins.find(ptrTypedPath);
                if (originIt != State.PointerBaseOrigins.end()) {
                    int64_t deltaBytes = delta;
                    if (auto elemSize = safeGetTypeSizeInChars(Ctx, SubExpr->getType()->getPointeeType())) {
                        deltaBytes = delta * (*elemSize);
                    }
                    originIt->second.offsetBytes = SymValue::Add(
                        originIt->second.offsetBytes, SymValue::Const(deltaBytes));
                } else {
                    // Check if this is a parameter - establish base origin
                    auto paramIdx = getParameterIndex(SubExpr, CurrentFunctionDecl);
                    if (paramIdx) {
                        int64_t deltaBytes = delta;
                        if (auto elemSize = safeGetTypeSizeInChars(Ctx, SubExpr->getType()->getPointeeType())) {
                            deltaBytes = delta * (*elemSize);
                        }
                        State.PointerBaseOrigins[ptrTypedPath] = BaseOriginInfo(
                            ptrTypedPath, SymValue::Const(deltaBytes), paramIdx);
                    }
                }

                SymValue baseLen = getFieldTyped(ptrTypedPath, "_base_length");
                if (baseLen.kind != SymValue::UNKNOWN) {
                    // Update offset (even if symbolic from loop)
                    // This handles patterns like ++p; *p where increment comes first
                    SymValue offset = getFieldTyped(ptrTypedPath, "_offset");
                    SymValue newOffset = SymValue::Add(offset, SymValue::Const(delta));
                    setFieldTyped(ptrTypedPath, "_offset", newOffset);
                }
            }
            return true;
        }

        if (UO->getOpcode() != UO_Deref) return true;

        if (!SubExpr->getType()->isPointerType()) return true;

        // Skip null check for 'this' pointer - assumed always valid in member functions
        if (isa<CXXThisExpr>(SubExpr)) return true;

        // Phase 1 Migration: Use typed path
        FieldPath ptrTypedPath = getTypedPath(SubExpr);
        std::string ptrPath = ptrTypedPath.toString();
        if (!ptrTypedPath.isValid()) return true;

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
        SymValue baseLen = getFieldTyped(ptrTypedPath, "_base_length");
        SymValue offset = getFieldTyped(ptrTypedPath, "_offset");
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
            } else if (!sizeVal && !ExploringLoop) {
                // Symbolic size - use Z3 to check if offset < baseLen
                auto result = checkBufferBoundsWithZ3(offset, baseLen, 0, ptrPath);
                if (result == false) {
                    // Proven OOB
                    Diag.Report(UO->getBeginLoc(), ErrBufferOOB)
                        << offset.str() << baseLen.str();
                } else if (!result) {
                    emitCategoryD(UO->getBeginLoc(), ptrPath, "_offset",
                                  offset.str() + " < " + baseLen.str(),
                                  "buffer bounds could not be verified");
                }
            }
        }

        return true;
    }

    // ============================================================
    // RETURN STATEMENT HANDLING: Scope-exit constraints + auto-postcondition inference
    // ============================================================
    // 1. Check scope-exit constraints (linear type tracking) at return
    // 2. Infer _result == expr postcondition for method returns
    bool VisitReturnStmt(ReturnStmt *RS) {
        // TIER 1: Check for uninitialized return value
        // Returning an uninitialized variable is undefined behavior
        if (const Expr *RetVal = RS->getRetValue()) {
            const Expr *Inner = RetVal->IgnoreParenImpCasts();
            if (auto *DRE = dyn_cast<DeclRefExpr>(Inner)) {
                if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    FieldPath varPath = FieldPath::fromDecl(VD);
                    QualType varType = VD->getType();
                    // Check scalars (pointers handled by checkUninitializedPointer elsewhere)
                    if ((varType->isArithmeticType() || varType->isBooleanType()) &&
                        !varType->isPointerType()) {
                        checkUninitializedVar(varPath.toStringRef(), RetVal->getBeginLoc());
                    }
                }
            }
            // TIER 5.2: Check for uninitialized heap read in return statement
            // Pattern: return *p; where p points to uninitialized heap memory
            else if (auto *UO = dyn_cast<UnaryOperator>(Inner)) {
                if (UO->getOpcode() == UO_Deref) {
                    Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                    FieldPath ptrPath = getTypedPath(SubExpr);
                    if (ptrPath.isValid()) {
                        auto it = State.VarToResource.find(ptrPath.toStringRef());
                        if (it != State.VarToResource.end()) {
                            ResourceId rid = it->second;
                            auto hosIt = State.HeapObjectStates.find(rid);
                            if (hosIt != State.HeapObjectStates.end() && !hosIt->second.initialized) {
                                Diag.Report(RetVal->getBeginLoc(), ErrUninitializedHeap) << ptrPath.toStringRef();
                                Diag.Report(hosIt->second.allocLoc, NoteHeapAllocatedHere);
                            }
                        }
                    }
                }
            }
        }

        // OWNERSHIP TRANSFER: If returning a resource pointer, remove its constraint
        // Pattern: void* alloc() { void* p = malloc(100); return p; }
        // Returning p transfers ownership to caller - no leak
        if (const Expr *RetVal = RS->getRetValue()) {
            if (RetVal->getType()->isPointerType()) {
                FieldPath retPath = getTypedPath(RetVal->IgnoreParenImpCasts());
                if (retPath.isValid()) {
                    clearConstraintsForVar(retPath.toStringRef());
                }
            }
        }

        // SCOPE-EXIT CONSTRAINT CHECK: Must happen for ALL return statements
        // This catches resource leaks on early returns like:
        //   FILE* f = fopen(...);
        //   if (error) return -1;  // ERROR: f still open
        if (CurrentFunctionDecl) {
            checkScopeExitConstraints(RS->getReturnLoc());
        }

        // AUTO-POSTCONDITION INFERENCE: Only for method bodies
        if (!CurrentMethodClass || !CurrentFunctionDecl) return true;

        const Expr *RetVal = RS->getRetValue();
        if (!RetVal) return true;  // void return

        // Skip pointer returns (handled by separate return analysis)
        if (RetVal->getType()->isPointerType()) return true;

        // Only track integer/boolean returns for now
        QualType retType = RetVal->getType();
        if (!retType->isIntegerType() && !retType->isBooleanType()) return true;

        // Parse the return expression using the unified expression parser
        auto parseResult = parsePostconditionExpr(RetVal, Ctx, "_result",
                                                  CurrentMethodClass, CurrentFunctionDecl);

        // Record the inferred _result postcondition
        const auto& expr = parseResult.expr;
        if (expr.kind != SymValue::UNKNOWN) {
            addInferredPostcondition("_result", expr,
                                     parseResult.usesOld, parseResult.argIdx);
        }

        return true;
    }

    // Handle GCC inline asm: output operands ("=r"(var), "+r"(var)) initialize
    // the variable. Without this, DTT falsely flags asm-initialized vars as
    // uninitialized. Found by scanning libwebp (src/dsp/cpu.c:74).
    bool VisitGCCAsmStmt(GCCAsmStmt *Asm) {
        // Output operands: constraints starting with '=' (write-only) or '+' (read-write)
        for (unsigned i = 0; i < Asm->getNumOutputs(); ++i) {
            StringRef constraint = Asm->getOutputConstraint(i);
            if (constraint.starts_with("=") || constraint.starts_with("+")) {
                Expr *OutputExpr = Asm->getOutputExpr(i);
                if (!OutputExpr) continue;
                FieldPath varPath = getTypedPath(OutputExpr->IgnoreParenImpCasts());
                if (varPath.isValid()) {
                    setFieldTyped(varPath, "_initialized", SymValue::Const(1));
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
            } else if (!ExploringLoop) {
                // Symbolic bounds - use Z3 to check
                bool isUnsignedIdx = idxType->isUnsignedIntegerType();
                auto result = checkBufferBoundsWithZ3(idx, size, minIdx, basePath, isUnsignedIdx);
                // Heuristic: a signed index whose only possible violation is
                // a negative value is accepted (docs/HEURISTICS.md #2).
                if (!result && !isUnsignedIdx && !idx.isGuaranteedNonNegative() &&
                    checkBufferBoundsWithZ3(idx, size, minIdx, basePath,
                                            /*isUnsignedIndex=*/true) == true)
                    result = true;
                if (result == false) {
                    Diag.Report(ASE->getBeginLoc(), ErrBufferOOB)
                        << idx.str() << size.str();
                    Diag.Report(ASE->getBeginLoc(), NoteBufferGuard)
                        << idx.str() << size.str();
                } else if (!result) {
                    // Category D: can't prove bounds are safe
                    std::string constraint = idx.str() + " < " + size.str();
                    emitCategoryD(ASE->getBeginLoc(), basePath, "_size", constraint,
                                  "buffer bounds could not be verified");
                }
            }
        }

        return true;
    }

    // Check if a dot-access MemberExpr reads an uninitialized scalar/pointer field.
    // Write contexts (assignment LHS, address-of, reference binding) mark as initialized instead.
    void checkUninitMemberRead(MemberExpr *ME) {
        auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (!FD || !FD->getIdentifier())
            return;

        QualType ft = FD->getType();
        if (!ft->isArithmeticType() && !ft->isBooleanType() && !ft->isPointerType())
            return;

        FieldPath memberPath = getTypedPath(ME);
        if (!memberPath.isValid())
            return;

        SymValue initVal = getFieldTyped(memberPath, "_initialized");
        if (initVal.kind != SymValue::CONST || initVal.constVal != 0)
            return;

        // Check if this is a write/escape context
        bool isWrite = false;
        for (const auto &P : Ctx.getParents(*ME)) {
            if (auto *BO = P.get<BinaryOperator>()) {
                if (BO->isAssignmentOp() &&
                    BO->getLHS()->IgnoreParenImpCasts() == ME) {
                    isWrite = true;
                    break;
                }
            } else if (auto *UO = P.get<UnaryOperator>()) {
                if (UO->getOpcode() == UO_AddrOf ||
                    UO->isIncrementDecrementOp()) {
                    isWrite = true;
                    break;
                }
            } else if (auto *VD = P.get<VarDecl>()) {
                if (VD->getType()->isReferenceType()) {
                    isWrite = true;
                    break;
                }
            }
        }

        if (isWrite) {
            setFieldTyped(memberPath, "_initialized", SymValue::Const(1));
        } else {
            Diag.Report(ME->getBeginLoc(), ErrUninitializedVar)
                << memberPath.toStringRef();
        }
    }

    // Handle member access - union tracking and pointer dereference checks
    bool VisitMemberExpr(MemberExpr *ME) {
        Expr *Base = ME->getBase()->IgnoreParenImpCasts();

        // Check uninitialized struct member read (dot access, scalar/pointer fields)
        if (!ME->isArrow())
            checkUninitMemberRead(ME);

        // Check union member access (applies to both dot and arrow)
        // Skip for 'this' access - anonymous unions in class templates
        // are typically guarded by member variables (like has_value_)
        // For anonymous unions: this->anon_union.field, base is MemberExpr
        bool skipUnionCheck = isa<CXXThisExpr>(Base);
        if (!skipUnionCheck) {
            // Check if base is MemberExpr accessing anonymous union from this
            if (auto *BaseME = dyn_cast<MemberExpr>(Base)) {
                auto *BaseBase = BaseME->getBase()->IgnoreParenImpCasts();
                if (isa<CXXThisExpr>(BaseBase)) {
                    // Anonymous union member access from this
                    skipUnionCheck = true;
                }
            }
        }

        QualType baseType = Base->getType();
        if (baseType->isPointerType())
            baseType = baseType->getPointeeType();
        if (auto *RD = baseType->getAsRecordDecl()) {
            if (RD->isUnion() && !skipUnionCheck) {
                FieldPath baseTypedPath = getTypedPath(Base);
                if (baseTypedPath.isValid()) {
                    SymValue active = getFieldTyped(baseTypedPath, "_union_active");
                    if (active.isString()) {
                        StringRef activeMember = active.getStringValue();
                        auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
                        if (FD && FD->getIdentifier()) {
                            StringRef accessedMember = FD->getName();
                            // Check if accessing inactive member
                            if (!activeMember.empty() && activeMember != accessedMember) {
                                // Check for various exceptions to union access rules
                                bool isAddressOf = false;
                                bool isDestructorCall = false;
                                bool isNestedAccess = false;
                                for (const auto &Parent : Ctx.getParents(*ME)) {
                                    // Exception 1: Address-of is always allowed
                                    if (auto *UO = Parent.get<UnaryOperator>()) {
                                        if (UO->getOpcode() == UO_AddrOf) {
                                            isAddressOf = true;
                                            break;
                                        }
                                    }
                                    // Exception 2: Destructor calls are allowed
                                    // (e.g., error_.~E() in conditional cleanup)
                                    // AST structure: MemberExpr(u.e) -> MemberExpr(.~E) -> CXXMemberCallExpr
                                    if (auto *MCE = Parent.get<CXXMemberCallExpr>()) {
                                        if (auto *MD = MCE->getMethodDecl()) {
                                            if (isa<CXXDestructorDecl>(MD)) {
                                                isDestructorCall = true;
                                                break;
                                            }
                                        }
                                    }
                                    // Also check if parent MemberExpr accesses a destructor
                                    if (auto *ParentME = Parent.get<MemberExpr>()) {
                                        if (auto *MD = dyn_cast<CXXMethodDecl>(ParentME->getMemberDecl())) {
                                            if (isa<CXXDestructorDecl>(MD)) {
                                                isDestructorCall = true;
                                                break;
                                            }
                                        }
                                    }
                                    // Exception 3: Nested struct access (common initial sequence)
                                    // If parent is another MemberExpr accessing a field, we're
                                    // accessing u.b.field which may be valid for common initial sequence
                                    if (auto *ParentME = Parent.get<MemberExpr>()) {
                                        // Only allow if the member type is a record type AND
                                        // the parent is accessing a field (not a method like destructor)
                                        if (FD->getType()->isRecordType()) {
                                            if (auto *ParentFD = dyn_cast<FieldDecl>(ParentME->getMemberDecl())) {
                                                (void)ParentFD;  // Just checking it's a field access
                                                isNestedAccess = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (isAddressOf || isDestructorCall || isNestedAccess) {
                                    // Taking address of inactive member is OK
                                    // Destructor calls are OK (guarded by conditionals)
                                    // Nested struct access may be common initial sequence
                                }
                                // Exception 4: char/unsigned char access is always allowed
                                else {
                                    QualType memberType = FD->getType().getCanonicalType();
                                    bool isCharAccess = memberType->isCharType() ||
                                        (memberType->isArrayType() &&
                                         memberType->getArrayElementTypeNoTypeQual()->isCharType());
                                    if (!isCharAccess) {
                                        // C++ mode: error (UB)
                                        // C mode: warning (type punning)
                                        if (Ctx.getLangOpts().CPlusPlus) {
                                            Diag.Report(ME->getBeginLoc(), ErrUnionInactiveMember)
                                                << accessedMember << activeMember;
                                        } else {
                                            Diag.Report(ME->getBeginLoc(), WarnUnionTypePunning)
                                                << accessedMember << activeMember;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Arrow operator null check (existing logic)
        if (!ME->isArrow()) return true;

        if (!Base->getType()->isPointerType()) return true;

        // Skip null check for 'this' pointer - assumed always valid in member functions
        if (isa<CXXThisExpr>(Base)) return true;

        // Phase 1 Migration: Use typed path
        FieldPath ptrTypedPath = getTypedPath(Base);
        std::string ptrPath = ptrTypedPath.toString();
        if (!ptrTypedPath.isValid()) return true;

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
        // Argument construction precedes the call's checks and postconditions.
        // Only a selected vector move constructor consumes the source; casts,
        // copies and reference binding alone do not establish a move.
        for (const Expr *Arg : E->arguments()) {
            const auto *Construction = dyn_cast<CXXConstructExpr>(Arg->IgnoreImplicit()->IgnoreParens());
            if (!Construction || Construction->getNumArgs() == 0)
                continue;
            const auto *Ctor = Construction->getConstructor();
            if (!Ctor->isMoveConstructor() || safeGetName(Ctor->getParent()) != "vector" ||
                !isInStdNamespace(Ctor->getParent()->getDeclContext()))
                continue;
            const Expr *Source = Construction->getArg(0)->IgnoreParenImpCasts();
            if (isStdMove(Source))
                Source = cast<CallExpr>(Source)->getArg(0);
            FieldPath Path = getTypedPath(Source->IgnoreParenCasts());
            if (Path.isValid() && !Path.hasRuntimeIndex())
                invalidatePropertiesWithProvenance(Path.toString(), Construction->getBeginLoc(),
                                                  "vector move construction");
        }

        // Skip if it's a member call or operator call (handled separately)
        if (isa<CXXMemberCallExpr>(E) || isa<CXXOperatorCallExpr>(E)) return true;

        auto *Callee = E->getDirectCallee();
        if (!Callee) return true;
        if (!Callee->getIdentifier()) return true;  // Skip operators, etc.
        StringRef fn = Callee->getName();

        // These builtins write the va_list destination; on Windows it is a
        // pointer passed by reference, not an uninitialized pointer read.
        bool StartsVAList = false;
        bool CopiesVAList = false;
        switch (Callee->getBuiltinID()) {
        case Builtin::BI__builtin_va_start:
        case Builtin::BI__builtin_ms_va_start:
            StartsVAList = true;
            break;
        case Builtin::BI__builtin_va_copy:
        case Builtin::BI__builtin_ms_va_copy:
            CopiesVAList = true;
            break;
        default:
            break;
        }
        if ((StartsVAList || CopiesVAList) && E->getNumArgs() >= 2) {
            SymValue Initialized = SymValue::Const(1);
            if (CopiesVAList) {
                const Expr *Source = E->getArg(1)->IgnoreParenImpCasts();
                FieldPath SourcePath = getTypedPath(Source);
                if (SourcePath.isValid()) {
                    checkUninitializedPointer(SourcePath.toStringRef(),
                                              Source->getBeginLoc());
                    checkUseAfterFree(SourcePath.toStringRef(), Source->getBeginLoc());
                    Initialized = getFieldTyped(SourcePath, "_initialized");
                } else {
                    Initialized = SymValue::Unknown();
                }
            }
            FieldPath Destination = getTypedPath(E->getArg(0)->IgnoreParenImpCasts());
            if (Destination.isValid())
                setFieldTyped(Destination, "_initialized", Initialized);
            return true;
        }

        // TIER 1: Check for uninitialized scalar arguments passed by value
        // Passing an uninitialized variable to a function is UB if passed by value
        for (unsigned i = 0; i < E->getNumArgs(); ++i) {
            const Expr *Arg = E->getArg(i)->IgnoreParenImpCasts();
            // Skip address-of expressions (output parameter pattern)
            if (auto *UO = dyn_cast<UnaryOperator>(Arg)) {
                if (UO->getOpcode() == UO_AddrOf) continue;
                // TIER 5.2: Check for uninitialized heap read in function argument
                // Pattern: foo(*p) where p points to uninitialized heap memory
                if (UO->getOpcode() == UO_Deref) {
                    Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
                    FieldPath ptrPath = getTypedPath(SubExpr);
                    if (ptrPath.isValid()) {
                        auto it = State.VarToResource.find(ptrPath.toStringRef());
                        if (it != State.VarToResource.end()) {
                            ResourceId rid = it->second;
                            auto hosIt = State.HeapObjectStates.find(rid);
                            if (hosIt != State.HeapObjectStates.end() && !hosIt->second.initialized) {
                                Diag.Report(Arg->getBeginLoc(), ErrUninitializedHeap) << ptrPath.toStringRef();
                                Diag.Report(hosIt->second.allocLoc, NoteHeapAllocatedHere);
                            }
                        }
                    }
                }
            }
            // After IgnoreParenImpCasts, Arg is exactly one of these types
            // Handle CXXConstructExpr (copy ctor when passing struct by value)
            if (auto *CE = dyn_cast<CXXConstructExpr>(Arg)) {
                if (CE->getNumArgs() == 1) {
                    // Copy constructor - check the source
                    const Expr *Src = CE->getArg(0)->IgnoreParenImpCasts();
                    if (auto *SrcDRE = dyn_cast<DeclRefExpr>(Src)) {
                        if (auto *VD = dyn_cast<VarDecl>(SrcDRE->getDecl())) {
                            QualType varType = VD->getType();
                            if (auto *RD = varType->getAsRecordDecl()) {
                                if (!RD->isUnion()) {
                                    FieldPath varPath = FieldPath::fromDecl(VD);
                                    checkStructMembersInitialized(varPath, RD, Arg->getBeginLoc());
                                }
                            }
                        }
                    }
                }
            }
            // Direct variable reference
            else if (auto *DRE = dyn_cast<DeclRefExpr>(Arg)) {
                if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    QualType varType = VD->getType();
                    // Check scalars passed by value
                    if ((varType->isArithmeticType() || varType->isBooleanType()) &&
                        !varType->isPointerType() && !varType->isReferenceType()) {
                        FieldPath varPath = FieldPath::fromDecl(VD);
                        checkUninitializedVar(varPath.toStringRef(), Arg->getBeginLoc());
                    }
                    // Check pointers passed by value (reading uninit pointer is UB)
                    else if (varType->isPointerType()) {
                        FieldPath varPath = FieldPath::fromDecl(VD);
                        StringRef path = varPath.toStringRef();
                        checkUninitializedPointer(path, Arg->getBeginLoc());
                        checkUseAfterFree(path, Arg->getBeginLoc());
                    }
                    // Check structs passed by value - all members are read
                    else if (auto *RD = varType->getAsRecordDecl()) {
                        if (!RD->isUnion()) {
                            FieldPath varPath = FieldPath::fromDecl(VD);
                            checkStructMembersInitialized(varPath, RD, Arg->getBeginLoc());
                        }
                    }
                }
            }
            // Struct member passed to function: sink(s.x)
            else if (auto *ME = dyn_cast<MemberExpr>(Arg)) {
                FieldPath memberPath = getTypedPath(ME);
                if (memberPath.isValid()) {
                    QualType memberType = ME->getType();
                    if ((memberType->isArithmeticType() || memberType->isBooleanType()) &&
                        !memberType->isPointerType()) {
                        checkUninitializedVar(memberPath.toStringRef(), Arg->getBeginLoc());
                    } else if (memberType->isPointerType()) {
                        StringRef path = memberPath.toStringRef();
                        checkUninitializedPointer(path, Arg->getBeginLoc());
                        checkUseAfterFree(path, Arg->getBeginLoc());
                    }
                }
            }
        }

        // Handle free() calls for lifetime tracking
        if (isDeallocationFunction(Callee) && E->getNumArgs() >= 1) {
            // Phase 1 Migration: Use typed path
            FieldPath ptrTypedPath = getTypedPath(E->getArg(0));
            std::string ptrPath = ptrTypedPath.toString();
            if (!ptrPath.empty()) {
                auto it = State.PointerLifetimes.find(ptrPath);
                if (it != State.PointerLifetimes.end()) {
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
                    isStdSwap = (NS->getName() == "std");
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

        // Handle std::sort(container.begin(), container.end())
        // After sorting, the container satisfies the 'sorted' concept
        if (isStdSort(E) && E->getNumArgs() >= 2) {
            // Get container from first iterator argument
            std::string containerPath = getContainerFromIterator(E->getArg(0));
            if (!containerPath.empty()) {
                // Verify second arg is also from same container (end iterator)
                std::string containerPath2 = getContainerFromIterator(E->getArg(1));
                if (containerPath == containerPath2) {
                    // Add proof that container is now sorted
                    // This enables passing to functions requiring 'sorted'
                    addProof(containerPath, "_sorted==1");
                    FieldPath typedPath = FieldPath::fromSynthetic(containerPath);
                    setFieldTyped(typedPath, "_sorted", SymValue::Const(1));
                }
            }
        }

        // Handle std::any_cast<T>(any) - check _has_value and _any_type
        if (fn == "any_cast" && E->getNumArgs() == 1 &&
            isInStdNamespace(Callee->getDeclContext())) {
            Expr *Arg = E->getArg(0)->IgnoreParenImpCasts();
            // Handle both any_cast(a) and any_cast(&a)
            bool isPointerVariant = false;
            if (auto *UO = dyn_cast<UnaryOperator>(Arg)) {
                if (UO->getOpcode() == UO_AddrOf) {
                    Arg = UO->getSubExpr()->IgnoreParenImpCasts();
                    isPointerVariant = true;
                }
            }
            FieldPath argPath = getTypedPath(Arg);
            if (argPath.isValid()) {
                std::string pathStr = argPath.toString();

                // 1. Check _has_value first
                bool hasValueOk = false;
                bool hasMatchingProof = hasProof(pathStr, "_has_value==1") ||
                                       hasProof(pathStr, "_has_value!=0");
                if (hasMatchingProof) {
                    hasValueOk = true;
                } else {
                    SymValue hasValue = getFieldTyped(argPath, "_has_value");
                    auto hasValueEval = hasValue.eval();
                    if (hasValueEval && *hasValueEval == 0) {
                        Diag.Report(E->getBeginLoc(), ErrMethodReq)
                            << "std" << "any_cast" << "_has_value == 1"
                            << pathStr << "_has_value" << hasValue.str();
                    } else if (hasValueEval && *hasValueEval == 1) {
                        hasValueOk = true;
                    } else {
                        // Unknown _has_value - only warn if not pointer variant
                        // Pointer variant (any_cast<T>(&a)) returns nullptr on failure
                        if (!isPointerVariant) {
                            if (!emitCategoryC(E->getBeginLoc(), pathStr, "_has_value", "_has_value == 1")) {
                                if (GlobalMode != DTTMode::Quiet) {
                                    emitCategoryD(E->getBeginLoc(), pathStr, "_has_value",
                                                 "_has_value == 1", "field value unknown");
                                }
                            }
                        }
                    }
                }

                // 2. Check _any_type if we know it (non-pointer variant only)
                // Pointer variant is safe - returns nullptr on mismatch
                if (hasValueOk && !isPointerVariant) {
                    // Get requested type from template argument using helper
                    if (auto tplArg = getFirstTemplateArg(E)) {
                        if (tplArg->type) {
                            // Strip references: any_cast<T&> should match stored T
                            QualType requestedType = tplArg->type->getNonReferenceType()
                                                                  .getCanonicalType();
                            std::string typeStr = requestedType.getAsString();

                            // Check for proof-based type matching/mismatching
                            llvm::SmallString<64> proofKey("_any_type==");
                            proofKey += typeStr;
                            llvm::SmallString<64> negProofKey("_any_type!=");
                            negProofKey += typeStr;

                            bool hasMatchingTypeProof = hasProof(pathStr, proofKey);
                            bool hasContradictingTypeProof = hasProof(pathStr, negProofKey);

                            if (hasContradictingTypeProof) {
                                // Proof exists that type is NOT what we need - definite error
                                llvm::SmallString<64> errMsg("proved !=");
                                errMsg += typeStr;
                                Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                    << "std" << "any_cast"
                                    << ("_any_type == " + typeStr)
                                    << pathStr << "_any_type" << errMsg;
                            } else if (hasMatchingTypeProof) {
                                // Proof exists - access is safe
                            } else {
                                // Fall back to tracking via AnyContainedTypes
                                auto typeIt = State.AnyContainedTypes.find(pathStr);
                                if (typeIt != State.AnyContainedTypes.end()) {
                                    QualType storedType = typeIt->second;
                                    // Use proper type comparison, not string comparison
                                    if (!Ctx.hasSameType(requestedType, storedType)) {
                                        Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                            << "std" << "any_cast"
                                            << ("_any_type == " + typeStr)
                                            << pathStr << "_any_type" << storedType.getAsString();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Handle std::get<I>(variant) and std::get<T>(variant) - check _variant_index
        if (fn == "get" && E->getNumArgs() == 1 && isInStdNamespace(Callee->getDeclContext())) {
            Expr *Arg = E->getArg(0)->IgnoreParenImpCasts();
            FieldPath argPath = getTypedPath(Arg);
            if (argPath.isValid()) {
                std::string pathStr = argPath.toString();
                QualType argType = Arg->getType().getNonReferenceType();

                // Check if argument is std::variant
                if (auto *TST = argType->getAs<TemplateSpecializationType>()) {
                    if (auto *TD = TST->getTemplateName().getAsTemplateDecl()) {
                        StringRef tplName = TD->getName();
                        if (tplName == "variant") {
                            // Get the template argument using helper
                            std::optional<int64_t> requestedIndex;
                            if (auto tplArg = getFirstTemplateArg(E)) {
                                if (tplArg->index) {
                                    // std::get<I>(v) - integral index
                                    requestedIndex = *tplArg->index;
                                } else if (tplArg->type) {
                                    // std::get<T>(v) - map type to index
                                    if (auto idx = getVariantAlternativeIndex(TST, *tplArg->type)) {
                                        requestedIndex = static_cast<int64_t>(*idx);
                                    }
                                }
                            }

                            if (requestedIndex.has_value()) {
                                int64_t reqIdx = *requestedIndex;

                                // Build proof keys for variant index
                                llvm::SmallString<32> proofKey("_variant_index==");
                                proofKey += std::to_string(reqIdx);
                                llvm::SmallString<32> negProofKey("_variant_index!=");
                                negProofKey += std::to_string(reqIdx);

                                bool hasMatchingProof = hasProof(pathStr, proofKey);
                                bool hasContradictingProof = hasProof(pathStr, negProofKey);

                                if (hasContradictingProof) {
                                    // Proof exists that index is NOT what we need - definite error
                                    llvm::SmallString<32> errMsg("proved !=");
                                    errMsg += std::to_string(reqIdx);
                                    Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                        << "std" << "get" << ("_variant_index == " + std::to_string(reqIdx))
                                        << pathStr << "_variant_index" << errMsg;
                                } else if (hasMatchingProof) {
                                    // Proof exists - access is safe
                                } else {
                                    SymValue variantIndex = getFieldTyped(argPath, "_variant_index");
                                    auto variantIndexEval = variantIndex.eval();
                                    if (variantIndexEval && *variantIndexEval != reqIdx) {
                                        // Known mismatch - definite error
                                        Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                            << "std" << "get" << ("_variant_index == " + std::to_string(reqIdx))
                                            << pathStr << "_variant_index" << variantIndex.str();
                                    } else if (variantIndexEval && *variantIndexEval == reqIdx) {
                                        // Known to match - access is safe
                                    } else {
                                        // Unknown - emit warning in non-quiet mode
                                        if (!emitCategoryC(E->getBeginLoc(), pathStr, "_variant_index",
                                                          "_variant_index == " + std::to_string(reqIdx))) {
                                            if (GlobalMode != DTTMode::Quiet) {
                                                emitCategoryD(E->getBeginLoc(), pathStr, "_variant_index",
                                                             "_variant_index == " + std::to_string(reqIdx),
                                                             "variant index unknown");
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

        // Handle std::holds_alternative<T>(variant) - creates proof
        // Note: Actual proof creation is handled in VisitIfStmt via extractHoldsAlternative
        // This is just a placeholder for future use (e.g., outside of if conditions)
        if (fn == "holds_alternative" && E->getNumArgs() == 1 &&
            isInStdNamespace(Callee->getDeclContext())) {
            // Guard extraction in VisitIfStmt handles the proof creation
        }

        // Handle C library free functions via annotation lookup (e.g., FILE* I/O)
        {
            FreeFunctionKey key{fn.data(), E->getNumArgs()};
            auto it = FreeFunctionAnnotations.find(key);
            if (it != FreeFunctionAnnotations.end()) {
                const FreeFunctionInfo& info = it->second;

                // Check all preconditions
                for (const auto& pre : info.preconditions) {
                    if (pre.argIndex >= 0 &&
                        static_cast<unsigned>(pre.argIndex) < E->getNumArgs()) {
                        Expr *ArgExpr = E->getArg(pre.argIndex)->IgnoreParenImpCasts();

                        // Raw argument value bounds: argN OP value (no field)
                        if (pre.isRawArg) {
                            std::optional<int64_t> argEval;
                            auto satisfied = checkArgBound(ArgExpr, pre.checkOp, pre.forbiddenValue,
                                                           Ctx, argEval);
                            if (satisfied == true) continue;
                            llvm::SmallString<16> argLabel("arg");
                            llvm::raw_svector_ostream(argLabel) << pre.argIndex;
                            llvm::SmallString<64> constraint(argLabel);
                            constraint += " ";
                            constraint += proofOpToString(pre.checkOp);
                            constraint += " ";
                            llvm::raw_svector_ostream(constraint) << pre.forbiddenValue;
                            if (!satisfied) {
                                if (GlobalMode != DTTMode::Quiet)
                                    emitCategoryD(E->getBeginLoc(), getExprPath(ArgExpr), "_value",
                                                  constraint.str().str(),
                                                  pre.errorReason[0] ? pre.errorReason
                                                                     : "runtime argument");
                                continue;
                            }
                            llvm::SmallString<64> reason;
                            if (pre.errorReason[0] == '\0') {
                                reason = argLabel;
                                reason += " is ";
                                llvm::raw_svector_ostream(reason) << *argEval;
                            } else {
                                reason = pre.errorReason;
                            }
                            Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                << "" << fn << constraint.str() << argLabel.str()
                                << "value" << reason.str();
                            continue;
                        }

                        // For _nonnull preconditions, try expression-level inference first.
                        // Handles &x, nullptr, string literals, array decay — works even
                        // when getTypedPath() can't resolve the expression.
                        if (StringRef(pre.field) == "_nonnull") {
                            auto inferred = inferNonnullFromExpr(ArgExpr);
                            if (inferred) {
                                if (checkProofOp(*inferred, pre.checkOp, pre.forbiddenValue))
                                    continue;
                                llvm::SmallString<16> argName("arg");
                                llvm::raw_svector_ostream(argName) << pre.argIndex;
                                Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                    << "" << fn << buildConstraintStr(pre).str()
                                    << argName.str()
                                    << pre.field << buildReasonStr(pre, *inferred).str();
                                continue;
                            }
                        }

                        FieldPath argPath = getTypedPath(ArgExpr);
                        if (argPath.isValid()) {
                            std::string pathStr = argPath.toString();

                            // Build proof key matching the actual precondition operator
                            llvm::SmallString<32> proofKey;
                            proofKey = pre.field;
                            proofKey += proofOpToString(pre.checkOp);
                            proofKey += std::to_string(pre.forbiddenValue);

                            if (hasProof(pathStr, proofKey.str()))
                                continue;

                            SymValue fieldVal = getFieldTyped(argPath, pre.field);
                            auto fieldEval = fieldVal.eval();
                            auto constraint = buildConstraintStr(pre);

                            if (fieldEval &&
                                checkProofOp(*fieldEval, pre.checkOp, pre.forbiddenValue)) {
                                // Known to satisfy precondition
                            } else if (fieldEval) {
                                Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                    << "" << fn << constraint.str()
                                    << pathStr << pre.field
                                    << buildReasonStr(pre, *fieldEval).str();
                            } else {
                                if (!emitCategoryC(E->getBeginLoc(), pathStr, pre.field, constraint.str().str())) {
                                    if (GlobalMode != DTTMode::Quiet) {
                                        emitCategoryD(E->getBeginLoc(), pathStr, pre.field,
                                                      constraint.str().str(), pre.errorReason);
                                    }
                                }
                            }
                        }
                    }
                }

                // Record .dtt preconditions as generic InferredPreconditions
                // for transitive propagation through internal call chains.
                // This enables the Datalog-style fixpoint to propagate ANY
                // .dtt requires directive, not just buffer size constraints.
                if (CurrentFnForPreconditions &&
                    hasInternalLinkage(CurrentFnForPreconditions)) {
                    for (const auto& pre : info.preconditions) {
                        if (pre.isRawArg) continue;  // Raw arg checks don't propagate
                        if (pre.argIndex < 0 ||
                            static_cast<unsigned>(pre.argIndex) >= E->getNumArgs())
                            continue;

                        const Expr *ArgExpr =
                            E->getArg(pre.argIndex)->IgnoreParenImpCasts();
                        auto callerParamIdx =
                            getParameterIndex(ArgExpr, CurrentFunctionDecl);
                        if (!callerParamIdx) continue;

                        // Build the generic precondition
                        dtt::InferredPrecondition genPre;
                        genPre.path =
                            (llvm::Twine("arg") +
                             llvm::Twine(*callerParamIdx)).str();
                        genPre.kind = dtt::PreconditionKind::Generic;
                        genPre.argIndex = *callerParamIdx;
                        genPre.field = pre.field;
                        genPre.constraintOp = pre.checkOp;
                        genPre.constraintValue = pre.forbiddenValue;
                        genPre.errorReason = pre.errorReason;

                        // Deduplicate
                        bool exists = false;
                        for (const auto& P : CurrentFnPreconditions) {
                            if (P.kind == dtt::PreconditionKind::Generic &&
                                P.argIndex == genPre.argIndex &&
                                P.field == genPre.field &&
                                P.constraintOp == genPre.constraintOp &&
                                P.constraintValue == genPre.constraintValue) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            CurrentFnPreconditions.push_back(
                                std::move(genPre));
                        }
                    }
                }

                // Postconditions applied after invalidation block (below)
            } else if (DiagArityMismatch.shouldEmit(GlobalMode) &&
                       FreeFunctionAnnotatedNames.contains(fn)) {
                // Name matches but arity differs — find the annotated arity
                for (const auto& [k, v] : FreeFunctionAnnotations) {
                    if (StringRef(k.functionName) == fn) {
                        Diag.Report(E->getBeginLoc(), DiagArityMismatch.get(GlobalMode))
                            << fn << E->getNumArgs() << k.numParams;
                        break;
                    }
                }
            }
        }

        // Helper lambda to get buffer size in bytes from an expression
        // Accounts for pointer offset: remaining capacity = total - offset*elemSize
        auto getBufferSizeBytes = [&](Expr *BufferArg) -> SymValue {
            FieldPath bufPath = getTypedPath(BufferArg);
            SymValue bufBytes = bufPath.isValid() ? getFieldTyped(bufPath, "_size_bytes") : SymValue::Unknown();

            // Fallback: compute from _base_length * element size
            if (bufBytes.kind == SymValue::UNKNOWN && bufPath.isValid()) {
                SymValue baseLen = getFieldTyped(bufPath, "_base_length");
                if (baseLen.kind != SymValue::UNKNOWN) {
                    QualType ptrType = BufferArg->getType();
                    if (ptrType->isPointerType()) {
                        QualType elemType = ptrType->getPointeeType();
                        auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
                        if (maybeElemSize) {
                            bufBytes = SymValue::Mul(baseLen, SymValue::Const(*maybeElemSize));
                        }
                    } else if (ptrType->isArrayType()) {
                        QualType elemType = Ctx.getAsArrayType(ptrType)->getElementType();
                        auto maybeElemSize = safeGetTypeSizeInChars(Ctx, elemType);
                        if (maybeElemSize) {
                            bufBytes = SymValue::Mul(baseLen, SymValue::Const(*maybeElemSize));
                        }
                    }
                }
            }

            // Final fallback: direct array type from DeclRefExpr
            if (bufBytes.kind == SymValue::UNKNOWN) {
                if (auto *DRE = dyn_cast<DeclRefExpr>(BufferArg)) {
                    QualType bufType = DRE->getType();
                    if (const auto *CAT = Ctx.getAsConstantArrayType(bufType)) {
                        auto maybeElemSize = safeGetTypeSizeInChars(Ctx, CAT->getElementType());
                        if (maybeElemSize) {
                            bufBytes = SymValue::Const(CAT->getSize().getZExtValue() * (*maybeElemSize));
                        }
                    }
                }
            }

            // Adjust for pointer offset: remaining = total - offset * elemSize
            if (bufBytes.kind != SymValue::UNKNOWN && bufPath.isValid()) {
                SymValue offset = getFieldTyped(bufPath, "_offset");
                if (offset.kind != SymValue::UNKNOWN) {
                    QualType ptrType = BufferArg->getType();
                    uint64_t elemSize = 1;
                    if (ptrType->isPointerType()) {
                        auto maybeElemSize = safeGetTypeSizeInChars(Ctx, ptrType->getPointeeType());
                        if (maybeElemSize) elemSize = *maybeElemSize;
                    }
                    SymValue offsetBytes = SymValue::Mul(offset, SymValue::Const(elemSize));
                    if (bufBytes.kind == SymValue::CONST && offset.kind == SymValue::CONST) {
                        uint64_t offBytes = offset.constVal * elemSize;
                        if (offBytes < bufBytes.constVal) {
                            bufBytes = SymValue::Const(bufBytes.constVal - offBytes);
                        } else {
                            bufBytes = SymValue::Const(0); // Offset exceeds buffer
                        }
                    } else {
                        bufBytes = SymValue::Sub(bufBytes, offsetBytes);
                    }
                }
            }

            return bufBytes;
        };

        // Helper lambda to get size argument as SymValue
        // Uses buildSymValueFromExpr to handle compound expressions like len + 10
        auto getSizeValue = [&](Expr *SizeArg) -> SymValue {
            // Try to build a symbolic expression tree (handles constants, vars, and arithmetic)
            if (auto val = buildSymValueFromExpr(SizeArg)) {
                return *val;
            }
            return SymValue::Unknown();
        };

        // Helper lambda to check if copySize <= bufSize is proven
        // Returns: true = proven safe, false = proven overflow, nullopt = unknown
        auto checkBoundsProven = [](const SymValue& size, const SymValue& bufSize) -> std::optional<bool> {
            if (size.kind == SymValue::CONST && bufSize.kind == SymValue::CONST) {
                return size.constVal <= bufSize.constVal;
            }
            // Use Z3 to check if size <= bufSize is proven
            // Pass assumeNonNegative=true since sizes are always non-negative (size_t)
            return checkComparisonWithZ3("_memfn", size, bufSize, ConstraintExpr::CmpLE, /*assumeNonNegative=*/true);
        };

        // Result of bounds checking - used by helper to communicate what diagnostic to emit
        enum class BoundsResult { Safe, Overflow, UnverifiedConst, UnverifiedSymbolic, UnverifiedUntracked, Skip };

        // Helper to determine bounds check result - reduces duplication across mem* functions
        auto checkBufferBounds = [&](const SymValue& opSize, const SymValue& bufSize) -> BoundsResult {
            // When buffer is untracked, infer precondition if operation size is known
            // This handles both symbolic sizes (n) and constant sizes (sizeof(int))
            // which is needed for pointer arithmetic: p = buf + off; memset(p, 0, 4)
            if (bufSize.kind == SymValue::UNKNOWN) {
                if (opSize.kind != SymValue::UNKNOWN) {
                    return BoundsResult::UnverifiedUntracked;
                }
                return BoundsResult::Skip;
            }

            auto proven = checkBoundsProven(opSize, bufSize);
            if (proven.has_value() && *proven == true) {
                return BoundsResult::Safe;
            }

            if (bufSize.kind == SymValue::CONST) {
                if (opSize.kind == SymValue::CONST && opSize.constVal > bufSize.constVal) {
                    return BoundsResult::Overflow;
                }
                return BoundsResult::UnverifiedConst;
            }

            return BoundsResult::UnverifiedSymbolic;
        };

        // Check memcpy/memmove bounds (both source and destination)
        // memcpy(dst, src, n) / memmove(dst, src, n)
        if ((fn == "memcpy" || fn == "memmove" || fn == "__builtin_memcpy" ||
             fn == "__builtin_memmove") && E->getNumArgs() >= 3) {
            Expr *DstArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SrcArg = E->getArg(1)->IgnoreParenImpCasts();
            Expr *SizeArg = E->getArg(2)->IgnoreParenImpCasts();

            SymValue dstBytes = getBufferSizeBytes(DstArg);
            SymValue srcBytes = getBufferSizeBytes(SrcArg);
            SymValue copySize = getSizeValue(SizeArg);

            // Check destination overflow
            switch (checkBufferBounds(copySize, dstBytes)) {
            case BoundsResult::Safe:
            case BoundsResult::Skip:
                break;
            case BoundsResult::Overflow:
                Diag.Report(E->getBeginLoc(), ErrMemcpyOverflow)
                    << (int)copySize.constVal << (int)dstBytes.constVal;
                break;
            case BoundsResult::UnverifiedConst: {
                Diag.Report(E->getBeginLoc(), DiagMemcpyUnverified.get(GlobalMode))
                    << (int)dstBytes.constVal;
                // Emit counterexample showing concrete values
                std::string cex = buildCounterexample(getExprPath(DstArg), "size", dstBytes, copySize);
                if (!cex.empty()) {
                    Diag.Report(E->getBeginLoc(), NoteCounterexample) << cex;
                }
                Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                    << (int)dstBytes.constVal;
                break;
            }
            case BoundsResult::UnverifiedSymbolic: {
                Diag.Report(E->getBeginLoc(), DiagMemcpySymbolicUnverified.get(GlobalMode));
                // Emit counterexample for symbolic values
                std::string cex = buildCounterexample(getExprPath(DstArg), "size", dstBytes, copySize);
                if (!cex.empty()) {
                    Diag.Report(E->getBeginLoc(), NoteCounterexample) << cex;
                }
                Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, copySize);
                break;
            }
            case BoundsResult::UnverifiedUntracked: {
                // Buffer is untracked but has symbolic size - infer precondition for call-site checking
                // Emit note explaining why buffer is untracked
                std::string path = getExprPath(DstArg);
                Diag.Report(E->getBeginLoc(), NoteUntracked) << path;
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, copySize);
                break;
            }
            }

            // Check source over-read (Heartbleed-style vulnerability)
            switch (checkBufferBounds(copySize, srcBytes)) {
            case BoundsResult::Safe:
            case BoundsResult::Skip:
                break;
            case BoundsResult::Overflow:
                Diag.Report(E->getBeginLoc(), ErrMemcpyOverread)
                    << (int)copySize.constVal << (int)srcBytes.constVal;
                break;
            case BoundsResult::UnverifiedConst:
                Diag.Report(E->getBeginLoc(), DiagMemcpySrcUnverified.get(GlobalMode))
                    << (int)srcBytes.constVal;
                Diag.Report(E->getBeginLoc(), NoteCheckSrcLen)
                    << (int)srcBytes.constVal;
                break;
            case BoundsResult::UnverifiedSymbolic:
                Diag.Report(E->getBeginLoc(), DiagMemcpySrcSymbolicUnverified.get(GlobalMode));
                Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                emitInferredPrecondition(E->getBeginLoc(), SrcArg, SizeArg, copySize);
                break;
            case BoundsResult::UnverifiedUntracked:
                // Buffer is untracked but has symbolic size - infer precondition for call-site checking
                emitInferredPrecondition(E->getBeginLoc(), SrcArg, SizeArg, copySize);
                break;
            }

            // TIER 5.3: memcpy/memmove initializes the destination
            markMemoryFunctionDstInitialized(DstArg);
        }

        // Check memset bounds
        // memset(dst, c, n) - check n <= dst size
        if ((fn == "memset" || fn == "__builtin_memset") && E->getNumArgs() >= 3) {
            Expr *DstArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SizeArg = E->getArg(2)->IgnoreParenImpCasts();

            SymValue dstBytes = getBufferSizeBytes(DstArg);
            SymValue setSize = getSizeValue(SizeArg);

            switch (checkBufferBounds(setSize, dstBytes)) {
            case BoundsResult::Safe:
            case BoundsResult::Skip:
                break;
            case BoundsResult::Overflow:
                Diag.Report(E->getBeginLoc(), ErrMemsetOverflow)
                    << (int)setSize.constVal << (int)dstBytes.constVal;
                break;
            case BoundsResult::UnverifiedConst: {
                Diag.Report(E->getBeginLoc(), DiagMemsetUnverified.get(GlobalMode))
                    << (int)dstBytes.constVal;
                // Emit counterexample showing concrete values
                std::string cex = buildCounterexample(getExprPath(DstArg), "size", dstBytes, setSize);
                if (!cex.empty()) {
                    Diag.Report(E->getBeginLoc(), NoteCounterexample) << cex;
                }
                Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                    << (int)dstBytes.constVal;
                break;
            }
            case BoundsResult::UnverifiedSymbolic: {
                Diag.Report(E->getBeginLoc(), DiagMemsetSymbolicUnverified.get(GlobalMode));
                // Emit counterexample for symbolic values
                std::string cex = buildCounterexample(getExprPath(DstArg), "size", dstBytes, setSize);
                if (!cex.empty()) {
                    Diag.Report(E->getBeginLoc(), NoteCounterexample) << cex;
                }
                Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, setSize);
                break;
            }
            case BoundsResult::UnverifiedUntracked: {
                // Emit note explaining why buffer is untracked
                std::string path = getExprPath(DstArg);
                Diag.Report(E->getBeginLoc(), NoteUntracked) << path;
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, setSize);
                break;
            }
            }

            // TIER 5.3: memset initializes the destination
            markMemoryFunctionDstInitialized(DstArg);
        }

        // Check memcmp bounds (both buffers)
        // memcmp(s1, s2, n) - check n <= s1 size AND n <= s2 size
        if ((fn == "memcmp" || fn == "__builtin_memcmp") && E->getNumArgs() >= 3) {
            Expr *S1Arg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *S2Arg = E->getArg(1)->IgnoreParenImpCasts();
            Expr *SizeArg = E->getArg(2)->IgnoreParenImpCasts();

            SymValue s1Bytes = getBufferSizeBytes(S1Arg);
            SymValue s2Bytes = getBufferSizeBytes(S2Arg);
            SymValue cmpSize = getSizeValue(SizeArg);

            // Check both buffers (avoid duplicate unverified warnings)
            bool warnedUnverified = false;
            auto s1Result = checkBufferBounds(cmpSize, s1Bytes);
            switch (s1Result) {
            case BoundsResult::Safe:
            case BoundsResult::Skip:
                break;
            case BoundsResult::Overflow:
                Diag.Report(E->getBeginLoc(), ErrMemcmpOverread)
                    << (int)cmpSize.constVal << (int)s1Bytes.constVal;
                break;
            case BoundsResult::UnverifiedConst:
                Diag.Report(E->getBeginLoc(), DiagMemcmpUnverified.get(GlobalMode))
                    << (int)s1Bytes.constVal;
                Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                    << (int)s1Bytes.constVal;
                warnedUnverified = true;
                break;
            case BoundsResult::UnverifiedSymbolic:
                Diag.Report(E->getBeginLoc(), DiagMemcmpSymbolicUnverified.get(GlobalMode));
                Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                emitInferredPrecondition(E->getBeginLoc(), S1Arg, SizeArg, cmpSize);
                warnedUnverified = true;
                break;
            case BoundsResult::UnverifiedUntracked:
                emitInferredPrecondition(E->getBeginLoc(), S1Arg, SizeArg, cmpSize);
                break;
            }

            auto s2Result = checkBufferBounds(cmpSize, s2Bytes);
            switch (s2Result) {
            case BoundsResult::Safe:
            case BoundsResult::Skip:
                break;
            case BoundsResult::Overflow:
                Diag.Report(E->getBeginLoc(), ErrMemcmpOverread)
                    << (int)cmpSize.constVal << (int)s2Bytes.constVal;
                break;
            case BoundsResult::UnverifiedConst:
                if (!warnedUnverified) {
                    Diag.Report(E->getBeginLoc(), DiagMemcmpUnverified.get(GlobalMode))
                        << (int)s2Bytes.constVal;
                    Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                        << (int)s2Bytes.constVal;
                }
                break;
            case BoundsResult::UnverifiedSymbolic:
                if (!warnedUnverified) {
                    Diag.Report(E->getBeginLoc(), DiagMemcmpSymbolicUnverified.get(GlobalMode));
                    Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                }
                emitInferredPrecondition(E->getBeginLoc(), S2Arg, SizeArg, cmpSize);
                break;
            case BoundsResult::UnverifiedUntracked:
                emitInferredPrecondition(E->getBeginLoc(), S2Arg, SizeArg, cmpSize);
                break;
            }
        }

        // Check memchr bounds
        // memchr(s, c, n) - check n <= s size
        if ((fn == "memchr" || fn == "__builtin_memchr") && E->getNumArgs() >= 3) {
            Expr *SArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SizeArg = E->getArg(2)->IgnoreParenImpCasts();

            SymValue sBytes = getBufferSizeBytes(SArg);
            SymValue searchSize = getSizeValue(SizeArg);

            switch (checkBufferBounds(searchSize, sBytes)) {
            case BoundsResult::Safe:
            case BoundsResult::Skip:
                break;
            case BoundsResult::Overflow:
                Diag.Report(E->getBeginLoc(), ErrMemchrOverread)
                    << (int)searchSize.constVal << (int)sBytes.constVal;
                break;
            case BoundsResult::UnverifiedConst:
                Diag.Report(E->getBeginLoc(), DiagMemchrUnverified.get(GlobalMode))
                    << (int)sBytes.constVal;
                Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                    << (int)sBytes.constVal;
                break;
            case BoundsResult::UnverifiedSymbolic:
                Diag.Report(E->getBeginLoc(), DiagMemchrSymbolicUnverified.get(GlobalMode));
                Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                emitInferredPrecondition(E->getBeginLoc(), SArg, SizeArg, searchSize);
                break;
            case BoundsResult::UnverifiedUntracked:
                emitInferredPrecondition(E->getBeginLoc(), SArg, SizeArg, searchSize);
                break;
            }
        }

        // strncpy: check n (arg2) <= dest (arg0) buffer size
        if ((fn == "strncpy" || fn == "__builtin_strncpy") && E->getNumArgs() >= 3) {
            Expr *DestArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SizeArg = E->getArg(2)->IgnoreParenImpCasts();

            SymValue destBytes = getBufferSizeBytes(DestArg);
            SymValue copySize = getSizeValue(SizeArg);

            if (destBytes.kind == SymValue::CONST) {
                auto proven = checkBoundsProven(copySize, destBytes);
                if (proven.has_value() && *proven == true) {
                    // Proven safe
                } else if (copySize.kind == SymValue::CONST && copySize.constVal > destBytes.constVal) {
                    Diag.Report(E->getBeginLoc(), ErrStrncpyOverflow)
                        << (int)copySize.constVal << (int)destBytes.constVal;
                } else if (copySize.kind != SymValue::CONST) {
                    Diag.Report(E->getBeginLoc(), DiagStrncpyUnverified.get(GlobalMode))
                        << (int)destBytes.constVal;
                    Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                        << (int)destBytes.constVal;
                }
            }
        }

        // snprintf: check n (arg1) <= dest (arg0) buffer size
        if ((fn == "snprintf" || fn == "__builtin_snprintf") && E->getNumArgs() >= 2) {
            Expr *DestArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SizeArg = E->getArg(1)->IgnoreParenImpCasts();

            SymValue destBytes = getBufferSizeBytes(DestArg);
            SymValue maxSize = getSizeValue(SizeArg);

            if (destBytes.kind == SymValue::CONST) {
                auto proven = checkBoundsProven(maxSize, destBytes);
                if (proven.has_value() && *proven == true) {
                    // Proven safe
                } else if (maxSize.kind == SymValue::CONST && maxSize.constVal > destBytes.constVal) {
                    Diag.Report(E->getBeginLoc(), ErrSnprintfOverflow)
                        << (int)maxSize.constVal << (int)destBytes.constVal;
                } else if (maxSize.kind != SymValue::CONST) {
                    Diag.Report(E->getBeginLoc(), DiagSnprintfUnverified.get(GlobalMode))
                        << (int)destBytes.constVal;
                    Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                        << (int)destBytes.constVal;
                }
            }
        }

        // sprintf: always warn - use snprintf instead
        if ((fn == "sprintf" || fn == "__builtin_sprintf") && E->getNumArgs() >= 1) {
            Diag.Report(E->getBeginLoc(), WarnSprintfUnsafe);
            Diag.Report(E->getBeginLoc(), NoteUseSnprintf);
        }

        // strcpy: check strlen(src) + 1 <= dest when src is a string literal
        if ((fn == "strcpy" || fn == "__builtin_strcpy") && E->getNumArgs() >= 2) {
            Expr *DestArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SrcArg = E->getArg(1)->IgnoreParenImpCasts();

            SymValue destBytes = getBufferSizeBytes(DestArg);

            if (destBytes.kind == SymValue::CONST) {
                // Check if source is a string literal
                if (auto *SL = dyn_cast<StringLiteral>(SrcArg)) {
                    uint64_t srcLen = SL->getLength() + 1; // +1 for null terminator
                    if (srcLen > destBytes.constVal) {
                        Diag.Report(E->getBeginLoc(), ErrStrcpyOverflow)
                            << (int)srcLen << (int)destBytes.constVal;
                        Diag.Report(E->getBeginLoc(), NoteUseStrncpy);
                    }
                } else {
                    // Source is not a literal - we can't verify the length
                    Diag.Report(E->getBeginLoc(), DiagStrcpyUnverified.get(GlobalMode))
                        << (int)destBytes.constVal;
                    Diag.Report(E->getBeginLoc(), NoteCheckSrcLen)
                        << (int)destBytes.constVal;
                }
            }
        }

        // strcat: check strlen(src) + 1 <= dest when src is a string literal
        // (assumes dest starts empty - conservative for non-empty dest)
        if ((fn == "strcat" || fn == "__builtin_strcat") && E->getNumArgs() >= 2) {
            Expr *DestArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SrcArg = E->getArg(1)->IgnoreParenImpCasts();

            SymValue destBytes = getBufferSizeBytes(DestArg);

            if (destBytes.kind == SymValue::CONST) {
                // Check if source is a string literal
                if (auto *SL = dyn_cast<StringLiteral>(SrcArg)) {
                    uint64_t srcLen = SL->getLength() + 1; // +1 for null terminator
                    if (srcLen > destBytes.constVal) {
                        Diag.Report(E->getBeginLoc(), ErrStrcatOverflow)
                            << (int)srcLen << (int)destBytes.constVal;
                        Diag.Report(E->getBeginLoc(), NoteUseStrncat);
                    }
                } else {
                    // Source is not a literal - we can't verify the length
                    Diag.Report(E->getBeginLoc(), DiagStrcatUnverified.get(GlobalMode))
                        << (int)destBytes.constVal;
                    Diag.Report(E->getBeginLoc(), NoteCheckSrcLen)
                        << (int)destBytes.constVal;
                }
            }
        }

        // strncat: check n + 1 <= dest (worst case: appends n chars + null)
        if ((fn == "strncat" || fn == "__builtin_strncat") && E->getNumArgs() >= 3) {
            Expr *DestArg = E->getArg(0)->IgnoreParenImpCasts();
            Expr *SizeArg = E->getArg(2)->IgnoreParenImpCasts();

            SymValue destBytes = getBufferSizeBytes(DestArg);
            SymValue appendSize = getSizeValue(SizeArg);

            if (destBytes.kind == SymValue::CONST) {
                // strncat appends at most n chars plus null terminator
                // Check: n + 1 <= dest capacity (worst case, assuming dest starts empty)
                auto proven = checkBoundsProven(appendSize, SymValue::Const(destBytes.constVal - 1));
                if (proven.has_value() && *proven == true) {
                    // Proven safe: n <= dest - 1, so n + 1 <= dest
                } else if (appendSize.kind == SymValue::CONST) {
                    uint64_t needed = appendSize.constVal + 1; // n chars + null
                    if (needed > destBytes.constVal) {
                        Diag.Report(E->getBeginLoc(), ErrStrncatOverflow)
                            << (int)appendSize.constVal << (int)destBytes.constVal;
                    }
                } else {
                    // Variable size, not proven safe
                    Diag.Report(E->getBeginLoc(), DiagStrncatUnverified.get(GlobalMode))
                        << (int)destBytes.constVal;
                    Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                        << (int)destBytes.constVal;
                }
            }
        }

        // Capture old values BEFORE precondition checks (for old() references in postconditions)
        auto oldValsForPostcond = captureOldValues(E);

        // Parse preconditions directly to ConstraintExprPtr (no bridge)
        llvm::SmallVector<ConstraintExprPtr, 4> preconditions;
        for (auto *A : Callee->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                llvm::StringRef s = Ann->getAnnotation();
                if (s.starts_with(kRequiresPrefix)) {
                    std::string translated = translateParamNames(s.drop_front(kRequiresPrefix.size()), Callee);
                    auto expr = parseConstraintExpr(translated);
                    if (expr) preconditions.push_back(expr);
                }
            }
        }

        llvm::SmallVector<std::string, 8> argPaths;
        for (unsigned i = 0; i < E->getNumArgs(); i++) {
            auto *Arg = E->getArg(i)->IgnoreParenImpCasts();
            argPaths.push_back(getExprPath(Arg));
        }

        // Build argument substitution map for expression evaluation
        std::map<std::string, SymValue> argVals;
        for (unsigned i = 0; i < E->getNumArgs(); i++) {
            std::string argKey = "arg" + std::to_string(i);
            auto *Arg = E->getArg(i)->IgnoreParenImpCasts();
            if (auto *IL = dyn_cast<IntegerLiteral>(Arg)) {
                argVals[argKey] = SymValue::Const(IL->getValue().getSExtValue());
            } else {
                std::string argPath = i < argPaths.size() ? argPaths[i] : "";
                if (!argPath.empty()) {
                    SymValue argVal = getEnvValue(argPath).value_or(SymValue::Unknown());
                    argVals[argKey] = (argVal.kind == SymValue::CONST) ? argVal : SymValue::Unknown();
                    for (const auto& [fieldPath, value] : State.TypedEnv) {
                        llvm::StringRef pathStr = fieldPath.toStringRef();
                        if (startsWithDotPrefix(pathStr, argPath) && pathStr.find("._proof_") == llvm::StringRef::npos) {
                            std::string field = pathStr.drop_front(argPath.size() + 1).str();
                            argVals[argKey + "." + field] = value;
                        }
                    }
                    // Copy array element facts for forall quantifier verification
                    // Map argPath's elements to argN so Z3 can verify forall(i, 0, n, arg0[i] > 0)
                    auto elemIt = State.ArrayElementFacts.find(argPath);
                    if (elemIt != State.ArrayElementFacts.end()) {
                        State.ArrayElementFacts[argKey] = elemIt->getValue();
                    }
                } else {
                    argVals[argKey] = SymValue::Unknown();
                }
            }
        }

        for (const auto& req : preconditions) {
            if (!req) continue;

            // Handle multi-arg comparisons: arg0._field op arg1 or arg0 op arg1._field
            if (auto mac = asMultiArgComparison(req)) {
                if (mac->lhsArgIdx < argPaths.size() && mac->rhsArgIdx < argPaths.size()) {
                    // Get LHS value - either arg._field or just arg's direct value
                    SymValue lhsVal;
                    if (!mac->lhsField.empty()) {
                        lhsVal = getField(argPaths[mac->lhsArgIdx], mac->lhsField);
                    } else {
                        // Direct arg value - check argVals or evaluate literal
                        std::string lhsKey = "arg" + std::to_string(mac->lhsArgIdx);
                        auto it = argVals.find(lhsKey);
                        if (it != argVals.end()) {
                            lhsVal = it->second;
                        } else if (mac->lhsArgIdx < E->getNumArgs()) {
                            auto *Arg = E->getArg(mac->lhsArgIdx)->IgnoreParenImpCasts();
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(Arg, R, Ctx)) {
                                lhsVal = SymValue::Const(R.Val.getInt().getSExtValue());
                            }
                        }
                    }

                    // Get RHS value - either arg._field or just arg's direct value
                    SymValue rhsVal;
                    if (!mac->rhsField.empty()) {
                        rhsVal = getField(argPaths[mac->rhsArgIdx], mac->rhsField);
                    } else {
                        // Direct arg value - check argVals or evaluate literal
                        std::string rhsKey = "arg" + std::to_string(mac->rhsArgIdx);
                        auto it = argVals.find(rhsKey);
                        if (it != argVals.end()) {
                            rhsVal = it->second;
                        } else if (mac->rhsArgIdx < E->getNumArgs()) {
                            auto *Arg = E->getArg(mac->rhsArgIdx)->IgnoreParenImpCasts();
                            Expr::EvalResult R;
                            if (safeEvaluateAsInt(Arg, R, Ctx)) {
                                rhsVal = SymValue::Const(R.Val.getInt().getSExtValue());
                            }
                        }
                    }

                    auto lhs = lhsVal.eval();
                    auto rhs = rhsVal.eval();
                    if (lhs && rhs) {
                        bool satisfied = false;
                        switch (mac->op) {
                            case ConstraintExpr::CmpGT: satisfied = *lhs > *rhs; break;
                            case ConstraintExpr::CmpGE: satisfied = *lhs >= *rhs; break;
                            case ConstraintExpr::CmpLT: satisfied = *lhs < *rhs; break;
                            case ConstraintExpr::CmpLE: satisfied = *lhs <= *rhs; break;
                            case ConstraintExpr::CmpEQ: satisfied = *lhs == *rhs; break;
                            case ConstraintExpr::CmpNE: satisfied = *lhs != *rhs; break;
                            default: break;
                        }
                        if (!satisfied) {
                            Diag.Report(E->getBeginLoc(), ErrMultiArg)
                                << fn << req->toString() << argPaths[mac->lhsArgIdx] << mac->lhsField << lhsVal.str()
                                << argPaths[mac->rhsArgIdx] << mac->rhsField << rhsVal.str();
                        }
                    } else {
                        // Could not evaluate - check for Category C (lost tracking)
                        if (!mac->lhsField.empty()) {
                            TrackingState ts = getFieldTrackingState(argPaths[mac->lhsArgIdx], mac->lhsField);
                            if (ts == TrackingState::UnknownLost) {
                                emitCategoryC(E->getBeginLoc(), argPaths[mac->lhsArgIdx], mac->lhsField, req->toString());
                                continue;
                            }
                        }
                        if (!mac->rhsField.empty()) {
                            TrackingState ts = getFieldTrackingState(argPaths[mac->rhsArgIdx], mac->rhsField);
                            if (ts == TrackingState::UnknownLost) {
                                emitCategoryC(E->getBeginLoc(), argPaths[mac->rhsArgIdx], mac->rhsField, req->toString());
                                continue;
                            }
                        }
                        // If not lost tracking, Category D
                        if (GlobalMode != DTTMode::Quiet) {
                            std::string fieldToReport = !mac->lhsField.empty() ? mac->lhsField : mac->rhsField;
                            std::string pathToReport = !mac->lhsField.empty() ? argPaths[mac->lhsArgIdx] : argPaths[mac->rhsArgIdx];
                            emitCategoryD(E->getBeginLoc(), pathToReport, fieldToReport, req->toString(), "could not evaluate");
                        }
                    }
                }
                continue;
            }

            // Handle nullptr/zero checks: arg0 != 0 or arg0 == 0
            if (auto npc = asNullptrCheck(req);
                npc && npc->argIdx < E->getNumArgs() &&
                E->getArg(npc->argIdx)->getType()->isPointerType()) {
                if (npc->argIdx >= argPaths.size()) continue;
                auto *Arg = E->getArg(npc->argIdx)->IgnoreParenImpCasts();
                std::string argPath = argPaths[npc->argIdx];

                if (argPath.empty()) continue;
                auto nonnullCheck = checkNonnull(argPath);
                if (!nonnullCheck.has_value()) {
                    Diag.Report(E->getBeginLoc(), ErrNullDerefUnk) << argPath;
                    emitNullProvenance(argPath, E->getBeginLoc());
                    Diag.Report(E->getBeginLoc(), NoteNullGuard) << argPath;
                } else if (!*nonnullCheck && npc->op == ConstraintExpr::CmpNE) {
                    Diag.Report(E->getBeginLoc(), ErrNullDeref) << argPath;
                } else if (*nonnullCheck && npc->op == ConstraintExpr::CmpEQ) {
                    Diag.Report(E->getBeginLoc(), ErrFail) << fn << req->toString() << argPath << "_nonnull" << "non-null";
                }
                continue;
            }

            // Handle arg vs constant: arg0 >= 1 (with no field)
            if (auto avc = asArgVsConstComparison(req)) {
                if (avc->argField.empty()) {
                    // Direct arg comparison: arg0 op constant
                    if (avc->argIdx >= E->getNumArgs()) continue;
                    auto *Arg = E->getArg(avc->argIdx)->IgnoreParenImpCasts();
                    std::string argPath = getExprPath(Arg);

                    auto Op = constraintExprKindToProofOp(avc->op);
                    std::optional<int64_t> Known;
                    auto Satisfied = Op ? checkArgBound(Arg, *Op, avc->value, Ctx, Known)
                                        : std::nullopt;
                    if (Satisfied == false) {
                        Diag.Report(E->getBeginLoc(), ErrFail) << fn << req->toString()
                            << argPath << "_value" << std::to_string(*Known);
                    } else if (!Satisfied && GlobalMode != DTTMode::Quiet) {
                        emitCategoryD(E->getBeginLoc(), argPath, "_value", req->toString(), "runtime argument");
                    }
                    continue;
                }

                // arg._field op constant
                if (avc->argIdx >= E->getNumArgs()) continue;
                auto *Arg = E->getArg(avc->argIdx)->IgnoreParenImpCasts();
                std::string argPath = getExprPath(Arg);
                if (argPath.empty() || hasRuntimeIndex(argPath)) continue;

                TrackingState trackState = getFieldTrackingState(argPath, avc->argField);
                if (trackState == TrackingState::UnknownLost) {
                    emitCategoryC(E->getBeginLoc(), argPath, avc->argField, req->toString());
                    continue;
                }

                SimpleFieldComparison sfc{avc->argField, avc->op, avc->value};
                auto result = checkSimpleFieldComparisonWithProofs(argPath, sfc);
                if (!result.has_value()) {
                    if (GlobalMode != DTTMode::Quiet) {
                        emitCategoryD(E->getBeginLoc(), argPath, avc->argField, req->toString(), "constraint undecidable");
                    }
                } else if (!*result) {
                    auto val = getField(argPath, avc->argField);
                    Diag.Report(E->getBeginLoc(), ErrFail) << fn << req->toString() << argPath << avc->argField << val.str();
                    emitProvenanceChain(argPath, avc->argField);
                    emitFixItSuggestion(E->getBeginLoc(), argPath, avc->argField, req->toString(), E);
                    // Backward search for free function preconditions
                    if (auto goalOp = constraintExprKindToProofOp(avc->op)) {
                        emitBackwardSearchForFreeFunc(E->getBeginLoc(), argPath,
                                                     avc->argField, *goalOp, avc->value);
                    }
                }
                continue;
            }

            // Handle forall/exists quantifiers via Z3
            if (req->kind == ConstraintExpr::Forall || req->kind == ConstraintExpr::Exists) {
                auto z3Result = checkWithZ3ExprWithArgs(req, argVals);
                if (!z3Result.has_value()) {
                    if (GlobalMode != DTTMode::Quiet) {
                        emitCategoryD(E->getBeginLoc(), "", "expr", req->toString(), "solver could not evaluate");
                    }
                } else if (!*z3Result) {
                    Diag.Report(E->getBeginLoc(), ErrFail) << fn << req->toString()
                        << (argPaths.empty() ? "" : argPaths[0]) << "expr" << "violated";
                }
                continue;
            }

            // Handle compound and complex expressions via general evaluator
            auto result = evaluateConstraintBool(req, argPaths.empty() ? "" : argPaths[0], argVals);
            if (!result.has_value()) {
                if (GlobalMode != DTTMode::Quiet) {
                    bool hasUntracked = false;
                    for (unsigned i = 0; i < argPaths.size(); i++) {
                        if (argPaths[i].empty()) continue;
                        if (getFieldTrackingState(argPaths[i], "_size") == TrackingState::UnknownLost) {
                            hasUntracked = true;
                            emitCategoryC(E->getBeginLoc(), argPaths[i], "_size", req->toString());
                            break;
                        }
                    }
                    if (!hasUntracked) {
                        emitCategoryD(E->getBeginLoc(), "", "expr", req->toString(), "could not evaluate");
                    }
                }
            } else if (!*result) {
                Diag.Report(E->getBeginLoc(), ErrFail) << fn << req->toString()
                    << (argPaths.empty() ? "" : argPaths[0]) << "expr" << "violated";
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
                        // Use frame-aware invalidation - only invalidate fields the function writes
                        llvm::SmallString<64> reason("call to unannotated function '");
                        reason += fn;
                        reason += "()'";
                        invalidateWrittenFieldsOnly(argPath, Callee, i, E->getBeginLoc(), reason);
                    }
                }
            }
        }

        // Out-parameter initialization heuristic: &local passed to non-const T*
        // implies the callee writes through the pointer. Mark local as initialized.
        // Scope: local scalars and structs only (arrays handled elsewhere).
        for (unsigned i = 0; i < E->getNumArgs() && i < Callee->getNumParams(); i++) {
            ParmVarDecl *Param = Callee->getParamDecl(i);
            QualType paramType = Param->getType();

            if (!paramType->isPointerType()) continue;
            QualType pointeeType = paramType->getPointeeType();
            if (pointeeType.isConstQualified()) continue;

            Expr *Arg = E->getArg(i)->IgnoreParenImpCasts();
            auto *UO = dyn_cast<UnaryOperator>(Arg);
            if (!UO || UO->getOpcode() != UO_AddrOf) continue;

            auto *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
            auto *DRE = dyn_cast<DeclRefExpr>(SubExpr);
            if (!DRE) continue;

            auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
            if (!VD || !VD->hasLocalStorage()) continue;

            FieldPath varPath = FieldPath::fromDecl(VD);
            QualType varType = VD->getType();
            if (varType->isArithmeticType() || varType->isBooleanType()) {
                setFieldTyped(varPath, "_initialized", SymValue::Const(1));
            } else if (auto *RD = varType->getAsRecordDecl()) {
                if (!RD->isUnion())
                    setStructMembersInitState(varPath, RD, true);
            }
        }

        // Check inferred requirements before applying any guarantees.
        checkCallSitePreconditions(E, Callee);
        if (!callRequirementsProven(E, Ctx)) return true;

        // Apply postconditions AFTER invalidation so they have the final word.
        // Postconditions from annotations describe the exact state after the call
        // and must not be overwritten by conservative argument invalidation.
        applyPostconditions("", E, oldValsForPostcond);  // C++ attribute postconds

        // Apply .dtt file arg postconditions.
        // hasDTTAnnotations() now recognizes .dtt files, so these functions
        // skip invalidation above. Postconditions set the expected state.
        {
            FreeFunctionKey freeKey{fn.data(), E->getNumArgs()};
            auto freeIt = FreeFunctionAnnotations.find(freeKey);
            if (freeIt != FreeFunctionAnnotations.end()) {
                for (const auto& post : freeIt->second.postconditions) {
                    if (post.argIndex >= 0 &&
                        static_cast<unsigned>(post.argIndex) < E->getNumArgs()) {
                        // Skip if `when` guard not satisfied (empty resultPath
                        // means result-targeted guards are correctly skipped)
                        if (post.guard && !isGuardSatisfied(*post.guard, "", E))
                            continue;
                        Expr *ModArg = E->getArg(post.argIndex)->IgnoreParenImpCasts();
                        FieldPath modPath = getTypedPath(ModArg);
                        if (modPath.isValid()) {
                            // Handle "deref." prefix in field names from .dtt annotations.
                            // Annotations like `ensures arg0.deref.@field = 0` mean
                            // "dereference arg0, then set @field". When the argument is
                            // &var, getTypedPath already resolves &var to var, so the
                            // deref is implicit and we strip the prefix.
                            StringRef fieldRef(post.field);
                            if (fieldRef.starts_with("deref."))
                                fieldRef = fieldRef.drop_front(6);

                            setFieldTyped(modPath, fieldRef, SymValue::Const(post.value));
                            // Clear stale proofs that conflict with the new value
                            std::string modPathStr = modPath.toString();
                            llvm::SmallString<32> proofKey;
                            proofKey = fieldRef;
                            proofKey += "!=0";
                            removeProof(modPathStr, proofKey.str());
                            proofKey = fieldRef;
                            proofKey += "==1";
                            removeProof(modPathStr, proofKey.str());
                        }
                    }
                }
            }
        }

        // Apply inferred postconditions from free function implementation analysis
        // (out-parameter postconditions like: void init(Vec& v, int n) { v._size = n; })
        auto inferredIt = InferredFnPostconditions.find(Callee->getCanonicalDecl());
        if (inferredIt != InferredFnPostconditions.end() && !inferredIt->second.empty()) {
            // Collect argument values for substitution
            std::map<std::string, SymValue> argSubst;
            for (unsigned i = 0; i < E->getNumArgs(); i++) {
                std::string argKey = "arg" + std::to_string(i);
                Expr *ArgExpr = E->getArg(i)->IgnoreParenImpCasts();
                Expr::EvalResult R;
                if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                    auto val = safeGetInt64(R.Val.getInt());
                    if (val) argSubst[argKey] = SymValue::Const(*val);
                } else {
                    FieldPath argTypedPath = getTypedPath(ArgExpr);
                    if (argTypedPath.isValid()) {
                        argSubst[argKey] = exprToSymValueImpl(ArgExpr, Ctx);
                    }
                }
            }

            // Apply each inferred postcondition (typically out-parameter postconditions)
            for (const auto& post : inferredIt->second) {
                if (post.targetArgIndex && *post.targetArgIndex < E->getNumArgs()) {
                    // Out-parameter postcondition: apply to argN
                    FieldPath argPath = getTypedPath(E->getArg(*post.targetArgIndex));
                    if (argPath.isValid()) {
                        SymValue result = post.expr.substitute(argSubst);
                        // Propagate to aliases
                        setFieldWithAliasPropagation(argPath, post.field, result);
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

        auto it = State.PointerLifetimes.find(ptrPath);
        if (it != State.PointerLifetimes.end()) {
            // Double-free check
            if (it->second.lifetime == PointerLifetime::Freed) {
                Diag.Report(DE->getBeginLoc(), ErrDoubleFree) << ptrPath;
                if (it->second.freeLocation.isValid()) {
                    Diag.Report(it->second.freeLocation, NoteFirstFree);
                }
            } else {
                if (it->second.lifetime == PointerLifetime::MaybeFreed)
                    reportMaybeFreed(ptrPath, it->second, DE->getBeginLoc());
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

    // Build "field OP value" constraint string from a precondition
    llvm::SmallString<64> buildConstraintStr(const dtt::FreeFunctionPrecondition& pre) {
        llvm::SmallString<64> s;
        s = pre.field;
        s += " ";
        s += proofOpToString(pre.checkOp);
        s += " ";
        llvm::raw_svector_ostream(s) << pre.forbiddenValue;
        return s;
    }

    // Build human-readable reason string for a precondition violation
    llvm::SmallString<64> buildReasonStr(const dtt::FreeFunctionPrecondition& pre,
                                         int64_t actualValue) {
        llvm::SmallString<64> s;
        if (pre.errorReason[0] != '\0') {
            s = pre.errorReason;
        } else {
            s = pre.field;
            s += " is ";
            llvm::raw_svector_ostream(s) << actualValue;
        }
        return s;
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
        FnScopeExitRequires.clear();
        TotalFunctions.clear();
        MethodAnnotations.clear();
        FunctionAnalysis.clear();
        initializeConceptRegistry();  // Reset to predefined concepts
        FnPreconditions.clear();          // Clear inferred preconditions from previous TU
        InferredFnPostconditions.clear(); // Clear inferred postconditions from previous TU
        if (!loadSyntheticAnnotations(Ctx.getDiagnostics())) return;

        // Install SARIF consumer wrapper if --sarif= was specified
        std::unique_ptr<dtt::SarifDiagConsumer> SarifConsumer;
        DiagnosticConsumer *OrigDiagClient = nullptr;
        bool OrigDiagOwned = false;
        if (!SarifOutputPath.empty()) {
            auto &SM = Ctx.getSourceManager();
            auto MainFile = SM.getFileEntryRefForID(SM.getMainFileID());
            // Clang can give stdin a virtual entry named <stdin>.
            auto Root = MainFile && MainFile->getName() != "<stdin>"
                            ? dtt::findRepoRoot(MainFile->getName())
                            : llvm::SmallString<128>();
            auto &DE = Ctx.getDiagnostics();
            OrigDiagClient = DE.getClient();
            OrigDiagOwned = DE.ownsClient();
            // Take ownership away so setClient won't delete the original
            std::unique_ptr<DiagnosticConsumer> OwnedOrig;
            if (OrigDiagOwned)
                OwnedOrig = DE.takeClient();
            SarifConsumer = std::make_unique<dtt::SarifDiagConsumer>(
                OrigDiagClient, SM, SarifOutputPath, Root);
            DE.setClient(SarifConsumer.get(), /*ShouldOwnClient=*/false);
            GlobalSarifConsumer = SarifConsumer.get();
            // Release original back — DE doesn't own it, we hold the ptr
            if (OwnedOrig)
                OwnedOrig.release();
        }

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
        // Function template pattern bodies are SKIPPED here (Pass 2b
        // analyzes their instantiations instead). Fold expressions and pack
        // expansions are unexpanded in patterns, producing false positives
        // (e.g., _size = 0 after fold push_back).
        DTTVisitor Visitor(Ctx);
        Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());

        // ====================================================
        // PASS 2b: Analyze function template instantiations
        // ====================================================
        // In template instantiations, fold expressions are fully expanded
        // into individual method calls that DTT's existing visitors handle.
        // We only traverse FUNCTION template specializations, not class
        // template member instantiations (to avoid false positives from
        // analyzing class methods without caller context).
        {
            auto &SM = Ctx.getSourceManager();
            SmallVector<FunctionDecl*, 16> Specs;
            // Collect function template specializations from all contexts
            SmallVector<DeclContext*, 8> Worklist;
            Worklist.push_back(Ctx.getTranslationUnitDecl());
            while (!Worklist.empty()) {
                DeclContext *DC = Worklist.pop_back_val();
                for (auto *D : DC->decls()) {
                    if (auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
                        for (auto *Spec : FTD->specializations()) {
                            // Skip explicit specializations — Pass 2 already
                            // analyzed them (they're concrete decls in the TU).
                            if (Spec->getTemplateSpecializationKind() ==
                                TSK_ExplicitSpecialization)
                                continue;
                            if (Spec->hasBody() &&
                                !SM.isInSystemHeader(Spec->getLocation())) {
                                Specs.push_back(Spec);
                            }
                        }
                    }
                    // Recurse into contexts that can contain function templates:
                    // namespaces, classes (method templates), linkage specs
                    if (isa<NamespaceDecl, CXXRecordDecl, LinkageSpecDecl>(D) &&
                        !SM.isInSystemHeader(D->getLocation())) {
                        Worklist.push_back(cast<DeclContext>(D));
                    }
                }
            }
            for (auto *Spec : Specs) {
                Visitor.TraverseFunctionDecl(Spec);
            }
        }

        // ====================================================
        // POST PASS 2: Propagate and emit call-site warnings
        // ====================================================
        propagateParamDereferenced();         // Compute transitive closure for null deref
        propagatePreconditionsTransitively(); // Compute transitive closure for buffer preconditions
        propagateFrameEffectsTransitively();  // Compute transitive closure for frame inference
        emitCallSiteWarnings(Ctx);
        emitTransitivePreconditionWarnings(Ctx);  // Check concrete buffers against propagated preconditions

        // Flush SARIF output and restore original diagnostic consumer
        if (SarifConsumer) {
            GlobalSarifConsumer = nullptr;
            auto &DE = Ctx.getDiagnostics();
            DE.setClient(OrigDiagClient, OrigDiagOwned);
            if (auto EC = SarifConsumer->flush()) {
                unsigned WarnId = DE.getCustomDiagID(
                    DiagnosticsEngine::Warning,
                    "DTT: failed to write SARIF output to '%0': %1");
                DE.Report(WarnId) << SarifOutputPath << EC.message();
            }
        }
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
            } else if (llvm::StringRef(arg).starts_with("--dtt-mode=") || llvm::StringRef(arg).starts_with("-dtt-mode=")) {
                // Unknown mode value
                llvm::errs() << "DTT: unknown mode '" << arg << "'. Valid modes: inform, enforce, quiet\n";
                return false;
            } else if (llvm::StringRef(arg).starts_with("--dtt-annotations=") ||
                       llvm::StringRef(arg).starts_with("-dtt-annotations=")) {
                llvm::StringRef path = llvm::StringRef(arg);
                path = path.substr(path.find('=') + 1);
                ExternalAnnotationFiles.push_back(path.str());
            } else if (llvm::StringRef(arg).starts_with("--sarif=") ||
                       llvm::StringRef(arg).starts_with("-sarif=")) {
                llvm::StringRef sPath = llvm::StringRef(arg);
                SarifOutputPath = sPath.substr(sPath.find('=') + 1);
            }
            // Ignore unknown args (might be for other plugins)
        }
        return true;
    }
    ActionType getActionType() override { return AddAfterMainAction; }
};

}

static FrontendPluginRegistry::Add<DTTPlugin> X("dtt", "DTT v25");
