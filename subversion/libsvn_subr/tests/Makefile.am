## Makefile.in is generated from this by automake.

noinst_PROGRAMS = hashdump-test stringtest
hashdump_test_SOURCES = hashdump-test.c
stringtest_SOURCES = stringtest.c

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Libraries this binary depends upon:
hashdump_test_LDADD = @SVN_TESTS_COMMON_MAIN_LIBS@ \
                      @SVN_LIBSVN_SUBR_LIBS@ \
                      @SVN_TESTS_COMMON_EDITOR_LIBS@ \
                      @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

stringtest_LDADD = @SVN_TESTS_COMMON_MAIN_LIBS@ \
                   @SVN_LIBSVN_SUBR_LIBS@ \
                   @SVN_TESTS_COMMON_EDITOR_LIBS@ \
                   @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic tests run by `make check` -----------------------------

## A list of test-programs to run.  (Each program contains sub-tests.)
SVN_TESTS = stringtest hashdump-test 

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


