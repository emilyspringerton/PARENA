" Vim syntax file for PARENA (.prn)
" Language: PARENA -- https://github.com/emilyspringerton/PARENA
" Keyword/type/builtin lists are grounded directly in this repo's own real
" lexer (src/lexer.c) and emitter (src/emit.c) -- every symbol highlighted
" below is one this compiler's own source actually special-cases somewhere,
" not guessed from the language's own docs. See NORTHSTAR.md/STDLIB.md for
" what each form means; this file only teaches vim to *see* them.

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syntax case match

" ---- Comments -- src/lexer.c: ';' or ';;' both run to end of line, the
" same real Lisp-family convention (no block-comment form exists).
syntax match parenaComment ";.*$" contains=@Spell,parenaTodo
syntax keyword parenaTodo contained TODO FIXME XXX NOTE

" ---- Strings -- double-quoted, backslash-escaped.
syntax region parenaString start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=parenaEscape,@Spell
syntax match parenaEscape contained +\\[\\"nrt0]+

" ---- Numbers -- src/lexer.c: optional leading -/+, digits, optional
" '.'-plus-digits for a real float (no exponent form, no hex/octal/binary
" literal support exists in this lexer today).
syntax match parenaNumber "\v<[-+]?\d+(\.\d+)?>"

" ---- Region/reference sigils -- PARENA-specific, not ordinary Lisp:
" `@ Region` (compile-time region annotation) and `&Type`/`&mut Type`
" (reference types, resolve_declared_type()'s own real vocabulary in
" src/emit.c). Highlighted distinctly since these carry PARENA's actual
" memory-safety semantics, not just decoration.
syntax match parenaRegionAt "@" nextgroup=parenaRegionName skipwhite
syntax match parenaRegionName "\v:region/[A-Za-z0-9_-]+" contained
syntax match parenaRefSigil "&mut\>"
syntax match parenaRefSigil "&[A-Za-z_][A-Za-z0-9_-]*"

" ---- Inline-C escape hatch -- src/emit.c's own "#target/inline-c FFI
" escape mechanism" (CLAUDE.md/STDLIB.md both name it), the one place a
" .prn file drops to real, verbatim C.
syntax match parenaTarget "#target\>"

" ---- Keywords (colon-prefixed symbols) -- src/lexer.c: "a colon on its
" own (followed by ...) is a keyword: :region/scratch". Covers ordinary
" map-literal keys (:data, :length) and namespaced region markers alike;
" parenaRegionName above already claims the :region/... shape specifically,
" so this is the general fallback for every other :keyword.
syntax match parenaKeyword "\v:[A-Za-z_][A-Za-z0-9_/-]*[!?]?"

" ---- Special forms -- every symbol src/emit.c's own is_call_named()/
" is_symbol() checks by exact name (defn, match, loop, etc: the language's
" real, closed special-form set as VS0 actually implements it, not a
" larger guessed Lisp-family list).
syntax keyword parenaSpecialForm defn defstruct defenum defmacro module import export
syntax keyword parenaSpecialForm let match loop recur cond do if fn when
syntax keyword parenaSpecialForm set! deref return unwrap get-field alloc with-arena

" ---- Built-in types -- resolve_declared_type()/resolve_base_type_name()'s
" own real recognized vocabulary (src/emit.c), plus Region/Raw (region-
" system and raw-buffer types used throughout stdlib/*.prn).
syntax keyword parenaType I32 F64 Bool String Unit Arena Vec Map Result Option Region Raw

" ---- Built-in constants/constructors -- Result/Option's own real,
" hardcoded variant names (Ok/Err/Some/None), plus the two Bool literals.
syntax keyword parenaConstant true false Ok Err Some None

" ---- Delimiters -- left plain/default on purpose (no bracket-rainbow
" here); grouped only so a colorscheme that wants to style them can.
syntax match parenaDelimiter "[()\[\]{}]"

highlight default link parenaComment      Comment
highlight default link parenaTodo         Todo
highlight default link parenaString       String
highlight default link parenaEscape       SpecialChar
highlight default link parenaNumber       Number
highlight default link parenaRegionAt     Operator
highlight default link parenaRegionName   Identifier
highlight default link parenaRefSigil     Operator
highlight default link parenaTarget       PreProc
highlight default link parenaKeyword      Constant
highlight default link parenaSpecialForm  Keyword
highlight default link parenaType         Type
highlight default link parenaConstant     Boolean
highlight default link parenaDelimiter    Delimiter

let b:current_syntax = "parena"

let &cpo = s:cpo_save
unlet s:cpo_save
