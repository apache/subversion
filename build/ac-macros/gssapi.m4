dnl
dnl  SVN_LIB_GSSAPI
dnl
dnl  Check configure options and assign variables related to
dnl  the gssapi library.
dnl

AC_DEFUN(SVN_LIB_RA_SERF_GSSAPI,
[
  AC_ARG_WITH(gssapi,
    [AS_HELP_STRING([[--with-gssapi[=PREFIX]]],
                    [GSSAPI (Kerberos) support])],
                    [svn_lib_gssapi="$withval"],
                    [svn_lib_gssapi="no"])

  AC_MSG_CHECKING([whether to look for GSSAPI (Kerberos)])
  if test "$svn_lib_gssapi" != "no"; then
    AC_MSG_RESULT([yes])
    if test "$svn_lib_gssapi" != "yes"; then
      AC_MSG_CHECKING([for krb5-config])
      KRB5_CONFIG="$svn_lib_gssapi/bin/krb5-config"
      if test -f "$KRB5_CONFIG" && test -x "$KRB5_CONFIG"; then
        AC_MSG_RESULT([yes])
      else
        KRB5_CONFIG=""
        AC_MSG_RESULT([no])
      fi
    else
      AC_PATH_PROG(KRB5_CONFIG, krb5-config)
    fi
    if test -n "$KRB5_CONFIG"; then
      AC_MSG_CHECKING([for GSSAPI (Kerberos)])
      old_CPPFLAGS="$CPPFLAGS"
      old_CFLAGS="$CFLAGS"
      old_LIBS="$LIBS"
      CFLAGS=""
      SVN_GSSAPI_INCLUDES="`$KRB5_CONFIG --cflags`"
      SVN_GSSAPI_LIBS="`$KRB5_CONFIG --libs gssapi`"
      CPPFLAGS="$CPPFLAGS $SVN_GSSAPI_INCLUDES"
      CFLAGS="$old_CFLAGS"
      LIBS="$LIBS $SVN_GSSAPI_LIBS"
      AC_LINK_IFELSE([
#include <gssapi.h>
int main()
{gss_init_sec_context(NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL);}],
        svn_lib_gssapi="yes", svn_lib_gssapi="no")
      if test "$svn_lib_gssapi" = "yes"; then
        AC_MSG_RESULT([yes])
        CPPFLAGS="$old_CPPFLAGS"
        LIBS="$old_LIBS"
      else
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([cannot find GSSAPI (Kerberos)])
      fi
    else
      AC_MSG_ERROR([cannot find krb5-config])
    fi
  else
    AC_MSG_RESULT([no])
  fi
  AC_SUBST(SVN_GSSAPI_INCLUDES)
  AC_SUBST(SVN_GSSAPI_LIBS)
])
