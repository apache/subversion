## Makefile.in is generated from this by automake.

noinst_PROGRAMS = ra-dav-test ra-commit
ra_dav_test_SOURCES = ra-dav-test.c

ra_commit_SOURCES = ra-commit.c

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Libraries this binary depends upon:
ra_dav_test_LDADD = @SVN_LIBSVN_RA_DAV_LIBS@ @SVN_LIBSVN_WC_LIBS@ \
                    @SVN_LIBSVN_SUBR_LIBS@ @SVN_LIBSVN_DELTA_LIBS@ \
                    @SVN_EXPAT_LIBS@ @SVN_NEON_LIBS@ @SVN_APR_LIBS@

## Libraries this binary depends upon:
ra_commit_LDADD = @SVN_LIBSVN_RA_DAV_LIBS@ @SVN_LIBSVN_WC_LIBS@ \
                    @SVN_LIBSVN_SUBR_LIBS@ @SVN_LIBSVN_DELTA_LIBS@ \
                    @SVN_EXPAT_LIBS@ @SVN_NEON_LIBS@ @SVN_APR_LIBS@

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



