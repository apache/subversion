## Makefile.in is generated from this by automake.

noinst_PROGRAMS = hashdump-test stringtest target-test path-test
hashdump_test_SOURCES = hashdump-test.c
stringtest_SOURCES = stringtest.c
target_test_SOURCES = target-test.c
path_test_SOURCES = path-test.c

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Libraries this binary depends upon:
if BUILD_FS_STUFF
hashdump_test_LDADD = @SVN_TESTS_MAIN_LIBS@ \
                      @SVN_LIBSVN_FS_LIBS@ \
                      @SVN_LIBSVN_DELTA_LIBS@ \
                      @SVN_LIBSVN_SUBR_LIBS@ \
                      @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

stringtest_LDADD = @SVN_TESTS_MAIN_LIBS@ \
                   @SVN_LIBSVN_FS_LIBS@ \
                   @SVN_LIBSVN_DELTA_LIBS@ \
                   @SVN_LIBSVN_SUBR_LIBS@ \
                   @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@
else
hashdump_test_LDADD = @SVN_TESTS_MAIN_LIBS@ \
                      @SVN_LIBSVN_DELTA_LIBS@ \
                      @SVN_LIBSVN_SUBR_LIBS@ \
                      @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

stringtest_LDADD = @SVN_TESTS_MAIN_LIBS@ \
                   @SVN_LIBSVN_DELTA_LIBS@ \
                   @SVN_LIBSVN_SUBR_LIBS@ \
                   @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@
endif

target_test_LDADD = @SVN_LIBSVN_SUBR_LIBS@ @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

path_test_LDADD = @SVN_TESTS_MAIN_LIBS@ \
                  @SVN_LIBSVN_SUBR_LIBS@ \
                  @SVN_APR_LIBS@ \
                  @SVN_EXPAT_LIBS@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic tests run by `make check` -----------------------------

## A list of test-programs to run.  (Each program contains sub-tests.)
SVN_TESTS = stringtest hashdump-test $(srcdir)/target-test.sh path-test

## Give the shell script along with the distribution
EXTRA_DIST = target-test.sh

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


