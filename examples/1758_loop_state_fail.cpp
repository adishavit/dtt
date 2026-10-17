// 1758: Loops - the state after a loop covers every iteration count
//
// DTT traversed a loop body once and carried its effects forward as if the
// loop ran exactly once: no zero-iteration path, no second iteration, and
// ++/-- and op= kept the old value as a fact.
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
bool c();
int a[4];

// Variant 1: the loop may not run, leaving v empty.
int variant1_zero_iterations() {
    std::vector<int> v;
    while (c()) v.push_back(1);
    return v[0];  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 2: n is 5 or -1 after the loop.
void variant2_value_after_loop() {
    int n = 5;
    while (c()) n = -1;
    take(n);  // dtt-error {{cannot verify}}
}

// Variant 3: the second iteration sees the write of the first.
void variant3_second_iteration() {
    int n = 5;
    for (int i = 0; i < 10; ++i) {
        take(n);  // dtt-error {{cannot verify}}
        n = -1;
    }
}

// Variant 4: the second iteration uses the pointer the first one freed.
void variant4_freed_in_loop(int n) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return;
    for (int i = 0; i < n; i++) {
        *p = i;  // dtt-error {{freed on some paths|use-after-free}}
        free(p);
    }
}

// Variant 5: counting down from 4 indexes a[4].
int variant5_count_down_overrun() {
    int s = 0;
    for (int k = 4; k >= 0; --k) s += a[k];  // dtt-error {{cannot verify|out of bounds}}
    return s;
}

// Variant 6: the body overwrites the loop variable.
int variant6_body_writes_loop_variable() {
    int s = 0;
    for (int k = 0; k < 4; ++k) {
        k = 7;
        s += a[k];  // dtt-error {{out of bounds}}
    }
    return s;
}

// Variant 7: ++ changes the value.
int variant7_increment() {
    int i = 3;
    ++i;
    return a[i];  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 8: a continue path that nulls p reaches the exit.
int variant8_continue_path(int *p) {
    while (p) {
        if (c()) {
            p = nullptr;
            continue;
        }
        break;
    }
    return *p;  // dtt-error {{null}}
}

// Variant 9: a file opened on one iteration and not closed.
void variant9_leak_in_loop(int n) {
    for (int i = 0; i < n; i++) {
        FILE *f = fopen("a", "r");  // dtt-error {{scope-exit|leak}}
        if (!f) continue;
        if (c()) fclose(f);
    }
}

// Variant 10: a variable bound one past the array.
int variant10_variable_bound_too_large() {
    int s = 0;
    int n = 11;
    for (int i = 0; i < n; i++) s += a[i];  // dtt-error {{cannot verify|out of bounds}}
    return s;
}

// Variant 11: -1 converted to unsigned bounds nothing.
int variant11_converted_bound(unsigned i) {
    int n = -1;
    if (i < n) return a[i];  // dtt-error {{cannot verify|out of bounds}}
    return 0;
}

// Variant 12: pointer iteration through the end pointer.
int variant12_pointer_past_end() {
    int arr[10] = {};
    int s = 0;
    int *end = arr + 10;
    for (int *p = arr; p <= end; p++) s += *p;  // dtt-error {{cannot verify|out of bounds}}
    return s;
}

// Variant 13: a pointer the body also moves.
int variant13_pointer_moved_in_body() {
    int arr[10] = {};
    int s = 0;
    for (int *p = arr; p < arr + 10; p++) {
        s += *p;
        p++;
        s += *p;  // dtt-error {{cannot verify|out of bounds}}
    }
    return s;
}

// Variant 14: -1 cast to unsigned bounds nothing, through a field too.
struct Neg { int n; };
int variant14_cast_field_bound(unsigned i) {
    Neg k;
    k.n = -1;
    if (i < static_cast<unsigned>(k.n)) return a[i];  // dtt-error {{cannot verify|out of bounds}}
    return 0;
}

// Variant 15: an open descriptor overwritten without close.
void variant15_fd_overwritten(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        fd = -1;  // dtt-error {{overwriting|leak|scope-exit}}
    }
}

// Variant 16: a value that takes more passes to reach the access than the
// others take to settle.
int arr1[1];
void variant16_long_chain() {
    int x0 = 0, x1 = 0, x2 = 0, x3 = 0, x4 = 0, x5 = 0, x6 = 0, x7 = 0, x8 = 0, x9 = 0;
    while (c()) {
        arr1[x0] = 0;  // dtt-error {{cannot verify|out of bounds}}
        x0 = x1; x1 = x2; x2 = x3; x3 = x4; x4 = x5;
        x5 = x6; x6 = x7; x7 = x8; x8 = x9; x9 = 1;
    }
}

// Variant 17: the body changes what the loop bound is computed from.
int arr9[9];
void variant17_bound_operand_written() {
    int n = 10;
    for (int i = 0; i < n - 1; i++) {
        arr9[i] = 0;  // dtt-error {{cannot verify|out of bounds}}
        n = 100;
    }
}

// Variant 18: a copy keeps the old value when the original moves.
int arr10[10];
void variant18_copy_then_decrement(int i) {
    if (i < 11 && i >= 0) {
        int j = i;
        i--;
        arr10[j] = 0;  // dtt-error {{cannot verify|out of bounds}}
    }
}

// Variant 19: a value set from an inferred size() and changed on one path.
struct Sized {
    int _size;
    int size() const { return _size; }
};
void variant19_size_value_joined(bool b) {
    Sized v;
    v._size = 9;
    int s = v.size();
    if (b) s = 100;
    arr10[s] = 0;  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 20: a value set from an inferred size() and then changed.
void variant20_size_value_changed() {
    Sized v;
    v._size = 9;
    int s = v.size();
    s++;
    arr10[s] = 0;  // dtt-error {{cannot verify|out of bounds}}
    int t = v.size();
    t *= 5;
    arr10[t] = 0;  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 21: a member operator writes the loop variable.
struct In { In &operator>>(int &x); };
void variant21_member_operator_write(In &in) {
    for (int i = 0; i != 10; i++) {
        arr10[i] = 0;  // dtt-error {{cannot verify|out of bounds}}
        in >> i;
    }
}

// Variant 22: an end pointer into another array.
void variant22_other_array_end() {
    int x[10], y[10];
    for (int *p = x; p < y + 10; ++p) *p = 0;  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 23: a pre-incremented counter that starts negative.
void variant23_preincrement_from_negative() {
    int i = -5;
    while (++i <= 3) take(i);  // dtt-error {{cannot verify|arg0 >= 0}}
}

// Variant 24: a pointer stepped twice on some iterations.
void variant24_conditional_double_step() {
    int arr[10];
    int *p = arr;
    for (int i = 0; i < 10; i++) {
        *p = 0;  // dtt-error {{cannot verify|out of bounds}}
        if (c()) {
            p++;
            p++;
        }
    }
}

// Variant 25: a goto into the body skips the loop's initialization.
void variant25_goto_into_body() {
    int i = 100, n = 0;
    if (c()) goto in;
    for (i = 3; i >= 0; --i) {
        a[i] = 0;  // dtt-error {{cannot verify|out of bounds}}
    in:
        n++;
    }
}

// Variant 26: the body moves the end pointer.
void variant26_end_moved() {
    char buf[10];
    char *end = buf + 5;
    for (char *p = buf; p < end; ++p) {
        *p = 0;  // dtt-error {{cannot verify|out of bounds}}
        ++end;
    }
}

// Variant 27: the body changes the variable bound.
int a100[100];
void variant27_bound_written(int n) {
    for (int i = 0; i < n; ++i) {
        n = 100;
        a100[i] = 0;  // dtt-error {{cannot verify|out of bounds}}
    }
}

// Variant 28: a file opened again on a later pass and lost on a return.
int variant28_reacquired_and_lost() {
    for (int k = 0; k < 3; ++k) {
        FILE *f = fopen("a", "r");
        if (!f) return -1;
        if (c()) return 1;  // dtt-error {{scope-exit|leak}}
        fclose(f);
    }
    return 0;
}

// Variant 29: a goto out of an inner loop back to a label.
[[clang::annotate("dtt:requires=n >= 0")]] void take2(int n);
void variant29_goto_from_inner_loop(int n) {
    int x = 5;
again:
    take2(x);  // dtt-error {{cannot verify|arg0 >= 0}}
    for (int i = 0; i < n; ++i) {
        if (c()) {
            x = -1;
            goto again;
        }
    }
}

// Variant 30: an unchecked handle overwritten.
void variant30_unchecked_overwritten() {
    FILE *f = fopen("a", "r");
    f = nullptr;  // dtt-error {{overwriting|leak}}
    int fd = open("a", O_RDONLY);
    fd = -1;  // dtt-error {{overwriting|leak|never}}
}

// Variant 31: decreasing loops that wrap or step the wrong way.
int variant31_decreasing_shapes() {
    int s = 0, j = 0;
    for (unsigned i = 3; i >= 0; --i) s += a[i];  // dtt-error {{cannot verify|out of bounds}}
    for (unsigned i = 2; i > 0; i -= 3) s += a[i];  // dtt-error {{cannot verify|out of bounds}}
    for (int i = 3; i >= 0; i -= -1) s += a[i];  // dtt-error {{cannot verify|out of bounds}}
    for (int i = 3; i >= 0; i++, j++) s += a[i];  // dtt-error {{cannot verify|out of bounds}}
    return s;
}

// Variant 32: a loop whose increment does not step its variable.
int variant32_increment_steps_other(int n) {
    int s = 0, j = 0;
    int *q = a;
    for (int i = 0; i < 1; ++j) {
        if (j > 10) break;
        s += *q;  // dtt-error {{cannot verify|out of bounds}}
        ++q;
    }
    for (int i = 0; i < 4; i += -1) take(i);  // dtt-error {{cannot verify|arg0 >= 0}}
    return s;
}

// Variant 33: a member moved after its guard.
struct Holder { int n; };
int variant33_member_incremented(Holder h) {
    if (h.n >= 0 && h.n < 4) {
        h.n++;
        return a[h.n];  // dtt-error {{cannot verify|out of bounds}}
    }
    return 0;
}

// Variant 34: a copy taken before a call resets the original.
void reset(int &i);
int variant34_copy_then_reset(int i) {
    int j = i;
    reset(i);
    if (i < 0 || i >= 4) return 0;
    return a[j];  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 35: a handle re-acquired in a loop before the first is released.
void variant35_reacquired_unreleased(int n) {
    FILE *f = fopen("a", "r");
    for (int i = 0; i < n; ++i) {
        f = fopen("b", "r");  // dtt-error {{overwriting|leak}}
    }
    if (f) fclose(f);
}

// Variant 36: a copy of a member keeps the member's old value.
struct Box { int x; };
int arr10b[10];
int variant36_member_copy(Box s) {
    int j = s.x;
    s.x -= 100;
    if (s.x >= 0 && s.x < 10) return arr10b[j];  // dtt-error {{cannot verify|out of bounds}}
    return 0;
}

// Variant 37: an unsigned bound that wraps, and a negative bound converted.
struct Buf16 { size_t len; int data[16]; };
int variant37_wrapping_bounds(Buf16 &b, int n) {
    int s = 0;
    if (b.len > 16) return 0;
    for (size_t i = 0; i < b.len - 1; ++i) s += b.data[i];  // dtt-error {{cannot verify|out of bounds}}
    if (n > 10) return 0;
    for (unsigned i = 0; i < n; ++i) s += arr10b[i];  // dtt-error {{cannot verify|out of bounds}}
    return s;
}

// Variant 39: a handle overwritten with a call that acquires nothing.
int other();
void variant39_overwritten_by_call(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    fd = other();  // dtt-error {{overwriting|leak}}
    if (fd >= 0) close(fd);
}

int main() { return 0; }
