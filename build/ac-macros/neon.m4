dnl   SVN_LIB_NEON(allowed_list_of_regex, recommended_ver, url)
dnl
dnl   Search for a suitable version of neon.  allowed_list_of_regex is a
dnl   list of regular expressions used in a grep invocation
dnl   to match versions of Neon that can be used.  recommended_ver is the
dnl   recommended version of Neon, which is not necessarily the latest
dnl   released version of neon that exists.  url is the URL of the
dnl   recommended version of Neon.
dnl
dnl   If a --with-neon option is passed (no argument), then a search for
dnl   neon on the system is performed.
dnl
dnl   If a --with-neon=PREFIX option is passed search for a suitable
dnl   neon installed on the system whose configuration can be found in
dnl   PREFIX/bin/neon-config.
dnl
dnl   If the search for neon fails, set svn_lib_neon to no, otherwise set 
dnl   it to yes.

AC_DEFUN(SVN_LIB_NEON,
[
  NEON_ALLOWED_LIST="$1"
  NEON_RECOMMENDED_VER="$2"
  NEON_URL="$3"

  AC_MSG_NOTICE([checking neon library])

  AC_ARG_WITH(neon,
              AS_HELP_STRING([--with-neon=PREFIX], 
              [Determine neon library configuration based on 
              'PREFIX/bin/neon-config'. Default is to search for
              neon-config in $PATH.]),
  [
    if test "$withval" = "yes" ; then
      if test -n "$PKG_CONFIG" && $PKG_CONFIG neon --exists ; then
        NEON_PKG_CONFIG="yes"
      else
        AC_PATH_PROG(neon_config, neon-config)
      fi
    else
      neon_config="$withval/bin/neon-config"
    fi

    SVN_NEON_CONFIG()
  ])
  
  AC_SUBST(SVN_NEON_INCLUDES)
  AC_SUBST(NEON_LIBS)
])

dnl SVN_NEON_CONFIG()
dnl neon-config found, gather relevant information from it
AC_DEFUN(SVN_NEON_CONFIG,
[
  if test "$NEON_PKG_CONFIG" = "yes" || test -f "$neon_config"; then
    if test "$NEON_PKG_CONFIG" = "yes" || test "$neon_config" != ""; then
      AC_MSG_CHECKING([neon library version])
      if test "$NEON_PKG_CONFIG" = "yes" ; then
        NEON_VERSION=`$PKG_CONFIG neon --modversion`
      else
        NEON_VERSION=`$neon_config --version | sed -e 's/^neon //'`
      fi
      AC_MSG_RESULT([$NEON_VERSION])

      if test -n ["`echo "$NEON_VERSION" | grep '^0\.2[6-9]\.'`"] ; then
        AC_DEFINE_UNQUOTED([SVN_NEON_0_26], [1],
                           [Define to 1 if you have Neon 0.26 or later.])
      fi

      if test -n ["`echo "$NEON_VERSION" | grep '^0\.2[7-9]\.'`"] ; then
        AC_DEFINE_UNQUOTED([SVN_NEON_0_27], [1],
                           [Define to 1 if you have Neon 0.27 or later.])
      fi

      if test -n ["`echo "$NEON_VERSION" | grep '^0\.2[8-9]\.'`"] ; then
        AC_DEFINE_UNQUOTED([SVN_NEON_0_28], [1],
                           [Define to 1 if you have Neon 0.28 or later.])
      fi

      for svn_allowed_neon in $NEON_ALLOWED_LIST; do
        if test -n "`echo "$NEON_VERSION" | grep "^$svn_allowed_neon"`" ||
           test "$svn_allowed_neon" = "any"; then
            svn_allowed_neon_on_system="yes"
            if test "$NEON_PKG_CONFIG" = "yes"; then
              SVN_NEON_INCLUDES=[`$PKG_CONFIG neon --cflags | sed -e 's/-D[^ ]*//g'`]
              CFLAGS=["$CFLAGS `$PKG_CONFIG neon --cflags | sed -e 's/-I[^ ]*//g'`"]
              old_CFLAGS="$CFLAGS"
              old_LIBS="$LIBS"
              NEON_LIBS=`$PKG_CONFIG neon --libs`
              CFLAGS="$CFLAGS $SVN_NEON_INCLUDES"
              LIBS="$LIBS $NEON_LIBS"
              neon_test_code="
#include <ne_compress.h>
#include <ne_xml.h>
int main()
{ne_xml_create(); ne_decompress_destroy(NULL);}"
              AC_LINK_IFELSE([$neon_test_code], shared_linking="yes", shared_linking="no")
              if test "$shared_linking" = "no"; then
                NEON_LIBS=`$PKG_CONFIG neon --libs --static`
                LIBS="$LIBS $NEON_LIBS"
                AC_LINK_IFELSE([$neon_test_code], , AC_MSG_ERROR([cannot find Neon]))
              fi
              CFLAGS="$old_CFLAGS"
              LIBS="$old_LIBS"
            else
              SVN_NEON_INCLUDES=[`$neon_config --cflags | sed -e 's/-D[^ ]*//g'`]
              CFLAGS=["$CFLAGS `$neon_config --cflags | sed -e 's/-I[^ ]*//g'`"]
              NEON_LIBS=`$neon_config --libs`
            fi
            svn_lib_neon="yes"
            break
        fi
      done

      if test -z $svn_allowed_neon_on_system; then
        echo "You have neon version $NEON_VERSION,"
        echo "but Subversion needs neon $NEON_RECOMMENDED_VER."
        SVN_DOWNLOAD_NEON()
      fi

    else
      # no neon subdir, no neon-config in PATH
      AC_MSG_RESULT([nothing])
      echo "No suitable neon can be found."
      SVN_DOWNLOAD_NEON()
    fi

  else
    # user probably passed --without-neon, or --with-neon=/something/dumb
    SVN_DOWNLOAD_NEON()
  fi
])

dnl SVN_DOWNLOAD_NEON()
dnl no neon found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_NEON,
[
  echo ""
  echo "An appropriate version of neon could not be found, so libsvn_ra_neon"
  echo "will not be built.  If you want to build libsvn_ra_neon, please"
  echo "install neon ${NEON_RECOMMENDED_VER} on this system."
  echo ""
  AC_MSG_RESULT([no suitable neon found])
  svn_lib_neon="no"
])
