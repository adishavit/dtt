// DTTWinLib.cpp - Windows API function annotations implementation for DTT
//
// Uses the same FreeFunctionAnnotations DenseMap and annotation patterns
// as DTTCLib.cpp.

#include "DTTWinLib.h"
#include "DTTCLib.h"

namespace dtt {

void registerWindowsAnnotations() {
    // =========================================================================
    // Registration helpers
    // =========================================================================

    // ret.@field = 1, scope-exit requires @field == 0
    auto addResourceAcquire = [](const char *fn, unsigned arity,
                                 const char *field) {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({field, 1});
        info.scopeExitRequires.push_back({field, ProofOp::EQ, 0});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // arg[argIdx].@field = 0 (marks resource as released)
    auto addResourceRelease = [](const char *fn, unsigned arity, int argIdx,
                                 const char *field) {
        FreeFunctionInfo info;
        info.postconditions.push_back({argIdx, field, 0});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Requires arg[argIdx]._nonnull != 0, then invalidates both ghost field
    // and _nonnull (prevents double-close)
    auto addResourceClose = [](const char *fn, unsigned arity, int argIdx,
                               const char *field, const char *errorMsg) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            argIdx, "_nonnull", 0, ProofOp::NE, errorMsg
        });
        info.postconditions.push_back({argIdx, field, 0});
        info.postconditions.push_back({argIdx, "_nonnull", 0});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Precondition-only: arg[argIdx]._nonnull != 0
    auto addPreNonnull = [](const char *fn, unsigned arity, int argIdx,
                            const char *errorMsg) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            argIdx, "_nonnull", 0, ProofOp::NE, errorMsg
        });
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Register both A (ANSI) and W (wide) variants with the same annotation
    auto addAcquireAW = [&](const char *fnA, const char *fnW,
                            unsigned arity, const char *field) {
        addResourceAcquire(fnA, arity, field);
        addResourceAcquire(fnW, arity, field);
    };

    // =========================================================================
    // Phase 1: HANDLE lifecycle
    //
    // Limitation: CreateFile and friends return INVALID_HANDLE_VALUE (-1) on
    // failure, not NULL. DTT tracks null/non-null but cannot distinguish -1
    // from a valid handle. The null-check pattern `if (h)` is still useful
    // since INVALID_HANDLE_VALUE is non-null and would pass the check.
    // =========================================================================

    static constexpr const char *HandleInvalid =
        "HANDLE may be invalid or already closed";

    addAcquireAW("CreateFileA",      "CreateFileW",      7, "@handle");
    addAcquireAW("FindFirstFileA",   "FindFirstFileW",   2, "@handle");
    addAcquireAW("CreateEventA",     "CreateEventW",     4, "@handle");
    addAcquireAW("CreateMutexA",     "CreateMutexW",     3, "@handle");
    addAcquireAW("CreateSemaphoreA", "CreateSemaphoreW", 4, "@handle");
    addAcquireAW("CreateFileMappingA", "CreateFileMappingW", 6, "@handle");

    addResourceClose("CloseHandle", 1, 0, "@handle", HandleInvalid);
    addResourceClose("FindClose",   1, 0, "@handle", HandleInvalid);

    addPreNonnull("FindNextFileA", 2, 0, HandleInvalid);
    addPreNonnull("FindNextFileW", 2, 0, HandleInvalid);

    // --- GDI objects ---
    addResourceAcquire("CreateCompatibleDC", 1, "@handle");
    addResourceClose("DeleteDC",     1, 0, "@handle",
                     "HDC may be invalid or already deleted");
    addResourceClose("DeleteObject", 1, 0, "@handle",
                     "GDI object may be invalid or already deleted");

    // --- Heap ---
    addResourceAcquire("HeapCreate", 3, "@heap");
    addResourceClose("HeapDestroy", 1, 0, "@heap",
                     "Heap HANDLE may be invalid or already destroyed");
    addResourceAcquire("HeapAlloc", 3, "@allocated");
    // HeapFree: arg2 is the pointer to free (not arg0)
    addResourceRelease("HeapFree", 3, 2, "@allocated");

    // --- Memory-mapped files ---
    addResourceAcquire("MapViewOfFile", 5, "@mapped");
    addResourceClose("UnmapViewOfFile", 1, 0, "@mapped",
                     "View may be invalid or already unmapped");

    // =========================================================================
    // Phase 2: COM/OLE allocator pairs
    // =========================================================================

    // --- BSTR ---
    addResourceAcquire("SysAllocString", 1, "@allocated");
    addResourceAcquire("SysAllocStringLen", 2, "@allocated");
    addResourceAcquire("SysAllocStringByteLen", 2, "@allocated");
    // SysFreeString(NULL) is safe — no precondition
    addResourceRelease("SysFreeString", 1, 0, "@allocated");

    // --- CoTaskMem ---
    addResourceAcquire("CoTaskMemAlloc", 1, "@allocated");
    // CoTaskMemFree(NULL) is safe
    addResourceRelease("CoTaskMemFree", 1, 0, "@allocated");

    // --- LocalAlloc / LocalFree ---
    addResourceAcquire("LocalAlloc", 2, "@allocated");
    // LocalFree(NULL) is safe
    addResourceRelease("LocalFree", 1, 0, "@allocated");

    // --- GlobalAlloc / GlobalFree ---
    addResourceAcquire("GlobalAlloc", 2, "@allocated");
    // GlobalFree(NULL) is safe
    addResourceRelease("GlobalFree", 1, 0, "@allocated");

    // =========================================================================
    // Phase 2b: SafeArray lifecycle
    // =========================================================================

    addResourceAcquire("SafeArrayCreate", 3, "@safearray");
    addResourceAcquire("SafeArrayCreateVector", 3, "@safearray");
    addResourceClose("SafeArrayDestroy", 1, 0, "@safearray",
                     "SAFEARRAY may be invalid or already destroyed");
}

} // namespace dtt
