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
dnl   apu_libdir : A custom directory to use for linking (the -L switch).
dnl                If APU exists in a standard location, this variable
dnl                will be empty
dnl
dnl   apu_la_file : If a libtool .la file exists, this will refer to it. If
dnl                 there is no .la file, then this variable will be empty.
dnl
dnl   apu_includes : Where the APU includes are located, if a non-standard
dnl                  location. This variable has the format "-Idir -Idir".
dnl                  It may specify more than one directory.
dnl
dnl   apu_srcdir : If an APU source tree is available and needs to be
dnl                (re)configured, this refers to it.
dnl
dnl   apu_config : If the apr-config tool exists, this refers to it.
dnl
dnl   apu_found : "yes", "no", "reconfig"
dnl
dnl Note: At this time, we cannot find *both* a source dir and a build dir.
dnl       If both are available, the build directory should be passed to
dnl       the --with-apr switch (apu_srcdir will be empty).
dnl
dnl Note: the installation layout is presumed to follow the standard
dnl       PREFIX/lib and PREFIX/include pattern. If the APU config file
dnl       is available (and can be found), then non-standard layouts are
dnl       possible, since it will be described in the config file.
dnl
dnl If apu_found is "yes" or "reconfig", then the caller should link using
dnl apu_la_file, if available; otherwise, -lapr should be used (and if
dnl apu_libdir is not null, then -L$apr_libdir). If apr_includes is not null,
dnl then it should be used during compilation.
dnl
dnl If a source directory is available and needs to be (re)configured, then
dnl apu_srcdir specifies the directory and apr_found is "reconfig".
dnl
dnl Note: This m4 macro set makes the assumption that APR has already
dnl       been found and properly configured.
dnl

AC_DEFUN(APR_FIND_APU, [
  apu_found="no"

  preserve_LIBS="$LIBS"
  preserve_LDFLAGS="$LDFLAGS"

  AC_MSG_CHECKING(for APR-util)
  AC_ARG_WITH(apr-util,
  [  --with-apr-util=DIR     prefix for installed APU, or path to APU build tree],
  [
    if test "$withval" = "no" || test "$withval" = "yes"; then
      AC_MSG_ERROR([--with-apr-util requires a directory to be provided])
    fi

    if test -x "$withval/bin/apu-config"; then
       apu_config="$withval/bin/apu-config"
       apu_found="yes"
       apu_libdir="$withval/lib"
       apu_includes="-I$withval/include"
    else
       dnl look for a build tree (note: already configured/built)
       if test -f "$withval/libaprutil.la"; then
        apu_found="yes"
        apu_libdir=""
        apu_la_file="$withval/libaprutil.la"
        if test -x "$withval/apu-config"; then
          apu_config="$withval/apu-config"
        else
          apu_config=""
        fi
        apu_includes="-I$withval/include"
      fi
    fi

    dnl if --with-apr is used, then the target prefix/directory must be valid
    if test "$apu_found" != "yes"; then
      AC_MSG_ERROR([
The directory given to --with-apr-util does not specify a prefix for an 
installed APU, nor an APR-util build directory.])
    fi
  ],[
    dnl always look in the builtin/default places
    LIBS="$LIBS -laprutil"
    AC_TRY_LINK_FUNC(apr_uri_parse, [
        dnl We don't have to do anything.
        apu_found="yes"
        apu_srcdir=""
        apu_libdir=""
        apu_includes=""
        apu_libtool=""
        apu_la_file=""
        apu_config=""
      ], [
      dnl look in the some standard places (apparently not in builtin/default)
      for lookdir in /usr /usr/local /opt/apr ; do
        if test "$apu_found" != "yes"; then
          LDFLAGS="$preserve_LDFLAGS -L$lookdir/lib"
          AC_TRY_LINK_FUNC(apr_uri_parse, [
            apu_found="yes"
            apu_libdir="$lookdir/lib" 
            apu_includes="-I$lookdir/include"
            if test -x "$withval/bin/apu-config"; then
              apu_config="$withval/bin/apu-config"
            else
              apu_config=""
            fi
          ])
        fi
      done
    ])
    dnl We attempt to guess what the data will be *after* configure is run.
    dnl Note, if we don't see configure, but do have configure.in, it'd be
    dnl nice to run buildconf, but that's for another day.
    if test "$apu_found" = "no" && test -d "$1" && test -x "$1/configure"; then
      apu_found="reconfig"
      apu_srcdir="$1"
      if test -n "$2"; then
        apu_builddir="$2/"
      else
        apu_builddir=""
      fi
      apu_libdir=""
      apu_la_file="$apu_builddir$apu_srcdir/libaprutil.la"
      if test -f "$apu_builddir$apu_srcdir/apu-config.in"; then
        apu_config="$apu_builddir$apu_srcdir/apu-config"
      else
        apu_config=""
      fi
      apu_includes="-I$apu_builddir$apu_srcdir/include"
    fi
  ])

  if test "$apu_found" != "no" && test "$apr_libdir" != ""; then
    if test "$apu_la_file" = "" && test -f "$apr_libdir/libapr.la"; then
      apu_la_file="$apr_libdir/libaprutil.la"
    fi
  fi

  AC_MSG_RESULT($apu_found)
  LIBS="$preserve_LIBS"
  LDFLAGS="$preserve_LDFLAGS"
])
