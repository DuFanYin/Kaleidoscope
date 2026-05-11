#pragma once

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

namespace kaleidoscope {

/// Source-level parameter / variable types. `Bool` is stored as i32 (0/1).
enum class KalType { Double, Int32, Bool };

inline bool isIntegerLike(KalType T) {
  return T == KalType::Int32 || T == KalType::Bool;
}

inline llvm::Type *llvmTypeForKal(KalType T, llvm::LLVMContext &Ctx) {
  if (T == KalType::Double)
    return llvm::Type::getDoubleTy(Ctx);
  return llvm::Type::getInt32Ty(Ctx);
}

} // namespace kaleidoscope
