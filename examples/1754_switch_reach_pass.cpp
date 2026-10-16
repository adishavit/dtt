// 1754: switch and reachability - each path is tracked on its own
//
// Each function below is safe. DTT used to run switch cases in sequence, as if
// every case executed, and treated only a block's last statement as an exit
// (not abort(), exit(), throw or nested exits).
#include <cstdio>
#include <cstdlib>

int g;

// Variant 1: each path frees the pointer once (audit S4).
void variant1_free_per_case(int c) {
    int *p = (int *)malloc(sizeof(int));
    switch (c) {
    case 1:
        free(p);
        break;
    default:
        free(p);
    }
}

// Variant 2: a case that frees and returns does not reach the code after the
// switch (zlib gz_open).
int variant2_case_frees_and_returns(const char *mode) {
    if (!mode) return 0;
    int *state = (int *)malloc(sizeof(int));
    if (!state) return 0;
    while (*mode) {
        switch (*mode) {
        case '+':
            free(state);
            return 0;
        case 'r':
            *state = 1;
            break;
        default:
            break;
        }
        mode++;
    }
    int r = *state;
    free(state);
    return r;
}

// Variant 3: abort() does not return.
int variant3_abort(int *p) {
    if (!p) abort();
    return *p;
}

// Variant 4: exit() does not return.
int variant4_exit(int *p) {
    if (!p) {
        g = 1;
        exit(1);
    }
    return *p;
}

// Variant 5: a branch that throws does not reach the code after the if.
int variant5_throw(int *p) {
    if (!p) throw 1;
    return *p;
}

// Variant 6: both branches of a nested if return.
int variant6_nested_exit(int *p, int c) {
    if (!p) {
        if (c) return 1;
        else return 2;
    }
    return *p;
}

// Variant 7: break inside a switch inside a loop leaves the switch only.
int variant7_break_leaves_switch(int n) {
    int *p = &g;
    for (int i = 0; i < n; i++) {
        switch (i) {
        case 0:
            break;
        default:
            break;
        }
        g = *p;
    }
    return *p;
}

// Variant 8: code after a return is not reached.
int variant8_dead_code() {
    int *p = nullptr;
    return 0;
    return *p;
}

// Variant 9: the for loop's initialization runs before the loop can exit.
int variant9_for_init_runs(int n) {
    int x;
    for (x = 0; x < n;) {
        return x;
    }
    return x;
}

// Variant 10: a call that cannot throw does not reach the handler.
void cannot_throw() noexcept;
int variant10_nothrow_call_in_try() {
    int *p = nullptr;
    try {
        cannot_throw();
        p = &g;
    } catch (...) {
        return *p;
    }
    return *p;
}

// Variant 11: the loop condition's side effect happens before the exit.
int variant11_condition_side_effect(FILE *f) {
    if (!f) return 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        return c;
    }
    return c;
}

int pick();
int more();

// Variant 12: the for loop's initialization happens before the exit.
int variant12_for_init(void) {
    int i;
    for (i = pick(); more();) {
        return 1;
    }
    return i;
}

// Variant 13: a file opened in a loop and kept by break is closed later.
void variant13_kept_by_break(int n, int x) {
    FILE *f = nullptr;
    while (n-- > 0) {
        f = fopen("a", "r");
        if (f) break;
    }
    if (x) {
        g = 1;
    }
    if (f) fclose(f);
}

// Variant 14: a function declared inside a function does not end it.
void variant14_local_declaration() {
    FILE *fp = fopen("a", "r");
    if (!fp) return;
    int helper(int);
    fclose(fp);
}

int main() { return 0; }
