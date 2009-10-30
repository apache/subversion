"Show in a new window the Subversion log for the revision number under the
"  cursor.
:function s:svnLog()
   let rev = expand("<cword>")
   " recognise "rN" as well as plain "N"
   let rev = substitute(rev, "^r", "", "")
   if rev
     botright 15new
     exec '%!svn log "^/" -r ' . rev
     normal gg
     setlocal nomodified readonly buftype=nofile nowrap
   else
     echo "svnLog: no revision number under cursor"
   endif
:endfunction
:map gl :call <SID>svnLog()<CR>
