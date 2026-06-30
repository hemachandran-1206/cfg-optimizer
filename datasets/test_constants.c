// TEST: Constant Folding + Constant Propagation
// Expected Phase 3A: 3+5 → 8, 10*2 → 20, 100-1 → 99
// Expected Phase 3B: x=8 propagates into y=x+1

int test_constants() {
    int a = 3 + 5;        // FOLD: 3+5 → 8
    int b = 10 * 2;       // FOLD: 10*2 → 20
    int c = 100 - 1;      // FOLD: 100-1 → 99
    int x = 8;            // PROPAGATE: x is always 8
    int y = x + 1;        // PROPAGATE: replace x with 8 → y = 8+1
    return y;
}
