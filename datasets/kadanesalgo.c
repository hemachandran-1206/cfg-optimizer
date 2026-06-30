#include <stdio.h>

// Function to find maximum subarray sum using Kadane's Algorithm
int kadane(int arr[], int n) {
    int max_so_far = arr[0];
    int current_max = arr[0];

    for (int i = 1; i < n; i++) {
        // Either extend the current subarray or start a new one
        if (current_max + arr[i] > arr[i])
            current_max = current_max + arr[i];
        else
            current_max = arr[i];

        // Update overall maximum
        if (current_max > max_so_far)
            max_so_far = current_max;
    }

    return max_so_far;
}

int main() {
    int arr[] = {-2, -3, 4, -1, -2, 1, 5, -3};
    int n = sizeof(arr) / sizeof(arr[0]);

    int max_sum = kadane(arr, n);

    printf("Maximum subarray sum = %d\n", max_sum);

    return 0;
}
