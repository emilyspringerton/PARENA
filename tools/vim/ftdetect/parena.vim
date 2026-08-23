" ftdetect/parena.vim -- .prn is PARENA source (S-expression syntax), not
" any of the other real "PRN" file formats vim already guesses at (printer
" spool files, MS-DOS PRN port redirection). Real PARENA source lives under
" stdlib/, examples/, tests/ in this repo, all real .prn files -- see
" README.md's own "closed VS0" status note for what actually compiles today.
au BufRead,BufNewFile *.prn set filetype=parena
