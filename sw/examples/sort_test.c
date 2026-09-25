#include <stdio.h>

#define N 8

void print_array(int arr[], int n, const char* label) {
    printf("%s: ", label);
    for (int i = 0; i < n; i++) {
        printf("%d ", arr[i]);
    }
    printf("\n");
}

void copy_array(int src[], int dst[], int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

// Bubble Sort
void bubble_sort(int arr[], int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }
}

// Selection Sort
void selection_sort(int arr[], int n) {
    for (int i = 0; i < n - 1; i++) {
        int min_idx = i;
        for (int j = i + 1; j < n; j++) {
            if (arr[j] < arr[min_idx]) {
                min_idx = j;
            }
        }
        int tmp = arr[i];
        arr[i] = arr[min_idx];
        arr[min_idx] = tmp;
    }
}

// Insertion Sort
void insertion_sort(int arr[], int n) {
    for (int i = 1; i < n; i++) {
        int key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

// Quick Sort
void quick_sort(int arr[], int low, int high) {
    if (low < high) {
        // Partition
        int pivot = arr[high];
        int i = low - 1;
        for (int j = low; j < high; j++) {
            if (arr[j] < pivot) {
                i++;
                int tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
            }
        }
        int tmp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = tmp;
        int pi = i + 1;
        
        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

int main(void) {
    int original[] = {64, 25, 12, 22, 11, 90, 45, 33};
    int arr[N];
    
    printf("=== Sorting Algorithms Test ===\n\n");
    print_array(original, N, "Original");
    printf("\n");
    
    // Bubble Sort
    copy_array(original, arr, N);
    bubble_sort(arr, N);
    print_array(arr, N, "Bubble  ");
    
    // Selection Sort
    copy_array(original, arr, N);
    selection_sort(arr, N);
    print_array(arr, N, "Selection");
    
    // Insertion Sort
    copy_array(original, arr, N);
    insertion_sort(arr, N);
    print_array(arr, N, "Insertion");
    
    // Quick Sort
    copy_array(original, arr, N);
    quick_sort(arr, 0, N - 1);
    print_array(arr, N, "Quick   ");
    
    printf("\nDone!\n");
    return 0;
}
