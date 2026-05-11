#include <stdio.h>

extern double entry(void);

int main(void) {
  double v = entry();
  fprintf(stderr, "entry() => %f\n", v);
  return 0;
}
