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
dnl Macros to find an Apache installation
dnl
dnl This will find an installed Apache.
dnl
dnl Note: If we don't have an installed Apache, then we can't install the
dnl       (dynamic) mod_dav_svn.so module.
dnl

AC_DEFUN(SVN_FIND_APACHE,[
AC_REQUIRE([AC_CANONICAL_HOST])

HTTPD_WANTED_MMN="$1"

HTTPD_WHITELIST_VER="$2"

AC_MSG_CHECKING(for Apache module support via DSO through APXS)
AC_ARG_WITH(apxs,
            [AS_HELP_STRING([[--with-apxs[=FILE]]],
                            [Build shared Apache modules.  FILE is the optional
                             pathname to the Apache apxs tool; defaults to
                             "apxs".])],
[
    if test "$withval" = "yes"; then
      APXS=apxs
    else
      APXS="$withval"
    fi
    APXS_EXPLICIT=1
])

if test -z "$APXS"; then
  for i in /usr/local/apache2/bin /usr/local/apache/bin /usr/bin /usr/sbin ; do
    if test -f "$i/apxs2"; then
      APXS="$i/apxs2"
      break
    fi
    if test -f "$i/apxs"; then
      APXS="$i/apxs"
      break
    fi
  done
fi

if test -n "$APXS" && test "$APXS" != "no"; then
    APXS_INCLUDE="`$APXS -q INCLUDEDIR`"
    if test -r $APXS_INCLUDE/mod_dav.h; then
        AC_MSG_RESULT(found at $APXS)

        AC_MSG_CHECKING([httpd version])
        AC_EGREP_CPP(VERSION_OKAY,
        [
#include "$APXS_INCLUDE/ap_mmn.h"
#if AP_MODULE_MAGIC_AT_LEAST($HTTPD_WANTED_MMN,0)
VERSION_OKAY
#endif],
        [AC_MSG_RESULT([recent enough])],
        [AC_MSG_RESULT([apache too old:  mmn must be at least $HTTPD_WANTED_MMN])
         if test "$APXS_EXPLICIT" != ""; then
             AC_MSG_ERROR([Apache APXS build explicitly requested, but apache version is too old])
         fi
         APXS=""
        ])

    elif test "$APXS_EXPLICIT" != ""; then
        AC_MSG_ERROR([no - APXS refers to an old version of Apache
                      Unable to locate $APXS_INCLUDE/mod_dav.h])
    else
        AC_MSG_RESULT(no - Unable to locate $APXS_INCLUDE/mod_dav.h)
        APXS=""
    fi
else
    AC_MSG_RESULT(no)
fi

# check for some busted versions of mod_dav
# in particular 2.2.25, 2.4.5, and 2.4.6 had the following bugs which are
# troublesome for Subversion:
# PR 55304: https://issues.apache.org/bugzilla/show_bug.cgi?id=55304
# PR 55306: https://issues.apache.org/bugzilla/show_bug.cgi?id=55306
# PR 55397: https://issues.apache.org/bugzilla/show_bug.cgi?id=55397
if test -n "$APXS" && test "$APXS" != "no"; then
  AC_MSG_CHECKING([mod_dav version])
  HTTPD_MAJOR=`$SED -ne '/^#define AP_SERVER_MAJORVERSION_NUMBER/p' "$APXS_INCLUDE/ap_release.h" | $SED -e 's/^.*NUMBER *//'`
  HTTPD_MINOR=`$SED -ne '/^#define AP_SERVER_MINORVERSION_NUMBER/p' "$APXS_INCLUDE/ap_release.h" | $SED -e 's/^.*NUMBER *//'`
  HTTPD_PATCH=`$SED -ne '/^#define AP_SERVER_PATCHLEVEL_NUMBER/p' "$APXS_INCLUDE/ap_release.h" | $SED -e 's/^.*NUMBER *//'`
  HTTPD_VERSION="${HTTPD_MAJOR}.${HTTPD_MINOR}.${HTTPD_PATCH}"
  case "$HTTPD_VERSION" in
    $HTTPD_WHITELIST_VER)
      AC_MSG_RESULT([acceptable (whitelist)])
      ;;
    2.2.25 | 2.4.[[5-6]])
      AC_MSG_RESULT([broken])
      AC_MSG_ERROR([Apache httpd version $HTTPD_VERSION includes a broken mod_dav; use a newer version of httpd])
      ;;
    2.[[0-9]]*.[[0-9]]*)
      AC_MSG_RESULT([acceptable])
      ;;
    *)
      AC_MSG_RESULT([unrecognised])
      AC_MSG_ERROR([Apache httpd version $HTTPD_VERSION not recognised])
      ;;
  esac
fi

if test -n "$APXS" && test "$APXS" != "no"; then
  AC_MSG_CHECKING([whether Apache version is compatible with APR version])
  apr_major_version="${apr_version%%.*}"
  case "$apr_major_version" in
    0)
      apache_minor_version_wanted_regex="0"
      ;;
    1)
      apache_minor_version_wanted_regex=["[1-5]"]
      ;;
    2)
      apache_minor_version_wanted_regex=["[3-5]"]
      ;;
    *)
      AC_MSG_ERROR([unknown APR version])
      ;;
  esac
  case $HTTPD_MINOR in
    $apache_minor_version_wanted_regex)
      AC_MSG_RESULT([yes])
      ;;
    *)
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([Apache version $HTTPD_VERSION incompatible with APR version $apr_version])
      ;;
  esac
fi

AC_ARG_WITH(apache-libexecdir,
            [AS_HELP_STRING([[--with-apache-libexecdir[=PATH]]],
                            [Install Apache modules to Apache's configured
                             modules directory instead of LIBEXECDIR;
                             if PATH is given, install to PATH.])],
[APACHE_LIBEXECDIR="$withval"],[APACHE_LIBEXECDIR='no'])

INSTALL_APACHE_MODS=false
if test -n "$APXS" && test "$APXS" != "no"; then
    APXS_CC="`$APXS -q CC`"
    APACHE_INCLUDES="$APACHE_INCLUDES -I$APXS_INCLUDE"

    if test "$APACHE_LIBEXECDIR" = 'no'; then
        APACHE_LIBEXECDIR="$libexecdir"
    elif test "$APACHE_LIBEXECDIR" = 'yes'; then
        APACHE_LIBEXECDIR="`$APXS -q libexecdir`"
    fi

    AC_CHECK_HEADERS(unistd.h, [AC_CHECK_FUNCS(getpid)], [])

    MMN_MAJOR=`$SED -ne '/^#define MODULE_MAGIC_NUMBER_MAJOR/p' "$APXS_INCLUDE/ap_mmn.h" | $SED -e 's/^.*MAJOR *//'`
    MMN_MINOR=`$SED -ne '/^#define MODULE_MAGIC_NUMBER_MINOR/p' "$APXS_INCLUDE/ap_mmn.h" | $SED -e 's/^.*MINOR *//' | $SED -e 's/ .*//'`
    if test "$MMN_MAJOR" = "20120211" && test "$MMN_MINOR" -lt "47" ; then
      # This is httpd 2.4 and it doesn't appear to have the required
      # API but the installation may have been patched.
      AC_ARG_ENABLE(broken-httpd-auth,
        AS_HELP_STRING([--enable-broken-httpd-auth],
                       [Force build against httpd 2.4 with broken auth. (This
                        is not recommended as Subversion will be vulnerable to
                        CVE-2015-3184.)]),
        [broken_httpd_auth=$enableval],[broken_httpd_auth=no])
      AC_MSG_CHECKING([for ap_some_authn_required])
      old_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $APACHE_INCLUDES $SVN_APR_INCLUDES"
      AC_EGREP_CPP([int.*\sap_some_authn_required\s*\(],
                   [#include "http_request.h"],
                   [AC_MSG_RESULT([yes])
                    working_auth=yes],
                   [AC_MSG_RESULT([no])])
      CPPFLAGS="$old_CPPFLAGS"
      if test "$working_auth" = "yes" ; then
        AC_DEFINE(SVN_USE_FORCE_AUTHN, 1,
                  [Defined to build with patched httpd 2.4 and working auth])
      elif test "$enable_broken_httpd_auth" = "yes"; then
        AC_MSG_WARN([==============================================])
        AC_MSG_WARN([Apache httpd $HTTPD_VERSION MMN $MMN_MAJOR.$MMN_MINOR])
        AC_MSG_WARN([Subversion will be vulnerable to CVE-2015-3184])
        AC_MSG_WARN([==============================================])
        AC_DEFINE(SVN_ALLOW_BROKEN_HTTPD_AUTH, 1,
                  [Defined to build against httpd 2.4 with broken auth])
      else
        AC_MSG_ERROR([Apache httpd $HTTPD_VERSION MMN $MMN_MAJOR.$MMN_MINOR has broken auth (CVE-2015-3184)])
      fi
    fi

    BUILD_APACHE_RULE=apache-mod
    INSTALL_APACHE_RULE=install-mods-shared
    INSTALL_APACHE_MODS=true
    case $host in
      *-*-cygwin*)
        APACHE_LDFLAGS="-shrext .so"
        ;;
    esac
elif test x"$APXS" != x"no"; then
    echo "=================================================================="
    echo "WARNING: skipping the build of mod_dav_svn"
    echo "         try using --with-apxs"
    echo "=================================================================="
fi

AC_SUBST(APXS)
AC_SUBST(APACHE_LDFLAGS)
AC_SUBST(APACHE_INCLUDES)
AC_SUBST(APACHE_LIBEXECDIR)
AC_SUBST(INSTALL_APACHE_MODS)
AC_SUBST(HTTPD_VERSION)

# there aren't any flags that interest us ...
#if test -n "$APXS" && test "$APXS" != "no"; then
#  CFLAGS="$CFLAGS `$APXS -q CFLAGS CFLAGS_SHLIB`"
#fi

if test -n "$APXS_CC" && test "$APXS_CC" != "$CC" ; then
  echo "=================================================================="
  echo "WARNING: You have chosen to compile Subversion with a different"
  echo "         compiler than the one used to compile Apache."
  echo ""
  echo "    Current compiler:      $CC"
  echo "   Apache's compiler:      $APXS_CC"
  echo ""
  echo "This could cause some problems."
  echo "=================================================================="
fi

])
