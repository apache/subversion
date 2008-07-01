dnl
dnl  SVN_LIB_APR_MEMCACHE
dnl
dnl  Check configure options and assign variables related to
dnl  the apr_memcache client library.
dnl  Sets svn_lib_apr_memcache to "yes" if memcache code is accessible
dnl  either from the standalone apr_memcache library or from apr-util.
dnl

AC_DEFUN(SVN_LIB_APR_MEMCACHE,
[
  apr_memcache_found=no

  AC_ARG_WITH(apr_memcache,AC_HELP_STRING([--with-apr_memcache=PREFIX],
                                  [Standalone apr_memcache client library]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-apr_memcache requires an argument.])
    else
      AC_MSG_NOTICE([looking for separate apr_memcache package])
      apr_memcache_prefix=$withval
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES -I$apr_memcache_prefix/include/apr_memcache-0"
      AC_CHECK_HEADER(apr_memcache.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="$LDFLAGS -L$apr_memcache_prefix/lib"
        AC_CHECK_LIB(apr_memcache, apr_memcache_create,
          [apr_memcache_found="standalone"])
        LDFLAGS="$save_ldflags"])
      CPPFLAGS="$save_cppflags"
    fi
  ], [
    if test -d "$srcdir/apr_memcache"; then
      apr_memcache_found=reconfig
    else
dnl   Try just looking in apr-util (>= 1.3 has it already).
      AC_MSG_NOTICE([looking for apr_memcache as part of apr-util])
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES"
      AC_CHECK_HEADER(apr_memcache.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="$LDFLAGS $SVN_APRUTIL_EXPORT_LIBS"
        AC_CHECK_LIB(aprutil-1, apr_memcache_create,
          [apr_memcache_found="aprutil"])
        LDFLAGS="$save_ldflags"])
      CPPFLAGS="$save_cppflags"

    fi
   ])


  if test $apr_memcache_found = "reconfig"; then
    SVN_EXTERNAL_PROJECT([apr_memcache], [--with-apr=$apr_config --with-apr-util=$apu_config])
    apr_memcache_prefix=$prefix
    SVN_APR_MEMCACHE_PREFIX="$apr_memcache_prefix"
    SVN_APR_MEMCACHE_INCLUDES="-I$srcdir/memcache"
    SVN_APR_MEMCACHE_LIBS="$abs_builddir/memcache/libapr_memcache.la"
    SVN_APR_MEMCACHE_EXPORT_LIBS="-L$apr_memcache_prefix/lib -lapr_memcache"
  fi

  if test $apr_memcache_found = "standalone"; then
    SVN_APR_MEMCACHE_PREFIX="$apr_memcache_prefix"
    SVN_APR_MEMCACHE_INCLUDES="-I$apr_memcache_prefix/include/apr_memcache-0"
    SVN_APR_MEMCACHE_LIBS="$apr_memcache_prefix/lib/libapr_memcache.la"
    SVN_APR_MEMCACHE_EXPORT_LIBS="-L$apr_memcache_prefix/lib -lapr_memcache"
    svn_lib_apr_memcache=yes
  elif test $apr_memcache_found = "aprutil"; then
dnl We are already linking apr-util everywhere, so no special treatement needed.
    SVN_APR_MEMCACHE_PREFIX=""
    SVN_APR_MEMCACHE_INCLUDES=""
    SVN_APR_MEMCACHE_LIBS=""
    SVN_APR_MEMCACHE_EXPORT_LIBS=""
    svn_lib_apr_memcache=yes
  elif test $apr_memcache_found = "reconfig"; then
    svn_lib_apr_memcache=yes
  else
    svn_lib_apr_memcache=no
  fi

  AC_SUBST(SVN_APR_MEMCACHE_PREFIX)
  AC_SUBST(SVN_APR_MEMCACHE_INCLUDES)
  AC_SUBST(SVN_APR_MEMCACHE_LIBS)
  AC_SUBST(SVN_APR_MEMCACHE_EXPORT_LIBS)
])
