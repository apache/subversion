## Makefile.in is generated from this by automake.

noinst_PROGRAMS = ra-dav-test ra-commit
ra_dav_test_SOURCES = ra-dav-test.c

ra_commit_SOURCES = ra-commit.c

## Flags needed when compiling:
INCLUDES = -I../../include -I../../../apr/include

## Libraries this binary depends upon:
ra_dav_test_LDADD = ../libsvn_ra_dav.la                     \
                    ../../libsvn_wc/libsvn_wc.la            \
                    ../../libsvn_subr/libsvn_subr.la        \
                    ../../libsvn_delta/libsvn_delta.la      \
                    ../../../expat-lite/libexpat.la         \
                    ../../../neon/libneon.la                \
                    ../../../apr/libapr.a

## Libraries this binary depends upon:
ra_commit_LDADD = ../libsvn_ra_dav.la                     \
                    ../../libsvn_wc/libsvn_wc.la            \
                    ../../libsvn_subr/libsvn_subr.la        \
                    ../../libsvn_delta/libsvn_delta.la      \
                    ../../../expat-lite/libexpat.la         \
                    ../../../neon/libneon.la                \
                    ../../../apr/libapr.a


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic tests run by `make check` -----------------------------

## A list of test-programs to run.  (Each program contains sub-tests.)
SVN_TESTS = 

## We're overriding automake's own `check' rule, because it's extremely
## inflexible;  we want better control over automated-test output. 
check:
	@echo > tests.log
	@if test -n "$(SVN_TESTS)"; then \
	  for prog in "$(SVN_TESTS)"; do \
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
	  done \
	fi



