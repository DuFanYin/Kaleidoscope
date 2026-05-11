#include "frontend/AST.h"
#include "frontend/Types.h"
#include "backend/Codegen.h"
#include "backend/JIT.h"
#include "frontend/Parser.h"
#include "backend/passes/Passes.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
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

#include <cassert>
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
std::map<std::string, KalType> NamedVarTypes;
/// When true, codegen uses IEEE double arithmetic (possibly mixed i32 locals).
/// When false, pure i32 arithmetic and conditions.
static bool CurFnUsesFloat = true;
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
static AllocaInst *CreateEntryBlockAllocaTyped(Function *TheFunction,
                                               StringRef VarName, KalType T)
{
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(llvmTypeForKal(T, *TheContext), nullptr, VarName);
}

static Value *literalZeroForFn()
{
  if (CurFnUsesFloat)
    return ConstantFP::get(*TheContext, APFloat(0.0));
  return ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
}

/// Coerce Value V to elemental type SlotTy for store.
static Value *coerceForStore(Value *V, Type *SlotTy)
{
  if (V->getType() == SlotTy)
    return V;
  if (SlotTy->isDoubleTy() && V->getType()->isIntegerTy(32))
    return Builder->CreateSIToFP(V, SlotTy);
  if (SlotTy->isIntegerTy(32) && V->getType()->isDoubleTy())
    return Builder->CreateFPToSI(V, SlotTy);
  (void)LogErrorV("internal: cannot coerce types for assignment");
  return nullptr;
}

static Value *coerceForCall(Value *V, KalType ParmTy)
{
  return coerceForStore(V, llvmTypeForKal(ParmTy, *TheContext));
}

/// Ensure return value matches the function's LLVM return type.
static Value *coerceForReturn(Value *V, KalType RetTy)
{
  Type *Want = llvmTypeForKal(RetTy, *TheContext);
  if (V->getType() == Want)
    return V;
  if (Want->isDoubleTy() && V->getType()->isIntegerTy(32))
    return Builder->CreateSIToFP(V, Want);
  if (Want->isIntegerTy(32) && V->getType()->isDoubleTy())
    return Builder->CreateFPToSI(V, Want);
  (void)LogErrorV("internal: cannot coerce return type");
  return nullptr;
}


Value *NumberExprAST::codegen()
{
  if (!CurFnUsesFloat)
  {
    if (!IsIntegral)
      return LogErrorV(
          "floating-point literal not allowed in int-only function — use "
          "integer literals (no `.`) or true/false");
    return ConstantInt::get(Type::getInt32Ty(*TheContext), APInt(32, IntVal, true));
  }
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen()
{
  Value *VP = NamedValues[Name];
  if (!VP)
    return LogErrorV("Unknown variable name");

  auto ItTy = NamedVarTypes.find(Name);
  KalType Ty = ItTy == NamedVarTypes.end() ? KalType::Double : ItTy->second;
  Type *ElemTy = llvmTypeForKal(Ty, *TheContext);
  Value *Loaded =
      Builder->CreateLoad(ElemTy, VP, Name.c_str());
  if (CurFnUsesFloat && isIntegerLike(Ty))
    return Builder->CreateSIToFP(
        Loaded, Type::getDoubleTy(*TheContext), "itof");
  return Loaded;
}

Value *UnaryExprAST::codegen()
{
  if (!CurFnUsesFloat)
    return LogErrorV("unary ops are only supported when using double arithmetic");

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

  if (CurFnUsesFloat)
  {
    Value *Zero = ConstantFP::get(*TheContext, APFloat(0.0));
    Value *IsZero = Builder->CreateFCmpOEQ(V, Zero, "notcmp");
    return Builder->CreateUIToFP(IsZero, Type::getDoubleTy(*TheContext), "notdbl");
  }

  Value *ZeroI = ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
  Value *IsZero = Builder->CreateICmpEQ(V, ZeroI, "noticmp");
  return Builder->CreateZExt(IsZero, Type::getInt32Ty(*TheContext), "not32");
}

Value *BreakExprAST::codegen() {
  if (LoopStack.empty())
    return LogErrorV("break outside loop");
  Builder->CreateBr(LoopStack.back().BreakDest);
  ActiveLoopControl = CFGTokenKind::Break;
  return literalZeroForFn();
}

Value *ContinueExprAST::codegen() {
  if (LoopStack.empty())
    return LogErrorV("continue outside loop");
  Builder->CreateBr(LoopStack.back().ContinueDest);
  ActiveLoopControl = CFGTokenKind::Continue;
  return literalZeroForFn();
}

Value *LogicalAndExprAST::codegen() {
  if (!CurFnUsesFloat)
  {
    Function *TheFunction = Builder->GetInsertBlock()->getParent();
    Value *L = LHS->codegen();
    if (!L)
      return nullptr;
    Value *Zero = ConstantInt::get(Type::getInt32Ty(*TheContext), 0);

    BasicBlock *RhsBB =
        BasicBlock::Create(*TheContext, "land.rhs.i", TheFunction);
    BasicBlock *MergeBB =
        BasicBlock::Create(*TheContext, "land.end.i", TheFunction);
    BasicBlock *LHSEnd = Builder->GetInsertBlock();

    Value *Ltruth =
        Builder->CreateICmpNE(L, Zero, "land.icmp lhs");
    Builder->CreateCondBr(Ltruth, RhsBB, MergeBB);

    Builder->SetInsertPoint(RhsBB);
    Value *R = RHS->codegen();
    if (!R)
      return nullptr;
    Value *Rtruth =
        Builder->CreateICmpNE(R, Zero, "land.icmp rhs");
    Value * Rz = Builder->CreateZExt(
        Rtruth, Type::getInt32Ty(*TheContext), "land32rhs");
    Builder->CreateBr(MergeBB);
    BasicBlock *RhsEnd = Builder->GetInsertBlock();

    Builder->SetInsertPoint(MergeBB);
    PHINode *PN = Builder->CreatePHI(Type::getInt32Ty(*TheContext), 2, "landphi.i");
    PN->addIncoming(Zero, LHSEnd);
    PN->addIncoming(Rz, RhsEnd);
    return PN;
  }

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

  if (!CurFnUsesFloat)
  {
    Value *L = LHS->codegen();
    if (!L)
      return nullptr;
    Value *Zero = ConstantInt::get(Type::getInt32Ty(*TheContext), 0);

    BasicBlock *TrueBB =
        BasicBlock::Create(*TheContext, "lor.true.i", TheFunction);
    BasicBlock *RhsBB =
        BasicBlock::Create(*TheContext, "lor.rhs.i", TheFunction);
    BasicBlock *MergeBB =
        BasicBlock::Create(*TheContext, "lor.end.i", TheFunction);

    Value *Ltruth =
        Builder->CreateICmpNE(L, Zero, "lor.icmp lhs");
    Builder->CreateCondBr(Ltruth, TrueBB, RhsBB);

    Builder->SetInsertPoint(TrueBB);
    Value *One =
        ConstantInt::get(Type::getInt32Ty(*TheContext), 1);
    Builder->CreateBr(MergeBB);
    BasicBlock *TrueEnd = Builder->GetInsertBlock();

    Builder->SetInsertPoint(RhsBB);
    Value *R = RHS->codegen();
    if (!R)
      return nullptr;
    Value *Rtruth =
        Builder->CreateICmpNE(R, Zero, "lor.icmp rhs");
    Value *Rz =
        Builder->CreateZExt(Rtruth, Type::getInt32Ty(*TheContext), "lor32rhs");
    Builder->CreateBr(MergeBB);
    BasicBlock *RhsEnd = Builder->GetInsertBlock();

    Builder->SetInsertPoint(MergeBB);
    PHINode *PN = Builder->CreatePHI(Type::getInt32Ty(*TheContext), 2, "lorphi.i");
    PN->addIncoming(One, TrueEnd);
    PN->addIncoming(Rz, RhsEnd);
    return PN;
  }

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
    VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
    if (!LHSE)
      return LogErrorV("destination of '=' must be a variable");
    Value *Val = RHS->codegen();
    if (!Val)
      return nullptr;

    Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogErrorV("Unknown variable name");

    KalType DstTy = KalType::Double;
    if (auto It = NamedVarTypes.find(LHSE->getName()); It != NamedVarTypes.end())
      DstTy = It->second;

    Type *SlotTy = llvmTypeForKal(DstTy, *TheContext);
    auto *Coerced = coerceForStore(Val, SlotTy);
    if (!Coerced)
      return nullptr;
    Builder->CreateStore(Coerced, Variable);
    return Coerced;
  }

  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  if (CurFnUsesFloat)
  {
    switch (Op)
    {
    case '+':
      return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
      return Builder->CreateFSub(L, R, "subtmp");
    case '*':
      return Builder->CreateFMul(L, R, "multmp");
    case '/':
      return Builder->CreateFDiv(L, R, "divtmp");
    case '<': {
      Value *C = Builder->CreateFCmpULT(L, R, "cmptmp");
      return Builder->CreateUIToFP(C, Type::getDoubleTy(*TheContext), "booltmp");
    }
    default:
      break;
    }
    Function *F = getFunction(std::string("binary") + Op);
    assert(F && "binary operator not found!");
    Value *Ops[] = {L, R};
    return Builder->CreateCall(F, Ops, "binop");
  }

  switch (Op)
  {
  case '+':
    return Builder->CreateNSWAdd(L, R, "iadd");
  case '-':
    return Builder->CreateNSWSub(L, R, "isub");
  case '*':
    return Builder->CreateNSWMul(L, R, "imul");
  case '/':
    return Builder->CreateSDiv(L, R, "idiv");
  case '<': {
    Value *Cmp = Builder->CreateICmpSLT(L, R, "icmp");
    return Builder->CreateZExt(Cmp, Type::getInt32Ty(*TheContext), "icmp32");
  }
  default:
    break;
  }

  Function *F = getFunction(std::string("binary") + Op);
  if (!F)
    return LogErrorV("binary operator not found!");
  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binopi");
}

Value *CallExprAST::codegen()
{
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i)
  {
    KalType Pt = KalType::Double;
    if (auto FI = FunctionProtos.find(Callee); FI != FunctionProtos.end())
    {
      if (i < FI->second->getArgCount())
        Pt = FI->second->getArgType(i);
    }
    Value *AV = Args[i]->codegen();
    if (!AV)
      return nullptr;
    AV = coerceForCall(AV, Pt);
    if (!AV)
      return nullptr;
    ArgsV.push_back(AV);
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::codegen()
{
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  Type *AggTy =
      CurFnUsesFloat ? Type::getDoubleTy(*TheContext)
                     : Type::getInt32Ty(*TheContext);
  Value *CondBr =
      CurFnUsesFloat
          ? Builder->CreateFCmpONE(
                CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond")
          : Builder->CreateICmpNE(
                CondV, ConstantInt::get(Type::getInt32Ty(*TheContext), 0),
                "ificmp");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);

  Builder->CreateCondBr(CondBr, ThenBB, ElseBB);

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
    return literalZeroForFn();
  if (Incomings.size() == 1)
    return Incomings[0].first;

  PHINode *PN =
      Builder->CreatePHI(AggTy, Incomings.size(), "iftmp");
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

  KalType LoopVarTy =
      CurFnUsesFloat ? KalType::Double : KalType::Int32;
  Type *LoopEltTy = llvmTypeForKal(LoopVarTy, *TheContext);
  AllocaInst *Alloca =
      CreateEntryBlockAllocaTyped(TheFunction, VarName, LoopVarTy);

  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;
  StartVal = coerceForStore(StartVal, LoopEltTy);
  if (!StartVal)
    return nullptr;

  Builder->CreateStore(StartVal, Alloca);

  BasicBlock *BodyBB = BasicBlock::Create(*TheContext, "for.body", TheFunction);
  BasicBlock *StepBB = BasicBlock::Create(*TheContext, "for.step", TheFunction);
  BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "afterloop", TheFunction);

  Builder->CreateBr(BodyBB);

  LoopTargets LTargets{StepBB, AfterBB};
  LoopStack.push_back(LTargets);

  AllocaInst *OldVal = NamedValues.count(VarName) ? NamedValues[VarName] : nullptr;
  bool HadOldTy = NamedVarTypes.count(VarName);
  KalType OldTyStored = HadOldTy ? NamedVarTypes[VarName] : KalType::Double;

  NamedVarTypes[VarName] = LoopVarTy;
  NamedValues[VarName] = Alloca;

  auto restoreLoopVarBinding = [&]()
  {
    if (OldVal)
      NamedValues[VarName] = OldVal;
    else
      NamedValues.erase(VarName);
    if (HadOldTy)
      NamedVarTypes[VarName] = OldTyStored;
    else
      NamedVarTypes.erase(VarName);
  };

  Builder->SetInsertPoint(BodyBB);
  ActiveLoopControl = CFGTokenKind::None;

  if (!Body->codegen()) {
    restoreLoopVarBinding();
    LoopStack.pop_back();
    return nullptr;
  }

  ActiveLoopControl = CFGTokenKind::None;

  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(StepBB);

  Builder->SetInsertPoint(StepBB);

  Value *StepVal = nullptr;
  if (Step)
  {
    StepVal = Step->codegen();
    if (!StepVal)
    {
      restoreLoopVarBinding();
      LoopStack.pop_back();
      return nullptr;
    }
    StepVal = coerceForStore(StepVal, LoopEltTy);
    if (!StepVal)
    {
      restoreLoopVarBinding();
      LoopStack.pop_back();
      return nullptr;
    }
  }
  else
  {
    if (LoopEltTy->isDoubleTy())
      StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
    else
      StepVal = ConstantInt::get(Type::getInt32Ty(*TheContext), 1);
  }

  Value *EndCond = End->codegen();
  if (!EndCond)
  {
    restoreLoopVarBinding();
    LoopStack.pop_back();
    return nullptr;
  }

  Value *CurVar = Builder->CreateLoad(LoopEltTy, Alloca, VarName.c_str());
  Value *NextVar =
      LoopEltTy->isDoubleTy()
          ? Builder->CreateFAdd(CurVar, StepVal, "nextvar")
          : Builder->CreateNSWAdd(CurVar, StepVal, "nextvar.i");

  Builder->CreateStore(NextVar, Alloca);

  EndCond =
      CurFnUsesFloat
          ? Builder->CreateFCmpONE(
                EndCond, ConstantFP::get(*TheContext, APFloat(0.0)),
                "loopcond")
          : Builder->CreateICmpNE(
                EndCond,
                ConstantInt::get(Type::getInt32Ty(*TheContext), 0),
                "loopcond.i");

  Builder->CreateCondBr(EndCond, BodyBB, AfterBB);

  LoopStack.pop_back();

  restoreLoopVarBinding();

  Builder->SetInsertPoint(AfterBB);

  return literalZeroForFn();
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
  CondV =
      CurFnUsesFloat
          ? Builder->CreateFCmpONE(
                CondV, ConstantFP::get(*TheContext, APFloat(0.0)),
                "whilecond")
          : Builder->CreateICmpNE(
                CondV,
                ConstantInt::get(Type::getInt32Ty(*TheContext), 0),
                "whilecond.i");
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
  return literalZeroForFn();
}

Value *VarExprAST::codegen()
{
  struct OldBind {
    AllocaInst *Ptr;
    KalType Ty;
    bool HadTy;
  };
  std::vector<OldBind> OldBindings;
  OldBindings.reserve(VarNames.size());

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (unsigned i = 0, e = VarNames.size(); i < e; ++i)
  {
    const auto &VN = VarNames[i];
    const std::string &VarName = std::get<0>(VN);
    KalType VTy = std::get<1>(VN);
    ExprAST *Init = std::get<2>(VN).get();

    OldBind OB{nullptr, KalType::Double, false};
    if (auto It = NamedValues.find(VarName); It != NamedValues.end())
      OB.Ptr = It->second;
    if (auto It = NamedVarTypes.find(VarName); It != NamedVarTypes.end())
    {
      OB.Ty = It->second;
      OB.HadTy = true;
    }
    OldBindings.push_back(OB);

    assert(Init != nullptr &&
           "`var` must have been parsed with `= initializer`");
    Value *InitVal = Init->codegen();
    if (!InitVal)
      return nullptr;

    Type *SlotTy = llvmTypeForKal(VTy, *TheContext);
    InitVal = coerceForStore(InitVal, SlotTy);
    if (!InitVal)
      return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAllocaTyped(TheFunction, VarName, VTy);
    Builder->CreateStore(InitVal, Alloca);

    NamedValues[VarName] = Alloca;
    NamedVarTypes[VarName] = VTy;
  }

  Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  for (unsigned i = 0, e = VarNames.size(); i < e; ++i)
  {
    const std::string &VarName = std::get<0>(VarNames[i]);
    OldBind OB = OldBindings[i];
    if (!OB.Ptr)
      NamedValues.erase(VarName);
    else
      NamedValues[VarName] = OB.Ptr;
    if (OB.HadTy)
      NamedVarTypes[VarName] = OB.Ty;
    else
      NamedVarTypes.erase(VarName);
  }

  return BodyVal;
}

Function *PrototypeAST::codegen()
{
  std::vector<Type *> ParamTys;
  ParamTys.reserve(Args.size());
  for (const auto &A : Args)
    ParamTys.push_back(llvmTypeForKal(A.second, *TheContext));

  FunctionType *FT =
      FunctionType::get(llvmTypeForKal(RetTy, *TheContext), ParamTys, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++].first);

  return F;
}

Function *FunctionAST::codegen()
{
  clearCodegenLoopState();

  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  CurFnUsesFloat = (P.getReturnType() == KalType::Double);
  unsigned na = P.getArgCount();

  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  NamedValues.clear();
  NamedVarTypes.clear();

  unsigned Idx = 0;
  for (auto &Arg : TheFunction->args())
  {
    KalType Ty = Idx < na ? P.getArgType(Idx) : KalType::Double;
    std::string N = Arg.getName().str();
    AllocaInst *Alloca = CreateEntryBlockAllocaTyped(TheFunction, N, Ty);

    NamedVarTypes[N] = Ty;
    NamedValues[N] = Alloca;

    Builder->CreateStore(&Arg, Alloca);
    ++Idx;
  }

  if (Value *RetVal = Body->codegen())
  {
    Value *Adjusted = coerceForReturn(RetVal, P.getReturnType());
    if (!Adjusted)
    {
      TheFunction->eraseFromParent();
      if (P.isBinaryOp())
        BinopPrecedence.erase(P.getOperatorName());
      return nullptr;
    }
    Builder->CreateRet(Adjusted);

    verifyFunction(*TheFunction);

    OptFunctionPasses->run(*TheFunction, *TheFAM);

    return TheFunction;
  }

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
