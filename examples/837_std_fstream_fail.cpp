// 837: Unsafe std::fstream usage that DTT catches
// Tests: getline without is_open() guard, after close(), on default-constructed,
// after move, stale check, wrong file, binary I/O, seek/tell, and bad() misuse.

#include <fstream>
#include <string>

// =============================================================================
// SECTION 1: I/O without is_open() guard
// =============================================================================

// Variant 1.1: read without checking is_open
void test_read_no_guard() {
    std::ifstream file("nonexistent.txt");
    std::string line;
    std::getline(file, line);  // ERROR: file might not be open
}

// Variant 1.3: read from parameter file
void test_read_param(std::ifstream& file) {
    std::string line;
    std::getline(file, line);  // ERROR: can't prove file is open
}

// =============================================================================
// SECTION 2: I/O in wrong branch
// =============================================================================

// Variant 2.1: read in else of is_open
void test_wrong_branch() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        // OK here
    } else {
        std::string line;
        std::getline(file, line);  // ERROR: not open in else branch
    }
}

// Variant 2.2: read when fail() is true
void test_fail_branch() {
    std::ifstream file("test.txt");
    if (file.fail()) {
        std::string line;
        std::getline(file, line);  // ERROR: stream has failed
    }
}

// =============================================================================
// SECTION 4: I/O on default-constructed stream
// =============================================================================

// Variant 4.1: read from default-constructed ifstream
void test_default_constructed_read() {
    std::ifstream file;
    std::string line;
    std::getline(file, line);  // ERROR: never opened
}

// =============================================================================
// SECTION 5: I/O after move
// =============================================================================

// Variant 5.1: read from moved-from ifstream
void test_moved_from() {
    std::ifstream file1("test.txt");
    std::ifstream file2 = std::move(file1);
    std::string line;
    std::getline(file1, line);  // ERROR: moved-from state
}

// =============================================================================
// SECTION 6: Stale is_open check
// =============================================================================

// Variant 6.1: check, close, then use
void test_stale_is_open() {
    std::ifstream file("test.txt");
    bool was_open = file.is_open();
    file.close();
    if (was_open) {
        std::string line;
        std::getline(file, line);  // ERROR: stale check, file closed
    }
}

// =============================================================================
// SECTION 7: Binary I/O without guard
// =============================================================================

// Variant 7.1: read() without is_open check
void test_binary_read_no_guard() {
    std::ifstream file("test.bin", std::ios::binary);
    char buf[100];
    file.read(buf, sizeof(buf));  // ERROR: might not be open
}

// Variant 7.2: write() without is_open check
void test_binary_write_no_guard() {
    std::ofstream file("test.bin", std::ios::binary);
    char buf[100] = {};
    file.write(buf, sizeof(buf));  // ERROR: might not be open
}

// =============================================================================
// SECTION 8: tellg/seekg without guard
// =============================================================================

// Variant 8.1: tellg on potentially closed stream
void test_tellg_no_guard() {
    std::ifstream file("test.txt");
    auto pos = file.tellg();  // ERROR: might not be open
    (void)pos;
}

// Variant 8.2: seekg without guard
void test_seekg_no_guard() {
    std::ifstream file("test.txt");
    file.seekg(0, std::ios::end);  // ERROR: might not be open
}

// =============================================================================
// SECTION 9: Wrong file checked
// =============================================================================

// Variant 9.1: check file1, read from file2
void test_wrong_file_checked() {
    std::ifstream file1("test1.txt");
    std::ifstream file2("test2.txt");
    if (file1.is_open()) {
        std::string line;
        std::getline(file2, line);  // ERROR: checked file1, not file2
    }
}

// =============================================================================
// SECTION 10: Using bad() incorrectly
// =============================================================================

// Variant 10.1: !bad() doesn't mean open
void test_not_bad_not_open() {
    std::ifstream file;  // Never opened
    if (!file.bad()) {
        std::string line;
        std::getline(file, line);  // ERROR: !bad doesn't mean open
    }
}

int main() {
    return 0;
}
