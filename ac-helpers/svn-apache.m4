dnl
dnl Macros to find an Apache installation
dnl
dnl This will find either an installed Apache, or an Apache source directory.
dnl
dnl Note: If we don't have an installed Apache, then we can't install the
dnl       (dynamic) mod_dav_svn.so module. Similarly, without an Apache
dnl       source dir, we cannot create static builds of the system.
dnl

AC_DEFUN(SVN_FIND_APACHE,[

AC_MSG_CHECKING(for static Apache module support)
AC_ARG_WITH(apache,
[  --with-apache=DIR      Build static Apache module.  DIR is the path
                         to the top-level Apache source directory.],
[
	if test "$withval" = "yes"; then
		AC_MSG_ERROR(You need to specify a directory with --with-apache)
	fi

        if test -r $withval/modules/dav/main/mod_dav.h; then
		APACHE_INCLUDES="$APACHE_INCLUDES -I$withval/include -I$withval/os/unix -I$withval/modules/dav/main -I$withval/srclib/apr/include -I$withval/srclib/apr-util/include"
		APACHE_TARGET=$withval/modules/dav/svn
		INSTALL_APACHE_RULE=install-mods-static
		BINNAME=mod_dav_svn.a

  		AC_MSG_RESULT(yes - Apache 2.0.x)

                if test ! -r $withval/srclib/apr/include/apr.h; then
                        AC_MSG_WARN(Apache 2.0.x is not configured)
                fi
	else
		dnl if they pointed us at the wrong place, then just bail
		AC_MSG_ERROR(no - Unable to locate $withval/modules/dav/main/mod_dav.h)
	fi
],[
    AC_MSG_RESULT(no)
])


AC_MSG_CHECKING(for Apache module support via DSO through APXS)
AC_ARG_WITH(apxs,
[[  --with-apxs[=FILE]      Build shared Apache module.  FILE is the optional
                          pathname to the Apache apxs tool; defaults to "apxs".]],
[
    if test "$BINNAME" != ""; then
      AC_MSG_ERROR(--with-apache and --with-apxs are mutually exclusive)
    fi

    if test "$withval" = "yes"; then
      APXS=apxs
    else
      APXS="$withval"
    fi
    APXS_EXPLICIT=1
])

if test "$BINNAME" = "" -a "$APXS" = ""; then
  for i in /usr/sbin /usr/local/apache/bin ; do
    if test -f "$i/apxs"; then
      APXS="$i/apxs"
    fi
  done
fi

if test -n "$APXS"; then
    APXS_INCLUDE="`$APXS -q INCLUDEDIR`"
    if test -r $APXS_INCLUDE/mod_dav.h; then
        AC_MSG_RESULT(found at $APXS)
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

if test -n "$APXS"; then
    BINNAME=mod_dav_svn.so
    INSTALL_IT="\$(APXS) -i -a -n dav_svn $BINNAME"

    APXS_CC="`$APXS -q CC`"
    APACHE_INCLUDES="$APACHE_INCLUDES -I$APXS_INCLUDE"

    INSTALL_APACHE_RULE=install-mods-shared

    AC_SUBST(APXS)
    AC_SUBST(BINNAME)
    AC_SUBST(INSTALL_IT)
fi

# If we did not find a way to build/install mod_dav, then bail out.
if test "$BINNAME" = ""; then
    echo "=================================================================="
    echo "WARNING: skipping the build of mod_dav_svn"
    echo "         --with-apxs or --with-apache must be used"
    echo "=================================================================="
else
    BUILD_APACHE_RULE=apache-mod
fi
AC_SUBST(APACHE_TARGET)
AC_SUBST(APACHE_INCLUDES)
AC_SUBST(BUILD_APACHE_RULE)
AC_SUBST(INSTALL_APACHE_RULE)

# there aren't any flags that interest us ...
#if test -n "$APXS"; then
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
