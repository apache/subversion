## Makefile.in is generated from this by automake.

noinst_PROGRAMS = deltaparse-test vdelta-test random-test xml-output-test \
		  svndiff-test
deltaparse_test_SOURCES = deltaparse-test.c
vdelta_test_SOURCES = vdelta-test.c
random_test_SOURCES = random-test.c
xml_output_test_SOURCES = xml-output-test.c
svndiff_test_SOURCES = svndiff-test.c

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_EXPAT_INCLUDES@

## Libraries this binary depends upon:
if BUILD_FS_STUFF
deltaparse_test_LDADD = @SVN_TESTS_MAIN_LIBS@ @SVN_LIBSVN_FS_LIBS@ \
                        @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_SUBR_LIBS@ \
                        @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@
else
deltaparse_test_LDADD = @SVN_TESTS_MAIN_LIBS@ @SVN_LIBSVN_DELTA_LIBS@ \
                        @SVN_LIBSVN_SUBR_LIBS@ @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@
endif

vdelta_test_LDADD = @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_SUBR_LIBS@ \
                    @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

random_test_LDADD = @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_SUBR_LIBS@ \
                    @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

xml_output_test_LDADD = @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_SUBR_LIBS@ \
                        @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

svndiff_test_LDADD = @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_SUBR_LIBS@ \
                     @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

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
