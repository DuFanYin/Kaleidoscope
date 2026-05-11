#include "host/HostABI.h"
#include "frontend/AST.h"
#include "frontend/Types.h"

#include "llvm/ADT/StringRef.h"

namespace kaleidoscope {

static bool isKalRtBuiltin(llvm::StringRef name) {
  return name == "putchard" || name == "printd";
}

std::optional<std::string>
validateKalRtBuiltinPrototype(const PrototypeAST &proto) {
  if (!isKalRtBuiltin(proto.getName()))
    return std::nullopt;

  if (proto.isUnaryOp() || proto.isBinaryOp())
    return std::string("kal_rt builtin ") + proto.getName() +
           " cannot be declared as an operator";

  if (proto.getArgCount() != 1)
    return std::string("kal_rt builtin ") + proto.getName() +
           " must have exactly one argument";

  if (proto.getArgType(0) != KalType::Double)
    return std::string("kal_rt builtin ") + proto.getName() +
           " requires a single `double` parameter: `name(x:double)`";

  return std::nullopt;
}

const char *kalRtBuiltinListForDiagnostics() {
  return "putchard, printd";
}

} // namespace kaleidoscope
