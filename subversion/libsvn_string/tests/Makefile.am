## Makefile.in is generated from this by automake.

noinst_PROGRAMS = stringtest
stringtest_SOURCES = stringtest.c

## Flags needed when compiling:
CFLAGS = -pthread
INCLUDES = -I../../include -I../../../apr/include

## Libraries this binary depends upon:
stringtest_LDADD = ../libsvn_string.la                              \
                   ../../tests-common/libsvn_tests_main.la          \
                   ../../../apr/libapr.a


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic tests run by `make check` -----------------------------

## A list of test-programs to run.  (Each program contains sub-tests.)
SVN_TESTS = stringtest

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




