dnl
dnl find_apr.m4 : locate the APR include files and libraries
dnl
dnl This macro file can be used by applications to find and use the APR
dnl library. It provides a standardized mechanism for using APR. It supports
dnl embedding APR into the application source, or locating an installed
dnl copy of APR.
dnl
dnl APR_FIND_APR([srcdir, path])
dnl
dnl   where srcdir is the location of the bundled APR source directory, or
dnl   empty if source is not bundled.
dnl   where path is the prefix to the location where the bundled APR will
dnl   will be built.
dnl
dnl
dnl Sets the following variables on exit:
dnl
dnl   apr_libdir : A custom directory to use for linking (the -L switch).
dnl                If APR exists in a standard location, this variable
dnl                will be empty
dnl
dnl   apr_la_file : If a libtool .la file exists, this will refer to it. If
dnl                 there is no .la file, then this variable will be empty.
dnl
dnl   apr_includes : Where the APR includes are located, if a non-standard
dnl                  location. This variable has the format "-Idir -Idir".
dnl                  It may specify more than one directory.
dnl
dnl   apr_srcdir : If an APR source tree is available and needs to be
dnl                (re)configured, this refers to it.
dnl
dnl   apr_config : If the apr-config tool exists, this refers to it.
dnl
dnl   apr_vars : If the APR config file (APRVARS) exists, this refers to it.
dnl
dnl   apr_found : "yes", "no", "reconfig"
dnl
dnl Note: At this time, we cannot find *both* a source dir and a build dir.
dnl       If both are available, the build directory should be passed to
dnl       the --with-apr switch (apr_srcdir will be empty).
dnl
dnl Note: the installation layout is presumed to follow the standard
dnl       PREFIX/lib and PREFIX/include pattern. If the APR config file
dnl       is available (and can be found), then non-standard layouts are
dnl       possible, since it will be described in the config file.
dnl
dnl If apr_found is "yes" or "reconfig", then the caller should link using
dnl apr_la_file, if available; otherwise, -lapr should be used (and if
dnl apr_libdir is not null, then -L$apr_libdir). If apr_includes is not null,
dnl then it should be used during compilation.
dnl
dnl If a source directory is available and needs to be (re)configured, then
dnl apr_srcdir specifies the directory and apr_found is "reconfig".
dnl

AC_DEFUN(APR_FIND_APR, [
  apr_found="no"

  preserve_LIBS="$LIBS"
  preserve_LDFLAGS="$LDFLAGS"
  preserve_CFLAGS="$CFLAGS"

  AC_MSG_CHECKING(for APR)
  AC_ARG_WITH(apr,
  [  --with-apr=DIR          prefix for installed APR, or path to APR build tree],
  [
    if test "$withval" = "no" || test "$withval" = "yes"; then
      AC_MSG_ERROR([--with-apr requires a directory to be provided])
    fi

    if test -x "$withval/bin/apr-config"; then
       apr_config="$withval/bin/apr-config"
       CFLAGS="$CFLAGS `$withval/bin/apr-config --cflags`"
       LIBS="$LIBS `$withval/bin/apr-config --libs`"
       LDFLAGS="$LDFLAGS `$withval/bin/apr-config --ldflags`"
    else
       apr_config=""
    fi

    LIBS="$LIBS -lapr"
    LDFLAGS="$preserve_LDFLAGS -L$withval/lib"
    AC_TRY_LINK_FUNC(apr_initialize, [
      if test -f "$withval/include/apr.h"; then
        dnl found an installed version of APR
        apr_found="yes"
        apr_libdir="$withval/lib"
        apr_includes="-I$withval/include"
      fi
    ], [
      dnl look for a build tree (note: already configured/built)
      if test -f "$withval/libapr.la"; then
        apr_found="yes"
        apr_libdir=""
        apr_la_file="$withval/libapr.la"
        apr_vars="$withval/APRVARS"
        if test -x $withval/apr-config; then
          apr_config="$withval/apr-config"
        else
          apr_config=""
        fi
        apr_includes="-I$withval/include"
        if test ! -f "$withval/APRVARS.in"; then
          dnl extract the APR source directory without polluting our
          dnl shell variable space
          apr_srcdir="`sed -n '/APR_SOURCE_DIR/s/.*"\(.*\)"/\1/p' $apr_vars`"
          apr_includes="$apr_includes -I$apr_srcdir/include"
        fi
      fi
    ])

    dnl if --with-apr is used, then the target prefix/directory must be valid
    if test "$apr_found" != "yes"; then
      AC_MSG_ERROR([
The directory given to --with-apr does not specify a prefix for an installed
APR, nor an APR build directory.])
    fi
  ],[
    dnl always look in the builtin/default places
    LIBS="$LIBS -lapr"
    AC_TRY_LINK_FUNC(apr_initialize, [
        dnl We don't have to do anything.
        apr_found="yes"
        apr_srcdir=""
        apr_libdir=""
        apr_includes=""
        apr_la_file=""
        apr_config=""
        apr_vars=""
      ], [
      dnl look in the some standard places (apparently not in builtin/default)
      for lookdir in /usr /usr/local /opt/apr ; do
        if test "$apr_found" != "yes"; then
          LDFLAGS="$preserve_LDFLAGS -L$lookdir/lib"
          AC_TRY_LINK_FUNC(apr_initialize, [
            apr_found="yes"
            apr_libdir="$lookdir/lib" 
            apr_includes="-I$lookdir/include"
            if test -x "$withval/bin/apr-config"; then
              apr_config="$withval/bin/apr-config"
            else
              apr_config=""
            fi
          ])
        fi
      done
    ])
    dnl We attempt to guess what the data will be *after* configure is run.
    dnl Note, if we don't see configure, but do have configure.in, it'd be
    dnl nice to run buildconf, but that's for another day.
    if test "$apr_found" = "no" && test -n "$1" && test -x "$1/configure"; then
      apr_found="reconfig"
      apr_srcdir="$1"
      if test -n "$2"; then
        apr_builddir="$2/"
      else
        apr_builddir=""
      fi
      apr_libdir=""
      apr_la_file="$apr_builddir$apr_srcdir/libapr.la"
      apr_vars="$apr_builddir$apr_srcdir/APRVARS"
      if test -f "$apr_builddir$apr_srcdir/apr-config.in"; then
        apr_config="$apr_builddir$apr_srcdir/apr-config"
      else
        apr_config=""
      fi
      apr_includes="-I$apr_builddir$apr_srcdir/include"
    fi
  ])

  if test "$apr_found" != "no" && test "$apr_libdir" != ""; then
    if test "$apr_vars" = "" && test -f "$apr_libdir/APRVARS"; then
      apr_vars="$apr_libdir/APRVARS"
    fi
    if test "$apr_la_file" = "" && test -f "$apr_libdir/libapr.la"; then
      apr_la_file="$apr_libdir/libapr.la"
    fi
  fi

  AC_MSG_RESULT($apr_found)
  CFLAGS="$preserve_CFLAGS"
  LIBS="$preserve_LIBS"
  LDFLAGS="$preserve_LDFLAGS"
])
