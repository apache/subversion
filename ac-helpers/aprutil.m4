dnl  SVN_LIB_APRUTIL(version)
dnl
dnl  Check configure options and assign variables related to
dnl  the Apache Portable Runtime Utilities (APRUTIL) library.
dnl
dnl  If there is an apr-util source directory, there *must* be a
dnl  corresponding apr source directory.  APRUTIL's build system
dnl  is too tied in with apr.  (You can't use an installed APR and
dnl  a source APR-util.)
dnl


AC_DEFUN(SVN_LIB_APRUTIL,
[
  AC_MSG_NOTICE([Apache Portable Runtime Utility (APRUTIL) library configuration])

  APR_FIND_APU("$srcdir/apr-util", "./apr-util")

  if test $apu_found = "no"; then
    AC_MSG_WARN([APRUTIL not found])
    SVN_DOWNLOAD_APRUTIL
  fi

  if test $apu_found = "reconfig"; then
    dnl apr-util configure relies on the caller providing MKDIR
    ac_configure_args_save=$ac_configure_args
    ac_configure_args="$ac_configure_args MKDIR=\"$MKDIR\""
    SVN_SUBDIR_CONFIG(apr-util, --with-apr=../apr)
    ac_configure_args=$ac_configure_args_save
    SVN_SUBDIRS="$SVN_SUBDIRS apr-util"
  fi

  dnl Get libraries and thread flags from APRUTIL ---------------------

  LDFLAGS="$LDFLAGS `$apu_config --ldflags`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --ldflags failed])
  fi

  SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES `$apu_config --includes`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --includes failed])
  fi

  SVN_APRUTIL_LIBS="`$apu_config --link-libtool --libs`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --link-libtool --libs failed])
  fi

  AC_SUBST(SVN_APRUTIL_LIBS)
])

dnl SVN_DOWNLOAD_APRUTIL()
dnl no apr-util found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_APRUTIL,
[
  echo "The Apache Portable Runtime Utility (APRUTIL) library cannot be found."
  echo "Either install APRUTIL on this system and supply the appropriate"
  echo "--with-apr-util option"
  echo ""
  echo "or"
  echo ""
  echo "get it with CVS and put it in a subdirectory of this source:"
  echo ""
  echo "   cvs -d :pserver:anoncvs@cvs.apache.org:/home/cvspublic login"
  echo "      (password 'anoncvs')"
  echo ""
  echo "   cvs -d :pserver:anoncvs@cvs.apache.org:/home/cvspublic co apr-util"
  echo ""
  echo "Run that right here in the top-level of the Subversion tree."
  echo ""
  AC_MSG_ERROR([no suitable APRUTIL found])
])
