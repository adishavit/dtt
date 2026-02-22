// 1704: Remaining false positive — parameter-bounded array access
// DTT can't prove `count <= 16` from call-site analysis, so flags
// `mb->coeffs[i]` where `i < count` as potentially OOB.
// See 1704_unchecked_param_pass.cpp for null-deferral patterns that work.

#include <stdint.h>

struct MBInfo {
    int x_offset, y_offset;
    int coeffs[16];
};

// Parameter `count` controls loop bound but DTT can't verify count <= 16
static int sum_coeffs(const MBInfo* mb, int count) {
    int sum = 0;
    for (int i = 0; i < count; i++)
        sum += mb->coeffs[i];  // DTT FP: can't prove count <= 16
    return sum;
}

int main() {
    MBInfo a = {1, 2, {0}};
    return sum_coeffs(&a, 16);
}
