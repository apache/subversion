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

AC_DEFUN(SVN_LIB_NEON,
[
  NEON_WANTED_REGEX="$1"
  NEON_LATEST_WORKING_VER="$2"
  NEON_URL="$3"

  AC_MSG_NOTICE([checking neon library])

  AC_ARG_WITH(neon,
              [AC_HELP_STRING([--with-neon=PREFIX], 
	      [Determine neon library configuration based on 
	      'PREFIX/bin/neon-config'. Default is to search for neon 
	      in a subdirectory of the top source directory and then to
	      look for neon-config in $PATH.])],
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
      NEON_VERSION=`$abs_srcdir/ac-helpers/get-neon-ver.sh $abs_srcdir/neon`
      AC_MSG_RESULT([$NEON_VERSION])
      case "$NEON_VERSION" in
        $NEON_WANTED_REGEX)
          echo "Using neon found in source directory."
          SVN_NEON_INCLUDES=-'I$(abs_srcdir)/neon/src'
          NEON_LIBS="\$(abs_builddir)/neon/src/libneon.la"

dnl Configure neon --------------------------
          # The arguments passed to this configure script are passed down to
          # neon's configure script, but, since neon defaults to *not* building
          # shared libs, and we default to building shared libs, we have to 
          # explicitly pass down an --{enable,disable}-shared argument, to make
          # sure neon does the same as we do.
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
          SVN_SUBDIR_CONFIG(neon, $args)

          if test -f "$abs_builddir/neon/neon-config" ; then
            AC_MSG_CHECKING([for any extra libraries neon needs])
            # this is not perfect since it will pick up extra -L flags too,
            # but that shouldn't do any real damage.
            NEON_LIBS_NEW=`$SHELL $abs_builddir/neon/neon-config --libs | sed -e"s/-lneon//g"`
            AC_MSG_RESULT([$NEON_LIBS_NEW])
            NEON_LIBS="$NEON_LIBS $NEON_LIBS_NEW"
            # Also find out which macros neon defines (but ignore extra include paths):
            # this will include -DNEON_SSL if neon was built with SSL support
            changequote(<<, >>)dnl
            CFLAGS="$CFLAGS `$SHELL $abs_builddir/neon/neon-config --cflags | sed -e 's/-I[^ ]*//g'`"
            changequote([, ])dnl
          fi

          SVN_SUBDIRS="$SVN_SUBDIRS neon"
          ;;

        *)
          echo "You have a neon/ subdir containing version $NEON_VERSION,"
          echo "but Subversion needs neon ${NEON_LATEST_WORKING_VER}."
          SVN_DOWNLOAD_NEON()
          ;;
      esac
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
  if test "$neon_config" != ""; then
    AC_MSG_CHECKING([neon library version])
    NEON_VERSION=`$neon_config --version | sed -e 's/^neon //'`
    AC_MSG_RESULT([$NEON_VERSION])

    case "$NEON_VERSION" in
      $NEON_WANTED_REGEX)
        changequote(<<, >>)dnl
        SVN_NEON_INCLUDES=`$neon_config --cflags | sed -e 's/-D[^ ]*//g'`
        NEON_LIBS=`$neon_config --libs`
        CFLAGS="$CFLAGS `$neon_config --cflags | sed -e 's/-I[^ ]*//g'`"
        changequote([, ])dnl
        ;;
      *)
        echo "You have neon version $NEON_VERSION,"
        echo "but Subversion needs neon $NEON_LATEST_WORKING_VER."
        SVN_DOWNLOAD_NEON()
        ;;
    esac
  else
    # no neon subdir, no neon-config in PATH
    AC_MSG_RESULT([nothing])
    echo "No suitable neon can be found."
    SVN_DOWNLOAD_NEON()
  fi
])

dnl SVN_DOWNLOAD_NEON()
dnl no neon found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_NEON,
[
  echo "Please either install neon ${NEON_LATEST_WORKING_VER} on this system"
  echo ""
  echo "or"
  echo ""
  echo "get neon ${NEON_LATEST_WORKING_VER} from:"
  echo "    ${NEON_URL}"
  echo "unpack the archive using tar/gunzip and rename the resulting"
  echo "directory from ./neon-${NEON_LATEST_WORKING_VER}/ to ./neon/"
  AC_MSG_ERROR([no suitable neon found])
])
