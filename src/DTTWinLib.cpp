// DTTWinLib.cpp - Windows API function annotations implementation for DTT
//
// Modeled after DTTCLib.cpp. Uses the same FreeFunctionAnnotations DenseMap
// and the same addResourceAcquire/addResourceRelease patterns.

#include "DTTWinLib.h"
#include "DTTCLib.h"  // For FreeFunctionAnnotations (shared global map)

namespace dtt {

void registerWindowsAnnotations() {
    // =========================================================================
    // Helpers (same pattern as DTTCLib.cpp)
    // =========================================================================

    auto addResourceAcquire = [](const char* fn, unsigned arity,
                                  const char* field, int64_t acquireValue,
                                  int64_t releaseValue) {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({field, acquireValue});
        info.scopeExitRequires.push_back({field, ProofOp::EQ, releaseValue});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    auto addResourceRelease = [](const char* fn, unsigned arity, int argIdx,
                                  const char* field, int64_t /*acquireValue*/,
                                  int64_t releaseValue) {
        FreeFunctionInfo info;
        info.postconditions.push_back({argIdx, field, releaseValue});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Close function with _nonnull precondition (like fclose pattern).
    // Checks _nonnull before close, invalidates _nonnull after (double-close detection).
    auto addResourceClose = [](const char* fn, unsigned arity, int argIdx,
                                const char* field, int64_t releaseValue,
                                const char* errorMsg) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            argIdx, "_nonnull", 0, ProofOp::NE,
            errorMsg
        });
        info.postconditions.push_back({argIdx, field, releaseValue});
        info.postconditions.push_back({argIdx, "_nonnull", 0});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // =========================================================================
    // Phase 1: HANDLE lifecycle
    //
    // Limitation: CreateFile and friends return INVALID_HANDLE_VALUE (-1) on
    // failure, not NULL. DTT tracks null/non-null but cannot distinguish -1
    // from a valid handle. The null-check pattern `if (h)` is still useful
    // since INVALID_HANDLE_VALUE is non-null and would pass the check.
    // =========================================================================

    // --- CreateFile A/W (7 args) ---
    addResourceAcquire("CreateFileA", 7, "@handle", 1, 0);
    addResourceAcquire("CreateFileW", 7, "@handle", 1, 0);

    // --- CloseHandle (1 arg) ---
    addResourceClose("CloseHandle", 1, 0, "@handle", 0,
                     "HANDLE may be invalid or already closed");

    // --- FindFirstFile A/W (2 args) ---
    addResourceAcquire("FindFirstFileA", 2, "@handle", 1, 0);
    addResourceAcquire("FindFirstFileW", 2, "@handle", 1, 0);

    // --- FindNextFile A/W (2 args) - require valid handle ---
    {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            0, "_nonnull", 0, ProofOp::NE,
            "HANDLE may be invalid or already closed"
        });
        FreeFunctionAnnotations[{"FindNextFileA", 2}] = info;
        FreeFunctionAnnotations[{"FindNextFileW", 2}] = info;
    }

    // --- FindClose (1 arg) ---
    addResourceClose("FindClose", 1, 0, "@handle", 0,
                     "HANDLE may be invalid or already closed");

    // --- GDI objects ---
    // CreateCompatibleDC(HDC) -> HDC
    addResourceAcquire("CreateCompatibleDC", 1, "@handle", 1, 0);
    // DeleteDC(HDC) -> BOOL
    addResourceClose("DeleteDC", 1, 0, "@handle", 0,
                     "HDC may be invalid or already deleted");
    // DeleteObject(HGDIOBJ) -> BOOL
    addResourceClose("DeleteObject", 1, 0, "@handle", 0,
                     "GDI object may be invalid or already deleted");

    // --- Synchronization ---
    // CreateEventW(SECURITY_ATTRIBUTES*, BOOL, BOOL, LPCWSTR) -> HANDLE (4 args)
    addResourceAcquire("CreateEventA", 4, "@handle", 1, 0);
    addResourceAcquire("CreateEventW", 4, "@handle", 1, 0);
    // CreateMutexW(SECURITY_ATTRIBUTES*, BOOL, LPCWSTR) -> HANDLE (3 args)
    addResourceAcquire("CreateMutexA", 3, "@handle", 1, 0);
    addResourceAcquire("CreateMutexW", 3, "@handle", 1, 0);
    // CreateSemaphoreW(SECURITY_ATTRIBUTES*, LONG, LONG, LPCWSTR) -> HANDLE (4 args)
    addResourceAcquire("CreateSemaphoreA", 4, "@handle", 1, 0);
    addResourceAcquire("CreateSemaphoreW", 4, "@handle", 1, 0);

    // --- Heap ---
    // HeapCreate(DWORD, DWORD, DWORD) -> HANDLE (3 args)
    addResourceAcquire("HeapCreate", 3, "@heap", 1, 0);
    // HeapDestroy(HANDLE) -> BOOL (1 arg)
    addResourceClose("HeapDestroy", 1, 0, "@heap", 0,
                     "Heap HANDLE may be invalid or already destroyed");
    // HeapAlloc(HANDLE, DWORD, DWORD) -> PVOID (3 args)
    addResourceAcquire("HeapAlloc", 3, "@allocated", 1, 0);
    // HeapFree(HANDLE, DWORD, PVOID) -> BOOL (3 args)
    addResourceRelease("HeapFree", 3, 2, "@allocated", 1, 0);

    // --- Memory-mapped files ---
    // CreateFileMappingW(HANDLE, SA*, DWORD, DWORD, DWORD, LPCWSTR) (6 args)
    addResourceAcquire("CreateFileMappingA", 6, "@handle", 1, 0);
    addResourceAcquire("CreateFileMappingW", 6, "@handle", 1, 0);
    // MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, DWORD) -> PVOID (5 args)
    addResourceAcquire("MapViewOfFile", 5, "@mapped", 1, 0);
    // UnmapViewOfFile(PVOID) -> BOOL (1 arg)
    addResourceClose("UnmapViewOfFile", 1, 0, "@mapped", 0,
                     "View may be invalid or already unmapped");

    // =========================================================================
    // Phase 2: COM/OLE allocator pairs
    // =========================================================================

    // --- BSTR ---
    // SysAllocString(const wchar_t*) -> BSTR (1 arg)
    addResourceAcquire("SysAllocString", 1, "@allocated", 1, 0);
    // SysAllocStringLen(const wchar_t*, UINT) -> BSTR (2 args)
    addResourceAcquire("SysAllocStringLen", 2, "@allocated", 1, 0);
    // SysAllocStringByteLen(const char*, UINT) -> BSTR (2 args)
    addResourceAcquire("SysAllocStringByteLen", 2, "@allocated", 1, 0);
    // SysFreeString(BSTR) -> void (1 arg)
    // SysFreeString(NULL) is safe, so use addResourceRelease (no precondition)
    addResourceRelease("SysFreeString", 1, 0, "@allocated", 1, 0);

    // --- CoTaskMem ---
    // CoTaskMemAlloc(SIZE_T) -> void* (1 arg)
    addResourceAcquire("CoTaskMemAlloc", 1, "@allocated", 1, 0);
    // CoTaskMemFree(void*) -> void (1 arg)
    // CoTaskMemFree(NULL) is safe
    addResourceRelease("CoTaskMemFree", 1, 0, "@allocated", 1, 0);

    // --- LocalAlloc / LocalFree ---
    // LocalAlloc(UINT, SIZE_T) -> void* (2 args)
    addResourceAcquire("LocalAlloc", 2, "@allocated", 1, 0);
    // LocalFree(void*) -> void* (1 arg)
    // LocalFree(NULL) is safe
    addResourceRelease("LocalFree", 1, 0, "@allocated", 1, 0);

    // --- GlobalAlloc / GlobalFree ---
    // GlobalAlloc(UINT, SIZE_T) -> void* (2 args)
    addResourceAcquire("GlobalAlloc", 2, "@allocated", 1, 0);
    // GlobalFree(void*) -> void* (1 arg)
    // GlobalFree(NULL) is safe
    addResourceRelease("GlobalFree", 1, 0, "@allocated", 1, 0);

    // =========================================================================
    // Phase 2b: SafeArray lifecycle
    // =========================================================================

    // SafeArrayCreate(VARTYPE, UINT, SAFEARRAYBOUND*) -> SAFEARRAY* (3 args)
    addResourceAcquire("SafeArrayCreate", 3, "@safearray", 1, 0);
    // SafeArrayCreateVector(VARTYPE, LONG, UINT) -> SAFEARRAY* (3 args)
    addResourceAcquire("SafeArrayCreateVector", 3, "@safearray", 1, 0);
    // SafeArrayDestroy(SAFEARRAY*) -> long (1 arg)
    addResourceClose("SafeArrayDestroy", 1, 0, "@safearray", 0,
                     "SAFEARRAY may be invalid or already destroyed");
}

} // namespace dtt
