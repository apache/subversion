"Show in a new window the Subversion blame annotation for the current file.
"  Problem: when there are local mods this doesn't align with the source file.
"  To do: When invoked on a revnum in a Blame window, re-blame same file up to previous rev.
:function s:svnBlame()
   let line = line(".")
   setlocal nowrap
   aboveleft 18vnew
   setlocal nomodified readonly buftype=nofile nowrap winwidth=1
   NoSpaceHi
   " blame, ignoring white space changes
   %!svn blame -x-w "#"
   " find the highest revision number and highlight it
   "%!sort -n
   "normal G*u
   " return to original line
   exec "normal " . line . "G"
   setlocal scrollbind
   wincmd p
   setlocal scrollbind
   syncbind
:endfunction
:map gb :call <SID>svnBlame()<CR>
:command Blame call s:svnBlame()

