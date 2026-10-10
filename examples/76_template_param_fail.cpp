// Example 76: Template parameter OOB — should FAIL
// Accessing beyond template-specified bounds should be caught

#include <cstddef>

// User-defined template with non-type parameters
template<typename T, size_t Rows, size_t Cols>
struct Matrix {
    T data[Rows * Cols];

    [[clang::annotate("dtt:requires=arg0 < _tparam1"),
      clang::annotate("dtt:requires=arg1 < _tparam2")]]
    T& at(size_t row, size_t col) { return data[row * Cols + col]; }
};

void test_matrix_oob() {
    Matrix<int, 3, 4> m;  // _tparam1 = 3, _tparam2 = 4

    m.at(5, 0);  // ERROR: 5 >= 3 (row out of bounds)  dtt-error {{out of bounds}}
}

int main() {
    test_matrix_oob();
    return 0;
}
