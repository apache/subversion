## Makefile.in is generated from this by automake.

noinst_PROGRAMS = ra-dav-test
ra_dav_test_SOURCES = ra-dav-test.c

## Flags needed when compiling:
CFLAGS = -pthread
INCLUDES = -I../../include -I../../../apr/include

## Libraries this binary depends upon:
ra_dav_test_LDADD =   ../../tests-common/libsvn_tests_main.la   \
                      ../../libsvn_string/libsvn_string.la      \
                      ../../../expat-lite/libexpat.la           \
                      ../../../neon/libneon.la                  \
                      ../../../apr/libapr.a


## Automatic tests run by `make check` -----------------------------

## A list of test-programs to run.  (Each program contains sub-tests.)
SVN_TESTS = ra-dav-test

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


