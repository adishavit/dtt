// 964_matrix_multiply_pass.cpp
// Tests: Matrix dimension tracking - can only multiply compatible shapes
// From BLOG.md: Matrix Dimensions example

template<typename T>
struct Matrix {
    [[clang::annotate("dtt:ensures=_rows == arg0")]]
    [[clang::annotate("dtt:ensures=_cols == arg1")]]
    Matrix(int rows, int cols) {}

    [[clang::annotate("dtt:requires=arg0 < _rows")]]
    [[clang::annotate("dtt:requires=arg1 < _cols")]]
    T& at(int row, int col) { static T t; return t; }
};

// C = A * B requires A.cols == B.rows
// Result: C.rows = A.rows, C.cols = B.cols
template<typename T>
[[clang::annotate("dtt:requires=arg0._cols == arg1._rows")]]
Matrix<T> multiply(const Matrix<T>& a, const Matrix<T>& b) {
    return Matrix<T>(3, 5); // placeholder
}

// Variant 1: Compatible dimensions (3x4 * 4x5 = 3x5)
void test_compatible_multiply() {
    Matrix<float> A(3, 4);  // 3 rows, 4 cols
    Matrix<float> B(4, 5);  // 4 rows, 5 cols
    auto C = multiply(A, B); // OK: A.cols(4) == B.rows(4)
}

// Variant 2: Square matrices
void test_square_multiply() {
    Matrix<float> A(4, 4);
    Matrix<float> B(4, 4);
    auto C = multiply(A, B); // OK: 4 == 4
}

// Variant 3: Valid element access
void test_element_access() {
    Matrix<int> M(3, 4);
    M.at(0, 0);   // OK: 0 < 3, 0 < 4
    M.at(2, 3);   // OK: 2 < 3, 3 < 4
}

int main() {
    test_compatible_multiply();
    test_square_multiply();
    test_element_access();
    return 0;
}
