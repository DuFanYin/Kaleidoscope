// Minimal program linking the compiler core + embed API (no CLI main.cpp).
#include "embed/Embed.h"

#include <cstdio>

int main() {
  using namespace kaleidoscope::embed;
  Options opt;
  opt.jit = true;
  const char kal[] = "extern printd(x);\n"
                     "printd(3+4);\n";
  Result r = compileFromSource(kal, opt);
  std::fprintf(stderr, "embed_smoke: %s\n", r.message.c_str());
  return r.ok ? 0 : 1;
}
