## No particular reason to use `make' for this, just that it allows us
## to have a more generalized conversion script while still saving
## keystrokes when we want to convert inversion.txt.

all: html

html: inversion.html

inversion.html: inversion.txt
	./outline2html.pl inversion.txt

clean:
	rm -f *~ inversion.html
