// TEST: Reaching Definitions
// Expected Phase 2A:
//   - x defined in entry block, reaches the if-block and else-block
//   - result redefined in both branches → two defs reach the return
// Expected: uninitialized warning for 'uninit'

int test_reaching(int n) {
    int uninit;           // WARNING: declared with no initializer
    int x = 10;           // DEF of x — reaches both branches below
    int result;

    if (n > 0) {
        result = x + 1;   // USE of x (def from above reaches here)
    } else {
        result = x - 1;   // USE of x (same def reaches here)
    }

    return result;
}
