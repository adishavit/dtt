// Example 22: Z3 chained arithmetic — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

[[clang::annotate("dtt:requires=len >= 3")]]
void needsThree(Vec& v);

int main() {
    Vec a;
    a.len = 10;
    
    Vec b = tail(a);   // b.len = 9
    Vec c = tail(b);   // c.len = 8
    Vec d = tail(c);   // d.len = 7
    Vec e = tail(d);   // e.len = 6
    Vec f = tail(e);   // f.len = 5
    Vec g = tail(f);   // g.len = 4
    Vec h = tail(g);   // h.len = 3
    Vec i = tail(h);   // i.len = 2
    
    // Z3 disproves: 2 >= 3 is false
    needsThree(i);
    
    return 0;
}
