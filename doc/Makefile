### Convert texinfo files to Info, HTML, PostScript, etc. ###

default: html

all: txt info html dvi ps pdf

www: all
	cp -f ./svn-design.html ../../www
	cp -f ./svn-design.ps ../../www
	cp -f ./svn-design.pdf ../../www

txt: 
	makeinfo --no-headers svn-design.texi > svn-design.txt

info: 
	makeinfo svn-design.texi

html: 
	makeinfo --no-headers --html svn-design.texi \
                 > svn-design.html

html-web: html
	cp ./svn-design.html ../../www/

dvi: 
	tex svn-design.texi
	tex svn-design.texi

ps: dvi
	dvips svn-design.dvi -o svn-design.ps

pdf: dvi
	/usr/local/bin/dvipdf svn-design.dvi svn-design.pdf

log:
	cvs2cl.pl -r -t

texclean: 
	rm -f *~ *.aux *.cp *.cps *.dvi *.fn *.ky *.log *.pg *.toc *.tp *.vr

clean: texclean
	rm -f *.info *.html *.ps *.pdf *.tar *.gz *.zip
