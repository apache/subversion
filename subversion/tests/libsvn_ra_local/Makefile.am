## Makefile.in is generated from this by automake.

#noinst_PROGRAMS = ra-local-test 
#ra_local_test_SOURCES = ra-local-test.c

COMMON_LIBS = @SVN_TESTS_MAIN_LIBS@ \
              @SVN_LIBSVN_RA_LIBS@ \
              @LIBSVN_CLIENT_DEPS@ \
              @SVN_LIBSVN_FS_LIBS@ \
              @SVN_LIBSVN_DELTA_LIBS@ \
              @SVN_LIBSVN_SUBR_LIBS@ \
              @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@ -ldb
              ## Shouldn't -ldb be added automatically by the autoconf script?

#ra_local_test_LDADD = ${COMMON_LIBS}

## Flags needed when compiling:
INCLUDES = -I$(srcdir)/.. @SVN_INCLUDES@ @SVN_APR_INCLUDES@


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic tests run by `make check` -----------------------------

## A list of test-programs to run.  (Each program contains sub-tests.)
#SVN_TESTS = ra-local-test

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



