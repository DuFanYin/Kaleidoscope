#include "frontend/AST.h"
#include "backend/Codegen.h"
#include "backend/JIT.h"
#include "frontend/Parser.h"
#include "backend/passes/Passes.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

#include "llvm/IR/CFG.h"

#include <vector>

using namespace llvm;

namespace kaleidoscope {

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
std::unique_ptr<LLVMContext> TheContext;
std::unique_ptr<Module> TheModule;
std::unique_ptr<IRBuilder<>> Builder;
std::unique_ptr<ModuleAnalysisManager> TheMAM;
std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
std::unique_ptr<LoopAnalysisManager> TheLAM;
std::unique_ptr<FunctionAnalysisManager> TheFAM;
std::unique_ptr<FunctionPassManager> OptFunctionPasses;
std::map<std::string, AllocaInst *> NamedValues;
std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

//===----------------------------------------------------------------------===//
// Loop break/continue
//===----------------------------------------------------------------------===//

enum class CFGTokenKind { None, Break, Continue };
static CFGTokenKind ActiveLoopControl = CFGTokenKind::None;

struct LoopTargets {
  BasicBlock *ContinueDest;
  BasicBlock *BreakDest;
};
static std::vector<LoopTargets> LoopStack;

static void clearCodegenLoopState() {
  ActiveLoopControl = CFGTokenKind::None;
  LoopStack.clear();
}

static bool blockBranchesTo(BasicBlock *BB, BasicBlock *Target) {
  Instruction *TI = BB->getTerminator();
  if (!TI)
    return false;
  for (BasicBlock *Succ : successors(TI))
    if (Succ == Target)
      return true;
  return false;
}

Value *LogErrorV(const char *Str)
{
  LogError(Str);
  return nullptr;
}

Function *getFunction(std::string Name)
{
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}

/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          StringRef VarName)
{
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}

Value *NumberExprAST::codegen()
{
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen()
{
  // Look this variable up in the function.
  Value *V = NamedValues[Name];
  if (!V)
    return LogErrorV("Unknown variable name");

  // Load the value.
  return Builder->CreateLoad(Type::getDoubleTy(*TheContext), V, Name.c_str());
}

Value *UnaryExprAST::codegen()
{
  Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, OperandV, "unop");
}

Value *NotExprAST::codegen() {
  Value *V = Operand->codegen();
  if (!V)
    return nullptr;
  Value *Zero = ConstantFP::get(*TheContext, APFloat(0.0));
  Value *IsZero = Builder->CreateFCmpOEQ(V, Zero, "notcmp");
  return Builder->CreateUIToFP(IsZero, Type::getDoubleTy(*TheContext), "notdbl");
}

Value *BreakExprAST::codegen() {
  if (LoopStack.empty())
    return LogErrorV("break outside loop");
  Builder->CreateBr(LoopStack.back().BreakDest);
  ActiveLoopControl = CFGTokenKind::Break;
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *ContinueExprAST::codegen() {
  if (LoopStack.empty())
    return LogErrorV("continue outside loop");
  Builder->CreateBr(LoopStack.back().ContinueDest);
  ActiveLoopControl = CFGTokenKind::Continue;
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *LogicalAndExprAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  Value *L = LHS->codegen();
  if (!L)
    return nullptr;

  Value *Zero = ConstantFP::get(*TheContext, APFloat(0.0));
  Value *Ltruth = Builder->CreateFCmpONE(L, Zero, "landlhs");

  BasicBlock *RhsBB = BasicBlock::Create(*TheContext, "land.rhs", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "land.end", TheFunction);
  BasicBlock *LHSEnd = Builder->GetInsertBlock();

  Builder->CreateCondBr(Ltruth, RhsBB, MergeBB);

  Builder->SetInsertPoint(RhsBB);
  Value *R = RHS->codegen();
  if (!R)
    return nullptr;
  Value *Rtruth = Builder->CreateFCmpONE(R, Zero, "landrhs");
  Value *Rdbl =
      Builder->CreateUIToFP(Rtruth, Type::getDoubleTy(*TheContext), "landrhsdbl");
  Builder->CreateBr(MergeBB);
  BasicBlock *RhsEnd = Builder->GetInsertBlock();

  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "landphi");
  PN->addIncoming(Zero, LHSEnd);
  PN->addIncoming(Rdbl, RhsEnd);
  return PN;
}

Value *LogicalOrExprAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  Value *L = LHS->codegen();
  if (!L)
    return nullptr;

  Value *Zero = ConstantFP::get(*TheContext, APFloat(0.0));
  Value *One = ConstantFP::get(*TheContext, APFloat(1.0));
  Value *Ltruth = Builder->CreateFCmpONE(L, Zero, "lorlhs");

  BasicBlock *TrueBB = BasicBlock::Create(*TheContext, "lor.true", TheFunction);
  BasicBlock *RhsBB = BasicBlock::Create(*TheContext, "lor.rhs", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "lor.end", TheFunction);

  Builder->CreateCondBr(Ltruth, TrueBB, RhsBB);

  Builder->SetInsertPoint(TrueBB);
  Builder->CreateBr(MergeBB);
  BasicBlock *TrueEnd = Builder->GetInsertBlock();

  Builder->SetInsertPoint(RhsBB);
  Value *R = RHS->codegen();
  if (!R)
    return nullptr;
  Value *Rtruth = Builder->CreateFCmpONE(R, Zero, "lorrhs");
  Value *Rdbl =
      Builder->CreateUIToFP(Rtruth, Type::getDoubleTy(*TheContext), "lorrhsdbl");
  Builder->CreateBr(MergeBB);
  BasicBlock *RhsEnd = Builder->GetInsertBlock();

  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "lorphi");
  PN->addIncoming(One, TrueEnd);
  PN->addIncoming(Rdbl, RhsEnd);
  return PN;
}

Value *BinaryExprAST::codegen()
{
  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=')
  {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
    if (!LHSE)
      return LogErrorV("destination of '=' must be a variable");
    // Codegen the RHS.
    Value *Val = RHS->codegen();
    if (!Val)
      return nullptr;

    // Look up the name.
    Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogErrorV("Unknown variable name");

    Builder->CreateStore(Val, Variable);
    return Val;
  }

  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op)
  {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  Function *F = getFunction(std::string("binary") + Op);
  assert(F && "binary operator not found!");

  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen()
{
  // Look up the name in the global module table.
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i)
  {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::codegen()
{
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  Builder->SetInsertPoint(ThenBB);

  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;

  BasicBlock *ThenEnd = Builder->GetInsertBlock();
  if (!ThenEnd->getTerminator())
    Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(ElseBB);

  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;

  BasicBlock *ElseEnd = Builder->GetInsertBlock();
  if (!ElseEnd->getTerminator())
    Builder->CreateBr(MergeBB);

  SmallVector<std::pair<Value *, BasicBlock *>, 2> Incomings;
  if (blockBranchesTo(ThenEnd, MergeBB))
    Incomings.push_back({ThenV, ThenEnd});
  if (blockBranchesTo(ElseEnd, MergeBB))
    Incomings.push_back({ElseV, ElseEnd});

  Builder->SetInsertPoint(MergeBB);
  if (Incomings.empty())
    return ConstantFP::get(*TheContext, APFloat(0.0));
  if (Incomings.size() == 1)
    return Incomings[0].first;

  PHINode *PN =
      Builder->CreatePHI(Type::getDoubleTy(*TheContext), Incomings.size(), "iftmp");
  for (auto &Incoming : Incomings)
    PN->addIncoming(Incoming.first, Incoming.second);
  return PN;
}

// Output for-loop as:
//   var = alloca double
//   ...
//   start = startexpr
//   store start -> var
//   goto loop
// loop:
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   endcond = endexpr
//
//   curvar = load var
//   nextvar = curvar + step
//   store nextvar -> var
//   br endcond, loop, endloop
// outloop:
Value *ForExprAST::codegen()
{
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;

  Builder->CreateStore(StartVal, Alloca);

  BasicBlock *BodyBB = BasicBlock::Create(*TheContext, "for.body", TheFunction);
  BasicBlock *StepBB = BasicBlock::Create(*TheContext, "for.step", TheFunction);
  BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "afterloop", TheFunction);

  Builder->CreateBr(BodyBB);

  LoopTargets LTargets{StepBB, AfterBB};
  LoopStack.push_back(LTargets);

  AllocaInst *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Alloca;

  Builder->SetInsertPoint(BodyBB);
  ActiveLoopControl = CFGTokenKind::None;

  if (!Body->codegen()) {
    if (OldVal)
      NamedValues[VarName] = OldVal;
    else
      NamedValues.erase(VarName);
    LoopStack.pop_back();
    return nullptr;
  }

  ActiveLoopControl = CFGTokenKind::None;

  // If the body ends on a merge block (e.g. if/else where else breaks), that
  // block still needs a successor; break/continue already set a terminator on
  // their own blocks.
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(StepBB);

  Builder->SetInsertPoint(StepBB);

  Value *StepVal = nullptr;
  if (Step)
  {
    StepVal = Step->codegen();
    if (!StepVal)
    {
      if (OldVal)
        NamedValues[VarName] = OldVal;
      else
        NamedValues.erase(VarName);
      LoopStack.pop_back();
      return nullptr;
    }
  }
  else
  {
    StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
  }

  Value *EndCond = End->codegen();
  if (!EndCond)
  {
    if (OldVal)
      NamedValues[VarName] = OldVal;
    else
      NamedValues.erase(VarName);
    LoopStack.pop_back();
    return nullptr;
  }

  Value *CurVar = Builder->CreateLoad(Type::getDoubleTy(*TheContext), Alloca,
                                      VarName.c_str());
  Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
  Builder->CreateStore(NextVar, Alloca);

  EndCond = Builder->CreateFCmpONE(
      EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");

  Builder->CreateCondBr(EndCond, BodyBB, AfterBB);

  LoopStack.pop_back();

  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  Builder->SetInsertPoint(AfterBB);

  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *WhileExprAST::codegen()
{
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  BasicBlock *CondBB = BasicBlock::Create(*TheContext, "while.cond", TheFunction);
  BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "while.body", TheFunction);
  BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "while.end", TheFunction);

  Builder->CreateBr(CondBB);

  Builder->SetInsertPoint(CondBB);
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "whilecond");
  Builder->CreateCondBr(CondV, LoopBB, AfterBB);

  LoopStack.push_back({CondBB, AfterBB});

  Builder->SetInsertPoint(LoopBB);
  ActiveLoopControl = CFGTokenKind::None;
  if (!Body->codegen()) {
    LoopStack.pop_back();
    return nullptr;
  }

  ActiveLoopControl = CFGTokenKind::None;

  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(CondBB);

  LoopStack.pop_back();

  Builder->SetInsertPoint(AfterBB);
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *VarExprAST::codegen()
{
  std::vector<AllocaInst *> OldBindings;

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Register all variables and emit their initializer.
  for (unsigned i = 0, e = VarNames.size(); i < e; ++i)
  {
    const std::string &VarName = VarNames[i].first;
    ExprAST *Init = VarNames[i].second.get();

    // Emit the initializer before adding the variable to scope, this prevents
    // the initializer from referencing the variable itself, and permits stuff
    // like this:
    //  var a = 1 in
    //    var a = a in ...   # refers to outer 'a'.
    Value *InitVal;
    if (Init)
    {
      InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
    }
    else
    { // If not specified, use 0.0.
      InitVal = ConstantFP::get(*TheContext, APFloat(0.0));
    }

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);

    // Remember the old variable binding so that we can restore the binding when
    // we unrecurse.
    OldBindings.push_back(NamedValues[VarName]);

    // Remember this binding.
    NamedValues[VarName] = Alloca;
  }

  // Codegen the body, now that all vars are in scope.
  Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  // Pop all our variables from scope.
  for (unsigned i = 0, e = VarNames.size(); i < e; ++i)
    NamedValues[VarNames[i].first] = OldBindings[i];

  // Return the body computation.
  return BodyVal;
}

Function *PrototypeAST::codegen()
{
  // Make the function type:  double(double,double) etc.
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

Function *FunctionAST::codegen()
{
  clearCodegenLoopState();

  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // If this is an operator, install it.
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
  {
    // Create an alloca for this variable.
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

    // Store the initial value into the alloca.
    Builder->CreateStore(&Arg, Alloca);

    // Add arguments to variable symbol table.
    NamedValues[std::string(Arg.getName())] = Alloca;
  }

  if (Value *RetVal = Body->codegen())
  {
    // Finish off the function.
    Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // New PM: run scalar pipeline on this function (Mem2Reg + custom + LLVM passes).
    OptFunctionPasses->run(*TheFunction, *TheFAM);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();

  if (P.isBinaryOp())
    BinopPrecedence.erase(P.getOperatorName());
  return nullptr;
}

void InitializeModuleAndPassManager()
{
  // Open a new module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("my cool jit", *TheContext);

  // Create a new builder for the module.
  Builder = std::make_unique<IRBuilder<>>(*TheContext);

  // When using Orc JIT, match the JIT data layout (tutorial Chapter 4).
  if (TheJIT)
    TheModule->setDataLayout(TheJIT->getDataLayout());

  // New Pass Manager: wire analysis managers (required for GVN etc.).
  TheMAM = std::make_unique<ModuleAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerCGSCCAnalyses(*TheCGAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.registerLoopAnalyses(*TheLAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);

  OptFunctionPasses = std::make_unique<FunctionPassManager>();
  OptFunctionPasses->addPass(PromotePass());
  OptFunctionPasses->addPass(KaleidoscopeAlgebraicSimplifyPass());
  OptFunctionPasses->addPass(InstCombinePass());
  OptFunctionPasses->addPass(ReassociatePass());
  OptFunctionPasses->addPass(GVNPass());
  OptFunctionPasses->addPass(SimplifyCFGPass());
}

} // namespace kaleidoscope
