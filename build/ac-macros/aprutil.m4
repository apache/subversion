dnl  SVN_LIB_APRUTIL(wanted_regex, alt_wanted_regex)
dnl
dnl  'wanted_regex' and 'alt_wanted_regex are regular expressions 
dnl  that the aprutil version string must match.
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
  APRUTIL_WANTED_REGEXES="$1"

  AC_MSG_NOTICE([Apache Portable Runtime Utility (APRUTIL) library configuration])

  APR_FIND_APU("$abs_srcdir/apr-util", "$abs_builddir/apr-util", 1, [1 0])

  if test $apu_found = "no"; then
    AC_MSG_WARN([APRUTIL not found])
    SVN_DOWNLOAD_APRUTIL
  fi

  if test $apu_found = "reconfig"; then
    SVN_EXTERNAL_PROJECT([apr-util], [--with-apr=../apr])
  fi

  dnl check APRUTIL version number against regex  

  AC_MSG_CHECKING([APR-UTIL version])    
  apu_version="`$apu_config --version`"
  if test $? -ne 0; then
    # This is a hack as suggested by Ben Collins-Sussman.  It can be
    # removed after apache 2.0.44 has been released.  (The apu-config
    # shipped in 2.0.43 contains a correct version number, but
    # stupidly doesn't understand the --version switch.)
    apu_version=`grep "APRUTIL_DOTTED_VERSION=" $(which $apu_config) | tr -d "APRUTIL_DOTTED_VERSION="| tr -d '"'`
    #AC_MSG_ERROR([
    #    apu-config --version failed.
    #    Your apu-config doesn't support the --version switch, please upgrade
    #    to APR-UTIL more recent than 2002-Nov-05.])
  fi
  AC_MSG_RESULT([$apu_version])

  APU_WANTED_REGEX_MATCH=0
  for apu_wanted_regex in $APRUTIL_WANTED_REGEXES; do
    if test `expr $apu_version : $apu_wanted_regex` -ne 0; then
      APU_WANTED_REGEX_MATCH=1
      break
    fi
  done

  if test $APU_WANTED_REGEX_MATCH -eq 0; then
    echo "wanted regexes are $APRUTIL_WANTED_REGEXES"
    AC_MSG_ERROR([invalid apr-util version found])
  fi

  dnl Get libraries and thread flags from APRUTIL ---------------------

  LDFLAGS="$LDFLAGS `$apu_config --ldflags`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --ldflags failed])
  fi

  SVN_APRUTIL_INCLUDES="`$apu_config --includes`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --includes failed])
  fi

  SVN_APRUTIL_PREFIX="`$apu_config --prefix`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --prefix failed])
  fi

  dnl When APR stores the dependent libs in the .la file, we don't need
  dnl --libs.
  SVN_APRUTIL_LIBS="`$apu_config --link-libtool --libs`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --link-libtool --libs failed])
  fi

  SVN_APRUTIL_EXPORT_LIBS="`$apu_config --link-ld --libs`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apu-config --link-ld --libs failed])
  fi

  AC_SUBST(SVN_APRUTIL_INCLUDES)
  AC_SUBST(SVN_APRUTIL_LIBS)
  AC_SUBST(SVN_APRUTIL_EXPORT_LIBS)
  AC_SUBST(SVN_APRUTIL_PREFIX)

  dnl What version of Expat are we using? -----------------
  SVN_HAVE_OLD_EXPAT="`$apu_config --old-expat`"
  if test "$SVN_HAVE_OLD_EXPAT" = "yes"; then
    AC_DEFINE(SVN_HAVE_OLD_EXPAT, 1, [Defined if Expat 1.0 or 1.1 was found])
  fi
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
  echo "get it with SVN and put it in a subdirectory of this source:"
  echo ""
  echo "   svn co \\"
  echo "    http://svn.apache.org/repos/asf/apr/apr-util/branches/1.2.x \\"
  echo "    apr-util"
  echo ""
  echo "Run that right here in the top level of the Subversion tree."
  echo "Afterwards, run apr-util/buildconf in that subdirectory and"
  echo "then run configure again here."
  echo ""
  AC_MSG_ERROR([no suitable APRUTIL found])
])
