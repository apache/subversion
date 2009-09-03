dnl check to see if SWIG is current enough.
dnl
dnl if it is, then check to see if we have the correct version of python.
dnl
dnl if we do, then set up the appropriate SWIG_ variables to build the 
dnl python bindings.

AC_DEFUN(SVN_CHECK_SWIG,
[
  AC_ARG_WITH(swig,
              AS_HELP_STRING([--with-swig=PATH],
                             [Try to use 'PATH/bin/swig' to build the
                              swig bindings.  If PATH is not specified,
                              look for a 'swig' binary in your PATH.]),
  [
    case "$withval" in
      "no")
        SWIG_SUITABLE=no
        SVN_FIND_SWIG(no)
      ;;
      "yes")
        SVN_FIND_SWIG(check)
      ;;
      *)
        SVN_FIND_SWIG($withval)
      ;;
    esac
  ],
  [
    SVN_FIND_SWIG(check)
  ])
])

AC_DEFUN(SVN_FIND_SWIG,
[
  where=$1

  if test $where = no; then
    AC_PATH_PROG(SWIG, none, none)
  elif test $where = check; then
    AC_PATH_PROG(SWIG, swig, none)
  else
    if test -f "$where"; then
      SWIG="$where"
    else
      SWIG="$where/bin/swig"
    fi
    if test ! -f "$SWIG" || test ! -x "$SWIG"; then
      AC_MSG_ERROR([Could not find swig binary at $SWIG])
    fi 
  fi

  if test "$SWIG" != "none"; then
    AC_MSG_CHECKING([swig version])
    SWIG_VERSION_RAW="`$SWIG -version 2>&1 | \
                       $SED -ne 's/^.*Version \(.*\)$/\1/p'`"
    # We want the version as an integer so we can test against
    # which version we're using.  SWIG doesn't provide this
    # to us so we have to come up with it on our own. 
    # The major is passed straight through,
    # the minor is zero padded to two places,
    # and the patch level is zero padded to three places.
    # e.g. 1.3.24 becomes 103024
    SWIG_VERSION="`echo \"$SWIG_VERSION_RAW\" | \
                  $SED -e 's/[[^0-9\.]].*$//' \
                      -e 's/\.\([[0-9]]\)$/.0\1/' \
                      -e 's/\.\([[0-9]][[0-9]]\)$/.0\1/' \
                      -e 's/\.\([[0-9]]\)\./0\1/; s/\.//g;'`"
    AC_MSG_RESULT([$SWIG_VERSION_RAW])
    # If you change the required swig version number, don't forget to update:
    #   subversion/bindings/swig/INSTALL
    #   packages/rpm/redhat-8+/subversion.spec
    #   packages/rpm/redhat-7.x/subversion.spec
    #   packages/rpm/rhel-3/subversion.spec
    #   packages/rpm/rhel-4/subversion.spec
    if test -n "$SWIG_VERSION" &&
       test "$SWIG_VERSION" -ge "103024" &&
       test "$SWIG_VERSION" -le "103036"; then
      SWIG_SUITABLE=yes
    else
      SWIG_SUITABLE=no
      AC_MSG_WARN([Detected SWIG version $SWIG_VERSION_RAW])
      AC_MSG_WARN([Subversion requires 1.3.24 or later, and is known to work])
      AC_MSG_WARN([with versions up to 1.3.36])
    fi
  fi
 
  SWIG_PY_COMPILE="none"
  SWIG_PY_LINK="none"
  if test "$PYTHON" != "none"; then
    AC_MSG_NOTICE([Configuring python swig binding])

    AC_CACHE_CHECK([for Python includes], [ac_cv_python_includes],[
      ac_cv_python_includes="`$PYTHON ${abs_srcdir}/build/get-py-info.py --includes`"
    ])
    SWIG_PY_INCLUDES="\$(SWIG_INCLUDES) $ac_cv_python_includes"

    if test "$ac_cv_python_includes" = "none"; then
      AC_MSG_WARN([python bindings cannot be built without distutils module])
    fi

    AC_CACHE_CHECK([for compiling Python extensions], [ac_cv_python_compile],[
      ac_cv_python_compile="`$PYTHON ${abs_srcdir}/build/get-py-info.py --compile`"
    ])
    SWIG_PY_COMPILE="$ac_cv_python_compile $CFLAGS"

    AC_CACHE_CHECK([for linking Python extensions], [ac_cv_python_link],[
      ac_cv_python_link="`$PYTHON ${abs_srcdir}/build/get-py-info.py --link`"
    ])
    SWIG_PY_LINK="$ac_cv_python_link"

    AC_CACHE_CHECK([for linking Python libraries], [ac_cv_python_libs],[
      ac_cv_python_libs="`$PYTHON ${abs_srcdir}/build/get-py-info.py --libs`"
    ])
    SWIG_PY_LIBS="$ac_cv_python_libs"

    dnl Sun Forte adds an extra space before substituting APR_INT64_T_FMT
    dnl gcc-2.95 adds an extra space after substituting APR_INT64_T_FMT
    dnl thus the egrep patterns have a + in them.
    SVN_PYCFMT_SAVE_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES"
    AC_CACHE_CHECK([for apr_int64_t Python/C API format string],
                   [svn_cv_pycfmt_apr_int64_t], [
      if test "x$svn_cv_pycfmt_apr_int64_t" = "x"; then
        AC_EGREP_CPP([MaTcHtHiS +\"lld\" +EnDeNd],
                     [#include <apr.h>
                      MaTcHtHiS APR_INT64_T_FMT EnDeNd],
                     [svn_cv_pycfmt_apr_int64_t="L"])
      fi
      if test "x$svn_cv_pycfmt_apr_int64_t" = "x"; then
        AC_EGREP_CPP([MaTcHtHiS +\"ld\" +EnDeNd],r
                     [#include <apr.h>
                      MaTcHtHiS APR_INT64_T_FMT EnDeNd],
                     [svn_cv_pycfmt_apr_int64_t="l"])
      fi
      if test "x$svn_cv_pycfmt_apr_int64_t" = "x"; then
        AC_EGREP_CPP([MaTcHtHiS +\"d\" +EnDeNd],
                     [#include <apr.h>
                      MaTcHtHiS APR_INT64_T_FMT EnDeNd],
                     [svn_cv_pycfmt_apr_int64_t="i"])
      fi
    ])
    CPPFLAGS="$SVN_PYCFMT_SAVE_CPPFLAGS"
    if test "x$svn_cv_pycfmt_apr_int64_t" = "x"; then
      AC_MSG_ERROR([failed to recognize APR_INT64_T_FMT on this platform])
    fi
    AC_DEFINE_UNQUOTED([SVN_APR_INT64_T_PYCFMT],
                       ["$svn_cv_pycfmt_apr_int64_t"],
                       [Define to the Python/C API format character suitable]
                       [ for apr_int64_t])
  fi

  if test "$PERL" != "none"; then
    AC_MSG_CHECKING([perl version])
    dnl Note that the q() bit is there to avoid unbalanced brackets
    dnl which m4 really doesn't like.
    PERL_VERSION="`$PERL -e 'q([[); print $]] * 1000000,$/;'`"
    AC_MSG_RESULT([$PERL_VERSION])
    if test "$PERL_VERSION" -ge "5008000"; then
      SWIG_PL_INCLUDES="\$(SWIG_INCLUDES) `$PERL -MExtUtils::Embed -e ccopts`"
    else
      AC_MSG_WARN([perl bindings require perl 5.8.0 or newer.])
    fi
  fi

  SWIG_RB_COMPILE="none"
  SWIG_RB_LINK="none"
  if test "$RUBY" != "none"; then
    rbconfig="$RUBY -rrbconfig -e "

    for var_name in arch archdir CC LDSHARED DLEXT LIBRUBYARG \
                    rubyhdrdir sitedir sitelibdir sitearchdir libdir
    do
      rbconfig_tmp=`$rbconfig "print Config::CONFIG@<:@'$var_name'@:>@"`
      eval "rbconfig_$var_name=\"$rbconfig_tmp\""
    done

    AC_MSG_NOTICE([Configuring Ruby SWIG binding])

    AC_CACHE_CHECK([for Ruby include path], [svn_cv_ruby_includes],[
    if test -d "$rbconfig_rubyhdrdir"; then
      dnl Ruby >=1.9
      svn_cv_ruby_includes="-I. -I$rbconfig_rubyhdrdir -I$rbconfig_rubyhdrdir/ruby -I$rbconfig_rubyhdrdir/ruby/backward -I$rbconfig_rubyhdrdir/$rbconfig_arch"
    else
      dnl Ruby 1.8
      svn_cv_ruby_includes="-I. -I$rbconfig_archdir"
    fi
    ])
    SWIG_RB_INCLUDES="\$(SWIG_INCLUDES) $svn_cv_ruby_includes"

    AC_CACHE_CHECK([how to compile Ruby extensions], [svn_cv_ruby_compile],[
      # Ruby doesn't like '-ansi', so strip that out of CFLAGS
      svn_cv_ruby_compile="$rbconfig_CC `echo $CFLAGS | $SED -e "s/ -ansi//g"`"
    ])
    SWIG_RB_COMPILE="$svn_cv_ruby_compile"

    AC_CACHE_CHECK([how to link Ruby extensions], [svn_cv_ruby_link],[
      svn_cv_ruby_link="`$RUBY -e 'ARGV.shift; print ARGV.join(%q( ))' \
                           $rbconfig_LDSHARED`"
      svn_cv_ruby_link="$rbconfig_CC $svn_cv_ruby_link"
      svn_cv_ruby_link="$svn_cv_ruby_link -shrext .$rbconfig_DLEXT"
    ])
    SWIG_RB_LINK="$svn_cv_ruby_link"

    AC_CACHE_CHECK([for linking Ruby libraries], [ac_cv_ruby_libs], [
      ac_cv_ruby_libs="$rbconfig_LIBRUBYARG"
    ])
    SWIG_RB_LIBS="$ac_cv_ruby_libs"

    AC_MSG_CHECKING([for rb_errinfo])
    old_CFLAGS="$CFLAGS"
    old_LIBS="$LIBS"
    CFLAGS="`echo $CFLAGS | $SED -e "s/ -ansi//g"` $svn_cv_ruby_includes"
    LIBS="$SWIG_RB_LIBS"
    AC_LINK_IFELSE([
#include <ruby.h>
int main()
{rb_errinfo();}], have_rb_errinfo="yes", have_rb_errinfo="no")
    if test "$have_rb_errinfo" = "yes"; then
      AC_MSG_RESULT([yes])
      AC_DEFINE([HAVE_RB_ERRINFO], [1],
                [Define to 1 if you have the `rb_errinfo' function.])
    else
      AC_MSG_RESULT([no])
    fi
    CFLAGS="$old_CFLAGS"
    LIBS="$old_LIBS"

    AC_CACHE_VAL([svn_cv_ruby_sitedir],[
      svn_cv_ruby_sitedir="$rbconfig_sitedir"
    ])
    AC_ARG_WITH([ruby-sitedir],
    AS_HELP_STRING([--with-ruby-sitedir=SITEDIR],
                               [install Ruby bindings in SITEDIR
                                (default is same as ruby's one)]),
    [svn_ruby_installdir="$withval"],
    [svn_ruby_installdir="$svn_cv_ruby_sitedir"])

    AC_MSG_CHECKING([where to install Ruby scripts])
    AC_CACHE_VAL([svn_cv_ruby_sitedir_libsuffix],[
      svn_cv_ruby_sitedir_libsuffix="`echo "$rbconfig_sitelibdir" | \
                                        $SED -e "s,^$rbconfig_sitedir,,"`"
    ])
    SWIG_RB_SITE_LIB_DIR="${svn_ruby_installdir}${svn_cv_ruby_sitedir_libsuffix}"
    AC_MSG_RESULT([$SWIG_RB_SITE_LIB_DIR])

    AC_MSG_CHECKING([where to install Ruby extensions])
    AC_CACHE_VAL([svn_cv_ruby_sitedir_archsuffix],[
      svn_cv_ruby_sitedir_archsuffix="`echo "$rbconfig_sitearchdir" | \
                                        $SED -e "s,^$rbconfig_sitedir,,"`"
    ])
    SWIG_RB_SITE_ARCH_DIR="${svn_ruby_installdir}${svn_cv_ruby_sitedir_archsuffix}"
    AC_MSG_RESULT([$SWIG_RB_SITE_ARCH_DIR])

    AC_MSG_CHECKING([how to use output level for Ruby bindings tests])
    AC_CACHE_VAL([svn_cv_ruby_test_verbose],[
      svn_cv_ruby_test_verbose="normal"
    ])
    AC_ARG_WITH([ruby-test-verbose],
    AS_HELP_STRING([--with-ruby-test-verbose=LEVEL],
                               [how to use output level for Ruby bindings tests
                                (default is normal)]),
    [svn_ruby_test_verbose="$withval"],
                  [svn_ruby_test_verbose="$svn_cv_ruby_test_verbose"])
      SWIG_RB_TEST_VERBOSE="$svn_ruby_test_verbose"
      AC_MSG_RESULT([$SWIG_RB_TEST_VERBOSE])
  fi
  AC_SUBST(SWIG)
  AC_SUBST(SWIG_PY_INCLUDES)
  AC_SUBST(SWIG_PY_COMPILE)
  AC_SUBST(SWIG_PY_LINK)
  AC_SUBST(SWIG_PY_LIBS)
  AC_SUBST(SWIG_PL_INCLUDES)
  AC_SUBST(SWIG_RB_LINK)
  AC_SUBST(SWIG_RB_LIBS)
  AC_SUBST(SWIG_RB_INCLUDES)
  AC_SUBST(SWIG_RB_COMPILE)
  AC_SUBST(SWIG_RB_SITE_LIB_DIR)
  AC_SUBST(SWIG_RB_SITE_ARCH_DIR)
  AC_SUBST(SWIG_RB_TEST_VERBOSE)
])
