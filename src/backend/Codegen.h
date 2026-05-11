#pragma once

#include "frontend/AST.h"

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

#include <map>
#include <memory>
#include <string>

namespace llvm {
namespace orc {
class KaleidoscopeJIT;
}
} // namespace llvm

namespace kaleidoscope {

extern std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

extern std::unique_ptr<llvm::LLVMContext> TheContext;
extern std::unique_ptr<llvm::Module> TheModule;
extern std::unique_ptr<llvm::IRBuilder<>> Builder;

/// New Pass Manager: analysis managers + one function pipeline (re-run/func).
extern std::unique_ptr<llvm::ModuleAnalysisManager> TheMAM;
extern std::unique_ptr<llvm::CGSCCAnalysisManager> TheCGAM;
extern std::unique_ptr<llvm::LoopAnalysisManager> TheLAM;
extern std::unique_ptr<llvm::FunctionAnalysisManager> TheFAM;
extern std::unique_ptr<llvm::FunctionPassManager> OptFunctionPasses;

extern std::map<std::string, llvm::AllocaInst *> NamedValues;
extern std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

llvm::Value *LogErrorV(const char *Str);
llvm::Function *getFunction(std::string Name);

void InitializeModuleAndPassManager();

} // namespace kaleidoscope
