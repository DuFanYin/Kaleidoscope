#include "embed/Embed.h"
#include "backend/Codegen.h"
#include "driver/Driver.h"
#include "backend/JIT.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>

using namespace llvm;

namespace kaleidoscope::embed {

static ExitOnError ExitOnErr;

Result compileFromSource(std::string_view source, const Options &opt) {
  Result R;

  installDefaultBinaryOperatorPrecedence();
  FunctionProtos.clear();
  NamedValues.clear();

  TheJIT.reset();
  if (opt.jit) {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    TheJIT = ExitOnErr(llvm::orc::KaleidoscopeJIT::Create());
  }

  setLexerStringSource(source);

  kaleidoscope::DriverConfig D;
  D.use_jit = opt.jit;
  D.print_ready_prompt = false;
  const bool parseAndCodegenOK = runCompilerMainLoop(D);

  if (opt.jit) {
    TheJIT.reset();
    setLexerStdinSource();
    R.ok = parseAndCodegenOK;
    R.message = parseAndCodegenOK ? "JIT run finished."
                                  : "JIT run finished with compile errors.";
    return R;
  }

  if (!parseAndCodegenOK) {
    setLexerStdinSource();
    R.message = "compilation had parse/codegen errors";
    return R;
  }

  if (!emitCurrentModuleToObjectFile(opt.object_path)) {
    setLexerStdinSource();
    R.message = "failed to emit object file";
    return R;
  }

  R.ok = true;
  std::ostringstream OS;
  OS << "Wrote " << opt.object_path;
  R.message = OS.str();
  llvm::outs() << R.message << "\n";
  setLexerStdinSource();
  return R;
}

} // namespace kaleidoscope::embed
