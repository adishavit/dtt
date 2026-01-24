// 968_capability_pass.cpp
// Tests: Capability security - resource binding with valid tokens
// From BLOG.md: Capability Security example

struct Capability {
    int _resource_id;
    int _can_read;
    int _can_write;
};

struct Resource {
    int _id;

    [[clang::annotate("dtt:requires=arg0._resource_id == _id")]]
    [[clang::annotate("dtt:requires=arg0._can_read == 1")]]
    void read(Capability& cap) {}

    [[clang::annotate("dtt:requires=arg0._resource_id == _id")]]
    [[clang::annotate("dtt:requires=arg0._can_write == 1")]]
    void write(Capability& cap, const char* data) {}
};

// Variant 1: Valid read capability
void test_valid_read() {
    Resource r;
    r._id = 42;

    Capability cap;
    cap._resource_id = 42;
    cap._can_read = 1;
    cap._can_write = 0;

    r.read(cap);  // OK: resource_id matches, can_read == 1
}

// Variant 2: Valid write capability
void test_valid_write() {
    Resource r;
    r._id = 42;

    Capability cap;
    cap._resource_id = 42;
    cap._can_read = 0;
    cap._can_write = 1;

    r.write(cap, "data");  // OK: resource_id matches, can_write == 1
}

// Variant 3: Full access capability
void test_full_access() {
    Resource r;
    r._id = 99;

    Capability cap;
    cap._resource_id = 99;
    cap._can_read = 1;
    cap._can_write = 1;

    r.read(cap);           // OK
    r.write(cap, "data");  // OK
}

int main() {
    test_valid_read();
    test_valid_write();
    test_full_access();
    return 0;
}
