// Example 4: Runtime value WITHOUT guard — should NOT compile

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v) { return v.data[0]; }

int main(int argc, char**) {
    Vec v;
    int arr[] = {1, 2, 3};
    v.data = arr;
    v.len = argc;  // Runtime value
    
    // No guard — plugin cannot verify len > 0
    return head(v);  // ERROR: v.len is unknown
}
