# S223-02 scoping pass: `let`/`match`/`loop` in expression position

## Status

Scoping only. No code changes follow from this doc alone. S223-01 (the clean rejection) is
shipped and stays as the current real behavior until a founder decision picks one of the options
below.

## What S223-01 already does

`src/emit.c`'s expression dispatcher explicitly rejects `let`/`match`/`loop` when they appear
somewhere `emit_expr` is called instead of `emit_body` — most commonly an `if`'s own condition,
e.g. `(if (let [x 1] (= x 1)) 1 0)`. The rejection message tells the caller to bind the result to
a name in an enclosing `let` first, then reference that name — a real, working manual pattern,
already used by hand three times this session alone (`v16/parser.prn`, `dot11.prn`'s `find-ssid-ie`
sourcing pattern, `net/proxy`).

## Why this isn't just a syntactic rewrite

`emit_if` (lines ~1419-1447) builds its C output as ONE ternary expression string, by calling
`emit_expr` on the condition/then/else and joining the results with a `StrBuf`. `emit_expr`'s
whole architecture is pure-expression-in, expression-string-out — it has no statement-emission
capability at all. `let`/`match`/`loop` are fundamentally statement-shaped (they need to declare a
temp, run some logic, then yield a value), so embedding one inside a pure expression string needs
a real mechanism for smuggling statements into expression position.

The obvious naive approach — a GNU C statement-expression, `({ Type tmp = ...; tmp; })` — is
already known to be blocked by this project's own `-pedantic -Werror` build flag, per a real,
previously-documented finding in `stdlib/datetime.prn`'s own header comment. `-pedantic` isn't
something this doc proposes carving out: it's this project's own existing correctness bar, and a
narrow exception for one feature just moves the risk elsewhere.

## The real precedent already in this file

`g_boxed_types`/`g_box_helpers` (lines ~492-527) solves a structurally similar problem for Ok/Err
boxing: rather than hoisting a statement into the expression, it generates a real
`static inline TypeName *TypeName_box(Arena *dest, TypeName v)` helper function once per distinct
type, collects the generated bodies in a separate buffer, and splices them into the output before
any `defn` body. The call site stays a plain, valid C99 function call — `NDArray_box(dest,
NDArray_new(...))` — composing exactly like anything else `emit_expr` already produces. That
file's own comment names the tradeoff directly: hoisting a temp declaration into the enclosing
statement would require `emit_expr` to grow real statement-emission capability everywhere it's
called from — "a much larger change" than a generated helper.

## Why this precedent doesn't transfer cleanly

The boxing helper's only "capture" is its own value parameter — there's no free-variable problem,
because the helper's entire body is the boxing logic itself, self-contained.

A `let`/`match`/`loop` used as an `if`'s own condition is different: its body can reference
*arbitrary outer-scope locals* — anything already bound in the enclosing `defn`, `let` chain, or
`match` clause. A synthesized helper function for this case needs real free-variable capture
analysis: walk the binding form's body AST, find every identifier reference, determine which ones
resolve to an outer `EmitScope` binding (not one newly introduced inside the binding form itself),
and pass each one as a parameter to the synthesized helper (or, if `#target`/arena-taking locals
are involved, thread the right pointer/value form through, matching `EmitScope`'s own existing
`is_arena_value` distinction).

Two real gaps found while investigating, neither of which exists yet anywhere in this codebase:

1. **No generic "walk all descendant identifiers in a subtree" utility.** AST traversal in this
   file is spread across `emit_expr`'s ~5000-line dispatch, one case per node shape, each already
   doing its own thing with the nodes it touches. A capture pass needs to correctly recurse
   through every expression shape (nested `let`s, `match` clause patterns and guards, lambda
   params, quoted forms, `#target` strings, etc.) while correctly *excluding* names the binding
   form itself introduces (its own `let` bindings, `match` clause pattern variables, nested
   `loop` params) so those aren't mistaken for outer-scope captures.
2. **No parameter-passing convention for a variable-arity synthesized function.** The boxing
   helper always takes exactly the boxed value; a capture-based helper's parameter list depends on
   what the specific `let`/`match`/`loop` body actually references — real, per-call-site
   generation, not a fixed shape.

## Why this is worth scoping instead of attempting

Getting free-variable capture wrong doesn't fail loud. Missing a captured variable, or resolving
one to the wrong `EmitScope` binding, produces C that still compiles — it just reads or mutates
the wrong value, or a variable that's out of scope by the time the helper runs. That's a silent
wrong-codegen bug in the compiler itself, categorically worse than S223-01's current honest
compile-time rejection. Given `EmitScope`'s existing `scope_lookup`/`scope_bind` machinery has no
enumeration primitive to build this on top of safely, a blind implementation risks landing exactly
that failure mode in a widely-used part of the emitter.

## Options for a founder decision

- **Option A — narrow first phase.** Support only `let` (not `match`/`loop` yet) directly in
  `if`-condition position, with capture analysis restricted to a single non-nested `let` body (no
  further `let`/`match`/`lambda` nesting inside it, which would need recursive scope tracking).
  Smaller, real, testable surface; still needs the new AST-walk capture primitive from scratch.
- **Option B — full generality.** All three forms, arbitrarily nested, anywhere `emit_expr` is
  called. Correct long-term fix, substantially larger and higher-risk; likely deserves its own
  design review of the capture-analysis approach before any code lands, not just this doc.
- **Option C — don't build it.** The existing "extract to a real named function, bind its result
  with an enclosing `let`" workaround is a proven, safe, already-used-three-times-this-session
  pattern. S223-01's rejection message already teaches it directly at the point of failure. This
  option keeps the compiler's correctness bar exactly where it is and spends the effort elsewhere.

No option is picked by this doc. S223-01 stays as the shipped, correct-if-unglamorous current
state either way.
