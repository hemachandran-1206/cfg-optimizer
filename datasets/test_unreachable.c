// TEST: Unreachable Code Removal
// Expected Phase 3D:
//   - The 'if (0)' branch is NEVER taken → its block is UNREACHABLE
//   - The 'if (1)' else-branch is NEVER taken → its block is UNREACHABLE

int test_unreachable() {
    int x = 5;

    if (0) {
        x = 999;    // UNREACHABLE: condition is always false
    }

    if (1) {
        x = x + 1;  // REACHABLE: condition is always true
    } else {
        x = 0;      // UNREACHABLE: else of always-true condition
    }

    return x;
}
