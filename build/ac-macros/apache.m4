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
  for i in /usr/sbin /usr/local/apache/bin /usr/local/apache2/bin /usr/bin ; do
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
        AC_MSG_ERROR(no - APXS refers to an old version of Apache
                     Unable to locate $APXS_INCLUDE/mod_dav.h)
    else
        AC_MSG_RESULT(no - Unable to locate $APXS_INCLUDE/mod_dav.h)
        APXS=""
    fi
else
    AC_MSG_RESULT(no)
fi

if test -n "$APXS" && test "$APXS" != "no"; then
  AC_MSG_CHECKING([whether Apache version is compatible with APR version])
  apr_major_version="${apr_version%%.*}"
  case "$apr_major_version" in
    0)
      apache_minor_version_wanted_regex="0"
      ;;
    1)
      apache_minor_version_wanted_regex=["[1-4]"]
      ;;
    *)
      AC_MSG_ERROR([unknown APR version])
      ;;
  esac
  AC_EGREP_CPP([apache_minor_version= *$apache_minor_version_wanted_regex],
               [
#include "$APXS_INCLUDE/ap_release.h"
apache_minor_version=AP_SERVER_MINORVERSION_NUMBER],
               [AC_MSG_RESULT([yes])],
               [AC_MSG_RESULT([no])
                AC_MSG_ERROR([Apache version incompatible with APR version])])
fi

AC_ARG_WITH(apache-libexecdir,
            [AS_HELP_STRING([[--with-apache-libexecdir[=PATH]]],
                            [Install Apache modules to PATH instead of Apache's
                             configured modules directory; PATH "no"
                             or --without-apache-libexecdir means install
                             to LIBEXECDIR.])],
[
    APACHE_LIBEXECDIR="$withval"
])

if test -n "$APXS" && test "$APXS" != "no"; then
    APXS_CC="`$APXS -q CC`"
    APACHE_INCLUDES="$APACHE_INCLUDES -I$APXS_INCLUDE"

    if test -z "$APACHE_LIBEXECDIR"; then
        APACHE_LIBEXECDIR="`$APXS -q libexecdir`"
    elif test "$APACHE_LIBEXECDIR" = 'no'; then
        APACHE_LIBEXECDIR="$libexecdir"
    fi

    BUILD_APACHE_RULE=apache-mod
    INSTALL_APACHE_RULE=install-mods-shared

    case $host in
      *-*-cygwin*)
        APACHE_LDFLAGS="-shrext .so"
        ;;
    esac
else
    echo "=================================================================="
    echo "WARNING: skipping the build of mod_dav_svn"
    echo "         try using --with-apxs"
    echo "=================================================================="
fi

AC_SUBST(APXS)
AC_SUBST(APACHE_LDFLAGS)
AC_SUBST(APACHE_INCLUDES)
AC_SUBST(APACHE_LIBEXECDIR)

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
