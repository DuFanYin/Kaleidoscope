#pragma once

#include "llvm/IR/PassManager.h"

namespace kaleidoscope {

/// Hand-written NPM pass: algebraic identities on Kaleidoscope `double` IR.
class KaleidoscopeAlgebraicSimplifyPass
    : public llvm::PassInfoMixin<KaleidoscopeAlgebraicSimplifyPass> {
public:
  llvm::PreservedAnalyses run(llvm::Function &F,
                               llvm::FunctionAnalysisManager &AM);
};

} // namespace kaleidoscope
