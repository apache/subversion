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

  APR_FIND_APU(apr-util, $abs_builddir)

  if test $apu_found = "no"; then
    AC_MSG_WARN([APRUTIL not found])
    SVN_DOWNLOAD_APRUTIL
  fi

  if test $apu_found = "reconfig"; then
    SVN_SUBDIR_CONFIG($apu_srcdir, --with-apr=../apr)
    SVN_SUBDIRS="$SVN_SUBDIRS $apu_srcdir"
  fi

  dnl Get libraries and thread flags from APRUTIL ---------------------

  if test -x "$apu_config"; then
    SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES `$apu_config --includes`"
    LDFLAGS="$LDFLAGS `$apu_config --ldflags`"
  else
    AC_MSG_WARN([apu-config not found])
    SVN_DOWNLOAD_APRUTIL
  fi

  SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES $apu_includes"
  if test "$abs_srcdir" != "$abs_builddir" && test -d "$abs_srcdir/apr-util" ; then
      SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES -I$abs_srcdir/apr-util/include"
  fi

  if test -z "$apu_la_file" ; then
    SVN_APRUTIL_LIBS="-laprutil $LIBTOOL_LIBS `$apu_config --libs`"
  else
    SVN_APRUTIL_LIBS="$apu_la_file $LIBTOOL_LIBS `$apu_config --libs`"
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
