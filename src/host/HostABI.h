#pragma once

#include <optional>
#include <string>

namespace kaleidoscope {

class PrototypeAST;

/// If @p proto names a kal_rt builtin (putchard, printd), checks arity and
/// shape; returns an error message on mismatch. Returns std::nullopt if the
/// name is not a reserved builtin (host must still supply the symbol for AOT).
std::optional<std::string> validateKalRtBuiltinPrototype(const PrototypeAST &proto);

/// Human-readable list of kal_rt builtin names (for diagnostics).
const char *kalRtBuiltinListForDiagnostics();

} // namespace kaleidoscope
