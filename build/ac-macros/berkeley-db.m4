dnl   SVN_LIB_BERKELEY_DB(major, minor, patch, libname)
dnl
dnl   Search for a useable version of Berkeley DB in a number of
dnl   common places.  The installed DB must be no older than the
dnl   version given by MAJOR, MINOR, and PATCH.  LIBNAME is a list of
dnl   names of the library to attempt to link against, typically
dnl   'db' and 'db4'.
dnl
dnl   If we find a useable version, set CPPFLAGS and LIBS as
dnl   appropriate, and set the shell variable `svn_lib_berkeley_db' to
dnl   `yes'.  Otherwise, set `svn_lib_berkeley_db' to `no'.
dnl
dnl   This macro also checks for the `--with-berkeley-db=PATH' flag;
dnl   if given, the macro will use the PATH specified, and the
dnl   configuration script will die if it can't find the library.  If
dnl   the user gives the `--without-berkeley-db' flag, the entire
dnl   search is skipped.
dnl
dnl   We cache the results of individual searches under particular
dnl   prefixes, not the overall result of whether we found Berkeley
dnl   DB.  That way, the user can re-run the configure script with
dnl   different --with-berkeley-db switch values, without interference
dnl   from the cache.


AC_DEFUN(SVN_LIB_BERKELEY_DB,
[
  db_version=$1.$2.$3
  dnl  Process the `with-berkeley-db' switch.  We set `status' to one
  dnl  of the following values:
  dnl    `required' --- the user specified that they did want to use
  dnl        Berkeley DB, so abort the configuration if we cannot find it.
  dnl    `try-link' --- See if APR-UTIL supplies the correct DB version;
  dnl        if it doesn't, just do not build the server code.
  dnl    `skip' --- Do not look for Berkeley DB, and do not build the
  dnl        server code.
  dnl
  dnl  Finding it is defined as doing a runtime check against the db
  dnl  that is supplied by APR-UTIL.
  dnl  Assuming `status' is not `skip', we do a runtime check against the db
  dnl  that is supplied by APR-UTIL.
  dnl
  dnl  Since APR-UTIL uses --with-berkeley-db aswell, and we pass it
  dnl  through when APR-UTIL is in the tree, we also accept a place spec
  dnl  as argument, and handle that case specifically.
  dnl
  dnl  A `place spec' is either:
  dnl    - a directory prefix P, indicating we should look for headers in
  dnl      P/include and libraries in P/lib, or
  dnl    - a string of the form `HEADER:LIB', indicating that we should look
  dnl      for headers in HEADER and libraries in LIB.

  AC_ARG_WITH(berkeley-db,
  [  --with-berkeley-db=PATH Find the Berkeley DB header and library in
                          `PATH/include' and `PATH/lib'.  If PATH is of the
                          form `HEADER:LIB', then search for header files in
                          HEADER, and the library in LIB.  If you omit the
                          `=PATH' part completely, the configure script will
                          search for Berkeley DB in a number of standard
                          places.

                          The Subversion server requires Berkeley DB
                          $db_version or newer.  If you specify
                          `--without-berkeley-db', the server will not be
                          built.  Otherwise, the configure script builds the
                          server if and only if APR-UTIL is linked against
                          a new enough version of Berkeley DB.],
  [
    if test "$withval" = "no"; then
      status=skip
    else
      apu_db_version="`$apu_config --db-version`"
      if test $? -ne 0; then
        # Until Apache 2.0.48 is released, apu-config will not have a
        # --db-version flag.  Therefor we issue a warning and rely on
        # the runtime check.  After we depend on a newer version we
        # can turn this into an error.
        #
        AC_MSG_WARN([Can't determine whether apr-util is linked against a
                     proper version of Berkeley DB.  Assuming '4'...])
        apu_db_version="4"
      fi

      if test "$withval" = "yes"; then
        if test "$apu_db_version" != "4"; then
          AC_MSG_ERROR([APR-UTIL wasn't linked against Berkeley DB 4,
                        while the fs component is required.  Reinstall
                        APR-UTIL with the appropiate options.])
        else
          status=required
        fi

      elif test "$apu_found" != "reconfig"; then
        if test "$apu_db_version" != 4; then
          AC_MSG_ERROR([APR-UTIL was installed independently, it won't be
                        possible to use the specified Berkeley DB: $withval])
        fi

        AC_MSG_WARN([APR-UTIL may or may not be using the specified
                     Berkeley DB at `$withval'.  Using the Berkeley DB
                     supplied by APR-UTIL.])

        status=required
      fi
    fi
  ],
  [
    # No --with-berkeley-db option:
    #
    # Check if APR-UTIL is providing the correct Berkeley DB version
    # for us.
    #
    apu_db_version="`$apu_config --db-version`"
    if test $? -ne 0; then
      AC_MSG_WARN([Detected older version of APR-UTIL, trying to determine
                   whether apr-util is linked against Berkeley DB
                   $db_version])
      status=try-link
    elif test "$apu_db_version" != "4"; then
      status=skip
    else
      status=try-link
    fi
  ])

  if test "$status" = "skip"; then
    svn_lib_berkeley_db=no
  else
    AC_MSG_CHECKING([for availability of Berkeley DB])
    SVN_LIB_BERKELEY_DB_TRY($1, $2, $3)
    if test "$svn_have_berkeley_db" = "yes"; then
      AC_MSG_RESULT([yes])
      svn_lib_berkeley_db=yes
    else
      AC_MSG_RESULT([no])
      svn_lib_berkeley_db=no
      if test "$status" = "required"; then
        AC_MSG_ERROR([Berkeley DB $db_version wasn't found.])
      fi
    fi
  fi
])


dnl   SVN_LIB_BERKELEY_DB_TRY(major, minor, patch)
dnl
dnl   A subroutine of SVN_LIB_BERKELEY_DB.
dnl
dnl   Check that a new-enough version of Berkeley DB is installed.
dnl   "New enough" means no older than the version given by MAJOR,
dnl   MINOR, and PATCH.  The result of the test is not cached; no
dnl   messages are printed.
dnl
dnl   Set the shell variable `svn_have_berkeley_db' to `yes' if we found
dnl   an appropriate version via APR-UTIL, or `no' otherwise.
dnl
dnl   This macro uses the Berkeley DB library function `db_version' to
dnl   find the version.  If the library linked to APR-UTIL doesn't have this
dnl   function, then this macro assumes it is too old.

dnl NOTE: This is pretty messed up.  It seems that the FreeBSD port of
dnl Berkeley DB 4 puts the header file in /usr/local/include/db4, but the
dnl database library in /usr/local/lib, as libdb4.[a|so].  There is no
dnl /usr/local/include/db.h.  So if you check for /usr/local first, you'll
dnl get the old header file from /usr/include, and the new library from
dnl /usr/local/lib.  Disaster.  Thus this test compares the version constants
dnl in the db.h header with the ones returned by db_version().


AC_DEFUN(SVN_LIB_BERKELEY_DB_TRY,
  [
    svn_lib_berkeley_db_try_save_cppflags="$CPPFLAGS"
    svn_lib_berkeley_db_try_save_libs="$LIBS"

    svn_check_berkeley_db_major=$1
    svn_check_berkeley_db_minor=$2
    svn_check_berkeley_db_patch=$3

    # Extract only the -ldb.* flag from the libs supplied by apu-config
    # Otherwise we get bit by the fact that expat might not be built yet
    # Or that it resides in a non-standard location which we would have
    # to compensate with using something like -R`$apu_config --prefix`/lib.
    #
    changequote(<<, >>)dnl
    svn_apu_bdb_lib="`$apu_config --libs | sed -e 's/.*\(-ldb[^ ]*\).*/\1/'`"
    changequote([, ])dnl

    CPPFLAGS="$SVN_APRUTIL_INCLUDES $CPPFLAGS" 
    LIBS="`$apu_config --ldflags` $svn_apu_bdb_lib $LIBS"

    AC_TRY_RUN(
      [
#include <stdio.h>
#include "db.h"
main ()
{
  int major, minor, patch;

  db_version (&major, &minor, &patch);

  /* Sanity check: ensure that db.h constants actually match the db library */
  if (major != DB_VERSION_MAJOR
      || minor != DB_VERSION_MINOR
      || patch != DB_VERSION_PATCH)
    exit (1);

  /* Run-time check:  ensure the library claims to be the correct version. */

  if (major < $svn_check_berkeley_db_major)
    exit (1);
  if (major > $svn_check_berkeley_db_major)
    exit (0);

  if (minor < $svn_check_berkeley_db_minor)
    exit (1);
  if (minor > $svn_check_berkeley_db_minor)
    exit (0);

  if (patch >= $svn_check_berkeley_db_patch)
    exit (0);
  else
    exit (1);
}
      ],
      [svn_have_berkeley_db=yes],
      [svn_have_berkeley_db=no],
      [svn_have_berkeley_db=yes]
    )

  CPPFLAGS="$svn_lib_berkeley_db_try_save_cppflags"
  LIBS="$svn_lib_berkeley_db_try_save_libs"
  ]
)
