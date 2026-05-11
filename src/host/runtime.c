#include "host/kal_runtime.h"

#include <stdio.h>

double putchard(double X) {
  fputc((char)X, stderr);
  return 0.0;
}

double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0.0;
}
