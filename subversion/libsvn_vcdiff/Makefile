# todo: eventually this will be replaced with Makefile.in.

default: vdelta

# Look, I just wanna compile my test code *now*... We'll clean up later.
vdelta: hash.c vdelta.c
	gcc -c ../svn_subr/alloc.c
	gcc -c hash.c
	gcc -c vdelta.c
	gcc -o vdelta vdelta.o hash.o alloc.o


check: test

test: 
	@for num in 1 2 3 4 5; do                                     \
          echo "Test #$${num}:";                                      \
          echo "Texts:";                                              \
          cat tests/source.txt;                                       \
          echo "";                                                    \
          cat tests/target$${num}.txt;                                \
          echo "";                                                    \
          echo "";                                                    \
          echo "Delta:";                                              \
          ./vdelta tests/source.txt tests/target$${num}.txt;          \
	  echo "";                                                    \
	done


clean:
	rm -f *~ *.o vdelta
