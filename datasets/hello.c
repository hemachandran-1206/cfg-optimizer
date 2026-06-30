#include <stdio.h>

int main() {

    // CONSTANT FOLDING
    int a = 3 + 5;

    // CONSTANT PROPAGATION
    int x = 10;
    int y = x + 2;

    // DEAD CODE
    int dead = 100;

    // BRANCH FOR PROPAGATION
    int z;
    if (1) {
        z = 20;
    } else {
        z = 20;
    }

    int k = z + 5;

    // UNREACHABLE CODE
    if (0) {
        int unreachable = 999;
        printf("This never runs\n");
    }

    return y + k;
}
