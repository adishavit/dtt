// Example 2: Type errors — this should NOT compile

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v) { return v.data[0]; }

[[clang::annotate("dtt:requires=len > 1")]]
int second(Vec& v) { return v.data[1]; }

int main() {
    // Error 1: Empty vector
    [[clang::annotate("dtt:len=0")]]
    Vec empty;
    head(empty);  // ERROR: 0 > 0 is false
    
    // Error 2: Length too small for second()
    [[clang::annotate("dtt:len=1")]]
    Vec single;
    second(single);  // ERROR: 1 > 1 is false
    
    // Error 3: Unknown length (no annotation, no tracking)
    Vec unknown;
    head(unknown);  // ERROR: unknown.len is unknown
    
    return 0;
}
