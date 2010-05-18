" diff-to-logmsg.vim by Ph. Marek <philipp.marek@bmlv.gv.at>
" 
" Usage is as follows:
"
"   diff -urp subversion.orig subversion.mine > patch
" or 
"   svn diff --diff-cmd=diff --extensions="-up" [TARGET...] > patch
"
" (the -p tells diff to output the function names), then
"
"   vi patch
"   :source diff_to_logmsg.vim
"
" and voila!  Just the documentation has to be written.
"
" Note from Julian Foad:
"   It ought to be noted that the generated list of function names
"   is only as accurate as the output of "diff -p", which is not very
"   accurate - e.g. for changes to a doc string appearing before a 
"   function, it generally outputs the name of the _previous_ function.


" goto start of patch and insert the header (until the .)
:0
insert
[[[


]]]


.


" search for file and function names and put them before the ]]]
:g/^\(---\|@@\)/normal ""yygg/]]]kk""p


" change the copied lines to the wanted scheme
:0
:1;/]]]/ s#--- \([^\t ]\+\).\+#\r* \1#e
:1;/]]]/ s#@@ .\+ @@.*\<\(\w\+\) *(.*#  (\1): #e
" all lines without function names are ignored
:1;/]]]/ g#@@ .\+#normal dd
" remove duplicates
:1;/]]]/ !uniq
