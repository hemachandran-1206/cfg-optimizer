// ─────────────────────────────────────────────────────────────
// TEST FILE 5: test_all_phases.c
//
// PURPOSE: One file that exercises ALL phases at once
//          Use this as your main stress test / demo file
//
// COMPLETE EXPECTED RESULTS:
//
// ── Function: compute() ─────────────────────────────────────
//
// [Phase 1 - CFG Image]
//   Generates: compute_cfg.png
//   Expected shape: ENTRY → Block(init) → Block(loop-check)
//                   → Block(loop-body) → Block(loop-check)  [back edge]
//                   → Block(after-loop) → EXIT
//
// [Phase 2A - Reaching Definitions]
//   i   is defined in two places: init (i=0) and loop-increment (i++)
//   Both definitions of i reach the loop condition check
//   sum is defined in init (sum=0) and in loop body (sum=sum+i)
//
// [Phase 2B - Live Variables]
//   i and sum are both LIVE inside the loop
//   After the loop, only sum is live (i is no longer needed)
//
// [Optimization 1 - Constant Folding]
//   (2 + 3) in the limit variable → folds to 5
//
// [Optimization 2 - Constant Propagation]
//   limit = 5 → the comparison (i < limit) can use 5 directly
//   start = 0 → i = start can use 0 directly
//
// [Optimization 3 - Dead Code Elimination]
//   debug_val is assigned but never used → DEAD
//
// [Optimization 4 - Unreachable Code]
//   All blocks reachable in this function (loop has real condition)
//
// ── Function: broken() ──────────────────────────────────────
//
// [Optimization 3 - Dead Code]
//   old_val assigned, immediately overwritten → DEAD
//
// [Optimization 4 - Unreachable]
//   Code after return is UNREACHABLE
//
// [Phase 2A - Uninitialized]
//   WARNING: raw declared without init
// ─────────────────────────────────────────────────────────────

int compute() {
    int limit = 2 + 3;   // FOLD: 2+3 → 5; PROPAGATE: limit known = 5
    int start = 0;        // PROPAGATE: start known = 0
    int debug_val = 42;   // DEAD: never used after this

    int i   = start;      // PROPAGATE: start=0, so i=0
    int sum = 0;

    while (i < limit) {   // PROPAGATE: limit=5 here
        sum = sum + i;
        i = i + 1;
    }

    return sum;           // expected result when run: 0+1+2+3+4 = 10
}

int broken(int x) {
    int raw;              // WARNING: uninitialized
    int old_val = x * 2;  // DEAD: overwritten immediately below
    old_val = x + 1;      // real assignment — this is what gets used
    return old_val;

    int phantom = 99;     // UNREACHABLE: after return
    return phantom;
}

int triangle(int n) {
    // Computes 1+2+3+...+n
    // Tool should show: i and total both LIVE in loop
    // No dead code, all blocks reachable, no foldable constants
    int i = 1;
    int total = 0;
    while (i <= n) {
        total = total + i;
        i = i + 1;
    }
    return total;
}
