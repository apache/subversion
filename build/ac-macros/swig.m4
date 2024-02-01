dnl ===================================================================
dnl   Licensed to the Apache Software Foundation (ASF) under one
dnl   or more contributor license agreements.  See the NOTICE file
dnl   distributed with this work for additional information
dnl   regarding copyright ownership.  The ASF licenses this file
dnl   to you under the Apache License, Version 2.0 (the
dnl   "License"); you may not use this file except in compliance
dnl   with the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl   Unless required by applicable law or agreed to in writing,
dnl   software distributed under the License is distributed on an
dnl   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
dnl   KIND, either express or implied.  See the License for the
dnl   specific language governing permissions and limitations
dnl   under the License.
dnl ===================================================================
dnl
dnl check to see if SWIG is current enough.
dnl
dnl if it is, then check to see if we have the correct version of python.
dnl
dnl if we do, then set up the appropriate SWIG_ variables to build the
dnl Python, Perl, and Ruby bindings.

AC_DEFUN(SVN_CHECK_SWIG,
[
  m4_ifndef([SVN_RELEASE_MODE],
  [
    AC_ARG_WITH(swig,
                AS_HELP_STRING([--with-swig=PATH],
                               [Try to use 'PATH/bin/swig' to build the
                                swig bindings.  If PATH is not specified,
                                look for a 'swig' binary in your PATH.]),
    [
      case "$withval" in
      yes)
        svn_find_swig_arg=required
      ;;
      *)
        svn_find_swig_arg=$withval
      ;;
      esac
    ],
    [
      if    test "$SWIG_PY_PYTHON" != "none" \
         || test "$SWIG_PL_PERL"   != "none" \
         || test "$SWIG_RB_RUBY"   != "none" ; then
        svn_find_swig_arg=check
      else
        svn_find_swig_arg=no
      fi
    ])
    SVN_FIND_SWIG($svn_find_swig_arg)
  ])
  SVN_DETERMINE_SWIG_OPTS
])

AC_DEFUN(SVN_FIND_SWIG,
[
  where=$1

  if test $where = no; then
    SWIG=none
  elif test $where = required || test $where = check; then
    AC_PATH_PROG(SWIG, swig, none)
    if test "$SWIG" = "none" && test $where = required; then
      AC_MSG_ERROR([SWIG required, but not found])
    fi
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
    if test ! -n "$SWIG_VERSION" || test "$SWIG_VERSION" -lt "103024"; then
      AC_MSG_WARN([Detected SWIG version $SWIG_VERSION_RAW])
      AC_MSG_WARN([Subversion requires SWIG >= 1.3.24])
    fi
  fi
])


AC_DEFUN(SVN_DETERMINE_SWIG_OPTS,
[
  m4_ifndef([SVN_RELEASE_MODE],
  [
    # not in release mode  
    SWIG_PY_COMPILE="none"
    SWIG_PY_LINK="none"
    SWIG_PY_OPTS="none"
    SWIG_PY_ERRMSG="check config.log for details"
    if test "$SWIG_PY_PYTHON" = "none"; then
      SWIG_PY_ERRMSG="You specified not to build Python bindings or \
suitable Python interpreter is not found."
    else
      if test "$SWIG" = "none"; then
        AC_MSG_WARN([You specified to build SWIG Python bindings, but SWIG is not found.])
        SWIG_PY_ERRMSG="SWIG is need to build SWIG Python bindings, but it is not found."
      else
        AC_MSG_NOTICE([Configuring python swig binding])

        AC_CACHE_CHECK([for Python includes], [ac_cv_python_includes],[
        ac_cv_python_includes="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --includes`"
        ])
        SWIG_PY_INCLUDES="\$(SWIG_INCLUDES) $ac_cv_python_includes"

        if test "$ac_cv_python_includes" = "none"; then
          SWIG_PY_ERRMSG="no distutils found"
          AC_MSG_WARN([python bindings cannot be built without distutils module])
        else

          python_header_found="no"

          save_cppflags="$CPPFLAGS"
          CPPFLAGS="$CPPFLAGS $ac_cv_python_includes"
          AC_CHECK_HEADER(Python.h, [
            python_header_found="yes"
          ])
          CPPFLAGS="$save_cppflags"

          if test "$python_header_found" = "no"; then
            SWIG_PY_ERRMSG="no Python.h found"
            AC_MSG_WARN([Python.h not found; disabling python swig bindings])
          else
            SVN_PY3C()

            if test "$py3c_found" = "no"; then
              SWIG_PY_ERRMSG="py3c library not found"
              AC_MSG_WARN([py3c library not found; disabling python swig bindings])
            else
              AC_CACHE_CHECK([for compiling Python extensions], [ac_cv_python_compile],[
                ac_cv_python_compile="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --compile`"
              ])
              SWIG_PY_COMPILE="$ac_cv_python_compile $CFLAGS"

              AC_CACHE_CHECK([for linking Python extensions], [ac_cv_python_link],[
                ac_cv_python_link="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --link`"
              ])
              SWIG_PY_LINK="$ac_cv_python_link"

              AC_CACHE_CHECK([for linking Python libraries], [ac_cv_python_libs],[
                ac_cv_python_libs="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --libs`"
              ])
              SWIG_PY_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS($ac_cv_python_libs)`"

              # Look more closely at the SWIG and Python versions to
              # determine SWIG_PY_OPTS. We can skip this if we already
              # have the SWIG-generated files.
              AC_CACHE_CHECK([for Python >= 3], [ac_cv_python_is_py3],[
                ac_cv_python_is_py3="no"
                $SWIG_PY_PYTHON -c 'import sys; sys.exit(0x3000000 > sys.hexversion)' && \
                   ac_cv_python_is_py3="yes"
              ])

              if test "$ac_cv_python_is_py3" = "yes"; then
                if test "$SWIG_VERSION" -ge "300010"; then
                  dnl SWIG Python bindings successfully configured, clear the error message dnl
                  SWIG_PY_ERRMSG=""
                  if test "$SWIG_VERSION" -lt "400000"; then
                    SWIG_PY_OPTS="-python -py3 -nofastunpack -modern"
                  elif test "$SWIG_VERSION" -lt "401000"; then 
                    SWIG_PY_OPTS="-python -py3 -nofastunpack"
                  else
                    SWIG_PY_OPTS="-python -nofastunpack"
                  fi
                  if test "$SWIG_VERSION" -gt "400002"; then 
                    AC_MSG_WARN([Subversion Python bindings may work,])
                    AC_MSG_WARN([but we didn't check with this SWIG version.])
                  fi
                else
                  SWIG_PY_OPTS="-no-such-option" # fool proof
                  SWIG_PY_ERRMSG="SWIG version is not suitable"
                  AC_MSG_WARN([Subversion Python bindings for Python 3 require SWIG 3.0.10 or newer])
                fi
              else
                if test "$SWIG_VERSION" -lt "400000"; then
                  SWIG_PY_OPTS="-python -classic"
                  dnl SWIG Python bindings successfully configured, clear the error message dnl
                  SWIG_PY_ERRMSG=""
                else
                  SWIG_PY_OPTS="-no-such-option" # fool proof
                  SWIG_PY_ERRMSG="SWIG version is not suitable"
                  AC_MSG_WARN([Subversion Python bindings for Python 2 require 1.3.24 <= SWIG < 4.0.0])
                fi
              fi
            fi
          fi
        fi
      fi
    fi

    SWIG_PL_ERRMSG="check config.log for details"
    if test "$SWIG_PL_PERL" = "none"; then
      SWIG_PL_ERRMSG="You specified not to build Perl bindings or \
suitable Perl interpreter is not found."
    else
      if test "$SWIG" = "none"; then
        AC_MSG_WARN([You specified to build SWIG Perl bindings, but SWIG is not found.])
        SWIG_PL_ERRMSG="SWIG is need to build SWIG Perl bindings, but it is not found."
      else
        AC_MSG_CHECKING([perl version])
        dnl Note that the q() bit is there to avoid unbalanced brackets
        dnl which m4 really doesn't like.
        PERL_VERSION="`$SWIG_PL_PERL -e 'q([[); print $]] * 1000000,$/;'`"
        AC_MSG_RESULT([$PERL_VERSION])
        if test "$PERL_VERSION" -ge "5008000"; then
          SWIG_PL_INCLUDES="\$(SWIG_INCLUDES) `$SWIG_PL_PERL -MExtUtils::Embed -e ccopts`"
          SWIG_PL_LINK="`$SWIG_PL_PERL -MExtUtils::Embed -e ldopts`"
          SWIG_PL_LINK="`SVN_REMOVE_STANDARD_LIB_DIRS($SWIG_PL_LINK)`"

          dnl SWIG Perl bindings successfully configured, clear the error message
          SWIG_PL_ERRMSG=""
        else
          AC_MSG_WARN([perl bindings require perl 5.8.0 or newer.])
        fi
      fi
    fi

    SWIG_RB_COMPILE="none"
    SWIG_RB_LINK="none"
    SWIG_RB_ERRMSG="check config.log for details"
    if test "$SWIG_RB_RUBY" = "none"; then
      SWIG_RB_ERRMSG="You specified not to build Ruby bindings or \
suitable Ruby interpreter is not found."
    else
      if test "$SWIG" = "none"; then
        AC_MSG_WARN([You specified to build SWIG Ruby bindings, but SWIG is not found.])
        SWIG_RB_ERRMSG="SWIG is need to build SWIG Ruby bindings, but it is not found."
      elif test x"$SWIG_VERSION" = x"4""02""000"; then
        ruby_swig_issue_2751='https://github.com/swig/swig/issues/2751'
        AC_MSG_WARN([Ruby bindings cannot be built with swig 4.2.0; see $ruby_swig_issue_2751])
        SWIG_RB_ERRMSG="SWIG 4.2.0 was found but it cannot be used for building SWIG Ruby bindings."
      else
        if test x"$SWIG_VERSION" = x"3""00""008"; then
          # Use a local variable to escape the '#' sign.
          ruby_swig_issue_602='https://subversion.apache.org/docs/release-notes/1.11#ruby-swig-issue-602'
          AC_MSG_WARN([Ruby bindings are known not to support swig 3.0.8; see $ruby_swig_issue_602])
        fi
        rbconfig="$SWIG_RB_RUBY -rrbconfig -e "

        for var_name in arch archdir CC LDSHARED DLEXT LIBS LIBRUBYARG \
                        rubyhdrdir rubyarchhdrdir sitedir sitelibdir sitearchdir libdir
        do
          rbconfig_tmp=`$rbconfig "print RbConfig::CONFIG@<:@'$var_name'@:>@"`
          eval "rbconfig_$var_name=\"$rbconfig_tmp\""
        done

        AC_MSG_NOTICE([Configuring Ruby SWIG binding])

        AC_CACHE_CHECK([for Ruby include path], [svn_cv_ruby_includes],[
        if test -d "$rbconfig_rubyhdrdir"; then
          dnl Ruby >=1.9
          svn_cv_ruby_includes="-I. -I$rbconfig_rubyhdrdir"
          if test -d "$rbconfig_rubyarchhdrdir"; then
            dnl Ruby >=2.0
            svn_cv_ruby_includes="$svn_cv_ruby_includes -I$rbconfig_rubyarchhdrdir"
          else
            svn_cv_ruby_includes="$svn_cv_ruby_includes -I$rbconfig_rubyhdrdir/$rbconfig_arch"
          fi
        else
          dnl Ruby 1.8
          svn_cv_ruby_includes="-I. -I$rbconfig_archdir"
        fi
        ])
        SWIG_RB_INCLUDES="\$(SWIG_INCLUDES) $svn_cv_ruby_includes"

        AC_CACHE_CHECK([how to compile Ruby extensions], [svn_cv_ruby_compile],[
          svn_cv_ruby_compile="$rbconfig_CC $CFLAGS"
        ])
        SWIG_RB_COMPILE="$svn_cv_ruby_compile"
        SVN_STRIP_FLAG([SWIG_RB_COMPILE], [-ansi])
        SVN_STRIP_FLAG([SWIG_RB_COMPILE], [-std=c89])
        SVN_STRIP_FLAG([SWIG_RB_COMPILE], [-std=c90])
        dnl FIXME: Check that the compiler for Ruby actually supports this flag
        SWIG_RB_COMPILE="$SWIG_RB_COMPILE -Wno-int-to-pointer-cast"

        AC_CACHE_CHECK([how to link Ruby extensions], [svn_cv_ruby_link],[
          svn_cv_ruby_link="`$SWIG_RB_RUBY -e 'ARGV.shift; print ARGV.join(%q( ))' \
                               $rbconfig_LDSHARED`"
          svn_cv_ruby_link="$rbconfig_CC $svn_cv_ruby_link"
          svn_cv_ruby_link="$svn_cv_ruby_link -shrext .$rbconfig_DLEXT"
        ])
        SWIG_RB_LINK="$svn_cv_ruby_link"

        AC_CACHE_CHECK([how to link Ruby libraries], [ac_cv_ruby_libs], [
          ac_cv_ruby_libs="$rbconfig_LIBRUBYARG $rbconfig_LIBS"
        ])
        SWIG_RB_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS($ac_cv_ruby_libs)`"

        AC_MSG_CHECKING([for rb_errinfo])
        old_CFLAGS="$CFLAGS"
        old_LIBS="$LIBS"
        CFLAGS="$CFLAGS $svn_cv_ruby_includes"
        SVN_STRIP_FLAG([CFLAGS], [-ansi])
        SVN_STRIP_FLAG([CFLAGS], [-std=c89])
        SVN_STRIP_FLAG([CFLAGS], [-std=c90])
        LIBS="$SWIG_RB_LIBS"
        AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <ruby.h>
int main()
{rb_errinfo();}]])], have_rb_errinfo="yes", have_rb_errinfo="no")
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

        dnl SWIG Ruby bindings successfully configured, clear the error message
        SWIG_RB_ERRMSG=""
      fi
    fi
  ],
  [
    # in release mode  
    SWIG_PY_COMPILE="none"
    SWIG_PY_LINK="none"
    SWIG_PY_OPTS="none"
    SWIG_PY_ERRMSG="check config.log for details"
    if test "$SWIG_PY_PYTHON" = "none"; then
      SWIG_PY_ERRMSG="You specified not to build Python bindings or \
suitable Python interpreter is not found."
    else
      AC_MSG_NOTICE([Configuring python swig binding])

      AC_CACHE_CHECK([for Python includes], [ac_cv_python_includes],[
        ac_cv_python_includes="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --includes`"
      ])
      SWIG_PY_INCLUDES="\$(SWIG_INCLUDES) $ac_cv_python_includes"

      if test "$ac_cv_python_includes" = "none"; then
        SWIG_PY_ERRMSG="no distutils found"
        AC_MSG_WARN([python bindings cannot be built without distutils module])
      else

        python_header_found="no"

        save_cppflags="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $ac_cv_python_includes"
        AC_CHECK_HEADER(Python.h, [
          python_header_found="yes"
        ])
        CPPFLAGS="$save_cppflags"

        if test "$python_header_found" = "no"; then
          SWIG_PY_ERRMSG="no Python.h found"
          AC_MSG_WARN([Python.h not found; disabling python swig bindings])
        else
          SVN_PY3C()

          if test "$py3c_found" = "no"; then
            SWIG_PY_ERRMSG="py3c library not found"
            AC_MSG_WARN([py3c library not found; disabling python swig bindings])
          else
            AC_CACHE_CHECK([for compiling Python extensions], [ac_cv_python_compile],[
              ac_cv_python_compile="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --compile`"
            ])
            SWIG_PY_COMPILE="$ac_cv_python_compile $CFLAGS"

            AC_CACHE_CHECK([for linking Python extensions], [ac_cv_python_link],[
              ac_cv_python_link="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --link`"
            ])
            SWIG_PY_LINK="$ac_cv_python_link"

            AC_CACHE_CHECK([for linking Python libraries], [ac_cv_python_libs],[
              ac_cv_python_libs="`$SWIG_PY_PYTHON ${abs_srcdir}/build/get-py-info.py --libs`"
            ])
            SWIG_PY_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS($ac_cv_python_libs)`"

            SWIG_PY_ERRMSG=""
          fi
        fi
      fi
    fi

    SWIG_PL_ERRMSG="check config.log for details"
    if test "$SWIG_PL_PERL" = "none"; then
      SWIG_PL_ERRMSG="You specified not to build Perl bindings or \
suitable Perl interpreter is not found."
    else
      AC_MSG_CHECKING([perl version])
      dnl Note that the q() bit is there to avoid unbalanced brackets
      dnl which m4 really doesn't like.
      PERL_VERSION="`$SWIG_PL_PERL -e 'q([[); print $]] * 1000000,$/;'`"
      AC_MSG_RESULT([$PERL_VERSION])
      if test "$PERL_VERSION" -ge "5008000"; then
        SWIG_PL_INCLUDES="\$(SWIG_INCLUDES) `$SWIG_PL_PERL -MExtUtils::Embed -e ccopts`"
        SWIG_PL_LINK="`$SWIG_PL_PERL -MExtUtils::Embed -e ldopts`"
        SWIG_PL_LINK="`SVN_REMOVE_STANDARD_LIB_DIRS($SWIG_PL_LINK)`"

        dnl SWIG Perl bindings successfully configured, clear the error message
        SWIG_PL_ERRMSG=""
      else
        AC_MSG_WARN([perl bindings require perl 5.8.0 or newer.])
      fi
    fi

    SWIG_RB_COMPILE="none"
    SWIG_RB_LINK="none"
    SWIG_RB_ERRMSG="check config.log for details"
    if test "$SWIG_RB_RUBY" = "none"; then
      SWIG_RB_ERRMSG="You specified not to build Ruby bindings or \
suitable Ruby interpreter is not found."
    else
      rbconfig="$SWIG_RB_RUBY -rrbconfig -e "

      for var_name in arch archdir CC LDSHARED DLEXT LIBS LIBRUBYARG \
                      rubyhdrdir rubyarchhdrdir sitedir sitelibdir sitearchdir libdir
      do
        rbconfig_tmp=`$rbconfig "print RbConfig::CONFIG@<:@'$var_name'@:>@"`
        eval "rbconfig_$var_name=\"$rbconfig_tmp\""
      done

      AC_MSG_NOTICE([Configuring Ruby SWIG binding])

      AC_CACHE_CHECK([for Ruby include path], [svn_cv_ruby_includes],[
      if test -d "$rbconfig_rubyhdrdir"; then
        dnl Ruby >=1.9
        svn_cv_ruby_includes="-I. -I$rbconfig_rubyhdrdir"
        if test -d "$rbconfig_rubyarchhdrdir"; then
          dnl Ruby >=2.0
          svn_cv_ruby_includes="$svn_cv_ruby_includes -I$rbconfig_rubyarchhdrdir"
        else
          svn_cv_ruby_includes="$svn_cv_ruby_includes -I$rbconfig_rubyhdrdir/$rbconfig_arch"
        fi
      else
        dnl Ruby 1.8
        svn_cv_ruby_includes="-I. -I$rbconfig_archdir"
      fi
      ])
      SWIG_RB_INCLUDES="\$(SWIG_INCLUDES) $svn_cv_ruby_includes"

      AC_CACHE_CHECK([how to compile Ruby extensions], [svn_cv_ruby_compile],[
        svn_cv_ruby_compile="$rbconfig_CC $CFLAGS"
      ])
      SWIG_RB_COMPILE="$svn_cv_ruby_compile"
      SVN_STRIP_FLAG([SWIG_RB_COMPILE], [-ansi])
      SVN_STRIP_FLAG([SWIG_RB_COMPILE], [-std=c89])
      SVN_STRIP_FLAG([SWIG_RB_COMPILE], [-std=c90])
      dnl FIXME: Check that the compiler for Ruby actually supports this flag
      SWIG_RB_COMPILE="$SWIG_RB_COMPILE -Wno-int-to-pointer-cast"

      AC_CACHE_CHECK([how to link Ruby extensions], [svn_cv_ruby_link],[
        svn_cv_ruby_link="`$SWIG_RB_RUBY -e 'ARGV.shift; print ARGV.join(%q( ))' \
                             $rbconfig_LDSHARED`"
        svn_cv_ruby_link="$rbconfig_CC $svn_cv_ruby_link"
        svn_cv_ruby_link="$svn_cv_ruby_link -shrext .$rbconfig_DLEXT"
      ])
      SWIG_RB_LINK="$svn_cv_ruby_link"

      AC_CACHE_CHECK([how to link Ruby libraries], [ac_cv_ruby_libs], [
        ac_cv_ruby_libs="$rbconfig_LIBRUBYARG $rbconfig_LIBS"
      ])
      SWIG_RB_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS($ac_cv_ruby_libs)`"

      AC_MSG_CHECKING([for rb_errinfo])
      old_CFLAGS="$CFLAGS"
      old_LIBS="$LIBS"
      CFLAGS="$CFLAGS $svn_cv_ruby_includes"
      SVN_STRIP_FLAG([CFLAGS], [-ansi])
      SVN_STRIP_FLAG([CFLAGS], [-std=c89])
      SVN_STRIP_FLAG([CFLAGS], [-std=c90])
      LIBS="$SWIG_RB_LIBS"
      AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <ruby.h>
int main()
{rb_errinfo();}]])], have_rb_errinfo="yes", have_rb_errinfo="no")
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

      dnl SWIG Ruby bindings successfully configured, clear the error message
      SWIG_RB_ERRMSG=""
    fi
  ])
  AC_SUBST(SWIG)
  AC_SUBST(SWIG_PY_INCLUDES)
  AC_SUBST(SWIG_PY_COMPILE)
  AC_SUBST(SWIG_PY_LINK)
  AC_SUBST(SWIG_PY_LIBS)
  AC_SUBST(SWIG_PY_OPTS)
  AC_SUBST(SWIG_PY_ERRMSG)
  AC_SUBST(SWIG_PL_INCLUDES)
  AC_SUBST(SWIG_PL_LINK)
  AC_SUBST(SWIG_PL_ERRMSG)
  AC_SUBST(SWIG_RB_LINK)
  AC_SUBST(SWIG_RB_LIBS)
  AC_SUBST(SWIG_RB_INCLUDES)
  AC_SUBST(SWIG_RB_COMPILE)
  AC_SUBST(SWIG_RB_SITE_LIB_DIR)
  AC_SUBST(SWIG_RB_SITE_ARCH_DIR)
  AC_SUBST(SWIG_RB_TEST_VERBOSE)
  AC_SUBST(SWIG_RB_ERRMSG)
])
