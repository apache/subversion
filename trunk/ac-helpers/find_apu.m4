dnl
dnl find_apu.m4 : locate the APR-util (APU) include files and libraries
dnl
dnl This macro file can be used by applications to find and use the APU
dnl library. It provides a standardized mechanism for using APU. It supports
dnl embedding APU into the application source, or locating an installed
dnl copy of APU.
dnl
dnl APR_FIND_APU([srcdir, path])
dnl
dnl   where srcdir is the location of the bundled APU source directory, or
dnl   empty if source is not bundled.
dnl   where path is the prefix to the location where the bundled APU will
dnl   will be built.
dnl
dnl Sets the following variables on exit:
dnl
dnl   apu_found : "yes", "no", "reconfig"
dnl
dnl   apu_config : If the apu-config tool exists, this refers to it.  If
dnl                apu_found is "reconfig", then the bundled directory
dnl                should be reconfigured *before* using apu_config.
dnl
dnl Note: At this time, we cannot find *both* a source dir and a build dir.
dnl       If both are available, the build directory should be passed to
dnl       the --with-apr-util switch.
dnl
dnl Note: the installation layout is presumed to follow the standard
dnl       PREFIX/lib and PREFIX/include pattern. If the APU config file
dnl       is available (and can be found), then non-standard layouts are
dnl       possible, since it will be described in the config file.
dnl
dnl If a bundled source directory is available and needs to be (re)configured,
dnl then apu_found is set to "reconfig". The caller should reconfigure the
dnl (passed-in) source directory, placing the result in the build directory,
dnl as appropriate.
dnl
dnl If apu_found is "yes" or "reconfig", then the caller should use the
dnl value of apu_config to fetch any necessary build/link information.
dnl

AC_DEFUN(APR_FIND_APU, [
  apu_found="no"

  AC_MSG_CHECKING(for APR-util)
  AC_ARG_WITH(apr-util,
  [  --with-apr-util=DIR     prefix for installed APU, or path to APU build tree],
  [
    if test "$withval" = "no" || test "$withval" = "yes"; then
      AC_MSG_ERROR([--with-apr-util requires a directory to be provided])
    fi

    if test -x "$withval/bin/apu-config"; then
       apu_found="yes"
       apu_config="$withval/bin/apu-config"
    elif test -x "$withval/apu-config"; then
       dnl Already configured build dir
       apu_found="yes"
       apu_config="$withval/apu-config"
    elif test -x "$withval" && $withval --help > /dev/null 2>&1 ; then
       apu_found="yes"
       apu_config="$withval"
    fi

    dnl if --with-apr-util is used, then the target prefix/directory must
    dnl be valid
    if test "$apu_found" != "yes"; then
      AC_MSG_ERROR([
The directory given to --with-apr-util does not specify a prefix for an 
installed APU, nor an APR-util build directory.])
    fi
  ],[
    if apu-config --help > /dev/null 2>&1 ; then
      apu_found="yes"
      apu_config="apu-config"
    else
      dnl look in the some standard places (apparently not in builtin/default)
      for lookdir in /usr /usr/local /opt/apr ; do
        if test -x "$lookdir/bin/apu-config"; then
          apu_found="yes"
          apu_config="$lookdir/bin/apu-config"
          break
        fi
      done
    fi
    dnl if we have a bundled source directory, then we may have more work
    if test -d "$1"; then
      apu_temp_abs_srcdir="`cd $1 && pwd`"
      if test "$apu_found" = "yes" \
         && test "`$apu_config --srcdir`" = "$apu_temp_abs_srcdir"; then
        dnl the installed apu-config represents our source directory, so
        dnl pretend we didn't see it and just use our bundled source
        apu_found="no"
      fi
      dnl We could not find an apu-config; use the bundled one
      if test "$apu_found" = "no"; then
        apu_found="reconfig"
        if test -n "$2"; then
          apu_config="$2/apu-config"
        else
          apu_config="$1/apu-config"
        fi
      fi
    fi
  ])

  AC_MSG_RESULT($apu_found)
])
