// Example 93: std::array of std::array — should COMPILE
// Tests nested template parameter extraction

#include <array>

void test() {
    std::array<std::array<int, 4>, 3> matrix;
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix[i][j] = i * 4 + j;  // OK
        }
    }
}

int main() {
    test();
    return 0;
}
