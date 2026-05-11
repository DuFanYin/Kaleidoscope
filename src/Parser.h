#pragma once

#include "AST.h"

#include <map>
#include <memory>
#include <string>

namespace kaleidoscope {

extern int CurTok;
int getNextToken();

extern std::map<char, int> BinopPrecedence;

std::unique_ptr<ExprAST> LogError(const char *Str);
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str);

int GetTokPrecedence();

std::unique_ptr<FunctionAST> ParseDefinition();
std::unique_ptr<FunctionAST> ParseTopLevelExpr();
std::unique_ptr<PrototypeAST> ParseExtern();

} // namespace kaleidoscope
