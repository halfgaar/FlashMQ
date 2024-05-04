" Vim syntax file
" Language:             FlashMQ configuration file

"
" Goals:
"
" Make incorrect / unknown options stand out. Special attention is given to
" also have this work correctly for blocks, E.G., the toplevel directive
" `log_file` is colored in a global scope but not when it's used inside a
" `listen` block
"
" https://vimdoc.sourceforge.net/htmldoc/syntax.html
"
" TODO:
" - Test number of arguments, specifically: most options take 1 argument, but
"   fe. bridge__subscribe takes an optional second argument
" - Deal with quoted options?
"

if exists("b:current_syntax")
  finish
endif
let b:current_syntax = "flashmq"

syn region  fmqComment               display oneline start='^\s*#' end='$'

" We "render" fmqWrongBlock as Error (which makes it red), but then also use
" transparent which makes it be the color of the parent, so it all becomes a
" neutral color.
" Without the transparent+Error trick you would see fmqTopLevelDirective
" highlighted inside blocks which is undesirable: you can't specify `log_file`
" inside a `listen` block, so it shouldn't get colorized.
" 
" Real blocks (like `listen` and `bridge`) are defined later and thus get a
" higher priority, and will replace this match.
syn region fmqWrongBlock  start=+^.*{+ end=+}+ transparent contains=NONE,fmqComment

hi link fmqComment              Comment
hi link fmqWrongBlock           Error
hi link fmqTopLevelDirective    Type

" The rest of this file has been dynamically generated
