dnl  SVN_LIB_APR(version)
dnl
dnl  Check configure options and assign variables related to
dnl  the Apache Portable Runtime (APR) library.
dnl

AC_DEFUN(SVN_LIB_APR,
[

  AC_MSG_NOTICE([Apache Portable Runtime (APR) library configuration])

  APR_FIND_APR($srcdir/apr, $abs_builddir)

  if test $apr_found = "no"; then
    AC_MSG_WARN([APR not found])
    SVN_DOWNLOAD_APR
  fi

  if test $apr_found = "reconfig"; then
    SVN_SUBDIR_CONFIG($apr_srcdir)
    SVN_SUBDIRS="$SVN_SUBDIRS $apr_srcdir"
  fi

  dnl Get libraries and thread flags from APR ---------------------

  if test -x "$apr_config"; then
    CPPFLAGS="$CPPFLAGS `$apr_config --cppflags`"
    CFLAGS="$CFLAGS `$apr_config --cflags`"
    LIBS="$LIBS `$apr_config --libs`"
  else
    AC_MSG_WARN([apr-config not found])
    SVN_DOWNLOAD_APR
  fi

  SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES $apr_includes"
  if test "$abs_srcdir" != "$abs_builddir" && test -d "$abs_srcdir/apr" ; then
      SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES -I$abs_srcdir/apr/include"
  fi

  if test -z "$apr_la_file" ; then
    SVN_APR_LIBS="-lapr $LIBTOOL_LIBS"
  else
    SVN_APR_LIBS="$apr_la_file $LIBTOOL_LIBS"
  fi
  AC_SUBST(SVN_APR_LIBS)

])

dnl SVN_DOWNLOAD_APR()
dnl no apr found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_APR,
[
  echo "No Apache Portable Runtime (APR) library can be found."
  echo "Either install APR on this system and supply appropriate"
  echo "--with-apr-libs and --with-apr-includes options"
  echo ""
  echo "or"
  echo ""
  echo "get it with CVS and put it in a subdirectory of this source:"
  echo ""
  echo "   cvs -d :pserver:anoncvs@cvs.apache.org:/home/cvspublic login"
  echo "      (password 'anoncvs')"
  echo ""
  echo "   cvs -d :pserver:anoncvs@cvs.apache.org:/home/cvspublic co apr"
  echo ""
  echo "Run that right here in the top-level of the Subversion tree."
  echo ""
  AC_MSG_ERROR([no suitable apr found])
])
