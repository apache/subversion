dnl  SVN_LIB_APRUTIL(version)
dnl
dnl  Check configure options and assign variables related to
dnl  the Apache Portable Runtime Utilities (APRUTIL) library.
dnl
dnl  If there is a apr-util/ subdir we assme we want to use it. In that 
dnl  case an option telling us to use a locally installed apr-util
dnl  triggers an error.
dnl
dnl  TODO : check apr-util version, link a test program


AC_DEFUN(SVN_LIB_APRUTIL,
[
  AC_MSG_NOTICE([Apache Portable Runtime Utility (APRUTIL) library configuration])
  
  AC_ARG_WITH(apr-util,
              [AC_HELP_STRING([--with-apr-util=PREFIX], 
	      [Use APRUTIL at PREFIX])],
  [
    if test -d $abs_srcdir/apr-util ; then
      AC_MSG_ERROR([--with-apr-util option but apr-util/ subdir exists.
Please either remove that subdir or don't use the --with-apr-util option.])
    fi
    if test "$withval" = "no" ; then
      AC_MSG_ERROR([--with-apr-util=no is an illegal option])
    fi

    if test "$withval" != "yes" ; then
      aprutil_location="$withval"
    fi
  ])

  if test -d $abs_srcdir/apr-util ; then
    echo "Using apr-util found in source directory"
    SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES -I\$(abs_builddir)/apr-util/include"
    if test "$abs_srcdir" != "$abs_builddir" ; then
      SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES -I\$(abs_srcdir)/apr-util/include"
    fi

dnl ### need to fix the --with-apr line. requires fixes in apr-util to
dnl ### allow rational values (currently, it must point to a source distro)
    SVN_SUBDIR_CONFIG(apr-util, --with-apr=../apr)
    SVN_SUBDIRS="$SVN_SUBDIRS apr-util"
    SVN_APRUTIL_LIBS='$(abs_builddir)/libaprutil.la'
dnl ### aprutil will probably change this to PREFIX/bin/apr-util-config
    aprutil_config="$abs_builddir/apr-util/export_vars.sh"
  else
    SVN_FIND_APRUTIL
    SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES -I$aprutil_location/include"
    SVN_APRUTIL_LIBS="-L$aprutil_location/lib -laprutil"
dnl ### aprutil will probably change this to PREFIX/bin/apr-util-config
    aprutil_config="$aprutil_location/export_vars.sh"
  fi
])

dnl SVN_FIND_APRUTIL()
dnl Look in standard places for APRUTIL headers and libraries
AC_DEFUN(SVN_FIND_APRUTIL,
[
  AC_CHECK_HEADER(apu.h)
  AC_CHECK_LIB(aprutil, apr_uri_parse)
dnl ### see that we actually found the header and lib. if not found, then
dnl ### call SVN_DOWNLOAD_APRUTIL.
dnl ### need figure out where to put the results of this search. let it
dnl ### go into LIBS, or move it to SVN_APRUTIL_LIBS.
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
