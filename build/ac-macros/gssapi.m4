dnl
dnl  SVN_LIB_GSSAPI
dnl
dnl  Check configure options and assign variables related to
dnl  the gssapi library.
dnl

AC_DEFUN(SVN_LIB_RA_SERF_GSSAPI,
[
  gssapi_found=no

  AC_ARG_WITH(gssapi,AS_HELP_STRING([--with-gssapi=PREFIX],
                                  [GSSAPI (Kerberos) support]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-gssapi requires an argument.])
    elif test "$withval" != "no" ; then
      AC_MSG_NOTICE([GSSAPI configuration])
      gssapi_prefix=$withval
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS -I$gssapi_prefix/include/gssapi"
      AC_CHECK_HEADERS(gssapi.h, [
        save_ldflags="$LDFLAGS"
        LDFLAGS="$LDFLAGS -L$gssapi_prefix/lib"
        AC_CHECK_LIB(gssapi_krb5, gss_init_sec_context, [gssapi_found="yes"])
        LDFLAGS="$save_ldflags"])
      CPPFLAGS="$save_cppflags"
    fi
  ])

  svn_lib_gssapi=$gssapi_found

  AC_SUBST(SVN_GSSAPI_PREFIX)
  AC_SUBST(SVN_GSSAPI_INCLUDES)
  AC_SUBST(SVN_GSSAPI_LIBS)
  AC_SUBST(SVN_GSSAPI_EXPORT_LIBS)
])
