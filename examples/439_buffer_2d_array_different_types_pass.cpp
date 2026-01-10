// Example 439: 2D array rows with different element types — should COMPILE
// Tests that row tracking works for various element types

void test_char_2d() {
    char text[3][80];      // 3 lines of 80 chars
    char* line = text[1];  // _size=80
    line[79] = '\0';       // OK: last char
}

void test_double_2d() {
    double matrix[4][4];   // 4x4 matrix
    double* row = matrix[2]; // _size=4
    row[3] = 1.0;          // OK: last element
}

struct Point { int x, y; };

void test_struct_2d() {
    Point points[10][5];   // 10 rows of 5 points
    Point* row = points[3]; // _size=5
    row[4].x = 100;        // OK: last point in row
}

int main() {
    test_char_2d();
    test_double_2d();
    test_struct_2d();
    return 0;
}
