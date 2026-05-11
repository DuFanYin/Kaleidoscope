#include "frontend/Lexer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace kaleidoscope {

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

std::string IdentifierStr; // Filled in if tok_identifier
double NumVal;              // Filled in if tok_number
std::int64_t IntLitVal;    // Filled in if tok_int_lit

namespace {

enum class LexerSource { Stdin, String };

LexerSource SourceMode = LexerSource::Stdin;
std::string StringBuffer;
size_t StringPos = 0;
int LastChar = ' ';

int readNextChar() {
  if (SourceMode == LexerSource::String) {
    if (StringPos >= StringBuffer.size())
      return EOF;
    return static_cast<unsigned char>(StringBuffer[StringPos++]);
  }
  return getchar();
}

} // namespace

void setLexerStdinSource() {
  SourceMode = LexerSource::Stdin;
  LastChar = ' ';
}

void setLexerStringSource(std::string_view source) {
  SourceMode = LexerSource::String;
  StringBuffer.assign(source.begin(), source.end());
  StringPos = 0;
  LastChar = ' ';
}

/// gettok - Return the next token from the configured source.
int gettok()
{
  // Skip any whitespace.
  while (isspace(LastChar))
    LastChar = readNextChar();

  if (isalpha(LastChar))
  { // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = readNextChar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    if (IdentifierStr == "if")
      return tok_if;
    if (IdentifierStr == "then")
      return tok_then;
    if (IdentifierStr == "else")
      return tok_else;
    if (IdentifierStr == "for")
      return tok_for;
    if (IdentifierStr == "in")
      return tok_in;
    if (IdentifierStr == "while")
      return tok_while;
    if (IdentifierStr == "true")
      return tok_true;
    if (IdentifierStr == "false")
      return tok_false;
    if (IdentifierStr == "binary")
      return tok_binary;
    if (IdentifierStr == "unary")
      return tok_unary;
    if (IdentifierStr == "var")
      return tok_var;
    if (IdentifierStr == "break")
      return tok_break;
    if (IdentifierStr == "continue")
      return tok_continue;
    if (IdentifierStr == "and")
      return tok_and;
    if (IdentifierStr == "or")
      return tok_or;
    if (IdentifierStr == "not")
      return tok_not;
    if (IdentifierStr == "int")
      return tok_kw_int;
    if (IdentifierStr == "double")
      return tok_kw_double;
    if (IdentifierStr == "bool")
      return tok_kw_bool;
    return tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.')
  { // Number: integer literal or floating [0-9]+ ('.' [0-9]*)?
    std::string NumStr;
    if (LastChar == '.')
    {
      NumStr += LastChar;
      LastChar = readNextChar();
      if (!isdigit(LastChar))
        return '.'; // lone dot: unknown token
      do
      {
        NumStr += LastChar;
        LastChar = readNextChar();
      } while (isdigit(LastChar));
      NumVal = strtod(NumStr.c_str(), nullptr);
      return tok_number;
    }
    do
    {
      NumStr += LastChar;
      LastChar = readNextChar();
    } while (isdigit(LastChar));
    if (LastChar == '.')
    {
      NumStr += LastChar;
      LastChar = readNextChar();
      while (isdigit(LastChar))
      {
        NumStr += LastChar;
        LastChar = readNextChar();
      }
      NumVal = strtod(NumStr.c_str(), nullptr);
      return tok_number;
    }
    IntLitVal = static_cast<std::int64_t>(strtoll(NumStr.c_str(), nullptr, 10));
    NumVal = static_cast<double>(IntLitVal);
    return tok_int_lit;
  }

  if (LastChar == '#')
  {
    // Comment until end of line.
    do
      LastChar = readNextChar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = readNextChar();
  return ThisChar;
}

} // namespace kaleidoscope
