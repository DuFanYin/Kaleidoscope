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
double NumVal;             // Filled in if tok_number

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
    if (IdentifierStr == "binary")
      return tok_binary;
    if (IdentifierStr == "unary")
      return tok_unary;
    if (IdentifierStr == "var")
      return tok_var;
    return tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.')
  { // Number: [0-9.]+
    std::string NumStr;
    do
    {
      NumStr += LastChar;
      LastChar = readNextChar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
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
