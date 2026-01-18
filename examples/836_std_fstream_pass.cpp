// 836_std_fstream_pass.cpp
// Tests: Safe std::fstream usage that should compile without DTT errors

#include <fstream>
#include <string>

// =============================================================================
// SECTION 1: Guard with is_open() before I/O
// =============================================================================

// Variant 1.1: if (is_open()) guard then read
void test_is_open_guard_read() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        std::string line;
        std::getline(file, line);  // OK: guarded
    }
}

// Variant 1.2: if (is_open()) guard then write
void test_is_open_guard_write() {
    std::ofstream file("test.txt");
    if (file.is_open()) {
        file << "hello";  // OK: guarded
    }
}

// Variant 1.3: early return on not open
void test_early_return() {
    std::ifstream file("test.txt");
    if (!file.is_open()) return;
    std::string line;
    std::getline(file, line);  // OK: guarded
}

// =============================================================================
// SECTION 2: Guard with bool conversion
// =============================================================================

// Variant 2.1: if (file) guard
void test_bool_guard_read() {
    std::ifstream file("test.txt");
    if (file) {
        std::string line;
        std::getline(file, line);  // OK: file is valid
    }
}

// Variant 2.2: if (file) after operation
void test_bool_after_op() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        std::string line;
        if (std::getline(file, line)) {
            // OK: getline succeeded
        }
    }
}

// =============================================================================
// SECTION 3: Guard with good()
// =============================================================================

// Variant 3.1: if (good()) before read
void test_good_guard() {
    std::ifstream file("test.txt");
    if (file.good()) {
        char c;
        file.get(c);  // OK: stream is good
    }
}

// =============================================================================
// SECTION 4: Guard with !fail()
// =============================================================================

// Variant 4.1: if (!fail()) guard
void test_not_fail_guard() {
    std::ifstream file("test.txt");
    if (!file.fail()) {
        std::string line;
        std::getline(file, line);  // OK: stream hasn't failed
    }
}

// =============================================================================
// SECTION 5: Loop with stream condition
// =============================================================================

// Variant 5.1: while (getline) loop
void test_while_getline() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            // OK: loop condition checks stream
        }
    }
}

// Variant 5.2: while (file >> word) loop
void test_while_extraction() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        std::string word;
        while (file >> word) {
            // OK: extraction returns stream reference
        }
    }
}

// =============================================================================
// SECTION 6: open() then is_open()
// =============================================================================

// Variant 6.1: open then check
void test_open_then_check() {
    std::ifstream file;
    file.open("test.txt");
    if (file.is_open()) {
        std::string line;
        std::getline(file, line);  // OK: guarded
    }
}

// =============================================================================
// SECTION 7: fstream (read/write)
// =============================================================================

// Variant 7.1: fstream with both guards
void test_fstream_both() {
    std::fstream file("test.txt", std::ios::in | std::ios::out);
    if (file.is_open()) {
        std::string line;
        std::getline(file, line);  // OK: guarded
        file << "hello";           // OK: guarded
    }
}

// =============================================================================
// SECTION 8: Binary I/O with guards
// =============================================================================

// Variant 8.1: read/write with is_open
void test_binary_io() {
    std::fstream file("test.bin", std::ios::binary | std::ios::in | std::ios::out);
    if (file.is_open()) {
        char buf[100];
        file.read(buf, sizeof(buf));  // OK: guarded
        if (file.gcount() > 0) {
            // OK: check how many bytes read
        }
    }
}

// =============================================================================
// SECTION 9: Check eof() in loop
// =============================================================================

// Variant 9.1: eof check after read
void test_eof_check() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        char c;
        while (file.get(c)) {
            // OK: get returns stream, checked in condition
        }
        if (file.eof()) {
            // OK: reached end of file
        }
    }
}

// =============================================================================
// SECTION 10: peek() with guard
// =============================================================================

// Variant 10.1: peek after open check
void test_peek() {
    std::ifstream file("test.txt");
    if (file.is_open() && file.peek() != EOF) {
        char c;
        file.get(c);  // OK: peeked first
    }
}

// =============================================================================
// SECTION 11: close() doesn't require guard
// =============================================================================

// Variant 11.1: close is always safe
void test_close_safe() {
    std::ifstream file("test.txt");
    file.close();  // OK: close on any stream is safe
}

// Variant 11.2: close after is_open check
void test_close_after_check() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        file.close();  // OK
    }
}

int main() {
    return 0;
}
