// DTT SARIF Output — DiagnosticConsumer wrapper for SARIF 2.1.0 output
#pragma once

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <system_error>

namespace llvm::json { class OStream; }

namespace dtt {

/// Static SARIF rule definition. Instances are compile-time string constants.
struct SarifRule {
    llvm::StringRef Id;           // e.g. "DTT-bounds-overflow"
    llvm::StringRef Description;  // e.g. "Out-of-bounds access"
    llvm::StringRef HelpUri;      // CWE URL or empty
    llvm::StringRef CweTag;       // e.g. "CWE-787" or empty
};

// Rule constants (defined in DTTSarif.cpp)
extern const SarifRule RuleBoundsOverflow;
extern const SarifRule RuleNullDeref;
extern const SarifRule RuleUseAfterFree;
extern const SarifRule RuleDoubleFree;
extern const SarifRule RuleUninitialized;
extern const SarifRule RuleMismatchedDealloc;
extern const SarifRule RuleResourceLeak;
extern const SarifRule RuleMemopOverflow;
extern const SarifRule RulePointerArith;
extern const SarifRule RuleSprintfUnsafe;
extern const SarifRule RuleUnionAccess;
extern const SarifRule RuleUnionTypePun;
extern const SarifRule RulePrecondition;
extern const SarifRule RuleEnumExhaustive;
extern const SarifRule RuleAnnotationConflict;
extern const SarifRule RuleUnverifiedBounds;
extern const SarifRule RuleUnverifiedPrecondition;
extern const SarifRule RuleLostTracking;
extern const SarifRule RuleSolverFailed;

/// DiagnosticConsumer that captures DTT diagnostics as SARIF results.
/// Wraps an existing consumer, forwarding all diagnostics unchanged.
class SarifDiagConsumer : public clang::DiagnosticConsumer {
public:
    SarifDiagConsumer(clang::DiagnosticConsumer *Wrapped,
                      const clang::SourceManager &SM,
                      llvm::StringRef OutputPath,
                      llvm::StringRef RepoRoot);

    void HandleDiagnostic(clang::DiagnosticsEngine::Level Level,
                          const clang::Diagnostic &Info) override;

    /// Register a DTT diagnostic ID → SARIF rule mapping.
    void registerDiag(unsigned Id, const SarifRule *Rule);

    /// Register a diagnostic ID as a note (attaches to parent result).
    void registerNote(unsigned Id);

    /// Write SARIF JSON to OutputPath.
    std::error_code flush();

    bool hasResults() const { return !Results.empty(); }

private:
    struct NoteInfo {
        clang::SourceLocation Loc;
        llvm::SmallString<128> Message;
    };

    struct CapturedResult {
        const SarifRule *Rule;
        clang::DiagnosticsEngine::Level Level;
        clang::SourceLocation Loc;
        llvm::SmallString<256> Message;
        llvm::SmallVector<NoteInfo, 2> Notes;
    };

    clang::DiagnosticConsumer *Wrapped;
    const clang::SourceManager *SM;
    llvm::SmallString<128> OutputPath;
    llvm::SmallString<128> RepoRoot;

    llvm::DenseMap<unsigned, const SarifRule *> DiagRuleMap;
    llvm::DenseSet<unsigned> NoteIds;
    llvm::SmallVector<CapturedResult, 32> Results;

    /// Index into Results for the most recent error/warning (for note grouping).
    int CurrentParentIdx = -1;

    llvm::SmallString<128> getRelativePath(clang::SourceLocation Loc) const;
    std::string computeLineHash(clang::SourceLocation Loc) const;
    llvm::StringRef sarifLevel(clang::DiagnosticsEngine::Level Level) const;

    /// Emit a SARIF physicalLocation object (artifactLocation + region).
    void emitPhysicalLocation(llvm::json::OStream &J,
                              clang::SourceLocation Loc) const;
};

/// Walk up from FilePath looking for .git/ to determine repo root.
llvm::SmallString<128> findRepoRoot(llvm::StringRef FilePath);

} // namespace dtt
