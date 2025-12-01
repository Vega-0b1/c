#include <iostream>
#include <vector>

void insertion_sort(std::vector<int> &arr) {
  int size_of_set = arr.size();
  for (int i = 1; i < size_of_set; ++i) {
    int key = arr[i];
    int j = i - 1;

    while (j >= 0 && arr[j] > key) {
      arr[j + 1] = arr[j];
      --j;
    }
    arr[j + 1] = key;
  }
}

int main() {
  std::vector<int> arr = {3, 1, 5, 4, 7};

  insertion_sort(arr);
  for (int x : arr) {
    std::cout << x << " ";
  }
  std::cout << '\n';
  return 0;
}
