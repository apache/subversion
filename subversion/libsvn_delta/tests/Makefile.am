## Makefile.in is generated from this by automake.

noinst_PROGRAMS = deltaparse-test vdelta-test random-test xml-output-test \
		  svndiff-test
deltaparse_test_SOURCES = deltaparse-test.c
vdelta_test_SOURCES = vdelta-test.c
random_test_SOURCES = random-test.c
xml_output_test_SOURCES = xml-output-test.c
svndiff_test_SOURCES = svndiff-test.c

## Flags needed when compiling:
CFLAGS = @CFLAGS@ -pthread
INCLUDES = -I../../include -I../../../apr/include -I../../../expat-lite

## Libraries this binary depends upon:
deltaparse_test_LDADD = ../libsvn_delta.la \
                        ../../libsvn_subr/libsvn_subr.la \
                        ../../libsvn_string/libsvn_string.la \
                        ../../../apr/libapr.a \
                        ../../../expat-lite/libexpat.la

vdelta_test_LDADD = ../libsvn_delta.la \
                    ../../libsvn_subr/libsvn_subr.la \
                    ../../libsvn_string/libsvn_string.la \
                    ../../../apr/libapr.a \
                    ../../../expat-lite/libexpat.la

random_test_LDADD = ../libsvn_delta.la \
                    ../../libsvn_subr/libsvn_subr.la \
                    ../../libsvn_string/libsvn_string.la \
                    ../../../apr/libapr.a \
                    ../../../expat-lite/libexpat.la

xml_output_test_LDADD = ../libsvn_delta.la \
                        ../../libsvn_subr/libsvn_subr.la \
                        ../../libsvn_string/libsvn_string.la \
                        ../../../apr/libapr.a \
                        ../../../expat-lite/libexpat.la

svndiff_test_LDADD = ../libsvn_delta.la \
                     ../../libsvn_subr/libsvn_subr.la \
                     ../../libsvn_string/libsvn_string.la \
                     ../../../apr/libapr.a \
                     ../../../expat-lite/libexpat.la


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic tests run by `make check` -----------------------------

## A list of test-programs to run.  (Each program contains sub-tests.)
SVN_TESTS = random-test

## We're overriding automake's own `check' rule, because it's extremely
## inflexible;  we want better control over automated-test output. 
check:
	@echo > tests.log
	@for prog in $(SVN_TESTS); do \
	  echo; \
	  echo -n "Running all sub-tests in $$prog..."; \
	  ./$$prog >> tests.log; \
	  if [ $$? -eq 0 ]; \
	  then \
	     echo "SUCCESS"; \
	  else \
	     echo; \
	     echo "at least one sub-test FAILED, check tests.log:"; echo; \
	     cat tests.log | grep FAIL; \
	  fi; \
	done
