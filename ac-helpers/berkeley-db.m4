dnl   SVN_CHECK_BERKELEY_DB(major, minor, patch)
dnl
dnl   Check that a new-enough version of Berkeley DB is installed.
dnl   "New enough" means no older than the version given by MAJOR,
dnl   MINOR, and PATCH.  The result of the test is cached.
dnl
dnl   If the proper version is present, add `-ldb' to the LIBS
dnl   variable, and set svn_cv_lib_berkeley_db to `yes'.
dnl
dnl   If Berkeley DB is not installed, or the installed version is too
dnl   old, set svn_cv_lib_berkeley_db to `no'.
dnl
dnl   This macro uses the Berkeley DB library function `db_version' to
dnl   find the version.  If the library installed doesn't have this
dnl   function, then this macro assumes it is too old.

AC_DEFUN(SVN_CHECK_BERKELEY_DB,
  [
    save_libs="$LIBS"
    LIBS="$LIBS -ldb"
    AC_CACHE_CHECK(for Berkeley DB $1.$2.$3 or newer,
                   svn_cv_lib_berkeley_db,
      [
	svn_check_berkeley_db_major=$1
	svn_check_berkeley_db_minor=$2
	svn_check_berkeley_db_patch=$3
	AC_TRY_RUN(
          [
#include <stdio.h>
#include "db.h"
main ()
{
  int major, minor, patch;

  db_version (&major, &minor, &patch);

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
	  [svn_cv_lib_berkeley_db=yes],
	  [svn_cv_lib_berkeley_db=no],
	  [svn_cv_lib_berkeley_db=yes]
        )
      ]
    )
    LIBS="$save_libs"
    if test "$svn_cv_lib_berkeley_db" = "yes"; then
      LIBS="$LIBS -ldb"
    fi
  ]
)
