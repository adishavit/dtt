// 1759: A label a later goto jumps back to is a loop head (safe uses)
#include <cstdio>

bool c();
int a[4];

// Variant 1: a goto loop bounded by its guard.
void variant1_bounded() {
    int i = 0;
again:
    if (i < 4) {
        a[i] = 1;
        i++;
        goto again;
    }
}

// Variant 2: a retry loop that opens a file.
void variant2_retry_open() {
    FILE *f;
retry:
    f = fopen("a", "r");
    if (!f) {
        if (c()) goto retry;
        return;
    }
    fclose(f);
}

// Variant 3: a fact the goto loop does not touch survives it.
int variant3_untouched_fact(int *p) {
    if (!p) return 0;
    int n = 0;
again:
    n++;
    if (c()) goto again;
    return *p + n;
}

// Variant 4: code after a return that no goto reaches is not analyzed.
int variant4_dead_label() {
    int *p = nullptr;
    return 0;
unused:
    return *p;
}

// Variant 5: a goto loop around a local class with its own goto loop.
int variant5_local_class_goto() {
    int n = 0;
again:
    struct Local {
        static int count(int k) {
        inner:
            if (k-- > 0) goto inner;
            return k;
        }
    };
    n += Local::count(3);
    if (n < 3) goto again;
    return n;
}

int main() { return 0; }
