dnl check to see if SWIG is current enough.
dnl
dnl if it is, then check to see if we have the correct version of python.
dnl
dnl if we do, then set up the appropriate SWIG_ variables to build the 
dnl python bindings.

AC_DEFUN(SVN_CHECK_SWIG,
[
  AC_ARG_WITH(swig,
              [AC_HELP_STRING([--with-swig=PATH],
                              [Try to use 'PATH/bin/swig' to build the
                               swig bindings.  If PATH is not specified,
                               look for a 'swig' binary in your PATH.])],
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

  if test $where = check; then
    AC_PATH_PROG(SWIG, swig, none)
  else
    SWIG=$where/bin/swig
  fi

  if test "$SWIG" != "none"; then
    AC_MSG_CHECKING([swig version])
    SWIG_VERSION="`$SWIG -version 2>&1 | sed -ne 's/^.*Version \(.*\)$/\1/p'`"
    AC_MSG_RESULT([$SWIG_VERSION])
    case $SWIG_VERSION in
        [1.3.1[45]*])
          SWIG_SUITABLE=yes
          ;;
        *)
          SWIG_SUITABLE=no
          AC_MSG_WARN([swig bindings require 1.3.15.])
          ;;
    esac
    if test "$PYTHON" != "none" -a "$SWIG_SUITABLE" = "yes"; then
      SWIG_BUILD_RULES="$SWIG_BUILD_RULES swig-py-lib"
      SWIG_INSTALL_RULES="$SWIG_INSTALL_RULES install-swig-py-lib"

      AC_CACHE_CHECK([for Python includes], [ac_cv_python_includes],[
        ac_cv_python_includes="`$PYTHON ${abs_srcdir}/ac-helpers/get-py-info.py --includes`"
      ])
      SWIG_PY_INCLUDES="-I$ac_cv_python_includes"
    fi
  fi
  AC_SUBST(SWIG_BUILD_RULES)
  AC_SUBST(SWIG_INSTALL_RULES)
  AC_SUBST(SWIG_PY_INCLUDES)
])
