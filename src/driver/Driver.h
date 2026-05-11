#pragma once

#include <string>

namespace kaleidoscope {

struct DriverConfig {
  bool use_jit = false;
  bool print_ready_prompt = true;
};

/// Returns true when no parse/codegen/extern-contract error happened.
bool runCompilerMainLoop(const DriverConfig &cfg);

bool emitCurrentModuleToObjectFile(const std::string &path);

} // namespace kaleidoscope
