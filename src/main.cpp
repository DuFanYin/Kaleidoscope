#include "driver/Driver.h"
#include "backend/Codegen.h"
#include "backend/JIT.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>

using namespace llvm;

static void printHelp(const char *argv0) {
  errs() << "Usage: " << argv0 << " [options]\n"
         << "  (default)  Read stdin, emit LLVM object file build/output.o\n"
         << "  --jit      Use in-process Orc JIT; execute top-level expressions\n"
         << "             (no object file; IR is owned by the JIT during the run)\n"
         << "  -h, --help Show this help\n";
}

int main(int argc, char **argv) {
  using namespace kaleidoscope;

  bool use_jit = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--jit") == 0)
      use_jit = true;
    else if (std::strcmp(argv[i], "-h") == 0 ||
             std::strcmp(argv[i], "--help") == 0) {
      printHelp(argv[0]);
      return 0;
    } else {
      errs() << "Unknown argument: " << argv[i] << "\n";
      printHelp(argv[0]);
      return 2;
    }
  }

  setLexerStdinSource();
  installDefaultBinaryOperatorPrecedence();

  static ExitOnError ExitOnErr;
  TheJIT.reset();
  if (use_jit) {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    TheJIT = ExitOnErr(llvm::orc::KaleidoscopeJIT::Create());
  }

  DriverConfig cfg;
  cfg.use_jit = use_jit;
  cfg.print_ready_prompt = true;
  const bool parseAndCodegenOK = runCompilerMainLoop(cfg);

  if (use_jit) {
    TheJIT.reset();
    outs() << "(JIT run finished; no object file written.)\n";
    return parseAndCodegenOK ? 0 : 1;
  }

  if (!parseAndCodegenOK)
    return 1;

  const std::string Filename = "build/output.o";
  if (!emitCurrentModuleToObjectFile(Filename))
    return 1;

  outs() << "Wrote " << Filename << "\n";
  return 0;
}
