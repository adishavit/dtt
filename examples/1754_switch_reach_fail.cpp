// 1754: switch and reachability - a bug on one path is still found
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>

int a[10];

// Variant 1: freed in one case, used after the switch.
int variant1_freed_in_one_case(int c) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 0;
    *p = 1;
    switch (c) {
    case 1:
        free(p);
        break;
    default:
        break;
    }
    return *p;  // dtt-error {{freed on some paths|use-after-free}}
}

// Variant 2: one case closes the file; the others do not.
void variant2_closed_in_one_case(int c) {
    FILE *f = fopen("a", "r");  // dtt-error {{scope-exit|leak}}
    if (!f) return;
    switch (c) {
    case 0:
        fclose(f);
        break;
    case 1:
        break;
    }
}

// Variant 3: falling through frees twice.
void variant3_fallthrough_double_free(int c) {
    int *p = (int *)malloc(sizeof(int));
    switch (c) {
    case 0:
        free(p);
        [[fallthrough]];
    case 1:
        free(p);  // dtt-error {{double-free|freed on some paths}}
        break;
    default:
        free(p);
    }
}

// Variant 4: inside `case 12`, k is 12.
int variant4_case_value(int k) {
    switch (k) {
    case 12:
        return a[k];  // dtt-error {{out of bounds}}
    default:
        return 0;
    }
}

// Variant 5: a loop left by break after a free (audit S6).
void variant5_break_after_free(int n, int c) {
    int *p = (int *)malloc(4 * sizeof(int));
    if (!p) return;
    for (int i = 0; i < n; i++) {
        if (c) {
            free(p);
            break;
        }
    }
    p[0] = 1;  // dtt-error {{freed on some paths|use-after-free}}
}

// Variant 6: the case that does not exit reaches the use.
int variant6_one_case_falls_out(int c) {
    int *p = nullptr;
    switch (c) {
    case 0:
        return 0;
    default:
        break;
    }
    return *p;  // dtt-error {{null}}
}

// Variant 7: an assert's failing branch does not end the function.
void variant7_after_assert(int c) {
    assert(c);
    int *p = nullptr;
    *p = 3;  // dtt-error {{null}}
}

// Variant 8: a range-for loop does not end the function.
void variant8_after_range_for(const std::vector<int> &v) {
    for (int x : v) (void)x;
    int *p = nullptr;
    *p = 1;  // dtt-error {{null}}
}

// Variant 9: the code after a goto label is checked.
int variant9_goto_cleanup(int c) {
    int *q = nullptr;
    if (c) goto out;
    return 0;
out:
    (void)c;
    *q = 2;  // dtt-error {{null}}
    return -1;
}

// Variant 10: a case reached on a later iteration is checked.
void variant10_state_machine(int n) {
    int st = 0;
    while (n--) {
        switch (st) {
        case 0:
            st = 1;
            break;
        case 1: {
            int *p = nullptr;
            *p = 7;  // dtt-error {{null}}
        }
        }
    }
}

// Variant 11: an unsigned condition matches its case.
void variant11_unsigned_case() {
    unsigned u = -1;
    switch (u) {
    case 0xFFFFFFFFu: {
        int *p = nullptr;
        *p = 1;  // dtt-error {{null}}
    }
    }
}

// Variant 12: throwing out of the function leaks the file.
void variant12_throw_leaks() {
    FILE *f = fopen("a", "r");
    if (!f) return;
    throw 1;  // dtt-error {{scope-exit|leak}}
}

// Variant 13: after a loop left only when its condition fails, p is null.
void variant13_loop_exit_condition(int *p, int c) {
    while (p) {
        if (c) continue;
        return;
    }
    *p = 1;  // dtt-error {{null}}
}

// Variant 14: the handler is reached from the throwing branch.
void variant14_handler_from_throw(int c) {
    int v = 0;
    int *p = &v;
    try {
        if (c) {
            p = nullptr;
            throw 1;
        }
    } catch (...) {
        *p = 6;  // dtt-error {{null}}
    }
}

// Variant 15: the body of a do loop runs before the loop exits.
void variant15_do_runs_once() {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return;
    do {
        free(p);
        break;
    } while (0);
    *p = 5;  // dtt-error {{use-after-free}}
}

// Variant 16: a constructor after a function that returns is analyzed.
int returns_one() { return 1; }
struct Holder {
    Holder() {
        int *p = nullptr;
        *p = 1;  // dtt-error {{null}}
    }
    ~Holder() {
        int *p = nullptr;
        *p = 2;  // dtt-error {{null}}
    }
};

// Variant 17: a local class does not end the enclosing function.
void variant17_local_class() {
    struct Local {
        static int get() { return 1; }
    };
    int *p = nullptr;
    *p = Local::get();  // dtt-error {{null}}
}

// Variant 18: continue in a do loop reaches its condition and the exit.
int variant18_do_continue(int c, int n) {
    do {
        if (c) continue;
        return 1;
    } while (n--);
    int *p = nullptr;
    *p = 3;  // dtt-error {{null}}
    return 0;
}

// Variant 19: a constructor in a try block may throw.
void variant19_constructor_throws() {
    try {
        std::vector<int> v(10);
    } catch (...) {
        int *z = nullptr;
        *z = 1;  // dtt-error {{null}}
    }
}

void report();
void may_throw();

// Variant 20: a call inside an if condition may throw after an earlier
// call in the same condition closed the file.
void variant20_throw_in_condition(FILE *fp) {
    if (!fp) return;
    try {
        if (fclose(fp) != 0) report();
    } catch (...) {
        fclose(fp);  // dtt-error {{closed|_nonnull}}
    }
}

// Variant 21: the condition of a do loop runs after continue.
void variant21_do_condition_after_continue(int c) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return;
    do {
        if (c) continue;
        break;
    } while (free(p), 0);
    free(p);  // dtt-error {{double-free|freed on some paths}}
}

// Variant 22: a function declared inside a function does not end it.
void variant22_local_declaration(int c) {
    FILE *fp = fopen("a", "r");
    if (!fp) return;
    int helper(int);
    if (c) return;  // dtt-error {{scope-exit|leak}}
    fclose(fp);
}

// Variant 23: a local class keeps the enclosing function's inferred contract.
void fill_with_local_class(char *buf, size_t n) {
    memset(buf, 0, n);
    struct Local {
        static int get() { return 1; }
    };
}
void variant23_caller() {
    char b[10];
    fill_with_local_class(b, 20);  // dtt-error {{fill_with_local_class|precondition}}
}

// Variant 24: a null check whose failing branch aborts proves nothing about
// a pointer that merely has a matching name.
int variant24_name_pairing(char *buf, int buf_len) {
    if (buf_len < 1) abort();
    return *buf;  // dtt-error {{null}}
}

int main() { return 0; }
