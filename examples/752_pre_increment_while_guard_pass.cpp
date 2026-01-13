// Tests: Pre-increment in while condition should establish bounds proof
// Issue #52: while (++i <= n) should prove i >= 1 in loop body
// Gap: DTT warns about arr[i-1] being out of bounds even though i >= 1

constexpr int SIZE = 10;
int arr[SIZE];

// Variant 1: Basic pre-increment while pattern (from SumatraPDF GlobalPrefs.cpp)
// After ++i executes, i >= 1, so i-1 >= 0
void variant1_basic_pre_increment() {
    int i = 0;
    while (++i <= SIZE) {
        // After ++i, i is at least 1, so i-1 is at least 0
        // DTT should NOT warn about negative index
        arr[i - 1] = i;  // gap: may warn about index -1
    }
}

// Variant 2: Pre-increment with < comparison
void variant2_pre_increment_less_than() {
    int i = 0;
    while (++i < SIZE) {
        // After ++i, i >= 1 and i < SIZE
        // So i-1 >= 0 and i-1 < SIZE-1
        arr[i - 1] = i;  // gap: may warn about bounds
    }
}

// Variant 3: Pre-increment with less-than-equal (different bound)
void variant3_pre_increment_le_smaller() {
    int i = 0;
    while (++i <= SIZE - 1) {
        // After ++i, i >= 1 and i <= SIZE-1 (9)
        arr[i - 1] = i;  // i-1 ranges from 0 to 8, valid
    }
}

// Variant 4: Pre-increment in nested while
void variant4_nested_pre_increment() {
    int i = 0;
    int j = 0;
    while (++i <= SIZE) {
        j = 0;
        while (++j < SIZE) {
            arr[j - 1] = i + j;  // j-1 is valid (0 to 8)
        }
    }
}

int main() {
    variant1_basic_pre_increment();
    variant2_pre_increment_less_than();
    variant3_pre_increment_le_smaller();
    variant4_nested_pre_increment();
    return 0;
}
