// DTT SARIF Output — Implementation

#include "DTTSarif.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace llvm;

namespace dtt {

// ============================================================
// SARIF Rule Constants
// ============================================================

const SarifRule RuleBoundsOverflow = {
    "DTT-bounds-overflow", "Out-of-bounds access",
    "https://cwe.mitre.org/data/definitions/787.html", "CWE-787"};
const SarifRule RuleNullDeref = {
    "DTT-null-deref", "Null pointer dereference",
    "https://cwe.mitre.org/data/definitions/476.html", "CWE-476"};
const SarifRule RuleUseAfterFree = {
    "DTT-use-after-free", "Use after free",
    "https://cwe.mitre.org/data/definitions/416.html", "CWE-416"};
const SarifRule RuleDoubleFree = {
    "DTT-double-free", "Double free",
    "https://cwe.mitre.org/data/definitions/415.html", "CWE-415"};
const SarifRule RuleUninitialized = {
    "DTT-uninitialized", "Use of uninitialized memory",
    "https://cwe.mitre.org/data/definitions/457.html", "CWE-457"};
const SarifRule RuleMismatchedDealloc = {
    "DTT-mismatched-dealloc", "Mismatched allocation/deallocation",
    "https://cwe.mitre.org/data/definitions/762.html", "CWE-762"};
const SarifRule RuleResourceLeak = {
    "DTT-resource-leak", "Resource leak",
    "https://cwe.mitre.org/data/definitions/401.html", "CWE-401"};
const SarifRule RuleMemopOverflow = {
    "DTT-memop-overflow", "Memory operation buffer overflow",
    "https://cwe.mitre.org/data/definitions/120.html", "CWE-120"};
const SarifRule RulePointerArith = {
    "DTT-pointer-arith", "Invalid pointer arithmetic",
    "https://cwe.mitre.org/data/definitions/823.html", "CWE-823"};
const SarifRule RuleSprintfUnsafe = {
    "DTT-sprintf-unsafe", "Unbounded sprintf",
    "https://cwe.mitre.org/data/definitions/120.html", "CWE-120"};
const SarifRule RuleUnionAccess = {
    "DTT-union-access", "Inactive union member access",
    "https://cwe.mitre.org/data/definitions/758.html", "CWE-758"};
const SarifRule RuleUnionTypePun = {
    "DTT-union-type-pun", "Union type punning",
    "https://cwe.mitre.org/data/definitions/758.html", "CWE-758"};
const SarifRule RulePrecondition = {
    "DTT-precondition", "Precondition violation", "", ""};
const SarifRule RuleEnumExhaustive = {
    "DTT-enum-exhaustive", "Non-exhaustive enum switch", "", ""};
const SarifRule RuleAnnotationConflict = {
    "DTT-annotation-conflict", "Annotation conflict", "", ""};
const SarifRule RuleUnverifiedBounds = {
    "DTT-unverified-bounds", "Unverified buffer bounds",
    "https://cwe.mitre.org/data/definitions/120.html", "CWE-120"};
const SarifRule RuleUnverifiedPrecondition = {
    "DTT-unverified-precondition", "Unverified precondition", "", ""};
const SarifRule RuleLostTracking = {
    "DTT-lost-tracking", "Lost tracking", "", ""};
const SarifRule RuleSolverFailed = {
    "DTT-solver-failed", "Solver failed", "", ""};

// ============================================================
// SarifDiagConsumer
// ============================================================

SarifDiagConsumer::SarifDiagConsumer(DiagnosticConsumer *Wrapped,
                                     const SourceManager &SM,
                                     StringRef OutputPath,
                                     StringRef RepoRoot)
    : Wrapped(Wrapped), SM(&SM), OutputPath(OutputPath), RepoRoot(RepoRoot) {
    // Exported source archives have no Git metadata. Anchor their diagnostics
    // to the compilation directory instead of emitting machine-specific paths.
    if (this->RepoRoot.empty())
        sys::fs::current_path(this->RepoRoot);
}

void SarifDiagConsumer::registerDiag(unsigned Id, const SarifRule *Rule) {
    DiagRuleMap[Id] = Rule;
}

void SarifDiagConsumer::registerNote(unsigned Id) {
    NoteIds.insert(Id);
}

void SarifDiagConsumer::HandleDiagnostic(DiagnosticsEngine::Level Level,
                                          const Diagnostic &Info) {
    // Always forward to the original consumer first
    if (Wrapped)
        Wrapped->HandleDiagnostic(Level, Info);

    unsigned Id = Info.getID();

    // Check if this is a registered DTT error/warning
    auto RuleIt = DiagRuleMap.find(Id);
    if (RuleIt != DiagRuleMap.end() && Level != DiagnosticsEngine::Note) {
        CapturedResult R;
        R.Rule = RuleIt->second;
        R.Level = Level;
        R.Loc = Info.getLocation();
        Info.FormatDiagnostic(R.Message);
        Results.push_back(std::move(R));
        CurrentParentIdx = static_cast<int>(Results.size()) - 1;
        return;
    }

    // Check if this is a registered note → attach to parent
    if (Level == DiagnosticsEngine::Note && CurrentParentIdx >= 0) {
        bool isRegisteredNote = NoteIds.count(Id) || DiagRuleMap.count(Id);
        if (isRegisteredNote) {
            NoteInfo N;
            N.Loc = Info.getLocation();
            Info.FormatDiagnostic(N.Message);
            Results[CurrentParentIdx].Notes.push_back(std::move(N));
        }
    }

    // Non-DTT error/warning resets note grouping
    if (Level == DiagnosticsEngine::Error || Level == DiagnosticsEngine::Warning) {
        CurrentParentIdx = -1;
    }
}

SmallString<128> SarifDiagConsumer::getRelativePath(SourceLocation Loc) const {
    if (Loc.isInvalid())
        return SmallString<128>("<unknown>");

    SourceLocation ExpLoc = SM->getExpansionLoc(Loc);
    StringRef Filename = SM->getFilename(ExpLoc);
    // Stdin is a logical artifact, not a path to a file on disk.
    if (Filename == "<stdin>")
        return SmallString<128>("stdin.cpp");
    if (Filename.empty())
        return SmallString<128>("buffer.cpp");

    SmallString<128> AbsolutePath(Filename);
    if (!RepoRoot.empty()) {
        if (!sys::fs::make_absolute(AbsolutePath)) {
            sys::path::remove_dots(AbsolutePath, true);
            Filename = AbsolutePath;
        }
    }
    if (!RepoRoot.empty() &&
        sys::path::root_path(Filename) == sys::path::root_path(RepoRoot)) {
        auto FileIt = sys::path::begin(Filename);
        auto BaseIt = sys::path::begin(RepoRoot);
        const auto FileEnd = sys::path::end(Filename);
        const auto BaseEnd = sys::path::end(RepoRoot);
        while (FileIt != FileEnd && BaseIt != BaseEnd && *FileIt == *BaseIt) {
            ++FileIt;
            ++BaseIt;
        }
        SmallString<128> RelativePath;
        for (; BaseIt != BaseEnd; ++BaseIt)
            sys::path::append(RelativePath, "..");
        for (; FileIt != FileEnd; ++FileIt)
            sys::path::append(RelativePath, *FileIt);
        return RelativePath;
    }
    return SmallString<128>(Filename);
}

std::string SarifDiagConsumer::computeLineHash(SourceLocation Loc) const {
    if (Loc.isInvalid())
        return "";

    SourceLocation ExpLoc = SM->getExpansionLoc(Loc);
    FileID FID = SM->getFileID(ExpLoc);
    bool Invalid = false;
    StringRef Buf = SM->getBufferData(FID, &Invalid);
    if (Invalid || Buf.empty())
        return "";

    unsigned Offset = SM->getFileOffset(ExpLoc);

    // Find line start
    size_t LineStart = Buf.rfind('\n', Offset);
    LineStart = (LineStart == StringRef::npos) ? 0 : LineStart + 1;

    // Find line end
    size_t LineEnd = Buf.find('\n', Offset);
    if (LineEnd == StringRef::npos)
        LineEnd = Buf.size();

    StringRef Line = Buf.slice(LineStart, LineEnd).trim();
    return llvm::utohexstr(xxh3_64bits(Line));
}

StringRef SarifDiagConsumer::sarifLevel(DiagnosticsEngine::Level Level) const {
    switch (Level) {
    case DiagnosticsEngine::Error:
    case DiagnosticsEngine::Fatal:
        return "error";
    case DiagnosticsEngine::Warning:
        return "warning";
    case DiagnosticsEngine::Remark:
        return "note";
    default:
        return "none";
    }
}

void SarifDiagConsumer::emitPhysicalLocation(json::OStream &J,
                                              SourceLocation Loc) const {
    J.attributeBegin("physicalLocation");
    J.objectBegin();

    J.attributeBegin("artifactLocation");
    J.objectBegin();
    auto RelPath = getRelativePath(Loc);
    J.attribute("uri", RelPath);
    if (Loc.isValid() &&
        SM->getFilename(SM->getExpansionLoc(Loc)) != "<stdin>" &&
        SM->getFileEntryRefForID(SM->getFileID(SM->getExpansionLoc(Loc))))
        J.attribute("uriBaseId", "%SRCROOT%");
    J.objectEnd();
    J.attributeEnd(); // artifactLocation

    if (Loc.isValid()) {
        SourceLocation ExpLoc = SM->getExpansionLoc(Loc);
        unsigned Line = SM->getExpansionLineNumber(ExpLoc);
        unsigned Col = SM->getExpansionColumnNumber(ExpLoc);
        J.attributeBegin("region");
        J.objectBegin();
        J.attribute("startLine", static_cast<int64_t>(Line));
        J.attribute("startColumn", static_cast<int64_t>(Col));
        J.objectEnd();
        J.attributeEnd(); // region
    }

    J.objectEnd();
    J.attributeEnd(); // physicalLocation
}

std::error_code SarifDiagConsumer::flush() {
    std::error_code EC;
    raw_fd_ostream OS(OutputPath, EC, sys::fs::OF_None);
    if (EC)
        return EC;

    json::OStream J(OS, /*IndentSize=*/2);
    J.objectBegin();
    J.attribute("$schema",
                "https://raw.githubusercontent.com/oasis-tcs/"
                "sarif-spec/main/sarif-2.1/schema/sarif-schema-2.1.0.json");
    J.attribute("version", "2.1.0");

    J.attributeBegin("runs");
    J.arrayBegin();
    J.objectBegin(); // single run

    // tool.driver
    J.attributeBegin("tool");
    J.objectBegin();
    J.attributeBegin("driver");
    J.objectBegin();
    J.attribute("name", "DTT");
    J.attribute("version", "1.0.0");
    J.attribute("informationUri", "https://github.com/adishavit/dtt");

    // Collect unique rules in insertion order (deterministic)
    SmallSetVector<const SarifRule *, 8> UsedRules;
    for (const auto &R : Results)
        UsedRules.insert(R.Rule);

    // Emit rules array
    J.attributeBegin("rules");
    J.arrayBegin();
    for (const auto *Rule : UsedRules) {
        J.objectBegin();
        J.attribute("id", Rule->Id);
        J.attributeBegin("shortDescription");
        J.objectBegin();
        J.attribute("text", Rule->Description);
        J.objectEnd();
        J.attributeEnd(); // shortDescription

        if (!Rule->HelpUri.empty())
            J.attribute("helpUri", Rule->HelpUri);

        if (!Rule->CweTag.empty()) {
            J.attributeBegin("properties");
            J.objectBegin();
            J.attributeBegin("tags");
            J.arrayBegin();
            J.value(Rule->CweTag);
            J.arrayEnd();
            J.attributeEnd(); // tags
            J.objectEnd();
            J.attributeEnd(); // properties
        }
        J.objectEnd(); // rule
    }
    J.arrayEnd();
    J.attributeEnd(); // rules

    J.objectEnd();
    J.attributeEnd(); // driver
    J.objectEnd();
    J.attributeEnd(); // tool

    // Preserve source for stdin/virtual main files: consumers cannot open them.
    FileID MainID = SM->getMainFileID();
    if (!SM->getFileEntryRefForID(MainID) ||
        SM->getFilename(SM->getLocForStartOfFile(MainID)) == "<stdin>") {
        J.attributeBegin("artifacts");
        J.arrayBegin();
        J.objectBegin();
        J.attributeBegin("location");
        J.objectBegin();
        J.attribute("uri", getRelativePath(SM->getLocForStartOfFile(MainID)));
        J.objectEnd();
        J.attributeEnd();
        J.attributeBegin("contents");
        J.objectBegin();
        J.attribute("text", SM->getBufferData(MainID));
        J.objectEnd();
        J.attributeEnd();
        J.objectEnd();
        J.arrayEnd();
        J.attributeEnd();
    }

    // results
    J.attributeBegin("results");
    J.arrayBegin();
    for (const auto &R : Results) {
        J.objectBegin();
        J.attribute("ruleId", R.Rule->Id);
        J.attribute("level", sarifLevel(R.Level));

        J.attributeBegin("message");
        J.objectBegin();
        J.attribute("text", R.Message);
        J.objectEnd();
        J.attributeEnd(); // message

        // locations
        J.attributeBegin("locations");
        J.arrayBegin();
        J.objectBegin();
        emitPhysicalLocation(J, R.Loc);
        J.objectEnd();
        J.arrayEnd();
        J.attributeEnd(); // locations

        // partialFingerprints
        auto Hash = computeLineHash(R.Loc);
        if (!Hash.empty()) {
            J.attributeBegin("partialFingerprints");
            J.objectBegin();
            J.attribute("primaryLocationLineHash", Hash);
            J.objectEnd();
            J.attributeEnd(); // partialFingerprints
        }

        // relatedLocations (from notes)
        if (!R.Notes.empty()) {
            J.attributeBegin("relatedLocations");
            J.arrayBegin();
            int NoteIdx = 0;
            for (const auto &N : R.Notes) {
                J.objectBegin();
                J.attribute("id", static_cast<int64_t>(NoteIdx++));
                J.attributeBegin("message");
                J.objectBegin();
                J.attribute("text", N.Message);
                J.objectEnd();
                J.attributeEnd(); // message

                if (N.Loc.isValid())
                    emitPhysicalLocation(J, N.Loc);
                J.objectEnd(); // relatedLocation
            }
            J.arrayEnd();
            J.attributeEnd(); // relatedLocations
        }

        J.objectEnd(); // result
    }
    J.arrayEnd();
    J.attributeEnd(); // results

    J.objectEnd(); // run
    J.arrayEnd();
    J.attributeEnd(); // runs

    J.objectEnd(); // root
    OS.flush();
    return {};
}

// ============================================================
// findRepoRoot
// ============================================================

SmallString<128> findRepoRoot(StringRef FilePath) {
    SmallString<128> Dir(FilePath);
    if (sys::fs::make_absolute(Dir)) return SmallString<128>();
    sys::path::remove_dots(Dir, true);
    sys::path::remove_filename(Dir);

    while (!Dir.empty()) {
        SmallString<128> GitDir(Dir);
        sys::path::append(GitDir, ".git");
        if (sys::fs::exists(GitDir))
            return Dir;

        StringRef Parent = sys::path::parent_path(Dir);
        if (Parent == Dir)
            break; // reached filesystem root
        // Parent borrows Dir's storage; assignment would invalidate its input.
        Dir.resize(Parent.size());
    }
    return SmallString<128>();
}

} // namespace dtt
