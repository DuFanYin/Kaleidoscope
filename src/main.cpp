#include "Codegen.h"
#include "JIT.h"
#include "Lexer.h"
#include "Parser.h"

#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <system_error>

using namespace llvm;
using namespace llvm::sys;

static ExitOnError ExitOnErr;

namespace kaleidoscope {

/// When true, use Orc JIT (addModule / lookup) per LLVM tutorial Chapter 4.
bool UseJIT = false;

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void HandleDefinition()
{
  if (auto FnAST = ParseDefinition())
  {
    if (auto *FnIR = FnAST->codegen())
    {
      fprintf(stderr, "Read function definition:");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      if (UseJIT && TheJIT)
      {
        ExitOnErr(TheJIT->addModule(llvm::orc::ThreadSafeModule(
            std::move(TheModule), std::move(TheContext))));
        InitializeModuleAndPassManager();
      }
    }
  }
  else
  {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern()
{
  if (auto ProtoAST = ParseExtern())
  {
    if (auto *FnIR = ProtoAST->codegen())
    {
      fprintf(stderr, "Read extern: ");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  }
  else
  {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression()
{
  if (auto FnAST = ParseTopLevelExpr())
  {
    if (FnAST->codegen())
    {
      if (UseJIT && TheJIT)
      {
        // Create a ResourceTracker to track JIT'd memory for the anonymous
        // expression so we can free it after executing.
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
    }
  }
  else
  {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | ';'
static void MainLoop()
{
  while (true)
  {
    switch (CurTok)
    {
    case tok_eof:
      return;
    case ';': // ignore top-level semicolons.
      getNextToken();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X)
{
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X)
{
  fprintf(stderr, "%f\n", X);
  return 0;
}

} // namespace kaleidoscope

static void printHelp(const char *argv0)
{
  errs() << "Usage: " << argv0 << " [options]\n"
         << "  (default)  Read stdin, emit LLVM object file output.o\n"
         << "  --jit      Use in-process Orc JIT; execute top-level expressions\n"
         << "             (no output.o; IR is owned by the JIT during the run)\n"
         << "  -h, --help Show this help\n";
}

int main(int argc, char **argv)
{
  using namespace kaleidoscope;

  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "--jit") == 0)
      kaleidoscope::UseJIT = true;
    else if (std::strcmp(argv[i], "-h") == 0 ||
             std::strcmp(argv[i], "--help") == 0)
    {
      printHelp(argv[0]);
      return 0;
    }
    else
    {
      errs() << "Unknown argument: " << argv[i] << "\n";
      printHelp(argv[0]);
      return 2;
    }
  }

  // Install standard binary operators.
  // 1 is lowest precedence.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40; // highest.

  if (UseJIT)
  {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    TheJIT = ExitOnErr(llvm::orc::KaleidoscopeJIT::Create());
  }

  // Prime the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  InitializeModuleAndPassManager();

  // Run the main "interpreter loop" now.
  MainLoop();

  if (UseJIT)
  {
    TheJIT.reset();
    outs() << "(JIT run finished; no object file written.)\n";
    return 0;
  }

  // --- Object file path (default): same as tutorial Chapter 8 style driver.
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  auto TargetTripleStr = getDefaultTargetTriple();
  llvm::Triple TargetTriple(TargetTripleStr);
  TheModule->setTargetTriple(TargetTriple);

  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTripleStr, Error);

  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!Target)
  {
    errs() << Error;
    return 1;
  }

  auto CPU = "generic";
  auto Features = "";

  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  auto TheTargetMachine =
      Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

  TheModule->setDataLayout(TheTargetMachine->createDataLayout());

  auto Filename = "output.o";
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

  if (EC)
  {
    errs() << "Could not open file: " << EC.message();
    return 1;
  }

  legacy::PassManager pass;
  auto FileType = CodeGenFileType::ObjectFile;

  if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType))
  {
    errs() << "TheTargetMachine can't emit a file of this type";
    return 1;
  }

  pass.run(*TheModule);
  dest.flush();

  outs() << "Wrote " << Filename << "\n";

  return 0;
}
