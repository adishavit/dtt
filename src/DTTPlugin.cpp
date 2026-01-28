// DTT Plugin — Dependent Type Tracking for C++

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
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
#include "DTTStdLib.h"
#include "DTTCLib.h"

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
llvm::StringMap<PointerLifetimeState> PointerLifetimes;

// Forward declarations for FieldState (defined after SymValue)
struct LossProvenance;
struct FieldState;

// Forward declaration for safe expression evaluation
bool safeEvaluateAsInt(const Expr *E, Expr::EvalResult &R, ASTContext &Ctx);

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

    static PathElement synthetic(const std::string& name) {
        PathElement e;
        e.kind = SyntheticField;
        e.syntheticName = name;
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
    static FieldPath fromSynthetic(const std::string& path) {
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

        p.syntheticBase = path.substr(0, baseEnd);
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
                if (closePos != std::string::npos) {
                    std::string indexStr = path.substr(pos + 1, closePos - pos - 1);
                    if (indexStr == "?" || indexStr.empty()) {
                        p.elements.push_back(PathElement::runtimeIdx());
                    } else {
                        int64_t idx = 0;
                        if (llvm::StringRef(indexStr).getAsInteger(10, idx)) {
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
    FieldPath withSynthetic(const std::string& name) const {
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
            cachedStr = baseDecl->getNameAsString();
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

// A Proof represents a proven fact about a field
// Examples: _size > 0, _nonnull != 0, _value >= 5
struct Proof {
    std::string field;    // The field this proof is about (_size, _nonnull, _value)
    ProofOp op;           // Comparison operator
    int64_t value;        // RHS constant

    bool operator==(const Proof& other) const {
        return field == other.field && op == other.op && value == other.value;
    }

    // Convert to legacy string key format (for backward compatibility during migration)
    std::string toKey() const {
        return field + ProofOpStrings[static_cast<int>(op)] + std::to_string(value);
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

    // Invalidate all proofs about a specific field for a path
    void invalidateField(const FieldPath& base, const std::string& field) {
        auto it = proofs_.find(base);
        if (it != proofs_.end()) {
            llvm::SmallVector<Proof, 4> toRemove;
            for (const auto& p : it->second) {
                if (p.field == field || p.field.find(field) == 0) {
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

// ProofContext provides entailment reasoning over proofs
// This is the Phase 3 implementation for structured proof checking
class ProofContext {
public:
    explicit ProofContext(ProofStore& store) : store_(store) {}

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

    // Check if existingProof implies requiredProof
    // Both proofs must be about the same field (caller ensures this)
    static bool implies(const Proof& existing, const Proof& required) {
        // Same proof trivially implies itself
        if (existing == required) return true;

        int64_t ev = existing.value;
        int64_t rv = required.value;

        switch (existing.op) {
            case ProofOp::GT:  // field > ev
                switch (required.op) {
                    case ProofOp::GT:  return ev >= rv;  // > 5 implies > 3
                    case ProofOp::GE:  return ev >= rv;  // > 5 implies >= 5, >= 3
                    case ProofOp::NE:  return true;      // > ev implies != ev
                    case ProofOp::LT:  return false;
                    case ProofOp::LE:  return false;
                    case ProofOp::EQ:  return false;
                }
                break;

            case ProofOp::GE:  // field >= ev
                switch (required.op) {
                    case ProofOp::GE:  return ev >= rv;  // >= 5 implies >= 3
                    case ProofOp::GT:  return ev > rv;   // >= 5 implies > 3 (not > 5)
                    case ProofOp::NE:  return false;     // >= 5 doesn't imply != 5
                    case ProofOp::LT:  return false;
                    case ProofOp::LE:  return false;
                    case ProofOp::EQ:  return false;
                }
                break;

            case ProofOp::LT:  // field < ev
                switch (required.op) {
                    case ProofOp::LT:  return ev <= rv;  // < 5 implies < 7
                    case ProofOp::LE:  return ev <= rv;  // < 5 implies <= 5, <= 7
                    case ProofOp::NE:  return true;      // < ev implies != ev
                    case ProofOp::GT:  return false;
                    case ProofOp::GE:  return false;
                    case ProofOp::EQ:  return false;
                }
                break;

            case ProofOp::LE:  // field <= ev
                switch (required.op) {
                    case ProofOp::LE:  return ev <= rv;  // <= 5 implies <= 7
                    case ProofOp::LT:  return ev < rv;   // <= 5 implies < 7 (not < 5)
                    case ProofOp::NE:  return false;     // <= 5 doesn't imply != 5
                    case ProofOp::GT:  return false;
                    case ProofOp::GE:  return false;
                    case ProofOp::EQ:  return false;
                }
                break;

            case ProofOp::EQ:  // field == ev
                switch (required.op) {
                    case ProofOp::EQ:  return ev == rv;  // == 5 implies == 5 only
                    case ProofOp::GE:  return ev >= rv;  // == 5 implies >= 3
                    case ProofOp::LE:  return ev <= rv;  // == 5 implies <= 7
                    case ProofOp::GT:  return ev > rv;   // == 5 implies > 3
                    case ProofOp::LT:  return ev < rv;   // == 5 implies < 7
                    case ProofOp::NE:  return ev != rv;  // == 5 implies != 3
                }
                break;

            case ProofOp::NE:  // field != ev
                switch (required.op) {
                    case ProofOp::NE:  return ev == rv;  // != 5 implies != 5 only
                    default:           return false;     // != doesn't imply order
                }
                break;
        }

        return false;
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
    ProofStore& store_;
};

// Global proof store - manages structured proofs for constraint checking
ProofStore GlobalProofs;

// Global proof context for entailment checking
// Provides canProve() with entailment reasoning over GlobalProofs
ProofContext GlobalProofContext(GlobalProofs);

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
        return DRE->getDecl()->getNameAsString();
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

struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR, BITAND, MOD } kind;
    int64_t constVal = 0;
    std::string varRef;              // Legacy string path (for backward compat)
    std::optional<FieldPath> varPath; // Typed path (preferred)
    std::shared_ptr<SymValue> left, right;

    static SymValue Const(int64_t v) { SymValue s; s.kind = CONST; s.constVal = v; return s; }
    static SymValue Unknown() { SymValue s; s.kind = UNKNOWN; return s; }
    // Legacy string-based Var (deprecated, use VarTyped)
    static SymValue Var(const std::string& ref) { SymValue s; s.kind = VAR; s.varRef = ref; return s; }
    // Type-safe Var using FieldPath
    static SymValue VarTyped(const FieldPath& path) {
        SymValue s;
        s.kind = VAR;
        s.varPath = path;
        s.varRef = path.toString();  // Keep string for compatibility
        return s;
    }

    static SymValue Add(SymValue a, SymValue b) {
        if (a.kind == CONST && b.kind == CONST) {
            if (auto r = safeAdd(a.constVal, b.constVal)) return Const(*r);
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

    bool isGuaranteedNonNegative() const {
        if (kind == CONST && constVal >= 0) return true;
        if (kind == BITAND && right && right->kind == CONST && right->constVal >= 0) return true;
        if (kind == MOD && right && right->kind == CONST && right->constVal > 0) return true;
        return false;
    }

    // Extract the constant offset from an expression like (n + 8) or (n - 3)
    // Returns the constant part: ADD(var, 8) -> 8, SUB(var, 3) -> -3, VAR -> 0
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
        return SymValue::Var(DRE->getDecl()->getNameAsString());
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

// Forward declarations for ConstraintExpr
struct ConstraintExpr;
using ConstraintExprPtr = std::shared_ptr<ConstraintExpr>;

// Base class for constraint expressions
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

    // Convert to string for diagnostics
    virtual std::string toString() const = 0;

    // Check if this is a comparison expression (returns bool)
    bool isComparison() const {
        return kind >= CmpGT && kind <= CmpNE;
    }

    // Check if this is a logical expression
    bool isLogical() const {
        return kind >= LogicalAnd && kind <= LogicalNot;
    }

    // Clone the expression
    virtual ConstraintExprPtr clone() const = 0;

    // Convert to SymValue for arithmetic evaluation
    // Returns nullopt for comparisons/logical ops (they don't produce values)
    virtual std::optional<SymValue> toSymValue() const { return std::nullopt; }
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

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Const(value);
    }
};

// Field reference: _size, _nonnull, _capacity, etc.
struct FieldRefExpr : ConstraintExpr {
    std::string fieldName;  // e.g., "_size", "_nonnull"

    FieldRefExpr(const std::string& name) : ConstraintExpr(FieldRef), fieldName(name) {}

    std::string toString() const override {
        return fieldName;
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<FieldRefExpr>(fieldName);
    }

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var(fieldName);
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

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("arg" + std::to_string(argIndex));
    }
};

// Argument field reference: arg0._size, arg1._nonnull, etc.
struct ArgFieldRefExpr : ConstraintExpr {
    unsigned argIndex;
    std::string fieldName;

    ArgFieldRefExpr(unsigned idx, const std::string& field)
        : ConstraintExpr(ArgFieldRef), argIndex(idx), fieldName(field) {}

    std::string toString() const override {
        return "arg" + std::to_string(argIndex) + "." + fieldName;
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<ArgFieldRefExpr>(argIndex, fieldName);
    }

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("arg" + std::to_string(argIndex) + "." + fieldName);
    }
};

// Old value reference: old(_size), old(_capacity)
struct OldRefExpr : ConstraintExpr {
    std::string fieldName;

    OldRefExpr(const std::string& field) : ConstraintExpr(OldRef), fieldName(field) {}

    std::string toString() const override {
        return "old(" + fieldName + ")";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<OldRefExpr>(fieldName);
    }

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("old:" + fieldName);
    }
};

// Result reference for postconditions: _result
struct ResultRefExpr : ConstraintExpr {
    ResultRefExpr() : ConstraintExpr(ResultRef) {}

    std::string toString() const override {
        return "_result";
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<ResultRefExpr>();
    }

    std::optional<SymValue> toSymValue() const override {
        return SymValue::Var("_result");
    }
};

// Binary operation expression
struct BinaryExpr : ConstraintExpr {
    ConstraintExprPtr left;
    ConstraintExprPtr right;

    BinaryExpr(Kind k, ConstraintExprPtr l, ConstraintExprPtr r)
        : ConstraintExpr(k), left(std::move(l)), right(std::move(r)) {}

    std::string toString() const override {
        std::string opStr;
        switch (kind) {
            case Add: opStr = " + "; break;
            case Sub: opStr = " - "; break;
            case Mul: opStr = " * "; break;
            case Div: opStr = " / "; break;
            case Mod: opStr = " % "; break;
            case BitAnd: opStr = " & "; break;
            case CmpGT: opStr = " > "; break;
            case CmpGE: opStr = " >= "; break;
            case CmpLT: opStr = " < "; break;
            case CmpLE: opStr = " <= "; break;
            case CmpEQ: opStr = " == "; break;
            case CmpNE: opStr = " != "; break;
            case LogicalAnd: opStr = " && "; break;
            case LogicalOr: opStr = " || "; break;
            default: opStr = " ? "; break;
        }
        // Add parentheses for clarity
        bool needLeftParen = left->isLogical() && (kind == LogicalAnd || kind == LogicalOr);
        bool needRightParen = right->isLogical() && (kind == LogicalAnd || kind == LogicalOr);

        std::string result;
        if (needLeftParen) result += "(";
        result += left->toString();
        if (needLeftParen) result += ")";
        result += opStr;
        if (needRightParen) result += "(";
        result += right->toString();
        if (needRightParen) result += ")";
        return result;
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<BinaryExpr>(kind, left->clone(), right->clone());
    }

    std::optional<SymValue> toSymValue() const override {
        // Only arithmetic operations produce SymValues
        if (kind < Add || kind > BitAnd) return std::nullopt;

        auto lhs = left->toSymValue();
        auto rhs = right->toSymValue();
        if (!lhs || !rhs) return std::nullopt;

        switch (kind) {
            case Add: return SymValue::Add(*lhs, *rhs);
            case Sub: return SymValue::Sub(*lhs, *rhs);
            case Mul: return SymValue::Mul(*lhs, *rhs);
            case Div: return SymValue::Div(*lhs, *rhs);
            case Mod: return SymValue::Mod(*lhs, *rhs);
            case BitAnd: return SymValue::BitAnd(*lhs, *rhs);
            default: return std::nullopt;
        }
    }
};

// Unary operation expression (currently just logical not)
struct UnaryExpr : ConstraintExpr {
    ConstraintExprPtr operand;

    UnaryExpr(Kind k, ConstraintExprPtr op)
        : ConstraintExpr(k), operand(std::move(op)) {}

    std::string toString() const override {
        if (kind == LogicalNot) {
            return "!" + operand->toString();
        }
        return "?" + operand->toString();
    }

    ConstraintExprPtr clone() const override {
        return std::make_shared<UnaryExpr>(kind, operand->clone());
    }
};

// ============================================================
// CONSTRAINT EXPRESSION ANALYSIS HELPERS
// ============================================================
// Pattern matching helpers using switch on kind (no dynamic_cast chains)

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
            argField = af->fieldName;
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
            argField = af->fieldName;
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
            lhsField = af->fieldName;
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
            rhsField = af->fieldName;
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
// CONSTRAINT EXPRESSION PARSER
// ============================================================
// Parses constraint strings into ConstraintExpr AST

class ConstraintExprParser {
public:
    // Parse a constraint string into an AST
    static ConstraintExprPtr parse(llvm::StringRef input) {
        ConstraintExprParser parser(input);
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

    // Parse atom: number, field, arg, old(...), etc.
    ConstraintExprPtr parseAtomInternal() {
        skipWhitespace();

        // Handle parentheses
        if (match("(")) {
            auto expr = parseLogicalOr();
            match(")");
            return expr;
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
llvm::StringMap<FieldState> FieldStates;

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
            bool startOK = (pos == 0 || !std::isalnum(result[pos - 1]));
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
    return std::string(result);
}

// Parse constraint string directly to ConstraintExpr AST
ConstraintExprPtr parseConstraintExpr(llvm::StringRef s) {
    return ConstraintExprParser::parse(s.trim());
}

// ============================================================
// POSTCONDITIONS (internal types for DTTPlugin.cpp)
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
    llvm::StringRef lhsRef(lhs);
    if (forMethod) {
        if (lhsRef.starts_with("result.")) p.resultField = lhsRef.drop_front(7).str();
        else if (!lhs.empty() && lhs[0] == '_') p.resultField = lhs;  // _size -> this->_size
        else p.resultField = lhs;
    } else {
        if (lhsRef.starts_with("result.")) p.resultField = lhsRef.drop_front(7).str();
        else return std::nullopt;
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

// TypedEnv uses FieldPath keys for type-safe path representation
// This is the sole environment store after Phase 1 migration
// Uses DenseMap for better cache locality and performance
llvm::DenseMap<FieldPath, SymValue> TypedEnv;

llvm::StringMap<llvm::SmallVector<Postcondition, 2>> FnPostconditions;
llvm::StringMap<llvm::SmallVector<BoolPostcondition, 2>> FnBoolPostconditions;
llvm::StringSet<> TotalFunctions;
// Track pointer aliases: alias -> source (e.g., "alias" -> "ptr" when alias = ptr)
llvm::StringMap<std::string> PointerAliases;

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
int getParamIndex(const FunctionDecl* F, const std::string& path) {
    if (!F) return -1;
    for (unsigned i = 0; i < F->getNumParams(); i++) {
        if (F->getParamDecl(i)->getName() == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
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

// Convert dtt:: annotation types to internal types
void loadSyntheticAnnotations() {
    dtt::registerSyntheticAnnotations();
    dtt::registerCLibAnnotations();

    // Copy from dtt::MethodAnnotations to internal MethodAnnotations
    for (const auto& [key, info] : dtt::MethodAnnotations) {
        MethodKey internalKey{key.className, key.methodName, key.numParams};
        MethodInfo internalInfo;

        // Convert preconditions (shared_ptr compatible since types match structurally)
        for (const auto& pre : info.preconditions) {
            // The dtt:: ConstraintExpr types have the same layout as internal types
            // This works because both hierarchies have identical structure
            internalInfo.preconditions.push_back(
                std::reinterpret_pointer_cast<ConstraintExpr>(pre));
        }

        // Convert postconditions
        for (const auto& post : info.ensures) {
            Postcondition internalPost;
            internalPost.resultField = post.resultField;
            internalPost.usesOld = post.usesOld;
            // Convert dtt::SymValue to internal SymValue
            internalPost.expr = [&]() -> SymValue {
                switch (post.expr.kind) {
                    case dtt::SymValue::CONST:
                        return SymValue::Const(post.expr.constVal);
                    case dtt::SymValue::VAR:
                        return SymValue::Var(post.expr.varRef);
                    case dtt::SymValue::ADD:
                        if (post.expr.left && post.expr.right) {
                            // Recursively convert - simplified for common patterns
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
    }
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

llvm::DenseMap<VarBoundKey, ProofOp> VarBoundProofs;

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

llvm::DenseMap<FieldPath, BaseOriginInfo> PointerBaseOrigins;

void beginFunctionPreconditions(const clang::FunctionDecl* FD) {
    // Use canonical decl for consistent lookup across declarations
    CurrentFnForPreconditions = FD ? FD->getCanonicalDecl() : nullptr;
    CurrentFnPreconditions.clear();
    PointerBaseOrigins.clear();
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
    VarBoundProofs[{var, obj, field}] = op;
}

void removeVarBoundProof(const std::string& var, const std::string& obj, const std::string& field) {
    VarBoundProofs.erase({var, obj, field});
}

bool hasVarBoundProof(const std::string& var, const std::string& obj, const std::string& field, ProofOp op) {
    auto it = VarBoundProofs.find({var, obj, field});
    if (it == VarBoundProofs.end()) return false;
    // Check if existing proof implies the needed one
    if (it->second == op) return true;
    if (it->second == ProofOp::LT && op == ProofOp::LE) return true;  // < implies <=
    return false;
}

// Clear all per-function analysis state (called at function entry)
void clearPerFunctionState() {
    TypedEnv.clear();
    GlobalProofs.clear();
    VarBoundProofs.clear();
    FieldStates.clear();
    WarnedLostTracking.clear();
    NullProvenanceMap.clear();
    PointerLifetimes.clear();
}

// ============================================================
// FIELDPATH-NATIVE API
// ============================================================
// These functions work directly with FieldPath for type-safe path representation.

// Set a field value using FieldPath
void setFieldTyped(const FieldPath& basePath, const std::string& field, SymValue v) {
    FieldPath fullPath = basePath.withSynthetic(field);
    TypedEnv[fullPath] = v;

    // FieldStates uses string key for tracking state
    std::string key = basePath.toString() + "." + field;
    FieldStates[key] = FieldState::Known(v);
    WarnedLostTracking.erase({basePath.toString(), field});

    // Invalidate VarBoundProofs that reference this field
    std::string baseStr = basePath.toString();
    llvm::SmallVector<VarBoundKey, 4> toRemove;
    for (const auto& [key, op] : VarBoundProofs) {
        if (key.objPath == baseStr && key.field == field) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        VarBoundProofs.erase(key);
    }
}

// Get a field value using FieldPath
SymValue getFieldTyped(const FieldPath& basePath, const std::string& field) {
    FieldPath fullPath = basePath.withSynthetic(field);
    auto it = TypedEnv.find(fullPath);
    return (it != TypedEnv.end()) ? it->second : SymValue::Unknown();
}

// String-based wrapper for backward compatibility
SymValue getField(const std::string& basePath, const std::string& field) {
    return getFieldTyped(FieldPath::fromSynthetic(basePath), field);
}

// Set environment value using FieldPath
void setEnvValueTyped(const FieldPath& path, SymValue v) {
    TypedEnv[path] = v;

    // Handle proof keys - add to GlobalProofs
    const auto& elems = path.elements;
    if (!elems.empty() && elems.back().kind == PathElement::SyntheticField) {
        llvm::StringRef synth = elems.back().syntheticName;
        if (synth.starts_with("_proof_")) {
            std::string proofKey = synth.drop_front(7).str();  // skip "_proof_"
            if (auto proof = Proof::fromKey(proofKey)) {
                FieldPath basePath = path.base();
                GlobalProofs.add(basePath, *proof);
            }
        }
    }
}

// Erase environment value using FieldPath
void eraseEnvValueTyped(const FieldPath& path) {
    TypedEnv.erase(path);

    // Handle proof keys - remove from GlobalProofs
    const auto& elems = path.elements;
    if (!elems.empty() && elems.back().kind == PathElement::SyntheticField) {
        llvm::StringRef synth = elems.back().syntheticName;
        if (synth.starts_with("_proof_")) {
            std::string proofKey = synth.drop_front(7).str();  // skip "_proof_"
            if (auto proof = Proof::fromKey(proofKey)) {
                FieldPath basePath = path.base();
                GlobalProofs.remove(basePath, *proof);
            }
        }
    }
}

// Add a proof using FieldPath
void addProofTyped(const FieldPath& basePath, const Proof& proof) {
    GlobalProofs.add(basePath, proof);
    TypedEnv[basePath.withSynthetic("_proof_" + proof.toKey())] = SymValue::Const(1);
}

// Check if a proof exists using FieldPath
bool hasProofTyped(const FieldPath& basePath, const Proof& proof) {
    return GlobalProofContext.canProve(basePath, proof);
}

// Read environment value using FieldPath
std::optional<SymValue> getEnvValueTyped(const FieldPath& path) {
    auto it = TypedEnv.find(path);
    if (it != TypedEnv.end()) {
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
    for (const auto& [fieldPath, value] : TypedEnv) {
        if (fieldPath.toStringRef().starts_with(prefix)) {
            result.push_back(fieldPath);
        }
    }
}

// Collect paths matching prefix OR exact match
void collectPathsMatching(llvm::StringRef path, llvm::SmallVectorImpl<FieldPath>& result) {
    for (const auto& [fieldPath, value] : TypedEnv) {
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
            for (const auto& [fieldPath, value] : TypedEnv) {
                llvm::StringRef pathStr = fieldPath.toStringRef();
                if (pathStr.find("._proof_") != llvm::StringRef::npos) continue;
                if (startsWithDotPrefix(pathStr, objPath)) {
                    std::string field = pathStr.drop_front(objPath.size() + 1).str();
                    if (!field.empty() && (field[0] == '_' || field[0] == '@')) {
                        subst[field] = value;
                    } else {
                        subst["_" + field] = value;
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
    auto it = FieldStates.find(key);
    if (it != FieldStates.end()) {
        return it->second.state;
    }
    // Check if field exists in TypedEnv but not in FieldStates
    FieldPath typedPath = FieldPath::fromSynthetic(basePath).withSynthetic(field);
    if (getEnvValueTyped(typedPath).has_value()) {
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

void addProof(llvm::StringRef basePath, llvm::StringRef proofKey) {
    // setEnvValueTyped handles both TypedEnv and GlobalProofs
    setEnvValue((llvm::Twine(basePath) + "._proof_" + proofKey).str(), SymValue::Const(1));
}

void removeProof(llvm::StringRef basePath, llvm::StringRef proofKey) {
    // eraseEnvValueTyped handles both TypedEnv and GlobalProofs
    eraseEnvValue((llvm::Twine(basePath) + "._proof_" + proofKey).str());
}

void invalidateProofs(llvm::StringRef basePath, llvm::StringRef field) {
    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsWithPrefix((llvm::Twine(basePath) + "._proof_" + field).str(), paths);
    for (const auto& fp : paths) {
        eraseEnvValueTyped(fp);
    }
    FieldPath typedBase = FieldPath::fromSynthetic(basePath.str());
    GlobalProofs.invalidateField(typedBase, field.str());
}

// Check if there are any proofs about a field (even without a concrete value)
bool hasAnyProofs(const std::string& basePath, const std::string& field) {
    FieldPath typedBase = FieldPath::fromSynthetic(basePath);
    if (auto* proofs = GlobalProofs.getProofs(typedBase)) {
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
        return GlobalProofs.has(typedBase, *proof);
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
    GlobalProofs.invalidateField(typedBase, "_nonnull");
}

void invalidateArrayElements(const std::string& arrayBase, const std::string& field) {
    // Iterate over TypedEnv to find and remove array element entries
    llvm::SmallVector<FieldPath, 8> toRemove;
    std::string pattern = arrayBase + "[";
    for (const auto& [fieldPath, value] : TypedEnv) {
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

    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsMatching(path, paths);
    for (const auto& fp : paths) {
        std::string key = fp.toString();
        if (key.find("._proof_") == std::string::npos) {
            FieldStates[key] = FieldState::Lost(loc, reason);
        }
        eraseEnvValueTyped(fp);
    }

    // Also remove any VarBoundProofs involving this path
    llvm::SmallVector<VarBoundKey, 4> toRemove;
    for (const auto& [key, op] : VarBoundProofs) {
        if (key.objPath == path || key.varName == path) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        VarBoundProofs.erase(key);
    }
}

// Backward-compatible version without provenance
void invalidateProperties(llvm::StringRef path) {
    if (path.empty()) return;

    llvm::SmallVector<FieldPath, 8> paths;
    collectPathsMatching(path, paths);
    for (const auto& fp : paths) {
        eraseEnvValueTyped(fp);
        FieldStates.erase(fp.toString());
    }

    // Also remove any VarBoundProofs involving this path
    llvm::SmallVector<VarBoundKey, 4> toRemove;
    for (const auto& [key, op] : VarBoundProofs) {
        if (key.objPath == path || key.varName == path) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        VarBoundProofs.erase(key);
    }
}

// Check if a function has ANY DTT annotations (requires, ensures, etc.)
// If a function has any annotations, assume the user knows what it does - don't invalidate
bool hasDTTAnnotations(FunctionDecl* FD) {
    if (!FD) return false;

    // Check for any dtt: annotations on the function
    for (auto *A : FD->attrs()) {
        if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
            if (Ann->getAnnotation().starts_with("dtt:")) {
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
            if (post.resultField == argPrefix || startsWithDotPrefix(post.resultField, argPrefix)) {
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
                        VarBoundProofs[{argName, objPath, fieldName}] = *proofOp;
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
    for (const auto& [fieldPath, value] : TypedEnv) {
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

    for (const auto& [fieldPath, value] : TypedEnv) {
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
        std::string varName = DRE->getDecl()->getNameAsString();
        // Check if we have a tracked value for this variable
        if (auto val = getEnvValue(varName)) return *val;
        return SymValue::Var(varName);
    }

    // Member expression: a.len, v._size, etc.
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        FieldPath typedPath = getTypedPath(E);
        if (typedPath.isValid() && !typedPath.hasRuntimeIndex()) {
            // Use typed path for lookup since member paths are stored via TypedEnv
            if (auto val = getEnvValueTyped(typedPath)) return *val;
            return SymValue::VarTyped(typedPath);
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
            MethodKey key{Method->getParent()->getName().str(), Method->getNameAsString(), MCE->getNumArgs()};

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

// ============================================================
// Z3 HELPERS FOR TYPEDENV AND GLOBALPROOFS
// ============================================================

// Add environment value constraints to Z3 solver
// Phase 1: Uses TypedEnv as primary source (migration complete)
void addEnvValuesToZ3(z3::context& ctx, z3::solver& solver,
                       std::map<std::string, z3::expr>& vars) {
    for (const auto& [fieldPath, value] : TypedEnv) {
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
            solver.add(getOrCreateVar(ctx, vars, v) == symValueToZ3(value, ctx, vars, v));
        }
    }
}

// Add proof constraints to Z3 solver from GlobalProofs (structured store)
// This replaces addEnvProofsToZ3 - uses the structured proof store instead of Env iteration
void addGlobalProofsToZ3(z3::context& ctx, z3::solver& solver,
                          std::map<std::string, z3::expr>& vars) {
    GlobalProofs.forEach([&](const FieldPath& path, const Proof& p) {
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
    for (const auto& [key, op] : VarBoundProofs) {
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
    if (!expr) return ctx.bool_val(true);

    switch (expr->kind) {
        case ConstraintExpr::Literal:
            return ctx.int_val(static_cast<LiteralExpr*>(expr.get())->value);

        case ConstraintExpr::FieldRef: {
            auto* fr = static_cast<FieldRefExpr*>(expr.get());
            return getOrCreateVar(ctx, vars, sanitizeForSMT(basePath + "_" + fr->fieldName));
        }

        case ConstraintExpr::ArgRef: {
            auto* ar = static_cast<ArgRefExpr*>(expr.get());
            return getOrCreateVar(ctx, vars, "arg" + std::to_string(ar->argIndex));
        }

        case ConstraintExpr::ArgFieldRef: {
            auto* afr = static_cast<ArgFieldRefExpr*>(expr.get());
            return getOrCreateVar(ctx, vars, "arg" + std::to_string(afr->argIndex) + "_" + afr->fieldName);
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
                case ConstraintExpr::Div: return left / right;
                case ConstraintExpr::Mod: return z3::mod(left, right);
                case ConstraintExpr::BitAnd: return left & right;
                case ConstraintExpr::CmpGT: return left > right;
                case ConstraintExpr::CmpGE: return left >= right;
                case ConstraintExpr::CmpLT: return left < right;
                case ConstraintExpr::CmpLE: return left <= right;
                case ConstraintExpr::CmpEQ: return left == right;
                case ConstraintExpr::CmpNE: return left != right;
                case ConstraintExpr::LogicalAnd: return left && right;
                case ConstraintExpr::LogicalOr: return left || right;
                default: return ctx.bool_val(true);
            }
        }

        default:
            return ctx.bool_val(true);
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
        if (includeVarBounds) {
            addVarBoundProofsToZ3(ctx, solver, vars);
        }
    }

    // Check if negated goal is unsatisfiable (proves goal is true)
    std::optional<bool> checkNegated(const z3::expr& goal) {
        solver.add(!goal);
        auto result = solver.check();
        if (result == z3::unsat) return true;
        if (result == z3::sat) return false;
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

// Check a comparison between two SymValues using Z3
// Returns: true = comparison proven, false = comparison disproven, nullopt = unknown
// assumeNonNegative: when true, adds var >= 0 constraints for all variables (for size_t comparisons)
std::optional<bool> checkComparisonWithZ3(const std::string& basePath, const SymValue& lhs, const SymValue& rhs,
                                           ConstraintExpr::Kind op, bool assumeNonNegative = false) {
    try {
        Z3SolverContext z3ctx;
        z3ctx.addEnvironmentConstraints();

        std::string varName = sanitizeForSMT(basePath);
        z3::expr z3Lhs = symValueToZ3(lhs, z3ctx.ctx, z3ctx.vars, varName);
        z3::expr z3Rhs = symValueToZ3(rhs, z3ctx.ctx, z3ctx.vars, varName);

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

        z3::expr z3Idx = symValueToZ3(idx, z3ctx.ctx, z3ctx.vars, "idx");
        z3::expr z3Size = symValueToZ3(size, z3ctx.ctx, z3ctx.vars, "size");

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
    if (auto* proofs = GlobalProofs.getProofs(typedBase)) {
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
        if (auto* varProofs = GlobalProofs.getProofs(varPath)) {
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
        std::string varName = VD->getName().str();

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
// VISITOR
// ============================================================

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

    // Emit inferred precondition note and store for call-site checking
    void emitInferredPrecondition(SourceLocation Loc, Expr* BufferArg, Expr* SizeArg,
                                   const SymValue& opSize) {
        FieldPath bufFieldPath = getTypedPath(BufferArg);
        std::string bufPath = bufFieldPath.toString();
        auto bufArgIdx = getParameterIndex(BufferArg, CurrentFunctionDecl);
        auto sizeArgIdx = getParameterIndex(SizeArg, CurrentFunctionDecl);
        // Extract constant part from expressions like (n + 8) for proper call-site checking
        int64_t constPart = opSize.extractConstantOffset();

        // Phase 3: Check for base origin - rewrite precondition in terms of base
        std::optional<unsigned> offsetArgIdx = std::nullopt;
        int64_t offsetConstant = 0;

        if (bufFieldPath.isValid()) {
            auto originIt = PointerBaseOrigins.find(bufFieldPath);
            if (originIt != PointerBaseOrigins.end() && originIt->second.baseParamIdx) {
                // Rewrite to base parameter
                bufPath = originIt->second.basePath.toString();
                bufArgIdx = originIt->second.baseParamIdx;
                offsetArgIdx = originIt->second.offsetParamIdx;

                // Extract offset info and build bound string
                const SymValue& offsetBytes = originIt->second.offsetBytes;
                std::string boundStr = opSize.str();
                // Extract constant part from offset expression (e.g., off+4 -> 4)
                offsetConstant = offsetBytes.extractConstantOffset();

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
                    auto originIt = PointerBaseOrigins.find(bufPath);
                    if (originIt != PointerBaseOrigins.end()) {
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
                    << Callee->getName();
            };

            // Get buffer size at call site
            // Use _size (remaining elements = _base_length - _offset) to account for
            // pointer offsets like p = arr + 6 having only 2 bytes remaining in arr[8]
            SymValue bufSize = SymValue::Unknown();
            if (bufPath.isValid()) {
                bufSize = getFieldTyped(bufPath, "_size_bytes");
                if (bufSize.kind == SymValue::UNKNOWN) {
                    // Prefer _size (remaining elements) over _base_length (total)
                    SymValue remainingElems = getFieldTyped(bufPath, "_size");
                    if (remainingElems.kind != SymValue::UNKNOWN) {
                        QualType ptrType = ActualBufArg->getType();
                        if (ptrType->isPointerType()) {
                            if (auto sz = safeGetTypeSizeInChars(Ctx, ptrType->getPointeeType()))
                                bufSize = SymValue::Mul(remainingElems, SymValue::Const(*sz));
                        } else if (ptrType->isArrayType()) {
                            QualType elemType = Ctx.getAsArrayType(ptrType)->getElementType();
                            if (auto sz = safeGetTypeSizeInChars(Ctx, elemType))
                                bufSize = SymValue::Mul(remainingElems, SymValue::Const(*sz));
                        }
                    }
                }
            }

            // Get bound value at call site
            // For expressions like (n + 8): boundArgIndex=1, boundConstant=8
            // Need to compute: actual_arg_value + boundConstant
            SymValue boundVal = SymValue::Unknown();
            if (Pre.boundArgIndex && *Pre.boundArgIndex < E->getNumArgs()) {
                Expr* ActualSizeArg = E->getArg(*Pre.boundArgIndex)->IgnoreParenImpCasts();
                if (auto val = buildSymValueFromExpr(ActualSizeArg)) {
                    boundVal = *val;
                    // Add constant offset if present (e.g., n + 8 where 8 is the constant part)
                    if (Pre.boundConstant != 0) {
                        boundVal = SymValue::Add(boundVal, SymValue::Const(Pre.boundConstant));
                    }
                }
            } else if (Pre.boundConstant != 0) {
                boundVal = SymValue::Const(Pre.boundConstant);
            }

            if (boundVal.kind == SymValue::UNKNOWN) continue;

            // Phase 3: Add/subtract offset to bound if present (for p = base +/- off patterns)
            // Scale offset by element size: for int* p = buf + off, off is in elements
            if (Pre.offsetArgIndex && *Pre.offsetArgIndex < E->getNumArgs()) {
                Expr* ActualOffsetArg = E->getArg(*Pre.offsetArgIndex)->IgnoreParenImpCasts();
                if (auto offVal = buildSymValueFromExpr(ActualOffsetArg)) {
                    SymValue scaledOff = *offVal;
                    if (Pre.offsetElemSize != 1) {
                        scaledOff = SymValue::Mul(*offVal, SymValue::Const(Pre.offsetElemSize));
                    }
                    // For subtraction patterns (p = buf - off), subtract the offset
                    if (Pre.offsetNegated) {
                        boundVal = SymValue::Sub(boundVal, scaledOff);
                    } else {
                        boundVal = SymValue::Add(boundVal, scaledOff);
                    }
                }
            }
            if (Pre.offsetConstant != 0) {
                boundVal = SymValue::Add(boundVal, SymValue::Const(Pre.offsetConstant));
            }

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

    // Collect method annotations
    bool VisitCXXMethodDecl(CXXMethodDecl *M) {
        if (!M->getParent()) return true;
        std::string className = M->getParent()->getName().str();
        std::string methodName = M->getNameAsString();
        // Strip template parameters for constructors
        auto anglePos = methodName.find('<');
        if (anglePos != std::string::npos) methodName = methodName.substr(0, anglePos);
        // Include parameter count to distinguish overloads
        MethodKey key{className, methodName, M->getNumParams()};

        MethodInfo info;
        for (auto *A : M->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                llvm::StringRef s = Ann->getAnnotation();
                if (s.starts_with("dtt:requires=")) {
                    auto expr = parseConstraintExpr(s.drop_front(13).str());
                    if (expr) info.preconditions.push_back(expr);
                }
                if (s.starts_with("dtt:ensures=")) {
                    auto p = parsePostcondition(s.drop_front(12).str(), true);
                    if (p) info.ensures.push_back(*p);
                }
                if (s.starts_with("dtt:returns=")) {
                    info.returns = s.drop_front(12).str();
                }
                if (s.starts_with("dtt:ensures_if_true=")) {
                    auto bp = parseBoolPostcondition(s.drop_front(20).str(), true);
                    if (bp) info.boolPostconds.push_back(*bp);
                }
                if (s.starts_with("dtt:ensures_if_false=")) {
                    auto bp = parseBoolPostcondition(s.drop_front(21).str(), false);
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
        beginFunctionPreconditions(F);  // Start collecting inferred preconditions
        std::string fn = F->getNameAsString();

        for (auto *A : F->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                llvm::StringRef s = Ann->getAnnotation();
                if (s.starts_with("dtt:ensures=")) {
                    auto post = parsePostcondition(s.drop_front(12).str());
                    if (post) FnPostconditions[fn].push_back(*post);
                }
                if (s.starts_with("dtt:ensures_if_true=")) {
                    auto bp = parseBoolPostcondition(s.drop_front(20).str(), true);
                    if (bp) FnBoolPostconditions[fn].push_back(*bp);
                }
                if (s.starts_with("dtt:ensures_if_false=")) {
                    auto bp = parseBoolPostcondition(s.drop_front(21).str(), false);
                    if (bp) FnBoolPostconditions[fn].push_back(*bp);
                }
                if (s == "dtt:total") TotalFunctions.insert(fn);
            }
        }

        // Clear per-function state when entering a new function body
        // This prevents variable names from one function polluting another function's analysis
        if (F->hasBody() && F->isThisDeclarationADefinition()) {
            clearPerFunctionState();

            // Per C++ standard, main's first parameter (argc) is guaranteed >= 1
            // argv[0] always contains the program name (or empty string)
            if (fn == "main" && F->getNumParams() >= 1) {
                ParmVarDecl *firstParam = F->getParamDecl(0);
                if (firstParam->getType()->isIntegerType()) {
                    std::string paramName = firstParam->getName().str();
                    if (!paramName.empty()) {
                        FieldPath paramPath = FieldPath::fromSynthetic(paramName);
                        GlobalProofs.add(paramPath, Proof{"_value", ProofOp::GE, 1});
                    }
                }
            }

            // Contract propagation — inject dtt:requires as assumptions inside function body
            // This is sound because the caller MUST prove the requires at the call site.
            // If the caller cannot prove it, the call is rejected. Therefore, inside the
            // function body, we can safely assume the precondition holds.
            for (auto *A : F->attrs()) {
                if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                    llvm::StringRef s = Ann->getAnnotation();
                    if (s.starts_with("dtt:requires=")) {
                        auto expr = parseConstraintExpr(s.drop_front(13).str());
                        if (expr) {
                            injectRequiresAsAssumption(F, expr);
                        }
                    }
                }
            }

            // For methods, also check MethodAnnotations
            if (auto *M = dyn_cast<CXXMethodDecl>(F)) {
                if (M->getParent()) {
                    std::string className = M->getParent()->getName().str();
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

                std::string paramName = param->getName().str();
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
                    NullProvenanceMap[paramName] = NullProvenance{
                        param->getLocation(),
                        "parameter '" + paramName + "' may be NULL (callers could pass NULL)"
                    };
                }
            }
        }

        bool result = RecursiveASTVisitor::TraverseFunctionDecl(F);
        endFunctionPreconditions();  // Save collected preconditions for call-site checking
        CurrentFunctionDecl = nullptr;
        return result;
    }

    std::map<std::string, SymValue> captureOldValues(CallExpr *Call) {
        std::map<std::string, SymValue> oldValues;
        for (unsigned i = 0; i < Call->getNumArgs(); i++) {
            auto *Arg = Call->getArg(i)->IgnoreParenImpCasts();
            FieldPath argTypedPath = getTypedPath(Arg);
            std::string argPath = argTypedPath.toString();
            if (!argTypedPath.isValid() || hasRuntimeIndex(argPath)) continue;
            std::string argPrefix = "arg" + std::to_string(i) + ".";
            // Iterate over TypedEnv
            for (const auto& [fieldPath, value] : TypedEnv) {
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
        for (const auto& [fieldPath, value] : TypedEnv) {
            llvm::StringRef pathStr = fieldPath.toStringRef();
            if (startsWithDotPrefix(pathStr, objPath) && pathStr.find("._proof_") == llvm::StringRef::npos) {
                std::string field = pathStr.drop_front(objPath.size() + 1).str();
                oldValues[field] = value;
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
            FieldPath argTypedPath = getTypedPath(Arg);
            std::string argPath = argTypedPath.toString();
            if (!argTypedPath.isValid() || hasRuntimeIndex(argPath)) continue;
            std::string argPrefix = "arg" + std::to_string(i) + ".";
            // Iterate over TypedEnv
            for (const auto& [fieldPath, value] : TypedEnv) {
                llvm::StringRef pathStr = fieldPath.toStringRef();
                if (startsWithDotPrefix(pathStr, argPath) && pathStr.find("._proof_") == llvm::StringRef::npos) {
                    std::string field = pathStr.drop_front(argPath.size() + 1).str();
                    subst[argPrefix + field] = value;
                }
            }
        }
        for (auto& kv : oldValues) subst["old:" + kv.first] = kv.second;

        for (const auto& post : it->second) {
            SymValue result = post.expr.substitute(subst);
            FieldPath resultTypedPath = FieldPath::fromSynthetic(resultPath);
            setFieldTyped(resultTypedPath, post.resultField, result);
        }
    }
    
    void applyVoidPostconditions(CallExpr *Call, const std::map<std::string, SymValue>& oldValues) {
        auto *Callee = Call->getDirectCallee();
        if (!Callee) return;
        std::string fn = Callee->getNameAsString();
        auto it = FnPostconditions.find(fn);
        if (it == FnPostconditions.end()) return;

        // Build argument substitution map
        std::map<std::string, SymValue> argSubst;
        for (unsigned i = 0; i < Call->getNumArgs(); i++) {
            Expr *ArgExpr = Call->getArg(i)->IgnoreParenImpCasts();
            std::string argKey = "arg" + std::to_string(i);

            // Try to evaluate as constant
            Expr::EvalResult R;
            if (safeEvaluateAsInt(ArgExpr, R, Ctx)) {
                auto val = safeGetInt64(R.Val.getInt());
                if (val) argSubst[argKey] = SymValue::Const(*val);
            } else {
                // Try to get as variable reference
                FieldPath argTypedPath = getTypedPath(ArgExpr);
                std::string argPath = argTypedPath.toString();
                if (argTypedPath.isValid() && !hasRuntimeIndex(argPath)) {
                    argSubst[argKey] = SymValue::Var(argPath);
                    // Also add arg0._field for object fields
                    std::string argPrefix = argKey + ".";
                    // Iterate over TypedEnv
                    for (const auto& [fieldPath, value] : TypedEnv) {
                        llvm::StringRef pathStr = fieldPath.toStringRef();
                        if (startsWithDotPrefix(pathStr, argPath) &&
                            pathStr.find("._proof_") == llvm::StringRef::npos) {
                            std::string field = pathStr.drop_front(argPath.size() + 1).str();
                            argSubst[argPrefix + field] = value;
                        }
                    }
                }
            }
        }

        for (const auto& post : it->second) {
            std::map<std::string, SymValue> subst = argSubst;
            for (auto& kv : oldValues) subst["old:" + kv.first] = kv.second;
            SymValue result = post.expr.substitute(subst);

            if (Call->getNumArgs() > 0) {
                auto *Arg = Call->getArg(0)->IgnoreParenImpCasts();
                FieldPath argTypedPath2 = getTypedPath(Arg);
                std::string argPath = argTypedPath2.toString();
                if (argTypedPath2.isValid() && !hasRuntimeIndex(argPath)) {
                    setFieldTyped(argTypedPath2, post.resultField, result);
                }
            }
        }
    }
    
    // Apply method postconditions
    void applyMethodPostconditions(const FieldPath& objTypedPath, const MethodInfo& info,
                                   const std::map<std::string, SymValue>& oldValues,
                                   const std::map<std::string, SymValue>& argValues = {}) {
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
        for (const auto& [fieldPath, value] : TypedEnv) {
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
            SymValue result = post.expr.substitute(subst);
            setFieldTyped(objTypedPath, post.resultField, result);
        }
    }

    // Track buffer properties for pointers derived from arrays
    // Takes VarDecl* to enable type-safe FieldPath tracking
    void trackBufferPointerInit(const VarDecl* VD, QualType varType, Expr* Init) {
        if (!varType->isPointerType()) return;

        // Create typed path for the variable
        FieldPath varPath = FieldPath::fromDecl(VD);

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
                auto srcOriginIt = PointerBaseOrigins.find(srcPath);
                if (srcOriginIt != PointerBaseOrigins.end()) {
                    PointerBaseOrigins[varPath] = srcOriginIt->second;
                }
                return;
            }

            // Phase 3: Track base origin even for untracked pointers (e.g., raw parameters)
            // Check if source already has a base origin (from prior arithmetic)
            auto srcOriginIt = PointerBaseOrigins.find(srcPath);
            if (srcOriginIt != PointerBaseOrigins.end()) {
                PointerBaseOrigins[varPath] = srcOriginIt->second;
            } else {
                // Check if source is a parameter - establish base origin with zero offset
                auto srcParamIdx = getParameterIndex(Init, CurrentFunctionDecl);
                if (srcParamIdx) {
                    PointerBaseOrigins[varPath] = BaseOriginInfo(srcPath, SymValue::Const(0), srcParamIdx);
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

                        auto srcOriginIt = PointerBaseOrigins.find(srcPath);
                        auto offsetParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                        if (srcOriginIt != PointerBaseOrigins.end()) {
                            // Propagate existing base origin with accumulated offset
                            BaseOriginInfo newOrigin = srcOriginIt->second;
                            newOrigin.offsetBytes = SymValue::Add(newOrigin.offsetBytes, offsetBytes);
                            // Track offset param if this is the first offset contribution
                            if (!newOrigin.offsetParamIdx && offsetParamIdx) {
                                newOrigin.offsetParamIdx = offsetParamIdx;
                                newOrigin.offsetElemSize = elemSizeVal;
                                newOrigin.offsetNegated = isNegated;
                            }
                            PointerBaseOrigins[varPath] = std::move(newOrigin);
                        } else {
                            // Check if source is a parameter - create new base origin
                            auto srcParamIdx = getParameterIndex(LHS, CurrentFunctionDecl);
                            if (srcParamIdx) {
                                PointerBaseOrigins[varPath] = BaseOriginInfo(srcPath, offsetBytes, srcParamIdx, offsetParamIdx, elemSizeVal, isNegated);
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

                    auto srcOriginIt = PointerBaseOrigins.find(srcPath);
                    auto offsetParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                    if (srcOriginIt != PointerBaseOrigins.end()) {
                        BaseOriginInfo newOrigin = srcOriginIt->second;
                        newOrigin.offsetBytes = SymValue::Add(newOrigin.offsetBytes, offsetBytes);
                        if (!newOrigin.offsetParamIdx && offsetParamIdx) {
                            newOrigin.offsetParamIdx = offsetParamIdx;
                            newOrigin.offsetElemSize = elemSizeVal;
                            newOrigin.offsetNegated = isNegated;
                        }
                        PointerBaseOrigins[varPath] = std::move(newOrigin);
                    } else {
                        auto srcParamIdx = getParameterIndex(LHS, CurrentFunctionDecl);
                        if (srcParamIdx) {
                            PointerBaseOrigins[varPath] = BaseOriginInfo(srcPath, offsetBytes, srcParamIdx, offsetParamIdx, elemSizeVal, isNegated);
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
                    PointerLifetimes[VD->getName().str()] = pls;
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
        std::string var = D->getName().str();
        FieldPath varPath = FieldPath::fromDecl(D);

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
                    PointerLifetimes[var] = pls;
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
                PointerAliases[var] = srcPathStr;
                // Copy initialization state (to propagate uninitialized through copies)
                SymValue initState = getFieldTyped(srcPath, "_initialized");
                if (initState.kind != SymValue::UNKNOWN) {
                    setFieldTyped(varPath, "_initialized", initState);
                } else {
                    setFieldTyped(varPath, "_initialized", SymValue::Const(1));
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
                        setFieldTyped(varPath, "_nonnull", SymValue::Const(1));
                        gotNonnull = true;
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
                        PointerLifetimes[var] = pls;
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
        } else if (varType->isPointerType() && !D->hasInit()) {
            // Uninitialized local pointer - using it in any way is UB
            // Only track for local variables, not parameters or extern
            // Function parameters (ParmVarDecl) are initialized by the caller
            if (D->hasLocalStorage() && !D->isExceptionVariable() && !isa<ParmVarDecl>(D)) {
                setFieldTyped(varPath, "_initialized", SymValue::Const(0));
                setFieldTyped(varPath, "_nonnull", SymValue::Unknown());
            }
        }

        for (auto *A : D->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                llvm::StringRef s = Ann->getAnnotation();
                if (s.starts_with("dtt:")) {
                    llvm::StringRef rest = s.drop_front(4);
                    auto eqPos = rest.find('=');
                    if (eqPos != llvm::StringRef::npos) {
                        std::string field = rest.substr(0, eqPos).str();
                        auto value = safeStoll(rest.substr(eqPos + 1).str());
                        if (value) setFieldTyped(varPath, field, SymValue::Const(*value));
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
                llvm::SmallVector<std::pair<VarBoundKey, ProofOp>, 4> newProofs;
                for (const auto& [key, op] : VarBoundProofs) {
                    if (key.varName == srcVar) {
                        newProofs.push_back({{var, key.objPath, key.field}, op});
                    }
                }
                for (const auto& [key, op] : newProofs) {
                    VarBoundProofs[key] = op;
                }
            }
            // Also unwrap for CXXConstructExpr (reuse Unwrapped from above)
            if (auto *CE = dyn_cast<CXXConstructExpr>(Unwrapped)) {
                // Apply constructor postconditions
                auto *Ctor = CE->getConstructor();
                if (Ctor && Ctor->getParent()) {
                    std::string className = Ctor->getParent()->getName().str();

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
                    if (className == "variant" && CE->getNumArgs() >= 1) {
                        QualType variantType = D->getType().getNonReferenceType();
                        if (auto *TST = variantType->getAs<TemplateSpecializationType>()) {
                            Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
                            QualType argType = Arg->getType().getNonReferenceType().getUnqualifiedType();

                            // Search variant's template args for matching type
                            unsigned idx = 0;
                            bool found = false;
                            for (const auto &variantArg : TST->template_arguments()) {
                                if (variantArg.getKind() == TemplateArgument::Type) {
                                    QualType altType = variantArg.getAsType().getUnqualifiedType();
                                    if (Ctx.hasSameType(altType, argType)) {
                                        setFieldTyped(varPath, "_variant_index", SymValue::Const(idx));
                                        found = true;
                                        break;
                                    }
                                    idx++;
                                }
                            }
                            // If we didn't find exact match, try implicit conversions
                            if (!found) {
                                idx = 0;
                                for (const auto &variantArg : TST->template_arguments()) {
                                    if (variantArg.getKind() == TemplateArgument::Type) {
                                        QualType altType = variantArg.getAsType();
                                        // Check if arg is implicitly convertible to this alternative
                                        if (argType->isIntegralOrEnumerationType() && altType->isIntegralOrEnumerationType()) {
                                            setFieldTyped(varPath, "_variant_index", SymValue::Const(idx));
                                            found = true;
                                            break;
                                        }
                                        if (argType->isFloatingType() && altType->isFloatingType()) {
                                            setFieldTyped(varPath, "_variant_index", SymValue::Const(idx));
                                            found = true;
                                            break;
                                        }
                                        // Check for const char* -> std::string conversion
                                        if (argType->isPointerType()) {
                                            QualType pointee = argType->getPointeeType();
                                            if (pointee->isCharType()) {
                                                std::string altTypeName = altType.getAsString();
                                                if (altTypeName.find("basic_string") != std::string::npos ||
                                                    altTypeName.find("string") != std::string::npos) {
                                                    setFieldTyped(varPath, "_variant_index", SymValue::Const(idx));
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                        idx++;
                                    }
                                }
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
                        
                        for (const auto& post : it->second.ensures) {
                            SymValue result = post.expr.substitute(argVals);
                            setFieldTyped(varPath, post.resultField, result);
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
                auto originIt = PointerBaseOrigins.find(lhsTypedPath);
                auto offsetParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                int64_t elemSizeVal = 1;
                bool isNegated = (B->getOpcode() == BO_SubAssign);
                if (auto elemSize = safeGetTypeSizeInChars(Ctx, LHS->getType()->getPointeeType())) {
                    elemSizeVal = *elemSize;
                }
                if (originIt != PointerBaseOrigins.end()) {
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
                        PointerBaseOrigins[lhsTypedPath] = BaseOriginInfo(lhsTypedPath, deltaBytes, lhsParamIdx, offsetParamIdx, elemSizeVal, isNegated);
                    }
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

                                auto srcOriginIt = PointerBaseOrigins.find(srcTypedPath);
                                auto offsetParamIdx = getParameterIndex(deltaExpr, CurrentFunctionDecl);
                                if (srcOriginIt != PointerBaseOrigins.end()) {
                                    // Propagate existing base origin with accumulated offset
                                    BaseOriginInfo newOrigin = srcOriginIt->second;
                                    newOrigin.offsetBytes = SymValue::Add(newOrigin.offsetBytes, deltaBytes);
                                    // Track offset param if first contribution
                                    if (!newOrigin.offsetParamIdx && offsetParamIdx) {
                                        newOrigin.offsetParamIdx = offsetParamIdx;
                                        newOrigin.offsetElemSize = elemSizeVal;
                                        newOrigin.offsetNegated = isNegated;
                                    }
                                    PointerBaseOrigins[lhsTypedPath] = std::move(newOrigin);
                                } else {
                                    // Check if source is a parameter - create new base origin
                                    auto srcParamIdx = getParameterIndex(ptrExpr, CurrentFunctionDecl);
                                    if (srcParamIdx) {
                                        PointerBaseOrigins[lhsTypedPath] = BaseOriginInfo(srcTypedPath, deltaBytes, srcParamIdx, offsetParamIdx, elemSizeVal, isNegated);
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
                PointerAliases.erase(lhsPath);
                // Phase 3: Clear stale base origin - will be updated below if applicable
                PointerBaseOrigins.erase(lhsTypedPath);
                // Also invalidate aliases that point TO this pointer (they're now stale)
                llvm::SmallVector<llvm::StringRef, 4> toErase;
                for (const auto& entry : PointerAliases) {
                    if (entry.second == lhsPath) {
                        toErase.push_back(entry.first());
                    }
                }
                for (const auto& key : toErase) {
                    PointerAliases.erase(key);
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
                        PointerLifetimes[lhsPath] = pls;
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
                    PointerAliases[lhsPath] = srcPath;

                    // Phase 3: Track base origin for simple pointer copy (p = q)
                    auto srcOriginIt = PointerBaseOrigins.find(srcTypedPath);
                    if (srcOriginIt != PointerBaseOrigins.end()) {
                        // Propagate existing base origin
                        PointerBaseOrigins[lhsTypedPath] = srcOriginIt->second;
                    } else {
                        // Check if source is a parameter - establish base origin with zero offset
                        auto srcParamIdx = getParameterIndex(RHS, CurrentFunctionDecl);
                        if (srcParamIdx) {
                            PointerBaseOrigins[lhsTypedPath] = BaseOriginInfo(srcTypedPath, SymValue::Const(0), srcParamIdx);
                        }
                    }
                } else if (auto *CE = dyn_cast<CallExpr>(RHS->IgnoreParenCasts())) {
                    // Function call - mark as unknown and track malloc provenance
                    setFieldTyped(lhsTypedPath, "_nonnull", SymValue::Unknown());
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
                        trackAllocationSize(lhsTypedPath, lhsType, CE, allocName);
                    }
                } else {
                    // Unknown source - mark as unknown
                    setFieldTyped(lhsTypedPath, "_nonnull", SymValue::Unknown());
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
            std::string field = Mem->getMemberDecl()->getName().str();
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
        ProofOp op;
        ProofOp negOp;
    };

    // Pre-increment/decrement guard info for "while (++i <= N)" or "while (--i >= 0)"
    struct PreIncDecGuardInfo {
        std::string varName;
        std::optional<int64_t> lowerBound;  // Established by pre-inc (e.g., i >= 1)
        std::optional<int64_t> upperBound;  // From comparison RHS
        ProofOp upperOp;             // LT or LE
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
            result.upperOp = (opcode == BO_LE) ? ProofOp::LE : ProofOp::LT;
        } else {  // isPreDec
            // --i >= N: after decrement, i >= N (comparison passed)
            // --i > N: after decrement, i > N
            // For --i >= 0, we know i >= 0 in body
            result.lowerBound = bound;
            if (opcode == BO_GE) {
                // --i >= 0 means i >= 0 after decrement
                result.upperOp = ProofOp::LE;  // placeholder, no upper bound from this
            } else {
                // --i > N means i > N, i.e., i >= N+1
                result.lowerBound = bound + 1;
                result.upperOp = ProofOp::LE;
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
                        MethodKey key{Method->getParent()->getName().str(), Method->getNameAsString(), MCE->getNumArgs()};

                        auto it = MethodAnnotations.find(key);
                        if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
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
                                if (val && *val > 0) {
                                    // i < _size - N (N > 0) implies i < _size
                                    // Phase 1 Migration: Use typed path
                                    objPath = getTypedPath(ME->getBase()).toString();
                                    field = ME->getMemberDecl()->getName().str();
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
                        MethodKey key{Method->getParent()->getName().str(),
                                     Method->getNameAsString(),
                                     MCE->getNumArgs()};

                        auto it = MethodAnnotations.find(key);
                        if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
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

            return VarGuardInfo{varName, objPath, field, op, negOp};
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
                    MethodKey key{Method->getParent()->getName().str(),
                                  Method->getNameAsString(),
                                  MCE->getNumArgs()};

                    auto it = MethodAnnotations.find(key);
                    if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
                        GuardInfo gi;
                        // Phase 1 Migration: Use typed path
                        FieldPath baseTypedPath = getTypedPath(MCE->getImplicitObjectArgument());
                        gi.basePath = baseTypedPath.toString();
                        gi.field = it->second.returns;  // _size
                        if (!baseTypedPath.isValid()) return std::nullopt;
                        
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
                    // Phase 1 Migration: Use typed path
                    FieldPath baseTypedPath = getTypedPath(Mem->getBase());
                    gi.basePath = baseTypedPath.toString();
                    gi.field = Mem->getMemberDecl()->getName().str();
                    if (!baseTypedPath.isValid() || hasRuntimeIndex(gi.basePath)) return std::nullopt;
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

            std::string className = Method->getParent()->getName().str();
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
        std::string variantPath;
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

        std::string fn = Callee->getNameAsString();
        if (fn != "holds_alternative") return std::nullopt;

        // Check if it's std::holds_alternative
        bool isStdHolds = false;
        if (auto *DC = Callee->getDeclContext()) {
            while (DC) {
                if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                    if (NS->getName() == "std") {
                        isStdHolds = true;
                        break;
                    }
                }
                DC = DC->getParent();
            }
        }
        if (!isStdHolds) return std::nullopt;

        Expr *Arg = Call->getArg(0)->IgnoreParenImpCasts();
        FieldPath argPath = getTypedPath(Arg);
        if (!argPath.isValid()) return std::nullopt;

        // Get variant type
        QualType variantType = Arg->getType().getNonReferenceType();
        auto *TST = variantType->getAs<TemplateSpecializationType>();
        if (!TST) return std::nullopt;

        // Get template argument (the type T in holds_alternative<T>)
        if (auto *DRE = dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts())) {
            if (auto TAL = DRE->getTemplateArgs()) {
                unsigned numArgs = DRE->getNumTemplateArgs();
                if (numArgs > 0) {
                    const TemplateArgumentLoc &TALoc = TAL[0];
                    const TemplateArgument &TA = TALoc.getArgument();
                    if (TA.getKind() == TemplateArgument::Type) {
                        QualType requestedType = TA.getAsType();
                        // Find index in variant's template args
                        unsigned idx = 0;
                        for (const auto &variantArg : TST->template_arguments()) {
                            if (variantArg.getKind() == TemplateArgument::Type) {
                                if (Ctx.hasSameType(variantArg.getAsType(), requestedType)) {
                                    return HoldsAlternativeInfo{argPath.toString(), static_cast<int64_t>(idx), negated};
                                }
                                idx++;
                            }
                        }
                    }
                }
            }
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
            // Path-sensitive lifetime tracking
            auto savedLifetimes = PointerLifetimes;

            addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->op);
            TraverseStmt(If->getThen());
            removeVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field);

            // Early return: if then-block terminates and no else, negation holds after if
            if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->negOp);
                PointerLifetimes = savedLifetimes;
            }

            if (If->getElse()) {
                auto afterThenLifetimes = PointerLifetimes;
                PointerLifetimes = savedLifetimes;  // Restore pre-if state for else-branch
                addVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field, varGuard->negOp);
                TraverseStmt(If->getElse());
                removeVarBoundProof(varGuard->varName, varGuard->objPath, varGuard->field);
                if (doesBlockTerminate(If->getElse())) {
                    PointerLifetimes = afterThenLifetimes;
                }
            }
            return true;
        }

        // Try extracting multiple guards (handles compound conditions like k >= 0 && k < 10)
        auto guards = extractAllGuards(If->getCond());
        if (!guards.empty()) {
            // Path-sensitive lifetime tracking
            auto savedLifetimes = PointerLifetimes;

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
                removeProof(g.basePath, g.proofKey);
            }

            // Early return: if then-block terminates and no else, negation holds after if
            if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                PointerLifetimes = savedLifetimes;
                // Add negation proofs that persist after the if-statement
                if (guards.size() == 1) {
                    addProof(guards[0].basePath, guards[0].negProofKey);
                    if (!guards[0].extraNegProofKey.empty()) {
                        addProof(guards[0].basePath, guards[0].extraNegProofKey);
                    }
                    // Array element propagation: if arr != NULL is proven (for pointer-to-pointer),
                    // also mark arr[?]._nonnull = 1 (trust array elements are valid)
                    if (guards[0].negProofKey == "_nonnull!=0") {
                        FieldPath elemPath = FieldPath::fromSynthetic(guards[0].basePath + "[?]");
                        setFieldTyped(elemPath, "_nonnull", SymValue::Const(1));
                    }
                    // Size-parameter to pointer-parameter inference:
                    // If size >= N (N > 0) is proven, associated pointer is non-null.
                    // Common convention: buffer/buffer_len, data/data_size, ptr/ptr_length
                    if (guards[0].field == "_value") {
                        // Check if negProofKey indicates size >= N for some N > 0
                        // negProofKey format: "_value>=N" or "_value>N"
                        const std::string& negKey = guards[0].negProofKey;
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
                            std::string sizeName = guards[0].basePath;
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
            }

            if (If->getElse()) {
                auto afterThenLifetimes = PointerLifetimes;
                PointerLifetimes = savedLifetimes;  // Restore pre-if state for else-branch
                // Add negative proofs for else-block (only for single guard case)
                if (guards.size() == 1) {
                    addProof(guards[0].basePath, guards[0].negProofKey);
                    if (!guards[0].extraNegProofKey.empty()) {
                        addProof(guards[0].basePath, guards[0].extraNegProofKey);
                    }
                    TraverseStmt(If->getElse());
                    removeProof(guards[0].basePath, guards[0].negProofKey);
                    if (!guards[0].extraNegProofKey.empty()) {
                        removeProof(guards[0].basePath, guards[0].extraNegProofKey);
                    }

                    // Else-return: if else terminates, original condition holds after if-else
                    if (doesBlockTerminate(If->getElse())) {
                        addProof(guards[0].basePath, guards[0].proofKey);
                        PointerLifetimes = afterThenLifetimes;
                    }
                } else {
                    // For compound conditions, we can't negate properly, just traverse
                    TraverseStmt(If->getElse());
                    if (doesBlockTerminate(If->getElse())) {
                        PointerLifetimes = afterThenLifetimes;
                    }
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
            // Path-sensitive lifetime tracking
            auto savedLifetimes = PointerLifetimes;

            // In the then-block, we only know one condition is true (not which)
            // So we traverse without adding proofs
            TraverseStmt(If->getThen());

            // Early return: if then-block terminates and no else, ALL negations hold
            if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                PointerLifetimes = savedLifetimes;
                for (const auto& g : orGuards) {
                    addProof(g.basePath, g.negProofKey);
                    if (!g.extraNegProofKey.empty()) {
                        addProof(g.basePath, g.extraNegProofKey);
                    }
                    // Array element propagation for pointer-to-pointer guards
                    if (g.negProofKey == "_nonnull!=0") {
                        FieldPath elemPath = FieldPath::fromSynthetic(g.basePath + "[?]");
                        setFieldTyped(elemPath, "_nonnull", SymValue::Const(1));
                    }
                }
            }

            if (If->getElse()) {
                auto afterThenLifetimes = PointerLifetimes;
                PointerLifetimes = savedLifetimes;  // Restore pre-if state for else-branch
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
                    removeProof(g.basePath, g.negProofKey);
                    if (!g.extraNegProofKey.empty()) {
                        removeProof(g.basePath, g.extraNegProofKey);
                    }
                }

                // Else-return: if else terminates, at least one original condition holds
                // But we can't determine which, so no proofs added
                if (doesBlockTerminate(If->getElse())) {
                    PointerLifetimes = afterThenLifetimes;
                }
            }
            return true;
        }

        auto boolCall = extractBoolCall(If->getCond());
        if (boolCall) {
            llvm::SmallVectorImpl<BoolPostcondition>* postconds = nullptr;

            if (boolCall->isMethod) {
                MethodKey key{boolCall->className, boolCall->fnName, boolCall->argCount};
                auto it = MethodAnnotations.find(key);
                if (it != MethodAnnotations.end()) postconds = &it->second.boolPostconds;
            } else {
                auto it = FnBoolPostconditions.find(boolCall->fnName);
                if (it != FnBoolPostconditions.end()) postconds = &it->second;
            }

            if (postconds) {
                // Path-sensitive lifetime tracking
                auto savedLifetimes = PointerLifetimes;

                bool thenBranchIsTrue = !boolCall->negated;

                for (const auto& bp : *postconds) {
                    if (bp.onTrue == thenBranchIsTrue) {
                        std::string actualPath = boolCall->argPath;
                        std::string proofKey = bp.field + ProofOpStrings[static_cast<int>(bp.op)] + std::to_string(bp.rhs);
                        addProof(actualPath, proofKey);
                    }
                }

                TraverseStmt(If->getThen());

                for (const auto& bp : *postconds) {
                    if (bp.onTrue == thenBranchIsTrue) {
                        std::string actualPath = boolCall->argPath;
                        std::string proofKey = bp.field + ProofOpStrings[static_cast<int>(bp.op)] + std::to_string(bp.rhs);
                        removeProof(actualPath, proofKey);
                    }
                }

                // Early return: if then-block terminates and no else, negation proofs hold
                if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                    PointerLifetimes = savedLifetimes;
                    // Add the false-branch proofs since we know condition was false
                    for (const auto& bp : *postconds) {
                        if (bp.onTrue != thenBranchIsTrue) {
                            std::string actualPath = boolCall->argPath;
                            std::string proofKey = bp.field + ProofOpStrings[static_cast<int>(bp.op)] + std::to_string(bp.rhs);
                            addProof(actualPath, proofKey);
                        }
                    }
                }

                if (If->getElse()) {
                    auto afterThenLifetimes = PointerLifetimes;
                    PointerLifetimes = savedLifetimes;  // Restore pre-if state for else-branch
                    for (const auto& bp : *postconds) {
                        if (bp.onTrue != thenBranchIsTrue) {
                            std::string actualPath = boolCall->argPath;
                            std::string proofKey = bp.field + ProofOpStrings[static_cast<int>(bp.op)] + std::to_string(bp.rhs);
                            addProof(actualPath, proofKey);
                        }
                    }

                    TraverseStmt(If->getElse());

                    for (const auto& bp : *postconds) {
                        if (bp.onTrue != thenBranchIsTrue) {
                            std::string actualPath = boolCall->argPath;
                            std::string proofKey = bp.field + ProofOpStrings[static_cast<int>(bp.op)] + std::to_string(bp.rhs);
                            removeProof(actualPath, proofKey);
                        }
                    }

                    if (doesBlockTerminate(If->getElse())) {
                        PointerLifetimes = afterThenLifetimes;
                    }
                }
                return true;
            }
        }

        // Handle std::holds_alternative<T>(v) - variant type guard
        auto holdsAlt = extractHoldsAlternative(If->getCond());
        if (holdsAlt) {
            auto savedLifetimes = PointerLifetimes;

            std::string proofKey = "_variant_index==" + std::to_string(holdsAlt->alternativeIndex);
            std::string negProofKey = "_variant_index!=" + std::to_string(holdsAlt->alternativeIndex);

            if (!holdsAlt->negated) {
                // if (holds_alternative<T>(v)) - T's index holds in then-branch
                addProof(holdsAlt->variantPath, proofKey);
                TraverseStmt(If->getThen());
                removeProof(holdsAlt->variantPath, proofKey);

                // Early return: if then terminates and no else, negation holds after
                if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                    PointerLifetimes = savedLifetimes;
                    addProof(holdsAlt->variantPath, negProofKey);
                }

                if (If->getElse()) {
                    auto afterThenLifetimes = PointerLifetimes;
                    PointerLifetimes = savedLifetimes;
                    addProof(holdsAlt->variantPath, negProofKey);
                    TraverseStmt(If->getElse());
                    removeProof(holdsAlt->variantPath, negProofKey);

                    if (doesBlockTerminate(If->getElse())) {
                        PointerLifetimes = afterThenLifetimes;
                        addProof(holdsAlt->variantPath, proofKey);
                    }
                }
            } else {
                // if (!holds_alternative<T>(v)) - NOT T's index holds in then-branch
                addProof(holdsAlt->variantPath, negProofKey);
                TraverseStmt(If->getThen());
                removeProof(holdsAlt->variantPath, negProofKey);

                if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
                    PointerLifetimes = savedLifetimes;
                    addProof(holdsAlt->variantPath, proofKey);
                }

                if (If->getElse()) {
                    auto afterThenLifetimes = PointerLifetimes;
                    PointerLifetimes = savedLifetimes;
                    addProof(holdsAlt->variantPath, proofKey);
                    TraverseStmt(If->getElse());
                    removeProof(holdsAlt->variantPath, proofKey);

                    if (doesBlockTerminate(If->getElse())) {
                        PointerLifetimes = afterThenLifetimes;
                        addProof(holdsAlt->variantPath, negProofKey);
                    }
                }
            }
            return true;
        }

        TraverseStmt(If->getCond());

        // Path-sensitive lifetime tracking: save state before branches
        // If a branch terminates (early return), its state changes don't affect code after
        auto savedLifetimes = PointerLifetimes;

        TraverseStmt(If->getThen());

        if (doesBlockTerminate(If->getThen()) && !If->getElse()) {
            // Then-block terminates with no else: restore state
            // Code after if only executes if condition was false
            PointerLifetimes = savedLifetimes;
        }

        if (If->getElse()) {
            auto afterThenLifetimes = PointerLifetimes;
            PointerLifetimes = savedLifetimes;  // Restore pre-if state for else-branch
            TraverseStmt(If->getElse());
            if (doesBlockTerminate(If->getElse())) {
                // Else terminates: restore to after-then state
                // Code after if-else only executes if we took the then branch
                PointerLifetimes = afterThenLifetimes;
            }
        }
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
                removeProof(g.basePath, g.proofKey);
            }
            return true;
        }

        // Try pre-increment/decrement pattern: while (++i <= N) or while (--i >= 0)
        // Pre-increment: ++i executes BEFORE comparison, so in body i >= 1
        // Pre-decrement: --i executes BEFORE comparison, so in body i < original
        if (auto preIncGuard = extractPreIncDecGuard(While->getCond())) {
            llvm::SmallVector<std::string, 4> proofsToRemove;

            // Add lower bound proof (for pre-increment: i >= 1)
            if (preIncGuard->lowerBound) {
                std::string proof = "_value>=" + std::to_string(*preIncGuard->lowerBound);
                addProof(preIncGuard->varName, proof);
                proofsToRemove.push_back(preIncGuard->varName + "._proof_" + proof);
            }

            // Add upper bound proof
            if (preIncGuard->upperBound) {
                std::string opStr = (preIncGuard->upperOp == ProofOp::LT) ? "<" : "<=";
                std::string proof = "_value" + opStr + std::to_string(*preIncGuard->upperBound);
                addProof(preIncGuard->varName, proof);
                proofsToRemove.push_back(preIncGuard->varName + "._proof_" + proof);
            }

            // Clear concrete value so variable is treated as symbolic with bounds
            std::optional<SymValue> savedValue = getEnvValue(preIncGuard->varName);
            if (savedValue) {
                eraseEnvValue(preIncGuard->varName);
            }

            TraverseStmt(While->getBody());

            // Restore
            if (savedValue && savedValue->kind != SymValue::UNKNOWN) {
                setEnvValue(preIncGuard->varName, *savedValue);
            }
            for (const auto& key : proofsToRemove) {
                eraseEnvValue(key);
            }
            return true;
        }

        // Default: traverse condition and body normally
        TraverseStmt(While->getCond());
        TraverseStmt(While->getBody());
        return true;
    }

    // Helper: Find all pointer variables that are incremented/decremented in a statement
    llvm::StringSet<> findIncrementedPointers(Stmt *S) {
        llvm::StringSet<> result;
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
            for (const auto& entry : childResult)
                result.insert(entry.getKey());
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
        ProofOp upperOp = ProofOp::LT;
        bool isForwardCondition = true;  // < or <= or != (ascending)
        bool incrementMatchesCondition = true;  // Verify direction matches

        // Extract loop variable and lower bound from init
        if (auto *Init = For->getInit()) {
            // Case 1: Declaration (for (int i = 0; ...))
            if (auto *DS = dyn_cast<DeclStmt>(Init)) {
                if (DS->isSingleDecl()) {
                    if (auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl())) {
                        loopVar = VD->getName().str();
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
                                upperOp = ProofOp::LT;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_LE) {
                                upperOp = ProofOp::LE;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_NE) {
                                // != implies < when incrementing from start to bound
                                upperOp = ProofOp::LT;
                                isForwardCondition = true;
                            }
                            else if (BO->getOpcode() == BO_GT) {
                                upperOp = ProofOp::GT;
                                isForwardCondition = false;
                            }
                            else if (BO->getOpcode() == BO_GE) {
                                upperOp = ProofOp::GE;
                                isForwardCondition = false;
                            }
                            else goto default_traverse;

                            // Case 1: Method call (v.size())
                            if (auto *MCE = dyn_cast<CXXMemberCallExpr>(RHS)) {
                                auto *Method = MCE->getMethodDecl();
                                if (Method && Method->getParent()) {
                                    MethodKey key{Method->getParent()->getName().str(),
                                                  Method->getNameAsString(),
                                                  MCE->getNumArgs()};

                                    auto it = MethodAnnotations.find(key);
                                    if (it != MethodAnnotations.end() && !it->second.returns.empty()) {
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
                                                // Phase 1 Migration: Use typed path
                                                objPath = getTypedPath(ME->getBase()).toString();
                                                boundField = ME->getMemberDecl()->getName().str();
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
                                        setEnvValue(boundField, boundVal);
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
            llvm::SmallVector<std::string, 4> proofsToRemove;

            // Add lower bound proof: loopVar._value >= N
            // Using _value field so it can be matched against arg0 constraints
            if (lowerBound) {
                std::string proof = "_value>=" + std::to_string(*lowerBound);
                addProof(loopVar, proof);
                proofsToRemove.push_back(loopVar + "._proof_" + proof);
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
                std::string opStr = (upperOp == ProofOp::LT) ? "<" : "<=";
                std::string proof = "_value" + opStr + std::to_string(*constUpperBound);
                addProof(loopVar, proof);
                proofsToRemove.push_back(loopVar + "._proof_" + proof);

                // When step > 1, compute tighter upper bound
                // For i < bound with step S starting from L: max i = L + floor((bound-1-L)/S)*S
                // This enables proving arr[i+K] safe when K < S
                if (stepValue && *stepValue > 1 && lowerBound && upperOp == ProofOp::LT) {
                    int64_t L = *lowerBound;
                    int64_t B = *constUpperBound;
                    int64_t S = *stepValue;
                    if (B > L && S > 0) {
                        int64_t maxI = L + ((B - 1 - L) / S) * S;
                        std::string tightProof = "_value<=" + std::to_string(maxI);
                        addProof(loopVar, tightProof);
                        proofsToRemove.push_back(loopVar + "._proof_" + tightProof);
                    }
                }
            }

            // Traverse loop body with proofs active
            if (For->getInit()) TraverseStmt(For->getInit());

            // IMPORTANT: Remove the loop variable's concrete value from TypedEnv.
            // The init (e.g., "int i = 0") sets i to a constant, but during
            // loop body analysis, i should be treated as symbolic with only
            // the loop bounds as constraints (not the initial value).
            std::optional<SymValue> savedLoopVarValue = getEnvValue(loopVar);
            if (savedLoopVarValue) {
                eraseEnvValue(loopVar);
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
            for (const auto& entry : incrementedPtrs) {
                std::string ptrPathStr = entry.getKey().str();
                FieldPath ptrPath = FieldPath::fromSynthetic(ptrPathStr);
                SymValue baseLen = getFieldTyped(ptrPath, "_base_length");
                if (baseLen.kind != SymValue::UNKNOWN) {
                    SymValue offset = getFieldTyped(ptrPath, "_offset");
                    savedOffsets[ptrPathStr] = offset;
                    // Set offset = initial + loopVar (symbolic)
                    SymValue loopVarSym = SymValue::Var(loopVar);
                    SymValue newOffset = (offset.kind == SymValue::CONST && offset.constVal == 0)
                        ? loopVarSym
                        : SymValue::Add(offset, loopVarSym);
                    setFieldTyped(ptrPath, "_offset", newOffset);
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
            if (savedLoopVarValue && savedLoopVarValue->kind != SymValue::UNKNOWN) {
                // After loop, i has an indeterminate value - don't restore constant
                // Just leave it erased so subsequent uses are symbolic
            }

            // Remove proofs after loop
            if (hasFieldUpperBound) {
                removeVarBoundProof(loopVar, objPath, boundField);
            }
            for (const auto& key : proofsToRemove) {
                eraseEnvValue(key);
            }

            return true;
        }

        // Default traversal if we couldn't extract bounds
        default_traverse:
        // When there's a direction mismatch (e.g., i < 10 with --i), the loop is unsound
        // and we cannot prove any bounds. The loop variable should be treated as unknown
        // during body analysis to avoid false negatives from using the init value.
        // Only apply this when we detected a mismatch, not just when bounds weren't extracted.
        if (!loopVar.empty() && !incrementMatchesCondition && (hasFieldUpperBound || constUpperBound.has_value())) {
            if (For->getInit()) TraverseStmt(For->getInit());
            eraseEnvValue(loopVar);  // Erase the init value
            if (For->getCond()) TraverseStmt(For->getCond());
            if (For->getInc()) TraverseStmt(For->getInc());
            if (For->getBody()) TraverseStmt(For->getBody());
            return true;
        }
        return RecursiveASTVisitor::TraverseForStmt(For);
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

        std::string className = Method->getParent()->getName().str();
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
                    savedNonnull = getEnvValue(objPath + "._nonnull");
                }

                std::string reason = "call to unannotated method '" + className + "::" + methodName + "()'";
                invalidatePropertiesWithProvenance(objPath, E->getBeginLoc(), reason);

                // Restore _nonnull for arrow calls since pointer nullability is unchanged
                if (isArrowCall && savedNonnull) {
                    setEnvValue(objPath + "._nonnull", *savedNonnull);
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
                    // Also add arg0._field for object argument fields
                    std::string argPrefix = argKey + ".";
                    // Iterate over TypedEnv
                    for (const auto& [fieldPath, value] : TypedEnv) {
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
                auto argIt = argVals.find("arg" + std::to_string(avc->argIdx));
                if (argIt != argVals.end()) {
                    auto argEval = argIt->second.eval();
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
        if (!info.ensures.empty()) {
            auto oldVals = captureOldValuesMethod(objPath);
            applyMethodPostconditions(objTypedPath, info, oldVals, argVals);
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
                if (NS->getName() == "std") return true;
            }
        }
        return false;
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
                if (Method && Method->getParent()) {
                    std::string className = Method->getParent()->getName().str();
                    auto anglePos = className.find('<');
                    if (anglePos != std::string::npos) className = className.substr(0, anglePos);

                    // Special handling for std::variant assignment - compute index from RHS type
                    if (className == "variant") {
                        QualType lhsType = E->getArg(0)->getType().getNonReferenceType();
                        if (auto *TST = lhsType->getAs<TemplateSpecializationType>()) {
                            QualType rhsType = RHS->getType().getNonReferenceType();
                            // Remove cv-qualifiers for comparison
                            rhsType = rhsType.getUnqualifiedType();

                            // Search variant's template args for matching type
                            unsigned idx = 0;
                            bool found = false;
                            for (const auto &variantArg : TST->template_arguments()) {
                                if (variantArg.getKind() == TemplateArgument::Type) {
                                    QualType altType = variantArg.getAsType().getUnqualifiedType();
                                    if (Ctx.hasSameType(altType, rhsType)) {
                                        setFieldTyped(lhsTypedPath, "_variant_index", SymValue::Const(idx));
                                        found = true;
                                        break;
                                    }
                                    idx++;
                                }
                            }
                            // If we didn't find exact match, try implicit conversions
                            if (!found) {
                                idx = 0;
                                for (const auto &variantArg : TST->template_arguments()) {
                                    if (variantArg.getKind() == TemplateArgument::Type) {
                                        QualType altType = variantArg.getAsType();
                                        // Check if RHS is implicitly convertible to this alternative
                                        if (rhsType->isIntegralOrEnumerationType() && altType->isIntegralOrEnumerationType()) {
                                            // Integer types can convert
                                            setFieldTyped(lhsTypedPath, "_variant_index", SymValue::Const(idx));
                                            found = true;
                                            break;
                                        }
                                        if (rhsType->isFloatingType() && altType->isFloatingType()) {
                                            // Float types can convert
                                            setFieldTyped(lhsTypedPath, "_variant_index", SymValue::Const(idx));
                                            found = true;
                                            break;
                                        }
                                        // Check for const char* -> std::string conversion
                                        if (rhsType->isPointerType()) {
                                            QualType pointee = rhsType->getPointeeType();
                                            if (pointee->isCharType()) {
                                                // const char* - check if alternative is basic_string
                                                std::string altTypeName = altType.getAsString();
                                                if (altTypeName.find("basic_string") != std::string::npos ||
                                                    altTypeName.find("string") != std::string::npos) {
                                                    setFieldTyped(lhsTypedPath, "_variant_index", SymValue::Const(idx));
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                        idx++;
                                    }
                                }
                            }
                        }
                    }

                    MethodKey key{className, "operator=", E->getNumArgs() - 1};
                    auto it = MethodAnnotations.find(key);
                    if (it != MethodAnnotations.end()) {
                        const MethodInfo& info = it->second;
                        std::map<std::string, SymValue> subst;
                        for (unsigned i = 1; i < E->getNumArgs(); i++) {
                            std::string argKey = "arg" + std::to_string(i-1);
                            subst[argKey] = exprToSymValueImpl(E->getArg(i), Ctx);
                        }
                        for (const auto& post : info.ensures) {
                            SymValue result = post.expr.substitute(subst);
                            setFieldTyped(lhsTypedPath, post.resultField, result);
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

            std::string className = Method->getParent()->getName().str();
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
                    subst["old:" + post.resultField] = getField(objPath, post.resultField);
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

            // Apply postconditions
            for (const auto& post : info.ensures) {
                SymValue result = post.expr.substitute(subst);
                setFieldTyped(objTypedPath, post.resultField, result);
            }

            return true;
        }

        // Handle operator* and operator-> (dereference operators with preconditions)
        // Used by std::optional, std::unique_ptr, iterators, etc.
        if (Op == OO_Star || Op == OO_Arrow) {
            auto *Method = dyn_cast<CXXMethodDecl>(E->getCalleeDecl());
            if (!Method || !Method->getParent()) return true;

            std::string className = Method->getParent()->getName().str();
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
                    SymValue fieldVal = getField(objPath, sfc->field);
                    auto fieldEval = fieldVal.eval();

                    if (fieldEval) {
                        bool satisfied = false;
                        switch (sfc->op) {
                            case ConstraintExpr::CmpEQ: satisfied = (*fieldEval == sfc->value); break;
                            case ConstraintExpr::CmpNE: satisfied = (*fieldEval != sfc->value); break;
                            case ConstraintExpr::CmpGT: satisfied = (*fieldEval > sfc->value); break;
                            case ConstraintExpr::CmpGE: satisfied = (*fieldEval >= sfc->value); break;
                            case ConstraintExpr::CmpLT: satisfied = (*fieldEval < sfc->value); break;
                            case ConstraintExpr::CmpLE: satisfied = (*fieldEval <= sfc->value); break;
                            default: satisfied = true; break;
                        }
                        if (!satisfied) {
                            Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                << className << opName << req->toString()
                                << objPath << sfc->field << fieldVal.str();
                        }
                    } else {
                        // Field is unknown - check for proofs
                        auto proofOp = constraintExprKindToProofOp(sfc->op);
                        if (proofOp) {
                            std::string proofKey = sfc->field + ProofOpStrings[static_cast<int>(*proofOp)] + std::to_string(sfc->value);
                            bool hasMatchingProof = hasProof(objPath, proofKey);

                            // Check equivalent proofs: field==1 ⟺ field!=0, field==0 ⟺ field!=1
                            if (!hasMatchingProof && sfc->op == ConstraintExpr::CmpEQ) {
                                if (sfc->value == 1) {
                                    hasMatchingProof = hasProof(objPath, sfc->field + "!=0");
                                } else if (sfc->value == 0) {
                                    hasMatchingProof = hasProof(objPath, sfc->field + "!=1");
                                }
                            }

                            if (!hasMatchingProof) {
                                if (!emitCategoryC(E->getBeginLoc(), objPath, sfc->field, req->toString())) {
                                    if (GlobalMode != DTTMode::Quiet) {
                                        emitCategoryD(E->getBeginLoc(), objPath, sfc->field, req->toString(), "field value unknown");
                                    }
                                }
                            }
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

        std::string className = Method->getParent()->getName().str();
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
                std::string varName = DRE->getDecl()->getNameAsString();

                bool varHasAnyProof = llvm::any_of(VarBoundProofs,
                    [&](const auto& entry) { return entry.first.varName == varName; });

                if (varHasAnyProof) {
                    if (reqProofOp && hasVarBoundProof(varName, objPath, avf->rhsField, *reqProofOp)) {
                        continue;  // Matching proof exists
                    }
                    if (fieldEval) {
                        Diag.Report(E->getBeginLoc(), ErrIndexBounds)
                            << className << "operator[]" << varName
                            << std::to_string(*fieldEval) << objPath << avf->rhsField << fieldVal.str();
                    }
                    continue;
                }

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
                        ProofContext proofCtx(GlobalProofs);
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

                // Try Z3 for symbolic bounds
                auto z3Result = checkComparisonWithZ3(objPath, indexVal, fieldVal, avf->op);
                if (z3Result.has_value() && *z3Result) {
                    continue;  // Z3 proved safe
                }
                // Z3 returned false (counterexample found) or nullopt (unknown)
                // Either way, we can't prove bounds are safe - emit Category D
                std::string constraint = indexVal.str() + " < " + fieldVal.str();
                emitCategoryD(E->getBeginLoc(), objPath, avf->rhsField, constraint,
                              "bounds could not be verified");
            }
        }
        return true;
    }

    // Check if a pointer dereference is safe (has nonnull proof)
    // Returns: true = safe, false = known null, nullopt = unknown
    std::optional<bool> checkNonnull(const std::string& ptrPath) {
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
        auto aliasIt = PointerAliases.find(ptrPath);
        if (aliasIt != PointerAliases.end()) {
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
                auto originIt = PointerBaseOrigins.find(ptrTypedPath);
                if (originIt != PointerBaseOrigins.end()) {
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
                        PointerBaseOrigins[ptrTypedPath] = BaseOriginInfo(
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
        // Skip if it's a member call or operator call (handled separately)
        if (isa<CXXMemberCallExpr>(E) || isa<CXXOperatorCallExpr>(E)) return true;

        auto *Callee = E->getDirectCallee();
        if (!Callee) return true;
        std::string fn = Callee->getNameAsString();

        // Handle free() calls for lifetime tracking
        if (isDeallocationFunction(Callee) && E->getNumArgs() >= 1) {
            // Phase 1 Migration: Use typed path
            FieldPath ptrTypedPath = getTypedPath(E->getArg(0));
            std::string ptrPath = ptrTypedPath.toString();
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

        // Handle std::any_cast<T>(any) - check _has_value
        if (fn == "any_cast" && E->getNumArgs() == 1) {
            bool isStdAnyCast = false;
            if (auto *DC = Callee->getDeclContext()) {
                while (DC) {
                    if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                        if (NS->getName() == "std") {
                            isStdAnyCast = true;
                            break;
                        }
                    }
                    DC = DC->getParent();
                }
            }
            if (isStdAnyCast) {
                Expr *Arg = E->getArg(0)->IgnoreParenImpCasts();
                // Handle both any_cast(a) and any_cast(&a)
                if (auto *UO = dyn_cast<UnaryOperator>(Arg)) {
                    if (UO->getOpcode() == UO_AddrOf) {
                        Arg = UO->getSubExpr()->IgnoreParenImpCasts();
                    }
                }
                FieldPath argPath = getTypedPath(Arg);
                if (argPath.isValid()) {
                    std::string pathStr = argPath.toString();

                    // Check proofs first (guards can override static value)
                    bool hasMatchingProof = hasProof(pathStr, "_has_value==1") ||
                                           hasProof(pathStr, "_has_value!=0");
                    if (hasMatchingProof) {
                        // Proof exists - access is safe
                    } else {
                        SymValue hasValue = getFieldTyped(argPath, "_has_value");
                        auto hasValueEval = hasValue.eval();
                        if (hasValueEval && *hasValueEval == 0) {
                            Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                << "std" << "any_cast" << "_has_value == 1"
                                << pathStr << "_has_value" << hasValue.str();
                        } else if (hasValueEval && *hasValueEval == 1) {
                            // Known to be 1 - access is safe
                        } else {
                            // Unknown - emit warning
                            if (!emitCategoryC(E->getBeginLoc(), pathStr, "_has_value", "_has_value == 1")) {
                                if (GlobalMode != DTTMode::Quiet) {
                                    emitCategoryD(E->getBeginLoc(), pathStr, "_has_value", "_has_value == 1", "field value unknown");
                                }
                            }
                        }
                    }
                }
            }
        }

        // Handle std::get<I>(variant) and std::get<T>(variant) - check _variant_index
        if (fn == "get" && E->getNumArgs() == 1) {
            bool isStdGet = false;
            if (auto *DC = Callee->getDeclContext()) {
                while (DC) {
                    if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                        if (NS->getName() == "std") {
                            isStdGet = true;
                            break;
                        }
                    }
                    DC = DC->getParent();
                }
            }
            if (isStdGet) {
                Expr *Arg = E->getArg(0)->IgnoreParenImpCasts();
                FieldPath argPath = getTypedPath(Arg);
                if (argPath.isValid()) {
                    std::string pathStr = argPath.toString();
                    QualType argType = Arg->getType().getNonReferenceType();

                    // Check if argument is std::variant
                    if (auto *TST = argType->getAs<TemplateSpecializationType>()) {
                        if (auto *TD = TST->getTemplateName().getAsTemplateDecl()) {
                            std::string tplName = TD->getName().str();
                            if (tplName == "variant") {
                                // Get the template argument (index or type) from std::get<>
                                std::optional<int64_t> requestedIndex;

                                // Try to extract template argument from the call
                                if (auto *DRE = dyn_cast<DeclRefExpr>(E->getCallee()->IgnoreParenImpCasts())) {
                                    if (auto TAL = DRE->getTemplateArgs()) {
                                        unsigned numArgs = DRE->getNumTemplateArgs();
                                        if (numArgs > 0) {
                                            const TemplateArgumentLoc &TALoc = TAL[0];
                                            const TemplateArgument &TA = TALoc.getArgument();
                                            if (TA.getKind() == TemplateArgument::Integral) {
                                                // std::get<I>(v) - integral index
                                                requestedIndex = TA.getAsIntegral().getExtValue();
                                            } else if (TA.getKind() == TemplateArgument::Type) {
                                                // std::get<T>(v) - type index, need to find which index
                                                QualType requestedType = TA.getAsType();
                                                // Search variant's template args for matching type
                                                unsigned idx = 0;
                                                for (const auto &variantArg : TST->template_arguments()) {
                                                    if (variantArg.getKind() == TemplateArgument::Type) {
                                                        if (Ctx.hasSameType(variantArg.getAsType(), requestedType)) {
                                                            requestedIndex = idx;
                                                            break;
                                                        }
                                                        idx++;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                if (requestedIndex.has_value()) {
                                    int64_t reqIdx = *requestedIndex;

                                    // Build proof key for variant index
                                    llvm::SmallString<32> proofKey;
                                    proofKey = "_variant_index==";
                                    proofKey += std::to_string(reqIdx);

                                    bool hasMatchingProof = hasProof(pathStr, proofKey.str());
                                    if (hasMatchingProof) {
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
        }

        // Handle std::holds_alternative<T>(variant) - creates proof
        if (fn == "holds_alternative" && E->getNumArgs() == 1) {
            bool isStdHolds = false;
            if (auto *DC = Callee->getDeclContext()) {
                while (DC) {
                    if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                        if (NS->getName() == "std") {
                            isStdHolds = true;
                            break;
                        }
                    }
                    DC = DC->getParent();
                }
            }
            if (isStdHolds) {
                // Record this call for guard extraction in VisitIfStmt
                // The actual proof creation happens in handleCondition() when
                // holds_alternative is used as an if condition
                // For now, this is a placeholder - guard extraction handles it
            }
        }

        // Handle C library free functions via annotation lookup (e.g., FILE* I/O)
        {
            FreeFunctionKey key{fn.c_str(), E->getNumArgs()};
            auto it = FreeFunctionAnnotations.find(key);
            if (it != FreeFunctionAnnotations.end()) {
                const FreeFunctionInfo& info = it->second;

                // Check all preconditions
                for (const auto& pre : info.preconditions) {
                    if (pre.argIndex >= 0 &&
                        static_cast<unsigned>(pre.argIndex) < E->getNumArgs()) {
                        Expr *ArgExpr = E->getArg(pre.argIndex)->IgnoreParenImpCasts();
                        FieldPath argPath = getTypedPath(ArgExpr);
                        if (argPath.isValid()) {
                            std::string pathStr = argPath.toString();

                            // Lifetime check (use-after-free detection)
                            if (pre.checkLifetime) {
                                checkUseAfterFree(pathStr, E->getBeginLoc());
                                continue;  // Lifetime precondition handled, skip field checks
                            }

                            // Build proof keys without heap allocation
                            llvm::SmallString<32> proofKey, proofKey2;
                            proofKey = pre.field;
                            proofKey += "!=0";
                            proofKey2 = pre.field;
                            proofKey2 += "==1";

                            bool hasMatchingProof = hasProof(pathStr, proofKey.str()) ||
                                                   hasProof(pathStr, proofKey2.str());
                            if (hasMatchingProof) {
                                // Proof exists - precondition satisfied
                            } else {
                                SymValue fieldVal = getFieldTyped(argPath, pre.field);
                                auto fieldEval = fieldVal.eval();
                                if (fieldEval && *fieldEval == pre.forbiddenValue) {
                                    // Known to violate precondition - definite error
                                    llvm::SmallString<64> reason;
                                    if (pre.errorReason[0] == '\0') {
                                        reason = pre.field;
                                        reason += " is ";
                                        reason += std::to_string(pre.forbiddenValue);
                                    } else {
                                        reason = pre.errorReason;
                                    }
                                    llvm::SmallString<64> constraint;
                                    constraint = pre.field;
                                    constraint += " != ";
                                    constraint += std::to_string(pre.forbiddenValue);
                                    Diag.Report(E->getBeginLoc(), ErrMethodReq)
                                        << "" << fn << constraint.str()
                                        << pathStr << pre.field << reason.str();
                                } else if (fieldEval && *fieldEval != pre.forbiddenValue) {
                                    // Known to satisfy precondition - OK
                                } else {
                                    // Unknown - emit warning
                                    llvm::SmallString<64> constraint;
                                    constraint = pre.field;
                                    constraint += " != ";
                                    constraint += std::to_string(pre.forbiddenValue);
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
                }

                // Apply all postconditions
                for (const auto& post : info.postconditions) {
                    if (post.argIndex >= 0 &&
                        static_cast<unsigned>(post.argIndex) < E->getNumArgs()) {
                        Expr *ModArg = E->getArg(post.argIndex)->IgnoreParenImpCasts();
                        FieldPath modPath = getTypedPath(ModArg);
                        if (modPath.isValid()) {
                            std::string modPathStr = modPath.toString();
                            setFieldTyped(modPath, post.field, SymValue::Const(post.value));
                            llvm::SmallString<32> proofKey, proofKey2;
                            proofKey = post.field;
                            proofKey += "!=0";
                            proofKey2 = post.field;
                            proofKey2 += "==1";
                            removeProof(modPathStr, proofKey.str());
                            removeProof(modPathStr, proofKey2.str());
                        }
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
            case BoundsResult::UnverifiedConst:
                Diag.Report(E->getBeginLoc(), DiagMemcpyUnverified.get(GlobalMode))
                    << (int)dstBytes.constVal;
                Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                    << (int)dstBytes.constVal;
                break;
            case BoundsResult::UnverifiedSymbolic:
                Diag.Report(E->getBeginLoc(), DiagMemcpySymbolicUnverified.get(GlobalMode));
                Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, copySize);
                break;
            case BoundsResult::UnverifiedUntracked:
                // Buffer is untracked but has symbolic size - infer precondition for call-site checking
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, copySize);
                break;
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
            case BoundsResult::UnverifiedConst:
                Diag.Report(E->getBeginLoc(), DiagMemsetUnverified.get(GlobalMode))
                    << (int)dstBytes.constVal;
                Diag.Report(E->getBeginLoc(), NoteAddBoundsCheck)
                    << (int)dstBytes.constVal;
                break;
            case BoundsResult::UnverifiedSymbolic:
                Diag.Report(E->getBeginLoc(), DiagMemsetSymbolicUnverified.get(GlobalMode));
                Diag.Report(E->getBeginLoc(), NoteAddSymbolicBoundsCheck);
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, setSize);
                break;
            case BoundsResult::UnverifiedUntracked:
                emitInferredPrecondition(E->getBeginLoc(), DstArg, SizeArg, setSize);
                break;
            }
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

        if (Callee->getReturnType()->isVoidType()) {
            auto oldVals = captureOldValues(E);
            applyVoidPostconditions(E, oldVals);
        }

        // Parse preconditions directly to ConstraintExprPtr (no bridge)
        llvm::SmallVector<ConstraintExprPtr, 4> preconditions;
        for (auto *A : Callee->attrs()) {
            if (auto *Ann = dyn_cast<AnnotateAttr>(A)) {
                llvm::StringRef s = Ann->getAnnotation();
                if (s.starts_with("dtt:requires=")) {
                    std::string translated = translateParamNames(s.drop_front(13).str(), Callee);
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
                    SymValue argVal = getField(argPath, "_value");
                    argVals[argKey] = (argVal.kind == SymValue::CONST) ? argVal : SymValue::Unknown();
                    for (const auto& [fieldPath, value] : TypedEnv) {
                        llvm::StringRef pathStr = fieldPath.toStringRef();
                        if (startsWithDotPrefix(pathStr, argPath) && pathStr.find("._proof_") == llvm::StringRef::npos) {
                            std::string field = pathStr.drop_front(argPath.size() + 1).str();
                            argVals[argKey + "." + field] = value;
                        }
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
            if (auto npc = asNullptrCheck(req)) {
                if (npc->argIdx >= argPaths.size()) continue;
                auto *Arg = E->getArg(npc->argIdx)->IgnoreParenImpCasts();
                std::string argPath = argPaths[npc->argIdx];

                if (!Arg->getType()->isPointerType()) {
                    // Integer comparison against 0
                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(Arg, R, Ctx)) {
                        int64_t val = R.Val.getInt().getSExtValue();
                        bool satisfied = (npc->op == ConstraintExpr::CmpNE) ? (val != 0) : (val == 0);
                        if (!satisfied) {
                            Diag.Report(E->getBeginLoc(), ErrFail) << fn << req->toString()
                                << argPath << "_value" << std::to_string(val);
                        }
                    }
                    continue;
                }

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

                    Expr::EvalResult R;
                    if (safeEvaluateAsInt(Arg, R, Ctx)) {
                        int64_t val = R.Val.getInt().getSExtValue();
                        bool satisfied = false;
                        switch (avc->op) {
                            case ConstraintExpr::CmpGT: satisfied = val > avc->value; break;
                            case ConstraintExpr::CmpGE: satisfied = val >= avc->value; break;
                            case ConstraintExpr::CmpLT: satisfied = val < avc->value; break;
                            case ConstraintExpr::CmpLE: satisfied = val <= avc->value; break;
                            case ConstraintExpr::CmpEQ: satisfied = val == avc->value; break;
                            case ConstraintExpr::CmpNE: satisfied = val != avc->value; break;
                            default: break;
                        }
                        if (!satisfied) {
                            Diag.Report(E->getBeginLoc(), ErrFail) << fn << req->toString()
                                << argPath << "_value" << std::to_string(val);
                        }
                    } else if (GlobalMode != DTTMode::Quiet) {
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
                        std::string reason = "call to unannotated function '" + fn + "()'";
                        invalidatePropertiesWithProvenance(argPath, E->getBeginLoc(), reason);
                    }
                }
            }
        }

        // Phase 2: Check inferred preconditions at call site
        checkCallSitePreconditions(E, Callee);

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
        loadSyntheticAnnotations();  // Register std:: type bounds checking

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
            } else if (llvm::StringRef(arg).starts_with("--dtt-mode=") || llvm::StringRef(arg).starts_with("-dtt-mode=")) {
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
