dnl
dnl find_apr.m4 : locate the APR include files and libraries
dnl
dnl This macro file can be used by applications to find and use the APR
dnl library. It provides a standardized mechanism for using APR. It supports
dnl embedding APR into the application source, or locating an installed
dnl copy of APR.
dnl
dnl APR_FIND_APR([srcdir [, builddir]])
dnl
dnl   where srcdir is the location of the bundled APR source directory, or
dnl   empty if source is not bundled.
dnl
dnl   where blddir is the location where the bundled APR will will be built,
dnl   or empty if the build will occur in the srcdir.
dnl
dnl
dnl Sets the following variables on exit:
dnl
dnl   apr_found : "yes", "no", "reconfig"
dnl
dnl   apr_config : If the apr-config tool exists, this refers to it. If
dnl                apr_found is "reconfig", then the bundled directory
dnl                should be reconfigured *before* using apr_config.
dnl
dnl Note: this macro file assumes that apr-config has been installed; it
dnl       is normally considered a required part of an APR installation.
dnl
dnl If a bundled source directory is available and needs to be (re)configured,
dnl then apr_found is set to "reconfig". The caller should reconfigure the
dnl (passed-in) source directory, placing the result in the build directory,
dnl as appropriate.
dnl
dnl If apr_found is "yes" or "reconfig", then the caller should use the
dnl value of apr_config to fetch any necessary build/link information.
dnl

AC_DEFUN(APR_FIND_APR, [
  apr_found="no"

  AC_MSG_CHECKING(for APR)
  AC_ARG_WITH(apr,
  [  --with-apr=DIR|FILE     prefix for installed APR, path to APR build tree,
                          or the full path to apr-config],
  [
    if test "$withval" = "no" || test "$withval" = "yes"; then
      AC_MSG_ERROR([--with-apr requires a directory to be provided])
    fi

    if test -x "$withval/bin/apr-config"; then
      apr_found="yes"
      apr_config="$withval/bin/apr-config"
    elif test -x "$withval/apr-config"; then
      apr_found="yes"
      apr_config="$withval/apr-config"
    elif test -x "$withval" && $withval --help > /dev/null 2>&1 ; then
      apr_found="yes"
      apr_config="$withval"
    fi

    dnl if --with-apr is used, then the target prefix/directory must be valid
    if test "$apr_found" != "yes"; then
      AC_MSG_ERROR([the --with-apr parameter is incorrect. It must specify an install prefix, a
build directory, or an apr-config file.])
    fi
  ],[
    dnl always look in the builtin/default places
    if apr-config --help > /dev/null 2>&1 ; then
      apr_found="yes"
      apr_config="apr-config"
    else
      dnl look in some standard places (apparently not in builtin/default)
      for lookdir in /usr /usr/local /opt/apr ; do
        if test -x "$lookdir/bin/apr-config"; then
          apr_found="yes"
          apr_config="$lookdir/bin/apr-config"
          break
        fi
      done
    fi
    dnl if we have a bundled source directory, then we may have more work
    if test -n "$1"; then
      apr_temp_abs_srcdir="`cd $1 && pwd`"
      if test "$apr_found" = "yes" \
              -a "`$apr_config --srcdir`" = "$apr_temp_abs_srcdir"; then
        dnl the installed apr-config represents our source directory, so
        dnl pretend we didn't see it and just use our bundled source
        apr_found="no"
      fi
      dnl We could not find an apr-config; use the bundled one
      if test "$apr_found" = "no"; then
        apr_found="reconfig"
        if test -n "$2"; then
          apr_config="$2/apr-config"
        else
          apr_config="$1/apr-config"
        fi
      fi
    fi
  ])

  AC_MSG_RESULT($apr_found)
])
