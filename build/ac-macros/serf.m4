dnl
dnl  SVN_LIB_SERF
dnl
dnl  Check configure options and assign variables related to
dnl  the serf library.
dnl

AC_DEFUN(SVN_LIB_SERF,
[
  serf_found=no

  AC_ARG_WITH(serf,AC_HELP_STRING([--with-serf=PREFIX],
                                  [Serf WebDAV client library]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-serf requires an argument.])
    else
      if test "$svn_lib_neon" = "yes"; then
        AC_MSG_ERROR([neon and serf can't be used at the same time.])
      fi
      AC_MSG_NOTICE([serf library configuration])
      serf_prefix=$withval
      save_cppflags="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES"
      CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES -I$serf_prefix/include/serf-0"
      AC_CHECK_HEADERS(serf.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="-L$serf_prefix/lib"
        AC_CHECK_LIB(serf-0, serf_context_create,[serf_found="yes"])
        LDFLAGS="$save_ldflags"])
      CPPFLAGS="$save_cppflags"
    fi
  ])

  svn_lib_serf=$serf_found

  if test $serf_found = "reconfig"; then
    SVN_EXTERNAL_PROJECT([serf], [--with-apr=../apr --with-apr-util=../apr-util])
  fi

  if test $serf_found = "yes"; then
    SVN_SERF_PREFIX="$serf_prefix"
    SVN_SERF_INCLUDES="-I$serf_prefix/include/serf-0"
    SVN_SERF_LIBS="$serf_prefix/lib/libserf-0.la"
    SVN_SERF_EXPORT_LIBS="-L$serf_prefix/lib -lserf-0"
  fi

  AC_SUBST(SVN_SERF_PREFIX)
  AC_SUBST(SVN_SERF_INCLUDES)
  AC_SUBST(SVN_SERF_LIBS)
  AC_SUBST(SVN_SERF_EXPORT_LIBS)
])
