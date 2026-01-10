// Example 13: Proof copies with assignment inside guard — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Vec v;
    v.len = argc;

    if (v.len > 0) {
        Vec w = v;   // w copies v's proof inside guarded block
        head(w);     // OK: inside guarded block with proof
    }

    return 0;
}
