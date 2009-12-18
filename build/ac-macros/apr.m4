dnl
dnl  SVN_LIB_APR(wanted_regex, alt_wanted_regex)
dnl
dnl  'wanted_regex' and 'alt_wanted_regex are regular expressions
dnl  that the apr version string must match.
dnl
dnl  Check configure options and assign variables related to
dnl  the Apache Portable Runtime (APR) library.
dnl

AC_DEFUN(SVN_LIB_APR,
[
  APR_WANTED_REGEXES="$1"

  AC_MSG_NOTICE([Apache Portable Runtime (APR) library configuration])

  APR_FIND_APR("$abs_srcdir/apr", "$abs_builddir/apr", 1, [1 0])

  if test $apr_found = "no"; then
    AC_MSG_WARN([APR not found])
    SVN_DOWNLOAD_APR
  fi

  if test $apr_found = "reconfig"; then
    SVN_EXTERNAL_PROJECT([apr])
  fi

  dnl check APR version number against regex  

  AC_MSG_CHECKING([APR version])    
  apr_version="`$apr_config --version`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --version failed])
  fi
  AC_MSG_RESULT([$apr_version])

  APR_WANTED_REGEX_MATCH=0
  for apr_wanted_regex in $APR_WANTED_REGEXES; do
    if test `expr $apr_version : $apr_wanted_regex` -ne 0; then
      APR_WANTED_REGEX_MATCH=1
      break
    fi
  done
      
  if test $APR_WANTED_REGEX_MATCH -eq 0; then
    echo "wanted regexes are $APR_WANTED_REGEXES"
    AC_MSG_ERROR([invalid apr version found])
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

  SVN_APR_PREFIX="`$apr_config --prefix`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --prefix failed])
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

  SVN_APR_SHLIB_PATH_VAR="`$apr_config --shlib-path-var`"
  if test $? -ne 0; then
    AC_MSG_ERROR([apr-config --shlib-path-var failed])
  fi

  AC_SUBST(SVN_APR_PREFIX)
  AC_SUBST(SVN_APR_INCLUDES)
  AC_SUBST(SVN_APR_LIBS)
  AC_SUBST(SVN_APR_EXPORT_LIBS)
  AC_SUBST(SVN_APR_SHLIB_PATH_VAR)
])

dnl SVN_DOWNLOAD_APR()
dnl no apr found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_APR,
[
  echo "The Apache Portable Runtime (APR) library cannot be found."
  echo "Please install APR on this system and supply the appropriate"
  echo "--with-apr option to 'configure'"
  echo ""
  echo "or"
  echo ""
  echo "get it with SVN and put it in a subdirectory of this source:"
  echo ""
  echo "   svn co \\"
  echo "    http://svn.apache.org/repos/asf/apr/apr/branches/1.2.x \\"
  echo "    apr"
  echo ""
  echo "Run that right here in the top level of the Subversion tree."
  echo "Afterwards, run apr/buildconf in that subdirectory and"
  echo "then run configure again here."
  echo ""
  echo "Whichever of the above you do, you probably need to do"
  echo "something similar for apr-util, either providing both"
  echo "--with-apr and --with-apr-util to 'configure', or"
  echo "getting both from SVN with:"
  echo ""
  echo "   svn co \\"
  echo "    http://svn.apache.org/repos/asf/apr/apr-util/branches/1.2.x \\"
  echo "    apr-util"
  echo ""
  AC_MSG_ERROR([no suitable apr found])
])
