// Example 2: Type errors — this should NOT compile

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v) { return v.data[0]; }

[[clang::annotate("dtt:requires=len > 1")]]
int second(Vec& v) { return v.data[1]; }

int main() {
    int arr[] = {42};

    // Error 1: Empty vector
    Vec empty;
    empty.data = arr;
    empty.len = 0;
    head(empty);  // ERROR: 0 > 0 is false  dtt-error {{len > 0}}

    // Error 2: Length too small for second()
    Vec single;
    single.data = arr;
    single.len = 1;
    second(single);  // ERROR: 1 > 1 is false  dtt-error {{len > 1}}

    // Error 3: Unknown length (no annotation, no tracking)
    Vec unknown;
    unknown.data = arr;
    head(unknown);  // ERROR: unknown.len is unknown  dtt-error {{len > 0}}

    return 0;
}
