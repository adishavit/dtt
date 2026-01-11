// WHAT IS TESTED:
// Complex null guard patterns should be recognized.
//
// Expected: PASS

struct Data {
    int value;
    bool ready;
};

// 1. Double negation guard
void test_double_negation() {
    Data* ptr = new Data();
    if (!(ptr == nullptr)) {
        ptr->value = 1;
    }
}

// 2. Compound && with short-circuit
void test_compound_and() {
    Data* ptr = new Data();
    ptr->ready = true;
    if (ptr && ptr->ready) {
        ptr->value = 2;
    }
}

// 3. Early return pattern
void test_early_return() {
    Data* ptr = nullptr;
    if (!ptr) return;
    ptr->value = 3;  // Safe: early return proves non-null
}

// 4. Negated equality in else
void test_negated_else() {
    Data* ptr = new Data();
    if (ptr == nullptr) {
        return;
    } else {
        ptr->value = 4;  // Safe: in else branch of null check
    }
}

// 5. Double negation with nullptr literal
void test_double_neg_nullptr() {
    Data* ptr = new Data();
    if (!(nullptr == ptr)) {
        ptr->value = 5;
    }
}

// 6. Compound with member check
void test_compound_member() {
    Data* ptr = new Data();
    ptr->ready = false;
    if (ptr != nullptr && !ptr->ready) {
        ptr->value = 6;  // Safe: first condition proves non-null
    }
}

int main() {
    test_double_negation();
    test_compound_and();
    test_early_return();
    test_negated_else();
    test_double_neg_nullptr();
    test_compound_member();
    return 0;
}
