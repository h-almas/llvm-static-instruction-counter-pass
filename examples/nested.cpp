#include <cstddef>
#include <iostream>

std::size_t sum_to_n(std::size_t n) {
  std::size_t sum{};
  for (std::size_t i{1}; i <= n; i++) {
    sum += 1;
  }
  return sum;
}

int main() {
  std::size_t sum{};
  std::size_t iterations;
  std::cin >> iterations;
  for (std::size_t i{1}; i <= iterations; i++) {
    sum += sum_to_n(2 * i);
  }
  std::cout << "Sum = " << sum << std::endl;
}
