// 1300_win_handle_pass.cpp
// Tests: Safe Win32 HANDLE usage that should compile without DTT errors
// CreateFile/FindFirstFile variants are in 1300_win_handle_createfile_xfail.cpp
// (they return INVALID_HANDLE_VALUE, not NULL, so deferred guards don't apply).

#include "win_api.h"

// =============================================================================
// SECTION 3: GDI objects
// =============================================================================

// Variant 3.1: CreateCompatibleDC + DeleteDC
void test_gdi_dc() {
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc) {
        DeleteDC(dc);
    }
}

// =============================================================================
// SECTION 4: Synchronization objects
// =============================================================================

// Variant 4.1: CreateEventW + CloseHandle
void test_event() {
    HANDLE h = CreateEventW(nullptr, 0, 0, nullptr);
    if (h) {
        CloseHandle(h);
    }
}

// Variant 4.2: CreateMutexW + CloseHandle
void test_mutex() {
    HANDLE h = CreateMutexW(nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);
    }
}

// Variant 4.3: CreateSemaphoreW + CloseHandle
void test_semaphore() {
    HANDLE h = CreateSemaphoreW(nullptr, 0, 10, nullptr);
    if (h) {
        CloseHandle(h);
    }
}

// =============================================================================
// SECTION 5: Heap
// =============================================================================

// Variant 5.1: HeapCreate + HeapAlloc + HeapFree + HeapDestroy
void test_heap() {
    HANDLE heap = HeapCreate(0, 0, 0);
    if (heap) {
        PVOID p = HeapAlloc(heap, 0, 100);
        if (p) {
            HeapFree(heap, 0, p);
        }
        HeapDestroy(heap);
    }
}

int main() {
    return 0;
}
