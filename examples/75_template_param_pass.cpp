// Example 75: Template parameter extraction — should COMPILE
// DTT extracts non-type template parameters as _tparam0, _tparam1, etc.
// Also applies semantic aliases for known std:: types

#include <cstddef>
#include <array>

// User-defined template with non-type parameters
template<typename T, size_t Rows, size_t Cols>
struct Matrix {
    T data[Rows * Cols];

    // Use _tparam1 (Rows) and _tparam2 (Cols) directly
    [[clang::annotate("dtt:requires=arg0 < _tparam1"),
      clang::annotate("dtt:requires=arg1 < _tparam2")]]
    T& at(size_t row, size_t col) { return data[row * Cols + col]; }
};

// Alternatively, expose as named properties
template<typename T, size_t N>
struct FixedBuffer {
    T data[N];
    static constexpr size_t _size = N;  // Workaround if _tparam1 is unclear

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    T& operator[](size_t i) { return data[i]; }
};

void test_matrix() {
    Matrix<int, 3, 4> m;  // _tparam1 = 3, _tparam2 = 4

    m.at(0, 0);  // OK: 0 < 3, 0 < 4
    m.at(2, 3);  // OK: 2 < 3, 3 < 4
}

void test_std_array() {
    std::array<int, 10> arr;  // _size = 10 (via alias), _tparam1 = 10

    // std::array doesn't have DTT annotations, but _size is set
    // This test verifies the extraction works
}

void test_fixed_buffer() {
    FixedBuffer<int, 5> buf;  // _tparam1 = 5, _size = 5

    buf[0];  // OK: 0 < 5
    buf[4];  // OK: 4 < 5
}

int main() {
    test_matrix();
    test_std_array();
    test_fixed_buffer();
    return 0;
}
