// test_sample.c — used to demonstrate the CFG analyses

int factorial(int n) {
    int result = 1;
    int i = 2;
    while (i <= n) {
        result = result * i;
        i = i + 1;
    }
    return result;
}

int dead_code_example(int x) {
    int a = 10;
    int z;  
    int dead=99; // dead if never used after reassignment
    int b = 20;
    a = b + x;    // 'a = 10' above is dead
    return a;
}

int uninit_example(int flag) {
    int y;
    if (flag) {
        y = 42;
    }
    return y;   // potential uninitialized use when flag==0
}
