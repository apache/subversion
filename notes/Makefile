## No particular reason to use `make' for this, just that it allows us
## to have a more generalized conversion script while still saving
## keystrokes when we want to convert subversion.txt.

all: html

html: svn-design.html

svn-design.html: 
	(cd ../src/doc/; make html; mv svn-design.html ../www/)

clean:
	rm -f *~ subversion.html
