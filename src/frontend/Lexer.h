#pragma once

#include <cstdint>
#include <string>

#include <string_view>

namespace kaleidoscope {

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,

  // control
  tok_if = -6,
  tok_then = -7,
  tok_else = -8,
  tok_for = -9,
  tok_in = -10,
  tok_while = -11,
  tok_true = -12,
  tok_false = -13,

  // operators
  tok_binary = -14,
  tok_unary = -15,

  // var definition
  tok_var = -16,

  tok_break = -17,
  tok_continue = -18,
  tok_and = -19,
  tok_or = -20,
  tok_not = -21,

  // built-in types
  tok_kw_int = -22,
  tok_kw_double = -23,
  tok_kw_bool = -24,

  tok_int_lit = -25,
};

extern std::string IdentifierStr; // Filled in if tok_identifier
extern double NumVal;               // Filled in if tok_number
extern std::int64_t IntLitVal;      // Filled in if tok_int_lit

int gettok();

/// Read tokens from stdin (default).
void setLexerStdinSource();

/// Read tokens from an in-memory buffer (e.g. embed API). Replaces stdin mode.
void setLexerStringSource(std::string_view source);

} // namespace kaleidoscope
