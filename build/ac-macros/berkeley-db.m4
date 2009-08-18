dnl   SVN_LIB_BERKELEY_DB(major, minor, patch)
dnl
dnl   Compare if the Berkeley DB specified by user or provided by APR-UTIL
dnl   is no older than the version given by MAJOR, MINOR, and PATCH.
dnl
dnl   If we find a useable version, set the shell variable
dnl   `svn_lib_berkeley_db' to `yes'.  Otherwise, set `svn_lib_berkeley_db'
dnl   to `no'.
dnl
dnl   This macro also checks for the `--with-berkeley-db=ARG' flag;
dnl   if given, the macro will use the ARG specified, and the
dnl   configuration script will die if it can't find the library.  If
dnl   the user gives the `--without-berkeley-db' flag, the entire
dnl   search is skipped.


AC_DEFUN(SVN_LIB_BERKELEY_DB,
[
  db_version=$1.$2.$3
  dnl  Process the `with-berkeley-db' switch.  We set `status' to one
  dnl  of the following values:
  dnl    `required' --- the user specified that they did want to use
  dnl        Berkeley DB, so abort the configuration if we cannot find it.
  dnl    `try-link' --- See if APR-UTIL supplies the correct DB version;
  dnl        if it doesn't, just do not build the bdb based filesystem.
  dnl    `skip' --- Do not look for Berkeley DB, and do not build the
  dnl        bdb based filesystem.

  AC_ARG_WITH(berkeley-db, [AS_HELP_STRING(
                                           [[--with-berkeley-db=[HEADER:INCLUDES:LIB_SEARCH_DIRS:LIBS]]], [
                          The Subversion Berkeley DB based filesystem library 
                          requires Berkeley DB $db_version or newer.  If you
                          specify `--without-berkeley-db', that library will
                          not be built.  If you omit the argument of this option
                          completely, the configure script will use Berkeley DB
                          used by APR-UTIL.])],
  [
    if test "$withval" = "no"; then
      status=skip
    elif test "$withval" = "yes"; then
      apu_db_version="`$apu_config --db-version`"
      if test $? -ne 0; then
        AC_MSG_ERROR([Can't determine whether apr-util is linked against a
                      proper version of Berkeley DB.])
      fi

      if test "$withval" = "yes"; then
        if test "$apu_db_version" != "4"; then
          AC_MSG_ERROR([APR-UTIL wasn't linked against Berkeley DB 4,
                        while the fs component is required.  Reinstall
                        APR-UTIL with the appropiate options.])
        fi
        
        status=required

      elif test "$apu_found" != "reconfig"; then
        if test "$apu_db_version" != 4; then
          AC_MSG_ERROR([APR-UTIL was installed independently, it won't be
                        possible to use the specified Berkeley DB: $withval])
        fi

        status=required
      fi
    else
      if echo "$withval" | $EGREP ":.*:.*:" > /dev/null; then
        svn_berkeley_db_header=["`echo "$withval" | sed -e "s/\([^:]*\):.*/\1/"`"]
        SVN_DB_INCLUDES=""
        for i in [`echo "$withval" | sed -e "s/.*:\([^:]*\):[^:]*:.*/\1/"`]; do
          SVN_DB_INCLUDES="$SVN_DB_INCLUDES -I$i"
        done
        SVN_DB_INCLUDES="${SVN_DB_INCLUDES## }"
        for l in [`echo "$withval" | sed -e "s/.*:[^:]*:\([^:]*\):.*/\1/"`]; do
          LDFLAGS="$LDFLAGS -L$l"
        done
        SVN_DB_LIBS=""
        for l in [`echo "$withval" | sed -e "s/.*:\([^:]*\)/\1/"`]; do
          SVN_DB_LIBS="$SVN_DB_LIBS -l$l"
        done
        SVN_DB_LIBS="${SVN_DB_LIBS## }"

        status=required
      else
        AC_MSG_ERROR([Invalid syntax of argument of --with-berkeley-db option])
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
        AC_MSG_ERROR([Berkeley DB $db_version or newer wasn't found.])
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
dnl   an appropriate version, or `no' otherwise.
dnl
dnl   This macro uses the Berkeley DB library function `db_version' to
dnl   find the version.  If the Berkeley DB library doesn't have this
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
    SVN_DB_LIBS=["${SVN_DB_LIBS-`$apu_config --libs | sed -e 's/.*\(-ldb[^[:space:]]*\).*/\1/' | $EGREP -- '-ldb[^[:space:]]*'`}"]

    CPPFLAGS="$SVN_DB_INCLUDES $SVN_APRUTIL_INCLUDES $CPPFLAGS" 
    LIBS="`$apu_config --ldflags` $SVN_DB_LIBS $LIBS"

    if test -n "$svn_berkeley_db_header"; then
      SVN_DB_HEADER="#include <$svn_berkeley_db_header>"
      svn_db_header="#include <$svn_berkeley_db_header>"
    else
      SVN_DB_HEADER="#include <apu_want.h>"
      svn_db_header="#define APU_WANT_DB
#include <apu_want.h>"
    fi

    AH_BOTTOM(
#ifdef SVN_WANT_BDB
#define APU_WANT_DB
@SVN_DB_HEADER@
#endif
)

    AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdlib.h>
$svn_db_header

int main ()
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
      ]])],
      [svn_have_berkeley_db=yes],
      [svn_have_berkeley_db=no],
      [svn_have_berkeley_db=yes]
    )

  CPPFLAGS="$svn_lib_berkeley_db_try_save_cppflags"
  LIBS="$svn_lib_berkeley_db_try_save_libs"
  ]
)
