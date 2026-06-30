#include <stdio.h>

int main() {

    // ─── CONSTANT FOLDING ───────────────────────────────────
    // 3 + 5 will be folded to 8
    // 10 * 2 will be folded to 20
    int a = 3 + 5;
    int b = 10 * 2;

    // ─── CONSTANT PROPAGATION ───────────────────────────────
    // a is proven = 8, b is proven = 20
    // so (a + b) will be propagated to (8 + 20)
    int c = a + b;

    // ─── DEAD CODE ELIMINATION ──────────────────────────────
    // d is assigned but NEVER used again anywhere
    // so this whole assignment is dead
    int d = 99;
    
    int var = z;

    // ─── UNREACHABLE CODE ───────────────────────────────────
    // after return, the code below is unreachable
    printf("Result: %d\n", c);

    return 0;

    // this block is UNREACHABLE — will be removed
    int unreachable_var = 42;
    printf("You will never see this: %d\n", unreachable_var);
}
