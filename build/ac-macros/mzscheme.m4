dnl
dnl  SVN_MZSCHEME
dnl
dnl  Check configure options and assign variables related to
dnl  the mzscheme library.
dnl

AC_DEFUN(SVN_MZSCHEME,
[
  mzscheme_found=no

  AC_ARG_WITH(mzscheme,AC_HELP_STRING([--with-mzscheme=PREFIX],
                                  [mzscheme include path]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-mzscheme requires an argument.])
    else
      AC_MSG_NOTICE([mzscheme include configuration])
      mzscheme_prefix=$withval
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS -I$mzscheme_prefix/include"
      AC_CHECK_HEADERS(plt/escheme.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="-L$mzscheme_prefix/lib"
        mzscheme_found="yes"
        LDFLAGS="$save_ldflags"
      ])
      CPPFLAGS="$save_cppflags"
    fi
  ],
  [
    AC_CHECK_HEADER(plt/escheme.h,  
    [mzscheme_found="yes"])
  ])

  if test "$mzscheme_found" = "no"; then
    AC_MSG_ERROR([subversion requires mzscheme])
  fi

  if test "$mzscheme_found" = "yes"; then
    for dir in $2; do
	SVN_MZSCHEME_INCLUDES="$SVN_MZSCHEME_INCLUDES -I{$dir} -I{$dir}/plt"
    done
    SVN_MZSCHEME_PREFIX="$mzscheme_prefix"
    SVN_MZSCHEME_INCLUDES="$SVN_MZSCHEME_INCLUDES -I$mzscheme_prefix/include"
    LDFLAGS="-L$mzscheme_prefix/lib"
  fi

  SVN_MZSCHEME_LIBS="-lmzscheme"

  AC_SUBST(SVN_MZSCHEME_PREFIX)
  AC_SUBST(SVN_MZSCHEME_INCLUDES)
  AC_SUBST(SVN_MZSCHEME_LIBS)
])
