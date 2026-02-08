// 1211_method_template_fold_pass.cpp
// Tests: Method template fold expression push_back — should compile clean
//
// Bug 1 fix: Pass 2b worklist must recurse into CXXRecordDecl to find
// method template specializations inside classes.

#include <vector>

// =============================================================================
// SECTION 1: Simple method template with fold push_back
// =============================================================================

// Variant 1.1: Basic struct with variadic method template
struct Processor {
    template<typename... Args>
    void run(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK: _size = N after fold expansion
    }
};

void variant1_method_fold() {
    Processor p;
    p.run(1, 2, 3);
}

// Variant 1.2: Single element
void variant2_single() {
    Processor p;
    p.run(42);
}

// =============================================================================
// SECTION 2: Method template in a class hierarchy
// =============================================================================

// Variant 2.1: Derived class with variadic method
struct Base {
    virtual ~Base() = default;
};

struct Derived : Base {
    template<typename... Args>
    void collect(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK
    }
};

void variant3_derived_method() {
    Derived d;
    d.collect(10, 20);
}

// =============================================================================
// SECTION 3: Method template in a namespace
// =============================================================================

namespace ns {
    struct Widget {
        template<typename... Args>
        void append(Args... args) {
            std::vector<int> v;
            (v.push_back(args), ...);
            v[0];  // OK
        }
    };
}  // namespace ns

void variant4_namespaced_method() {
    ns::Widget w;
    w.append(1, 2, 3, 4);
}

// =============================================================================
// SECTION 4: Multiple specializations of same method template
// =============================================================================

struct MultiSpec {
    template<typename... Args>
    void fill(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK for all specializations
    }
};

void variant5_multiple_specs() {
    MultiSpec m;
    m.fill(1);
    m.fill(1, 2);
    m.fill(1, 2, 3, 4, 5);
}

int main() {
    variant1_method_fold();
    variant2_single();
    variant3_derived_method();
    variant4_namespaced_method();
    variant5_multiple_specs();
    return 0;
}
