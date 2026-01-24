// 969_capability_xfail.cpp
// Tests: Capability security - forged token rejected
// From BLOG.md: Capability Security example
// XFAIL: Cross-argument field comparison (arg0._field == _id) not yet supported

struct Capability {
    int _resource_id;
    int _can_write;
};

struct Resource {
    int _id;

    [[clang::annotate("dtt:requires=arg0._resource_id == _id")]]
    [[clang::annotate("dtt:requires=arg0._can_write == 1")]]
    void write(Capability& cap, const char* data) {}
};

void test_forged_capability() {
    Resource r;
    r._id = 42;

    Capability forged;
    forged._resource_id = 99;  // Wrong resource_id!
    forged._can_write = 1;

    r.write(forged, "hack");   // ERROR: resource_id(99) != _id(42)
}

int main() {
    test_forged_capability();
    return 0;
}
