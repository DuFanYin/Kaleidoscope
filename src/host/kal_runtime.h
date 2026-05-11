/* Kaleidoscope host runtime (C ABI).
 *
 * Symbols below are the built-in "kal_rt" surface: declare them in Kal code as
 *   extern printd(x:double);
 *   extern putchard(x:double);
 * and provide these definitions when linking AOT object code (see src/runtime.c).
 *
 * The compiler binary also links the same implementation so Orc JIT resolves
 * them from the current process without an extra library.
 *
 * Calling convention: every parameter and return value is double (IEEE-754),
 * as in the LLVM Kaleidoscope tutorial (Kal source still requires `:double`).
 */
#ifndef KAL_RUNTIME_H
#define KAL_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/** Write one byte (low 8 bits of X) to stderr; returns 0. */
double putchard(double X);

/** Print X as "%f\\n" on stderr; returns 0. */
double printd(double X);

#ifdef __cplusplus
}
#endif

#endif /* KAL_RUNTIME_H */
