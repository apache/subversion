## Makefile.in is generated from this by automake.

noinst_PROGRAMS = checkout-test 
## FIXME: above used to include "commit-test" as well.  But
## commit_test is commented out, due to this utterly bizarre link error:
##
## gcc -DHAVE_CONFIG_H -I. -I. -I../../.. -I../../../subversion/include -I../../../apr/include -I../../../expat-lite  -D_REENTRANT -D_POSIX_SOURCE=1  -pthread -Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations -Wpointer-arith -Wwrite-strings -Wshadow -Wredundant-decls -DSVN_DEBUG -DAP_DEBUG -g -Wall  -c commit-test.c
## /bin/sh ../../../libtool --silent --mode=link gcc  -pthread -Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations -Wpointer-arith -Wwrite-strings -Wshadow -Wredundant-decls -DSVN_DEBUG -DAP_DEBUG -g -Wall   -o commit-test  commit-test.o ../../../subversion/libsvn_delta/libsvn_delta.la ../../../subversion/libsvn_wc/libsvn_wc.la                     ../../../subversion/libsvn_subr/libsvn_subr.la ../../../subversion/tests/libsvn_test_editor.la                     ../../../apr/libapr.la  /home/kfogel/src/subversion/apr/shmem/unix/mm/libmm.la ../../../expat-lite/libexpat.la -lcrypt -lnsl  -lm -ldl
## ../../../subversion/tests/.libs/libsvn_test_editor.a(svn_test_editor.o): In function `svn_test_get_editor':
## /home/kfogel/src/subversion/subversion/tests/svn_test_editor.c:411: undefined reference to `svn_delta_default_editor'
## make[4]: *** [commit-test] Error 1

checkout_test_SOURCES = checkout-test.c

## FIXME:
## commit_test_SOURCES = commit-test.c

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_EXPAT_INCLUDES@

## Libraries this binary depends upon:
checkout_test_LDADD = @SVN_LIBSVN_WC_LIBS@ @SVN_LIBSVN_DELTA_LIBS@ \
                      @SVN_LIBSVN_SUBR_LIBS@ @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

## FIXME:
## commit_test_LDADD = @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_WC_LIBS@ \
##                    @SVN_LIBSVN_SUBR_LIBS@ @SVN_TESTS_EDITOR_LIBS@ \
##                     @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic test scripts to run for `make check`
TESTS = checkout-test.sh

## For some reason, we need to include this in the distribution
EXTRA_DIST = checkout-test.sh
