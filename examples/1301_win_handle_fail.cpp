// 1301_win_handle_fail.cpp
// Tests: Unsafe Win32 HANDLE usage that DTT should catch
// Mirrors patterns from 989_fclose_double_fail.cpp and 841_fopen_fail.cpp

#include "win_api.h"

// =============================================================================
// SECTION 1: HANDLE leak (scope-exit violation)
// =============================================================================

// Variant 1.1: CreateFileW without close
void test_handle_leak() {
    HANDLE h = CreateFileW(L"test.txt", 0, 0, nullptr, 0, 0, nullptr);
    // ERROR: @handle == 1 at scope exit, need @handle == 0
}

// Variant 1.2: FindFirstFileW without FindClose
void test_findfile_leak() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"*.txt", &fd);
    // ERROR: @handle == 1 at scope exit, need @handle == 0
}

// =============================================================================
// SECTION 2: Double close
// =============================================================================

// Variant 2.1: CloseHandle twice
void test_double_close() {
    HANDLE h = CreateFileW(L"test.txt", 0, 0, nullptr, 0, 0, nullptr);
    if (h) {
        CloseHandle(h);
        CloseHandle(h);  // ERROR: HANDLE already closed
    }
}

// Variant 2.2: FindClose twice
void test_double_findclose() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"*.txt", &fd);
    if (h) {
        FindClose(h);
        FindClose(h);  // ERROR: HANDLE already closed
    }
}

// =============================================================================
// SECTION 3: CloseHandle on null
// =============================================================================

// Variant 3.1: CloseHandle(nullptr) is undefined
void test_close_nullptr() {
    HANDLE h = nullptr;
    CloseHandle(h);  // ERROR: HANDLE is null
}

// =============================================================================
// SECTION 4: GDI object leak
// =============================================================================

// Variant 4.1: CreateCompatibleDC without DeleteDC
void test_gdi_leak() {
    HDC dc = CreateCompatibleDC(nullptr);
    // ERROR: @handle == 1 at scope exit
}

// =============================================================================
// SECTION 5: Event handle leak
// =============================================================================

// Variant 5.1: CreateEventW without CloseHandle
void test_event_leak() {
    HANDLE h = CreateEventW(nullptr, 0, 0, nullptr);
    // ERROR: @handle == 1 at scope exit
}

int main() { return 0; }
