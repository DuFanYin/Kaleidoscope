// Tiny demo: links against an object file that defines extern "C" double average(...).
// Not part of the Kaleidoscope compiler; kept as a separate example.

#include <iostream>

extern "C" {
double average(double, double);
}

int main() {
  std::cout << "average of 3.0 and 4.0: " << average(3.0, 4.0) << std::endl;
}
