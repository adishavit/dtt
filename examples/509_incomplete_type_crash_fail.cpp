// WHAT IS TESTED:
// Dependent types in templates that may crash getTypeSizeInChars.
// These patterns are found in OpenCV and other complex C++ codebases.
//
// Expected behavior: Should NOT crash (graceful handling of dependent types)
//
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: Dependent type dereference in template
// -----------------------------------------------------------------------------
template<typename T>
class Iterator {
public:
    T* ptr;
    T& operator*() { return *ptr; }  // Dependent type dereference  dtt-error {{potential null pointer dereference}}
};

template<typename T>
void variant1_dependent_deref(Iterator<T>& it) {
    (void)*it;
}

// -----------------------------------------------------------------------------
// VARIANT 2: Dependent array access
// -----------------------------------------------------------------------------
template<typename T>
struct Container {
    T data[32];
    T& operator[](int i) { return data[i]; }  // Dependent type array
};

template<typename T>
void variant2_dependent_array(Container<T>& c, int i) {
    (void)c[i];
}

// -----------------------------------------------------------------------------
// VARIANT 3: Dependent type in reinterpret_cast
// -----------------------------------------------------------------------------
template<typename T>
void variant3_dependent_cast(char* data) {
    T* ptr = reinterpret_cast<T*>(data);
    (void)ptr;
}

// -----------------------------------------------------------------------------
// VARIANT 4: Dependent type pointer arithmetic
// -----------------------------------------------------------------------------
template<typename T>
void variant4_dependent_ptr_arith(T* ptr, int n) {
    T* p2 = ptr + n;
    (void)p2;
}

// -----------------------------------------------------------------------------
// VARIANT 5: Nested template with dependent type
// -----------------------------------------------------------------------------
template<typename T>
struct Outer {
    template<typename U>
    struct Inner {
        T* tptr;
        U* uptr;
    };
};

template<typename T, typename U>
void variant5_nested_dependent(typename Outer<T>::template Inner<U>& inner) {
    (void)inner.tptr;
    (void)inner.uptr;
}

// -----------------------------------------------------------------------------
// VARIANT 6: Dependent sizeof in template
// -----------------------------------------------------------------------------
template<typename T>
void variant6_dependent_sizeof() {
    char buffer[sizeof(T)];
    (void)buffer;
}

// -----------------------------------------------------------------------------
// VARIANT 7: Template with member pointer to dependent type array
// -----------------------------------------------------------------------------
template<typename T, int N>
class FixedArray {
    T data[N];
public:
    T* begin() { return data; }
    T* end() { return data + N; }
    T& at(int i) { return data[i]; }
};

template<typename T>
void variant7_fixed_array(FixedArray<T, 10>& arr) {
    (void)arr.at(0);
}

// -----------------------------------------------------------------------------
// VARIANT 8: SFINAE and dependent types
// -----------------------------------------------------------------------------
template<typename T>
struct HasSize {
    T* ptr;
    int size[sizeof(T)];  // Dependent array size
};

template<typename T>
void variant8_sfinae_dependent(HasSize<T>& hs) {
    (void)hs.ptr;
}

// -----------------------------------------------------------------------------
// VARIANT 9: Dependent type through alias template
// -----------------------------------------------------------------------------
template<typename T>
using Ptr = T*;

template<typename T>
void variant9_alias_template(Ptr<T> p) {
    (void)*p;
}

// -----------------------------------------------------------------------------
// VARIANT 10: Multiple dependent types
// -----------------------------------------------------------------------------
template<typename T, typename U>
struct Pair {
    T first;
    U second;
};

template<typename T, typename U>
void variant10_multi_dependent(Pair<T, U>* p) {
    (void)p->first;
    (void)p->second;
}

// Explicit instantiations to trigger analysis
template void variant1_dependent_deref<int>(Iterator<int>&);
template void variant2_dependent_array<double>(Container<double>&, int);
template void variant3_dependent_cast<float>(char*);
template void variant4_dependent_ptr_arith<long>(long*, int);
template void variant7_fixed_array<char>(FixedArray<char, 10>&);
template void variant9_alias_template<int>(Ptr<int>);
template void variant10_multi_dependent<int, double>(Pair<int, double>*);

int main() {
    Iterator<int> it;
    it.ptr = new int(42);
    variant1_dependent_deref(it);

    Container<double> c;
    variant2_dependent_array(c, 0);

    char data[100];
    variant3_dependent_cast<float>(data);

    long arr[10];
    variant4_dependent_ptr_arith(arr, 5);

    FixedArray<char, 10> fa;
    variant7_fixed_array(fa);

    int* p = new int(1);
    variant9_alias_template(p);

    Pair<int, double> pair = {1, 2.0};
    variant10_multi_dependent(&pair);

    delete it.ptr;
    delete p;
    return 0;
}
