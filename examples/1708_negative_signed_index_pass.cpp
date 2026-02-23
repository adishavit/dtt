// 1708: Negative signed index — additional safe patterns
// Signed array indices with proven upper bound are assumed non-negative by
// convention. DTT suppresses the lower-bound check when Z3 confirms the
// upper bound is within array size.

#include <stdint.h>

// Variant 1: upper-bound checked but no explicit lower-bound check
// (libwebp DistanceToPlaneCode pattern — distance is always >= 1 by domain)
#define DISTANCE_TABLE_SIZE 120

static const int kDistanceTable[DISTANCE_TABLE_SIZE] = {0};

int variant1_upper_bound_only(int distance) {
    if (distance < DISTANCE_TABLE_SIZE)
        return kDistanceTable[distance];  // DTT FP: distance could be negative
    return -1;
}

// Variant 2: return value of strlen-like function (always >= 0)
// DTT can't infer non-negativity across function boundaries
static int my_strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

int variant2_strlen_result() {
    const char* msg = "hello";
    int len = my_strlen(msg);
    char buf[10] = {0};
    if (len < 10)
        return buf[len];  // DTT FP: len could be negative
    return 0;
}

int main() {
    return variant1_upper_bound_only(50)
         + variant2_strlen_result();
}
