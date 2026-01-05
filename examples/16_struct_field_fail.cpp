// Example 16: No proof through struct field — should FAIL

struct Vec { int* data; int len; };

struct Player {
    Vec inventory;
};

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Player p;
    p.inventory.len = argc;  // Runtime
    
    // No guard!
    head(p.inventory);  // FAIL: no proof
    
    return 0;
}
