#include <iostream>
#include <vector>

int main() {
  std::vector<float> x{1, 2, 3, 4, 5};
  std::vector<float> y{1, 1, 2, 3, 5};
  float a{2.f};

  for (int i{}; i < std::min(x.size(), y.size()); i++) {
    y[i] += a * x[i];
  }

  float sum{};
  for (float y_ : y) {
    sum += y_;
  }

  std::cout << "Sum: " << sum << std::endl;
}
