// 1768: A static function's dereferenced parameter, checked at each call
// against what the caller knows there (enforce mode)

// Variant 1: a string literal is non-null.
static int first(const char *s) { return s[0]; }
int variant1_string_literal() { return first("hello"); }

// Variant 2: a recursive call on p + 1, after p is dereferenced.
static int length(const char *s) {
    if (*s == '\0') return 0;
    return 1 + length(s + 1);
}
int variant2_recursion() { return length("abc"); }

// Variant 3: a pointer checked before the call.
static void store(int *p) { *p = 1; }
void variant3_guarded(int *maybe) {
    if (!maybe) return;
    store(maybe);
}

// Variant 4: an array, and an offset into it.
void variant4_array_offset() {
    int a[4] = {};
    store(a);
    store(a + 2);
    store(&a[3]);
}

// Variant 5: a local made non-null and never changed, passed to a function
// all of whose calls in this file pass non-null.
struct Data { int value; };
void process(Data *d) { d->value = 42; }
void variant5_unchanged_local() {
    Data x;
    Data *p = &x;
    process(p);
    process(&x);
}

// Variant 6: a pointer non-null at the call, null where it was declared.
void variant6_set_before_call() {
    int x = 0;
    int *p = nullptr;
    p = &x;
    store(p);
}

int main() { return 0; }
