#include <cstddef>
#include <iostream>

std::size_t fib(std::size_t n) {
  if (n <= 1)
    return n;

  return fib(n - 1) + fib(n - 2);
}

int main() { std::cout << "Fib 2 = " << fib(2) << std::endl; }
