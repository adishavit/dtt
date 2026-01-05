// Example 17: Deeply nested struct — should COMPILE

struct Vec { int* data; int len; };
struct Inventory { Vec items; };
struct Player { Inventory inv; };
struct Game { Player player; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Game g;
    g.player.inv.items.len = argc;
    
    if (g.player.inv.items.len > 0) {
        head(g.player.inv.items);  // OK: full path tracked
    }
    
    return 0;
}
