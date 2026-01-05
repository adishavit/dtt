// Example 15: Proof through struct fields — should COMPILE

struct Vec { int* data; int len; };

struct Player {
    Vec inventory;
    int health;
};

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Player p;
    p.inventory.len = argc;
    
    if (p.inventory.len > 0) {
        head(p.inventory);  // OK: proof attached to p.inventory
    }
    
    return 0;
}
