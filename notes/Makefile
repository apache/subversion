## No particular reason to use `make' for this, just that it allows us
## to have a more generalized conversion script while still saving
## keystrokes when we want to convert subversion.txt.

all: html

html: subversion.html subversion.txt

subversion.html: subversion.txt
	./outline2html.pl subversion.txt
	mv -f subversion.html ../www/

clean:
	rm -f *~ subversion.html
