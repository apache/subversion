" $Revision$
" $Date$

" Vim setup for the Norwegian XML files in the Subversion Book.

set fenc=utf8
set tw=72
set fo+=w fo-=2
set et sw=2 ts=2 sts=2
set si
set cinw=<para>,<varlistentry>,<orderedlist>,<listitem>,<simplesect>,<chapter,<note>,<figure,<sect1,<sect2,<footnote>
set nowrap

runtime syntax/xml.vim
