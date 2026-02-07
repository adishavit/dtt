// 1300_win_handle_pass.cpp
// Tests: Safe Win32 HANDLE usage that should compile without DTT errors
// Mirrors pattern from 840_fopen_pass.cpp

#include "win_api.h"

// =============================================================================
// SECTION 1: CreateFileW + null check + CloseHandle
// =============================================================================

// Variant 1.1: if (h) guard then close
void test_createfile_null_check() {
    HANDLE h = CreateFileW(L"test.txt", 0, 0, nullptr, 0, 0, nullptr);
    if (h) {
        CloseHandle(h);
    }
}

// Variant 1.2: early return on null
void test_createfile_early_return() {
    HANDLE h = CreateFileW(L"test.txt", 0, 0, nullptr, 0, 0, nullptr);
    if (!h) return;
    CloseHandle(h);
}

// Variant 1.3: if (h != nullptr) explicit check
void test_createfile_explicit_check() {
    HANDLE h = CreateFileA("test.txt", 0, 0, nullptr, 0, 0, nullptr);
    if (h != nullptr) {
        CloseHandle(h);
    }
}

// =============================================================================
// SECTION 2: FindFirstFileW + null check + FindClose
// =============================================================================

// Variant 2.1: if (h) guard then FindClose
void test_findfile_null_check() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"*.txt", &fd);
    if (h) {
        FindClose(h);
    }
}

// Variant 2.2: early return on null
void test_findfile_early_return() {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*.txt", &fd);
    if (!h) return;
    FindClose(h);
}

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

// =============================================================================
// SECTION 6: Memory-mapped files
// =============================================================================

// Variant 6.1: MapViewOfFile + UnmapViewOfFile
void test_mmap() {
    HANDLE h = CreateFileW(L"test.dat", 0, 0, nullptr, 0, 0, nullptr);
    if (h) {
        HANDLE mapping = CreateFileMappingW(h, nullptr, 0, 0, 0, nullptr);
        if (mapping) {
            PVOID view = MapViewOfFile(mapping, 0, 0, 0, 0);
            if (view) {
                UnmapViewOfFile(view);
            }
            CloseHandle(mapping);
        }
        CloseHandle(h);
    }
}

int main() {
    return 0;
}
