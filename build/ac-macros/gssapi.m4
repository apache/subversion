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
      if test $? -ne 0; then
        dnl Some platforms e.g. Solaris 10 don't support the gssapi argument
        dnl and need krb5 instead.  Since it doesn't tell us about gssapi
        dnl we have to guess.  So let's try -lgss and /usr/include/gassapi
        SVN_GSSAPI_INCLUDES="$SVN_GSSAPI_INCLUDES -I/usr/include/gssapi"
        SVN_GSSAPI_LIBS="`$KRB5_CONFIG --libs krb5` -lgss"
        if test $? -ne 0; then
          dnl Both k5b-config commands failed.
          AC_MSG_ERROR([krb5-config returned an error]) 
        fi
      fi
      SVN_GSSAPI_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS($SVN_GSSAPI_LIBS)`"
      CPPFLAGS="$CPPFLAGS $SVN_GSSAPI_INCLUDES"
      CFLAGS="$old_CFLAGS"
      LIBS="$LIBS $SVN_GSSAPI_LIBS"
      AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <gssapi.h>
int main()
{gss_init_sec_context(NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL);}]])],
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
