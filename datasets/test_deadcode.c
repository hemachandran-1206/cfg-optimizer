// TEST: Dead Code Elimination
// Expected Phase 3C:
//   - 'unused' is assigned but never read → DEAD
//   - 'waste' is assigned but never read  → DEAD
//   - 'result' is returned so it is LIVE (not dead)

int test_deadcode() {
    int unused = 42;      // DEAD: never read after this
    int waste  = 99;      // DEAD: never read after this
    int result = 7;       // LIVE: returned below
    return result;
}
