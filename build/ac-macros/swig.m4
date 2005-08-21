dnl check to see if SWIG is current enough.
dnl
dnl if it is, then check to see if we have the correct version of python.
dnl
dnl if we do, then set up the appropriate SWIG_ variables to build the 
dnl python bindings.

AC_DEFUN(SVN_CHECK_SWIG,
[
  AC_ARG_WITH(swig,
              AC_HELP_STRING([--with-swig=PATH],
                             [Try to use 'PATH/bin/swig' to build the
                              swig bindings.  If PATH is not specified,
                              look for a 'swig' binary in your PATH.]),
  [
    case "$withval" in
      "no")
        SWIG_SUITABLE=no
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
  SWIG_SUITABLE=yes
  SWIG_PY_COMPILE="none"
  SWIG_PY_LINK="none"
  if test "$PYTHON" != "none" -a "$SWIG_SUITABLE" = "yes"; then
    AC_MSG_NOTICE([Configuring python swig binding])
    SWIG_CLEAN_RULES="$SWIG_CLEAN_RULES clean-swig-py" 

    AC_CACHE_CHECK([for Python includes], [ac_cv_python_includes],[
      ac_cv_python_includes="`$PYTHON ${abs_srcdir}/build/get-py-info.py --includes`"
    ])
    SWIG_PY_INCLUDES="\$(SWIG_INCLUDES) $ac_cv_python_includes"

    AC_CACHE_CHECK([for compiling Python extensions], [ac_cv_python_compile],[
      ac_cv_python_compile="`$PYTHON ${abs_srcdir}/build/get-py-info.py --compile`"
    ])
    SWIG_PY_COMPILE="$ac_cv_python_compile"

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

  if test "$PERL" != "none" -a "$SWIG_SUITABLE" = "yes"; then
    AC_MSG_CHECKING([perl version])
    dnl Note that the q() bit is there to avoid unbalanced brackets
    dnl which m4 really doesn't like.
    PERL_VERSION="`$PERL -e 'q([[); print $]] * 1000000,$/;'`"
    AC_MSG_RESULT([$PERL_VERSION])
    if test "$PERL_VERSION" -ge "5008000"; then
      SWIG_CLEAN_RULES="$SWIG_CLEAN_RULES clean-swig-pl" 
      SWIG_PL_INCLUDES="\$(SWIG_INCLUDES) `$PERL -MExtUtils::Embed -e ccopts`"
    else
      AC_MSG_WARN([perl bindings require perl 5.8.0 or newer.])
    fi
  fi

  SWIG_RB_COMPILE="none"
  SWIG_RB_LINK="none"
  if test "$RUBY" != "none" -a \
        "$SWIG_SUITABLE" = "yes"; then

    AC_MSG_NOTICE([Configuring Ruby SWIG binding])
    SWIG_CLEAN_RULES="$SWIG_CLEAN_RULES clean-swig-rb" 

    AC_CACHE_CHECK([for Ruby include path], [svn_cv_ruby_includes],[
    svn_cv_ruby_includes="-I. -I`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(archdir))'`"
    ])
    SWIG_RB_INCLUDES="\$(SWIG_INCLUDES) $svn_cv_ruby_includes"

    AC_CACHE_CHECK([how to compile Ruby extensions], [svn_cv_ruby_compile],[
      svn_cv_ruby_compile="`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(CC)), %q( ), Config::CONFIG.fetch(%q(CFLAGS))'` \$(SWIG_RB_INCLUDES)"
    ])
    SWIG_RB_COMPILE="$svn_cv_ruby_compile"

    AC_CACHE_CHECK([how to link Ruby extensions], [svn_cv_ruby_link],[
      svn_cv_ruby_link="`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(LDSHARED)).sub(/^\S+/, Config::CONFIG.fetch(%q(CC)) + %q( -shrext .) + Config::CONFIG.fetch(%q(DLEXT)))'`"
    ])
    SWIG_RB_LINK="$svn_cv_ruby_link"

    AC_CACHE_VAL([svn_cv_ruby_sitedir],[
      svn_cv_ruby_sitedir="`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(sitedir))'`"
    ])
    AC_ARG_WITH([ruby-sitedir],
    AC_HELP_STRING([--with-ruby-sitedir=SITEDIR],
                               [install Ruby bindings in SITEDIR
                                (default is same as ruby's one)]),
    [svn_ruby_installdir="$withval"],
    [svn_ruby_installdir="$svn_cv_ruby_sitedir"])

    AC_MSG_CHECKING([where to install Ruby scripts])
    AC_CACHE_VAL([svn_cv_ruby_sitedir_libsuffix],[
      svn_cv_ruby_sitedir_libsuffix="`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(sitelibdir)).sub(/^#{Config::CONFIG.fetch(%q(sitedir))}/, %q())'`"
    ])
    SWIG_RB_SITE_LIB_DIR="${svn_ruby_installdir}${svn_cv_ruby_sitedir_libsuffix}"
    AC_MSG_RESULT([$SWIG_RB_SITE_LIB_DIR])

    AC_MSG_CHECKING([where to install Ruby extensions])
    AC_CACHE_VAL([svn_cv_ruby_sitedir_archsuffix],[
      svn_cv_ruby_sitedir_archsuffix="`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(sitearchdir)).sub(/^#{Config::CONFIG.fetch(%q(sitedir))}/, %q())'`"
    ])
    SWIG_RB_SITE_ARCH_DIR="${svn_ruby_installdir}${svn_cv_ruby_sitedir_archsuffix}"
    AC_MSG_RESULT([$SWIG_RB_SITE_ARCH_DIR])

    AC_MSG_CHECKING([how to use output level for Ruby bindings tests])
    AC_CACHE_VAL([svn_cv_ruby_test_verbose],[
      svn_cv_ruby_test_verbose="normal"
    ])
    AC_ARG_WITH([ruby-test-verbose],
    AC_HELP_STRING([--with-ruby-test-verbose=LEVEL],
                               [how to use output level for Ruby bindings tests
                                (default is normal)]),
    [svn_ruby_test_verbose="$withval"],
		  [svn_ruby_test_verbose="$svn_cv_ruby_test_verbose"])
      SWIG_RB_TEST_VERBOSE="$svn_ruby_test_verbose"
      AC_MSG_RESULT([$SWIG_RB_TEST_VERBOSE])
  fi
  AC_SUBST(SWIG_CLEAN_RULES)
  AC_SUBST(SWIG_PY_INCLUDES)
  AC_SUBST(SWIG_PY_COMPILE)
  AC_SUBST(SWIG_PY_LINK)
  AC_SUBST(SWIG_PY_LIBS)
  AC_SUBST(SWIG_PL_INCLUDES)
  AC_SUBST(SWIG_RB_LINK)
  AC_SUBST(SWIG_RB_INCLUDES)
  AC_SUBST(SWIG_RB_COMPILE)
  AC_SUBST(SWIG_RB_SITE_LIB_DIR)
  AC_SUBST(SWIG_RB_SITE_ARCH_DIR)
  AC_SUBST(SWIG_RB_TEST_VERBOSE)
])
