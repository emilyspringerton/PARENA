/* emit_java.h — real, v0, narrow-scope Java emitter (founder real-time, 2026-08-30: "break out
 * the java emitter for PARENA", directly continuing the C-first-then-JVM/TypeScript/WebAssembly
 * multi-target roadmap this repo's own CLAUDE.md already commits to -- TypeScript shipped first
 * this same session, see emit_ts.c/STDLIB.md's own "real TypeScript-target proving ground"
 * section; this is the real, second additional target, not the JVM's own final form (a real
 * `.class`/bytecode backend is a much bigger, separate undertaking -- this emits real, honest
 * `.java` SOURCE, compiled by a real `javac` the same way emit_ts's own output is compiled by a
 * real `tsc`).
 *
 * Real, honest scope, the exact same real narrow slice emit_ts.h documents (see that file's own
 * header comment for the full real rationale -- not repeated here): a `defn` with zero or more
 * scalar (I32/F64/Bool/String) parameters, no Arena/region annotations, a body that is a SINGLE
 * real expression (number/symbol literals, the same real binop set, `if` as a ternary, calls to
 * another top-level defn or the same real, recognized `math/` primitive table emit_ts.c already
 * proves out -- `java.lang.Math` has the exact same real method names as JS's own `Math`, a real,
 * convenient overlap, not a coincidence worth re-deriving from scratch).
 *
 * Real, Java-specific structural difference from emit_ts's own shape (the one genuinely new part
 * of this file, not just a mechanical find-replace): Java has no top-level free functions -- every
 * real `.java` source file's own public top-level type must be a single class matching the
 * filename's own basename exactly (a real `javac` hard requirement, not a style convention). So
 * `emit_java` wraps every top-level `defn` from one compile invocation inside ONE real
 * `public final class <class_name> { ... }`, each becoming a `public static <RetType>
 * <camelName>(<params>) { return <expr>; }` inside it -- `class_name` is supplied by the caller
 * (main.c's own cmd_build derives it from the real output file's own basename, matching the real
 * javac constraint this exists to satisfy).
 *
 * Real, Java-specific binop difference: `=` lowers to Java's own real `==` (not TypeScript's
 * `===` -- Java has no triple-equals token at all), which gives correct real value-equality
 * semantics for every real scalar primitive type (int/double/boolean) this v0's own narrow type
 * table understands -- no boxing/reference-equality concern in this real, narrow scope.
 *
 * Deliberately NOT shared code with emit.c's own C backend OR emit_ts.c's own TypeScript backend
 * -- a real, separate, independent module (its own minimal string-builder, its own minimal
 * name-mangling), same real "each target module stays independent" discipline emit_ts.h's own
 * header comment already establishes and explains the real cost/benefit of.
 */
#ifndef PARENA_EMIT_JAVA_H
#define PARENA_EMIT_JAVA_H

#include "arena.h"
#include "ast.h"

/* emit_java walks every top-level (defn ...) form in `program` and produces a complete Java
 * source file defining one real `public final class <class_name> { ... }` (see this file's own
 * header comment for why the caller, not this function, is the real source of truth for
 * `class_name` -- it must match the real output file's own basename, a `javac` requirement this
 * function has no way to independently verify). Returns an arena-owned string on success with
 * *out_error set to NULL. On the first construct it doesn't know how to emit, returns NULL and
 * sets *out_error to an arena-owned message naming the unsupported form -- never emits partial or
 * guessed-at Java for something it doesn't actually understand, same real discipline emit_c and
 * emit_ts already established. */
const char *emit_java(Arena *arena, Node *program, const char *class_name, const char **out_error);

#endif /* PARENA_EMIT_JAVA_H */
