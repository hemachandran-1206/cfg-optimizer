// test_input.c
// Designed to trigger all 4 optimizations in Phase 3

int example(int flag) {

    // --- CONSTANT FOLDING ---
    // 3 + 5 is a compile-time constant expression → folds to 8
    int a = 3 + 5;

    // --- CONSTANT PROPAGATION ---
    // x is always 10, so any use of x can be replaced with 10
    int x = 10;
    int b = x + 2;   // x is known → can propagate 10 here

    // --- DEAD CODE ELIMINATION ---
    // dead is assigned but never read again after this block
    int dead = 99;

    // --- LIVE / USED variable (not dead) ---
    int result = a + b;

    // --- UNREACHABLE CODE ---
    // condition is always false → the if-body block is never entered
    if (0) {
        result = 999;   // this block is unreachable
    }

    return result;
}
