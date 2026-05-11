#include "Passes.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace kaleidoscope {

namespace {

static bool isZero(Value *V) {
  auto *C = dyn_cast<ConstantFP>(V);
  return C && C->isExactlyValue(0.0);
}

static bool isOne(Value *V) {
  auto *C = dyn_cast<ConstantFP>(V);
  return C && C->isExactlyValue(1.0);
}

} // namespace

PreservedAnalyses KaleidoscopeAlgebraicSimplifyPass::run(Function &F,
                                                          FunctionAnalysisManager &) {
  bool Changed = false;

  for (BasicBlock &BB : F) {
    for (Instruction &I : make_early_inc_range(BB)) {
      auto *BO = dyn_cast<BinaryOperator>(&I);
      if (!BO)
        continue;
      if (BO->getType() != Type::getDoubleTy(F.getContext()))
        continue;

      Value *L = BO->getOperand(0);
      Value *R = BO->getOperand(1);

      switch (BO->getOpcode()) {
      case Instruction::FAdd: {
        if (isZero(L) || isZero(R)) {
          Value *V = isZero(L) ? R : L;
          BO->replaceAllUsesWith(V);
          BO->eraseFromParent();
          Changed = true;
        }
        break;
      }
      case Instruction::FSub: {
        if (isZero(R)) {
          BO->replaceAllUsesWith(L);
          BO->eraseFromParent();
          Changed = true;
        }
        break;
      }
      case Instruction::FMul: {
        if (isZero(L) || isZero(R)) {
          BO->replaceAllUsesWith(ConstantFP::get(F.getContext(), APFloat(0.0)));
          BO->eraseFromParent();
          Changed = true;
        } else if (isOne(L) || isOne(R)) {
          Value *V = isOne(L) ? R : L;
          BO->replaceAllUsesWith(V);
          BO->eraseFromParent();
          Changed = true;
        }
        break;
      }
      default:
        break;
      }
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace kaleidoscope
