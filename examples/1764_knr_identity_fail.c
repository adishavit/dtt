/* 1764: a C declaration without a prototype and its definition are one
   function. */
static void deref();
void use2(void) {
    deref((int *)0); /* dtt-error {{null}} */
}
static void deref(int *p) { *p = 1; }

int g;
static void deref_shadowing(int *g) {
    (void)g;
    *g = 1;
}
void use3(void) {
    deref_shadowing((int *)0); /* dtt-error {{null}} */
}

int main(void) { return 0; }
