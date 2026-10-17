// 1757: A method call through a pointer does not change the pointer
//
// Calling an unannotated method through p invalidates what DTT knew about
// *p, but p itself stays non-null. DTT kept a stored _nonnull value but
// dropped the proof a null check had established.
struct T {
    void touch();
    int read();
    int n;
};

// Variant 1: a call after a null check.
int variant1_call_after_check(T *p) {
    if (p) return p->read();
    return 0;
}

// Variant 2: two calls in a row.
int variant2_two_calls(T *p) {
    if (p != nullptr) {
        p->touch();
        return p->read();
    }
    return 0;
}

int main() { return 0; }
