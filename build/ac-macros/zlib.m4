dnl
dnl  SVN_LIB_Z
dnl
dnl  Check configure options and assign variables related to
dnl  the zlib library.
dnl

AC_DEFUN(SVN_LIB_Z,
[
  zlib_found=no

  AC_ARG_WITH(zlib,AS_HELP_STRING([--with-zlib=PREFIX],
                                  [zlib compression library]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-zlib requires an argument.])
    else
      AC_MSG_NOTICE([zlib library configuration])
      zlib_prefix=$withval
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS -I$zlib_prefix/include"
      AC_CHECK_HEADERS(zlib.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="-L$zlib_prefix/lib"
        AC_CHECK_LIB(z, inflate, [zlib_found="yes"])
        LDFLAGS="$save_ldflags"
      ])
      CPPFLAGS="$save_cppflags"
    fi
  ],
  [
    AC_CHECK_HEADER(zlib.h, [
      AC_CHECK_LIB(z, inflate, [zlib_found="builtin"])
    ])
  ])

  if test "$zlib_found" = "no"; then
    AC_MSG_ERROR([subversion requires zlib])
  fi

  if test "$zlib_found" = "yes"; then
    SVN_ZLIB_PREFIX="$zlib_prefix"
    SVN_ZLIB_INCLUDES="-I$zlib_prefix/include"
    LDFLAGS="$LDFLAGS -L$zlib_prefix/lib"
  fi

  SVN_ZLIB_LIBS="-lz"

  AC_SUBST(SVN_ZLIB_PREFIX)
  AC_SUBST(SVN_ZLIB_INCLUDES)
  AC_SUBST(SVN_ZLIB_LIBS)
])
