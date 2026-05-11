#pragma once

#include <string>
#include <string_view>

namespace kaleidoscope::embed {

struct Options {
  bool jit = false;
  std::string object_path = "build/output.o";
};

struct Result {
  bool ok = false;
  std::string message;
};

/// Compile a full Kaleidoscope translation unit from memory.
///
/// - Default: IR is optimized and written to @p opt.object_path (creates parent
///   directories). Top-level expressions are not executed.
/// - `opt.jit == true`: Orc JIT runs each top-level expression; no object file.
///
/// Lexer/parser/codegen globals are reset for this call; safe to invoke again
/// after it returns.
Result compileFromSource(std::string_view source, const Options &opt = {});

} // namespace kaleidoscope::embed
