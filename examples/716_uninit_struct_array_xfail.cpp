// 716_uninit_struct_array_xfail.cpp
// GAP: DTT should detect uninitialized struct members in arrays
// Requires per-element struct member tracking

struct Point { int x; int y; };

void sink_int(int v);

// Array of structs - elements not tracked for uninitialized state
void variant1_array_member() {
    Point pts[2];         // All members indeterminate
    sink_int(pts[0].x);   // GAP: should error - pts[0].x uninitialized
}

// After partial init - other elements still uninit
void variant2_partial_array_init() {
    Point pts[2];
    pts[0].x = 1;
    pts[0].y = 2;
    sink_int(pts[1].x);   // GAP: should error - pts[1].x still uninit
}

int main() {
    return 0;
}
