dnl
dnl  SVN_LIB_APR(version)
dnl
dnl  Check configure options and assign variables related to
dnl  the Apache Portable Runtime (APR) library.
dnl

AC_DEFUN(SVN_LIB_APR,
[
  AC_MSG_NOTICE([Apache Portable Runtime (APR) library configuration])

  APR_FIND_APR("$srcdir/apr", "./apr")

  if test $apr_found = "no"; then
    AC_MSG_WARN([APR not found])
    SVN_DOWNLOAD_APR
  fi

  if test $apr_found = "reconfig"; then
    SVN_SUBDIR_CONFIG(apr)
    SVN_SUBDIRS="$SVN_SUBDIRS apr"
  fi

  dnl Get build information from APR

  CPPFLAGS="$CPPFLAGS `$apr_config --cppflags`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --cppflags failed])
  fi

  CFLAGS="$CFLAGS `$apr_config --cflags`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --cflags failed])
  fi

  LDFLAGS="$LDFLAGS `$apr_config --ldflags`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --ldflags failed])
  fi

  SVN_APR_INCLUDES="`$apr_config --includes`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --includes failed])
  fi

  dnl When APR stores the dependent libs in the .la file, we don't need 
  dnl --libs.
  SVN_APR_LIBS="`$apr_config --link-libtool --libs`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --link-libtool --libs failed])
  fi

  SVN_APR_EXPORT_LIBS="`$apr_config --link-ld --libs`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --link-ld --libs failed])
  fi

  AC_SUBST(SVN_APR_INCLUDES)
  AC_SUBST(SVN_APR_LIBS)
  AC_SUBST(SVN_APR_EXPORT_LIBS)
])

dnl SVN_DOWNLOAD_APR()
dnl no apr found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_APR,
[
  echo "The Apache Portable Runtime (APR) library cannot be found."
  echo "Please install APR on this system and supply appropriate the"
  echo "--with-apr option to 'configure'"
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
