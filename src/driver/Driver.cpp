#include "driver/Driver.h"
#include "backend/Codegen.h"
#include "host/HostABI.h"
#include "backend/JIT.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdio>

using namespace llvm;

namespace kaleidoscope {

static ExitOnError ExitOnErr;

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static bool HandleDefinition(const DriverConfig &cfg) {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definition:");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      if (cfg.use_jit && TheJIT) {
        ExitOnErr(TheJIT->addModule(llvm::orc::ThreadSafeModule(
            std::move(TheModule), std::move(TheContext))));
        InitializeModuleAndPassManager();
      }
      return true;
    }
    return false;
  } else {
    getNextToken();
    return false;
  }
}

static bool HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto Err = validateKalRtBuiltinPrototype(*ProtoAST)) {
      LogError(Err->c_str());
      return false;
    }
    if (auto *FnIR = ProtoAST->codegen()) {
      fprintf(stderr, "Read extern: ");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
      return true;
    }
    return false;
  } else {
    getNextToken();
    return false;
  }
}

static bool HandleTopLevelExpression(const DriverConfig &cfg) {
  if (auto FnAST = ParseTopLevelExpr()) {
    if (FnAST->codegen()) {
      if (cfg.use_jit && TheJIT) {
        auto RT = TheJIT->getMainJITDylib().createResourceTracker();

        auto TSM = llvm::orc::ThreadSafeModule(std::move(TheModule),
                                               std::move(TheContext));
        ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
        InitializeModuleAndPassManager();

        auto ExprSym = ExitOnErr(TheJIT->lookup("__anon_expr"));
        auto *FP = ExprSym.toPtr<double (*)()>();
        fprintf(stderr, "Evaluated to %f\n", FP());

        ExitOnErr(RT->remove());
      }
      return true;
    }
    return false;
  } else {
    getNextToken();
    return false;
  }
}

static bool MainLoop(const DriverConfig &cfg) {
  bool ok = true;
  while (true) {
    switch (CurTok) {
    case tok_eof:
      return ok;
    case ';':
      getNextToken();
      break;
    case tok_def:
      ok = HandleDefinition(cfg) && ok;
      break;
    case tok_extern:
      ok = HandleExtern() && ok;
      break;
    default:
      ok = HandleTopLevelExpression(cfg) && ok;
      break;
    }
  }
}

bool runCompilerMainLoop(const DriverConfig &cfg) {
  if (cfg.print_ready_prompt)
    fprintf(stderr, "ready> ");
  getNextToken();
  InitializeModuleAndPassManager();
  return MainLoop(cfg);
}

bool emitCurrentModuleToObjectFile(const std::string &path) {
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  auto TargetTripleStr = sys::getDefaultTargetTriple();
  Triple TargetTriple(TargetTripleStr);
  TheModule->setTargetTriple(TargetTriple);

  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTripleStr, Error);

  if (!Target) {
    errs() << Error;
    return false;
  }

  auto CPU = "generic";
  auto Features = "";

  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  auto TheTargetMachine =
      Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

  TheModule->setDataLayout(TheTargetMachine->createDataLayout());

  if (StringRef Dir = sys::path::parent_path(path); !Dir.empty()) {
    if (std::error_code MK = sys::fs::create_directories(Dir)) {
      errs() << "Could not create directory " << Dir << ": " << MK.message()
             << "\n";
      return false;
    }
  }

  std::error_code EC;
  raw_fd_ostream dest(path, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Could not open file: " << EC.message();
    return false;
  }

  legacy::PassManager pass;
  auto FileType = CodeGenFileType::ObjectFile;

  if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TheTargetMachine can't emit a file of this type";
    return false;
  }

  pass.run(*TheModule);
  dest.flush();
  return true;
}

} // namespace kaleidoscope
