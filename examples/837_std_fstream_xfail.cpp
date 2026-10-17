// 837: Remaining fstream gaps
// These patterns involve operator<< / operator>> (stream insertion/extraction)
// which DTT does not yet track. getline after close() is caught:
// 837b_std_fstream_after_close_fail.cpp.

#include <fstream>
#include <string>

// Variant 1.2: operator<< without is_open check
// DTT does not handle operator<< / operator>> for streams
void test_write_no_guard() {
    std::ofstream file("/invalid/path/file.txt");
    file << "hello";  // ERROR: file might not be open
}

// Variant 3.2: operator<< after close
void test_write_after_close() {
    std::ofstream file("test.txt");
    if (file.is_open()) {
        file.close();
        file << "hello";  // ERROR: file was closed
    }
}

// Variant 4.2: operator<< on default-constructed stream
void test_default_constructed_write() {
    std::ofstream file;
    file << "hello";  // ERROR: never opened
}

int main() {
    return 0;
}
