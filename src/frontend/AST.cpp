#include "frontend/AST.h"

namespace kaleidoscope {

void PrototypeAST::inferReturnType() {
  if (Args.empty()) {
    RetTy = KalType::Double;
    return;
  }
  RetTy = KalType::Int32;
  for (const auto &A : Args) {
    if (A.second == KalType::Double) {
      RetTy = KalType::Double;
      return;
    }
  }
}

PrototypeAST::PrototypeAST(const std::string &NameIn,
                           std::vector<std::pair<std::string, KalType>> ArgsIn,
                           bool IsOperatorIn, unsigned Prec)
    : Name(NameIn), Args(std::move(ArgsIn)), IsOperator(IsOperatorIn),
      Precedence(Prec) {
  inferReturnType();
}

} // namespace kaleidoscope
