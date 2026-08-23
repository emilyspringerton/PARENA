# PARENA vim syntax highlighting

Founder real-time ask (2026-08-23): "also we need syntax highlighting for vim parnea ty".

## Install

Copy (or symlink) this directory's contents into your vim runtime path:

```bash
mkdir -p ~/.vim/ftdetect ~/.vim/syntax
cp tools/vim/ftdetect/parena.vim ~/.vim/ftdetect/
cp tools/vim/syntax/parena.vim   ~/.vim/syntax/
```

Or, with a plugin manager that supports arbitrary runtimepath directories
(vim-plug, packer, etc.), point it at `tools/vim` directly — it already
follows the standard `ftdetect/` + `syntax/` runtime layout, no
plugin-specific manifest needed.

Neovim: same two files under `~/.config/nvim/ftdetect/` and
`~/.config/nvim/syntax/` (or `stdpath('config')` equivalent).

## What it covers

Grounded directly in this repo's own real lexer (`src/lexer.c`) and emitter
(`src/emit.c`) — every highlighted symbol is one this compiler actually
special-cases somewhere, not a guessed larger Lisp-family keyword list:

- Comments (`;`/`;;` to end of line), strings, numbers
- Special forms: `defn` `defstruct` `defenum` `defmacro` `module` `import`
  `export` `let` `match` `loop` `recur` `cond` `do` `if` `fn` `when`
  `set!` `deref` `return` `unwrap` `get-field` `alloc` `with-arena`
- Built-in types: `I32` `F64` `Bool` `String` `Unit` `Arena` `Vec` `Map`
  `Result` `Option` `Region` `Raw`
- Built-in constants: `true` `false` `Ok` `Err` `Some` `None`
- PARENA-specific syntax no stock `lisp.vim` covers: `@ Region` region
  annotations, `&Type`/`&mut Type` reference sigils, `:keyword` /
  `:region/name` keyword-symbols, the `#target` inline-C escape hatch

Not covered (real, honest scope limit, same as everywhere else in this
repo): no indentation file, no folding, no LSP/semantic highlighting —
this is a plain regex-based `syntax/` file, matching what an equivalent
`lisp.vim` gives you for any other Lisp-family language.
