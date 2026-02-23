// 1300: CreateFile/FindFirstFile HANDLE tracking — safe patterns
// Deferred guard evaluation via `when result._nonnull != 0` in win32.dtt
// enables DTT to track HANDLE resources through null-check guards.
// No false positives expected for if(h)/if(!h) patterns.

#include "win_api.h"

// Variant 1.1: CreateFileW + if (h) guard + CloseHandle
void test_createfile_null_check() {
    HANDLE h = CreateFileW(L"test.txt", 0, 0, nullptr, 0, 0, nullptr);
    if (h) {
        CloseHandle(h);
    }
}

// Variant 1.2: CreateFileW + early return on null
void test_createfile_early_return() {
    HANDLE h = CreateFileW(L"test.txt", 0, 0, nullptr, 0, 0, nullptr);
    if (!h) return;
    CloseHandle(h);
}

// Variant 1.3: CreateFileA + explicit nullptr check
void test_createfile_explicit_check() {
    HANDLE h = CreateFileA("test.txt", 0, 0, nullptr, 0, 0, nullptr);
    if (h != nullptr) {
        CloseHandle(h);
    }
}

// Variant 2.1: FindFirstFileW + if (h) guard + FindClose
void test_findfile_null_check() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"*.txt", &fd);
    if (h) {
        FindClose(h);
    }
}

// Variant 2.2: FindFirstFileA + early return
void test_findfile_early_return() {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*.txt", &fd);
    if (!h) return;
    FindClose(h);
}

// Variant 2.3: FindFirstFile + FindNextFile + FindClose
void test_findfile_with_next() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"*.txt", &fd);
    if (h) {
        FindNextFileW(h, &fd);
        FindClose(h);
    }
}

// Variant 6.1: CreateFileW + CreateFileMappingW + MapViewOfFile
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

int main() { return 0; }
