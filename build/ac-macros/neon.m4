dnl   SVN_LIB_NEON(wanted_regex, latest_working_ver, url)
dnl
dnl   Search for a suitable version of neon.  wanted_regex is a
dnl   regular expression used in a Bourne shell switch/case statement
dnl   to match versions of Neon that can be used.  latest_working_ver
dnl   is the latest version of neon that can be used, which is not
dnl   necessarily the latest released version of neon that exists.
dnl   url is the URL of the latest version of Neon.
dnl
dnl   If a --with-neon=PREFIX option is passed search for a suitable
dnl   neon installed on the system whose configuration can be found in
dnl   PREFIX/bin/neon-config.  In this case ignore any neon/ subdir 
dnl   within the source tree.
dnl
dnl   If no --with-neon option is passed look first for a neon/ subdir.
dnl   If a neon/ subdir exists and is the wrong version exit with a 
dnl   failure.  If no neon/ subdir is present search for a neon installed
dnl   on the system.
dnl
dnl   If the search for neon fails, set svn_lib_neon to no, otherwise set 
dnl   it to yes.

AC_DEFUN(SVN_LIB_NEON,
[
  NEON_ALLOWED_LIST="$1"
  NEON_LATEST_WORKING_VER="$2"
  NEON_URL="$3"

  AC_MSG_NOTICE([checking neon library])

  AC_ARG_WITH(neon,
              AC_HELP_STRING([--with-neon=PREFIX], 
	      [Determine neon library configuration based on 
	      'PREFIX/bin/neon-config'. Default is to search for neon 
	      in a subdirectory of the top source directory and then to
	      look for neon-config in $PATH.]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-neon requires an argument.])
    else
      neon_config="$withval/bin/neon-config"
    fi

    SVN_NEON_CONFIG()
  ],
  [
    if test -d $abs_srcdir/neon ; then
      AC_MSG_CHECKING([neon library version])

      NEON_VERSION=`cat $abs_srcdir/neon/.version`
      AC_MSG_RESULT([$NEON_VERSION])

      if test -n "`echo \"$NEON_VERSION\" | grep '^0\.2[[56]]\.'`" ; then
        AC_DEFINE_UNQUOTED([SVN_NEON_0_25], [1],
                           [Define to 1 if you have Neon 0.25 or later.])
      fi
      if test -n "`echo \"$NEON_VERSION\" | grep '^0\.26\.'`" ; then
        AC_DEFINE_UNQUOTED([SVN_NEON_0_26], [1],
                           [Define to 1 if you have Neon 0.26 or later.])
      fi

      for svn_allowed_neon in $NEON_ALLOWED_LIST; do
        if test "$NEON_VERSION" = "$svn_allowed_neon" ||
           test "$svn_allowed_neon" = "any"; then
          echo "Using neon found in source directory."
          svn_allowed_neon_in_srcdir="yes"
          SVN_NEON_INCLUDES=-'I$(abs_srcdir)/neon/src'
          NEON_LIBS="\$(abs_builddir)/neon/src/libneon.la"

dnl Configure neon --------------------------
          # The arguments passed to this configure script are passed
          # down to neon's configure script, but, since neon
          # defaults to *not* building shared libs, and we default
          # to building shared libs, we have to explicitly pass down
          # an --{enable,disable}-shared argument, to make sure neon
          # does the same as we do.
          if test "$enable_shared" = "yes"; then
            args="--enable-shared"
          else
            args="--disable-shared"
          fi

          # If we have apr-util and it's bundled expat, we can point neon
          # there, otherwise, neon is on its own to find expat. 
          if test -f "$abs_builddir/apr-util/xml/expat/lib/expat.h" ; then
            args="$args --with-expat='$abs_builddir/apr-util/xml/expat/lib/libexpat.la'"
          fi
          SVN_EXTERNAL_PROJECT([neon], [$args])

          if test -f "$abs_builddir/neon/neon-config" ; then
            # Also find out which macros neon defines (but ignore extra include paths):
            # this will include -DNEON_SSL if neon was built with SSL support
            CFLAGS=["$CFLAGS `$SHELL $abs_builddir/neon/neon-config --cflags | sed -e 's/-I[^ ]*//g'`"]
            SVN_NEON_INCLUDES=["$SVN_NEON_INCLUDES `$SHELL $abs_builddir/neon/neon-config --cflags | sed -e 's/-D[^ ]*//g'`"]
            svn_lib_neon="yes"
          fi

          break
        fi
      done

      if test -z $svn_allowed_neon_in_srcdir; then
        echo "You have a neon/ subdir containing version $NEON_VERSION,"
        echo "but Subversion needs neon ${NEON_LATEST_WORKING_VER}."
        SVN_DOWNLOAD_NEON()
      fi

    else
      # no --with-neon switch, and no neon subdir, look in PATH
      AC_PATH_PROG(neon_config,neon-config)
      SVN_NEON_CONFIG()
    fi

  ])
  
  AC_SUBST(SVN_NEON_INCLUDES)
  AC_SUBST(NEON_LIBS)
])

dnl SVN_NEON_CONFIG()
dnl neon-config found, gather relevant information from it
AC_DEFUN(SVN_NEON_CONFIG,
[
  if test -f "$neon_config"; then
    if test "$neon_config" != ""; then
      AC_MSG_CHECKING([neon library version])
      NEON_VERSION=`$neon_config --version | sed -e 's/^neon //'`
      AC_MSG_RESULT([$NEON_VERSION])

      if test -n "`echo \"$NEON_VERSION\" | grep '^0\.2[[56]]\.'`" ; then
        AC_DEFINE_UNQUOTED([SVN_NEON_0_25], [1],
                           [Define to 1 if you have Neon 0.25 or later.])
      fi
      if test -n "`echo \"$NEON_VERSION\" | grep '^0\.26\.'`" ; then
        AC_DEFINE_UNQUOTED([SVN_NEON_0_26], [1],
                           [Define to 1 if you have Neon 0.26 or later.])
      fi

      for svn_allowed_neon in $NEON_ALLOWED_LIST; do
        if test "$NEON_VERSION" = "$svn_allowed_neon" ||
           test "$svn_allowed_neon" = "any"; then
            svn_allowed_neon_on_system="yes"
            SVN_NEON_INCLUDES=[`$neon_config --cflags | sed -e 's/-D[^ ]*//g'`]
            NEON_LIBS=`$neon_config --la-file`
            CFLAGS=["$CFLAGS `$neon_config --cflags | sed -e 's/-I[^ ]*//g'`"]
            svn_lib_neon="yes"
            break
        fi
      done

      if test -z $svn_allowed_neon_on_system; then
        echo "You have neon version $NEON_VERSION,"
        echo "but Subversion needs neon $NEON_LATEST_WORKING_VER."
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
  echo "An appropriate version of neon could not be found, so libsvn_ra_dav"
  echo "will not be built.  If you want to build libsvn_ra_dav, please either"
  echo "install neon ${NEON_LATEST_WORKING_VER} on this system"
  echo ""
  echo "or"
  echo ""
  echo "get neon ${NEON_LATEST_WORKING_VER} from:"
  echo "    ${NEON_URL}"
  echo "unpack the archive using tar/gunzip and rename the resulting"
  echo "directory from ./neon-${NEON_LATEST_WORKING_VER}/ to ./neon/"
  echo ""
  AC_MSG_RESULT([no suitable neon found])
  svn_lib_neon="no"
])
