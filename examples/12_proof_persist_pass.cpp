// Example 12: Proof active inside guarded block — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    int arr[1] = {0};
    Vec v;
    v.data = arr;
    v.len = argc;  // Runtime value

    if (v.len > 0) {
        // Proof established inside this block
        head(v);  // OK: inside guarded block
    }

    return 0;
}
