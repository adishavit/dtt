// 837b: fstream - a stream used after close()
#include <fstream>
#include <string>

// Variant 1: getline after close. is_open() proves @is_open == 1; close()
// writes @is_open = 0, which replaces that proof.
void test_read_after_close() {
    std::ifstream file("test.txt");
    if (file.is_open()) {
        file.close();
        std::string line;
        std::getline(file, line);  // dtt-error {{is_open}}
    }
}

int main() { return 0; }
