## Makefile.in is generated from this by automake.

## automake's "default" target is just "info".  
## However, it also knows how to make "dvi".

## The main design document
info_TEXINFOS = svn-design.texi

## The design document's dependencies
svn_design_TEXINFOS = architecture.texi	\
                      model.texi        \
                      client.texi       \
                      deltas.texi       \
                      protocol.texi     \
                      future.texi	\
                      server.texi       \
                      goals.texi	\
                      license.texi


## Weird, automake has no built-in rule for `ps'
svn-design.ps:  svn-design.dvi
	dvips svn-design.dvi -o svn-design.ps

ps:  svn-design.ps

## Some of our own targets;  should we we test for these tools?

## Global goals, for different docs

everything:  info dvi ps pdf txt html

txt:  svn-design.txt
html:  svn-design.html
pdf:  svn-design.pdf

svn-design.txt: 
	makeinfo --no-headers svn-design.texi > svn-design.txt

svn-design.html: 
	makeinfo --no-headers --html svn-design.texi > svn-design.html

svn-design.pdf: svn-design.dvi
	dvipdf svn-design.dvi svn-design.pdf

ChangeLog:
	cvs2cl.pl -r -t

log:  ChangeLog


## For publishing the changed doc to the website:

www: html ps pdf
	cp -f ./svn-design.html ../www
	cp -f ./svn-design.ps ../www
	cp -f ./svn-design.pdf ../www
