// WHAT IS TESTED:
// Early-return null guard should prove non-null after the return.
//
// Expected: PASS

struct Data {
    int value;
    bool ready;
};

// Basic early return guard
void test_basic_early_return(Data* ptr) {
    if (!ptr) {
        return;
    }
    ptr->value = 1;  // ptr proven non-null
}

// Early return with explicit nullptr check
void test_explicit_nullptr(Data* ptr) {
    if (ptr == nullptr) {
        return;
    }
    ptr->value = 2;
}

// Multiple early returns
void test_multiple_early_returns(Data* ptr, bool flag) {
    if (!ptr) {
        return;
    }
    if (!flag) {
        return;
    }
    ptr->value = 3;  // ptr still proven non-null
}

// Early return in nested scope
void test_nested_early_return(Data* ptr) {
    if (!ptr) {
        return;
    }

    if (ptr->ready) {
        ptr->value = 4;
    } else {
        ptr->value = 5;
    }
}

// Else-return: if (ptr) { use } else { return; } proves ptr after block
void test_else_return(Data* ptr) {
    if (ptr) {
        ptr->value = 6;
    } else {
        return;
    }
    ptr->value = 7;  // ptr proven non-null (else returned)
}

// Else-return with explicit nullptr check
void test_else_return_explicit(Data* ptr) {
    if (ptr != nullptr) {
        ptr->value = 8;
    } else {
        return;
    }
    ptr->value = 9;
}

// Multiple accesses after else-return
void test_else_return_multiple(Data* ptr) {
    if (ptr) {
        ptr->value = 10;
    } else {
        return;
    }
    ptr->value = 11;
    ptr->value = 12;
}

// Else-throw pattern
void test_else_throw(Data* ptr) {
    if (ptr) {
        ptr->value = 13;
    } else {
        throw 1;
    }
    ptr->value = 14;  // ptr proven non-null (else threw)
}

// Else-break in loop
void test_else_break(Data* ptr) {
    for (int i = 0; i < 1; ++i) {
        if (ptr) {
            ptr->value = 15;
        } else {
            break;
        }
        ptr->value = 16;  // ptr proven non-null (else broke)
    }
}

// Else-continue in loop
void test_else_continue(Data* ptr) {
    for (int i = 0; i < 1; ++i) {
        if (ptr) {
            ptr->value = 17;
        } else {
            continue;
        }
        ptr->value = 18;  // ptr proven non-null (else continued)
    }
}

int main() {
    Data d;
    test_basic_early_return(&d);
    test_explicit_nullptr(&d);
    test_multiple_early_returns(&d, true);
    test_nested_early_return(&d);
    test_else_return(&d);
    test_else_return_explicit(&d);
    test_else_return_multiple(&d);
    test_else_throw(&d);
    test_else_break(&d);
    test_else_continue(&d);
    return 0;
}
