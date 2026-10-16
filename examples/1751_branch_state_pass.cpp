// 1751: Branch state - every branch starts from the state before the `if`
//
// Each function below is safe. DTT used to save and restore only some of its
// state around some kinds of `if`, so facts from one branch leaked into the
// other branch or past the `if`, giving proven errors on safe code.
#include <cstdio>
#include <any>
#include <cstdlib>
#include <variant>
#include <vector>

int g;

// Variant 1: a resource closed in an early-return branch is still open after.
int variant1_bool_call_early_return(std::vector<int> &v) {
    FILE *f = fopen("a", "r");
    if (!f) return -1;
    if (v.empty()) {
        fclose(f);
        return 0;
    }
    int c = fgetc(f);
    fclose(f);
    return c;
}

// Variant 2: a pointer nulled in an early-return branch is not null after.
int variant2_holds_alternative(std::variant<int, float> &v) {
    int *p = &g;
    if (std::holds_alternative<int>(v)) {
        p = nullptr;
        return 0;
    }
    return *p;
}

// Variant 3: a pointer nulled in the then branch is not null in the else branch.
int variant3_bool_call_else(std::vector<int> &v) {
    int *p = &g;
    if (v.empty()) {
        p = nullptr;
        return 0;
    } else {
        return *p;
    }
}

// Variant 4: a pointer freed on both branches is freed once.
void variant4_freed_on_both_branches(int c) {
    void *p = malloc(4);
    if (c) {
        free(p);
    } else {
        free(p);
    }
    p = nullptr;
}

// Variant 5: a proof established before the `if` survives a re-check inside it.
int variant5_recheck_keeps_proof(int *p) {
    if (!p) return 0;
    if (p != nullptr && g) {
        g = 1;
    }
    return *p;
}

// Variant 6: assigning null ends the freed pointer's lifetime; free(NULL) is a no-op.
void variant6_null_after_free() {
    char *p = (char *)malloc(4);
    free(p);
    p = nullptr;
    free(p);
}

// Variant 7: the null branch reports and falls through; the file is closed
// exactly when it was opened.
void variant7_report_then_close() {
    FILE *f = fopen("a", "r");
    if (!f) perror("fopen");
    if (f) fclose(f);
}

// Variant 8: assigning 0 also ends the lifetime.
void variant8_zero_after_free() {
    void *p = malloc(4);
    free(p);
    p = 0;
    free(p);
}

// Variant 9: a type check inside a branch proves nothing after it, so the
// earlier fact (a holds an int) still stands.
int variant9_type_check_in_branch(int c) {
    std::any a = 1;
    if (c) {
        if (a.type() == typeid(int)) return 0;
    }
    if (c) return 0;
    return std::any_cast<int>(a);
}

int main() { return 0; }
