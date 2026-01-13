// Tests: Guards on assignment expressions recognize the assigned variable
// Issue #30: FIXED - (local = ptr) != nullptr now guards 'local'

struct Data {
    int value;
};

Data* getExternalData();  // Unknown nullability - prevents ghost state inference

void test(Data* ptr) {
    Data* local = nullptr;
    if ((local = ptr) != nullptr) {
        local->value = 1;
    }
}

int main() {
    test(getExternalData());  // Pass unknown nullability
    return 0;
}
