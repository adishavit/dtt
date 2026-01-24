// 965_matrix_multiply_fail.cpp
// Tests: Matrix dimension tracking - incompatible shapes caught
// From BLOG.md: Matrix Dimensions example

template<typename T>
struct Matrix {
    [[clang::annotate("dtt:ensures=_rows == arg0")]]
    [[clang::annotate("dtt:ensures=_cols == arg1")]]
    Matrix(int rows, int cols) {}
};

[[clang::annotate("dtt:requires=arg0._cols == arg1._rows")]]
template<typename T>
Matrix<T> multiply(const Matrix<T>& a, const Matrix<T>& b) {
    return Matrix<T>(3, 4);
}

void test_incompatible_multiply() {
    Matrix<float> A(3, 4);  // 3 rows, 4 cols
    Matrix<float> C(2, 4);  // 2 rows, 4 cols
    auto D = multiply(A, C); // ERROR: A.cols(4) != C.rows(2)
}

int main() {
    test_incompatible_multiply();
    return 0;
}
