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
  dnl    `if-found' --- search for Berkeley DB in the usual places;
  dnl        if we cannot find it, just do not build the server code.
  dnl    `skip' --- Do not look for Berkeley DB, and do not build the
  dnl        server code.
  dnl
  dnl  Assuming `status' is not `skip', we set the variable `places' to
  dnl  either `search', meaning we should check in a list of typical places,
  dnl  or to a single place spec.
  dnl
  dnl  A `place spec' is either:
  dnl    - the string `std', indicating that we should look for headers and
  dnl      libraries in the standard places,
  dnl    - a directory prefix P, indicating we should look for headers in
  dnl      P/include and libraries in P/lib, or
  dnl    - a string of the form `HEADER:LIB', indicating that we should look
  dnl      for headers in HEADER and libraries in LIB.
  dnl 
  dnl  You'll notice that the value of the `--with-berkeley-db' switch is a
  dnl  place spec.

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
                          server if and only if it can find a new enough
                          version installed, or if a copy of Berkeley DB
                          exists in the subversion tree as subdir `db'.],
  [
    if test "$withval" = "yes"; then
      status=required
      places=search
    elif test "$withval" = "no"; then
      status=skip
    else
      status=required
      places="$withval"
    fi
  ],
  [
    # No --with-berkeley-db option:
    #
    # Check to see if a db directory exists in the build directory.
    # If it does then we will be using the berkeley DB version
    # from the source tree. We can't test it since it is not built
    # yet, so we have to assume it is the correct version.

    AC_MSG_CHECKING([for built-in Berkeley DB])

    if test -d db ; then
      status=builtin
      AC_MSG_RESULT([yes])
    else
      status=if-found
      places=search
      AC_MSG_RESULT([no])
    fi
  ])

  if test "$status" = "builtin"; then
    # Use the include and lib files in the build dir.
    dbdir=`cd db/dist ; pwd`
    SVN_DB_INCLUDES="-I$dbdir"
    svn_lib_berkeley_db=yes
    # Linking directly to the .la is broken with --disable-shared
    # because Berkeley db does not seem to generate a .la library.
    if test "$enable_shared" = "yes"; then
        DB_VERSION_MAJOR=`sed -n 's/#define[ 	]\+DB_VERSION_MAJOR[ 	]\+//p' db/dist/db.h`
        DB_VERSION_MINOR=`sed -n 's/#define[ 	]\+DB_VERSION_MINOR[ 	]\+//p' db/dist/db.h`
        SVN_DB_LIBS="$dbdir/libdb-$DB_VERSION_MAJOR.$DB_VERSION_MINOR.la"
    else
        SVN_DB_LIBS="-L$dbdir -ldb" # ignoring $db_libname here on purpose.
    fi
  elif test "$status" = "skip"; then
    svn_lib_berkeley_db=no
  else

    if test "$places" = "search"; then
      places="std /usr/local/include/db4:/usr/local/lib /usr/local
              /usr/local/BerkeleyDB.$1.$2 /usr/include/db4:/usr/lib"
    fi
    # Now `places' is guaranteed to be a list of place specs we should
    # search, no matter what flags the user passed.

    # Save the original values of the flags we tweak.
    SVN_LIB_BERKELEY_DB_save_libs="$LIBS"
    SVN_LIB_BERKELEY_DB_save_cppflags="$CPPFLAGS"

    # The variable `found' is the prefix under which we've found
    # Berkeley DB, or `not' if we haven't found it anywhere yet.
    found=not
    for place in $places; do

      LIBS="$SVN_LIB_BERKELEY_DB_save_libs"
      CPPFLAGS="$SVN_LIB_BERKELEY_DB_save_cppflags"
      case "$place" in
        "std" )
          description="the standard places"
        ;;
        *":"* )
          header="`echo $place | sed -e 's/:.*$//'`"
          lib="`echo $place | sed -e 's/^.*://'`"
	  CPPFLAGS="$CPPFLAGS -I$header"
	  LIBS="$LIBS -L$lib"
	  description="$header and $lib"
        ;;
        * )
	  LIBS="$LIBS -L$place/lib"
	  CPPFLAGS="$CPPFLAGS -I$place/include"
	  description="$place"
        ;;
      esac

      for db_libname in $4; do
        # We generate a separate cache variable for each prefix and libname
        # we search under.  That way, we avoid caching information that
        # changes if the user runs `configure' with a different set of
        # switches.
        changequote(,)
        cache_id="`echo svn_cv_lib_berkeley_db_$1_$2_$3_${db_libname}_in_${place} \
                   | sed -e 's/[^a-zA-Z0-9_]/_/g'`"
        changequote([,])
        dnl We can't use AC_CACHE_CHECK here, because that won't print out
        dnl the value of the computed cache variable properly.
        AC_MSG_CHECKING([for Berkeley DB in $description (as $db_libname)])
        AC_CACHE_VAL($cache_id,
          [
  	  SVN_LIB_BERKELEY_DB_TRY($1, $2, $3, $db_libname)
            eval "$cache_id=$svn_have_berkeley_db"
          ])
        result="`eval echo '$'$cache_id`"
        AC_MSG_RESULT($result)

        # If we found it, no need to search any more.
        if test "`eval echo '$'$cache_id`" = "yes"; then
          found="$place"
          break
        fi
      done
        test "$found" != "not" && break
    done

    # Restore the original values of the flags we tweak.
    LIBS="$SVN_LIB_BERKELEY_DB_save_libs"
    CPPFLAGS="$SVN_LIB_BERKELEY_DB_save_cppflags"

    case "$found" in
      "not" )
	if test "$status" = "required"; then
	  AC_MSG_ERROR([Could not find Berkeley DB $db_version with names: $4])
	fi
	svn_lib_berkeley_db=no
      ;;
      "std" )
        SVN_DB_INCLUDES=
        SVN_DB_LIBS=-l$db_libname
        svn_lib_berkeley_db=yes
      ;;
      *":"* )
	header="`echo $found | sed -e 's/:.*$//'`"
	lib="`echo $found | sed -e 's/^.*://'`"
        SVN_DB_INCLUDES="-I$header"
dnl ### should look for a .la file
        SVN_DB_LIBS="-L$lib -l$db_libname"
        svn_lib_berkeley_db=yes
      ;;
      * )
        SVN_DB_INCLUDES="-I$found/include"
dnl ### should look for a .la file
        SVN_DB_LIBS="-L$found/lib -l$db_libname"
	svn_lib_berkeley_db=yes
      ;;
    esac
  fi
])


dnl   SVN_LIB_BERKELEY_DB_TRY(major, minor, patch, db_name)
dnl
dnl   A subroutine of SVN_LIB_BERKELEY_DB.
dnl
dnl   Check that a new-enough version of Berkeley DB is installed.
dnl   "New enough" means no older than the version given by MAJOR,
dnl   MINOR, and PATCH.  The result of the test is not cached; no
dnl   messages are printed.  Use DB_NAME as the library to link against.
dnl   (e.g. DB_NAME should usually be "db" or "db4".)
dnl
dnl   Set the shell variable `svn_have_berkeley_db' to `yes' if we found
dnl   an appropriate version installed, or `no' otherwise.
dnl
dnl   This macro uses the Berkeley DB library function `db_version' to
dnl   find the version.  If the library installed doesn't have this
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
    svn_lib_berkeley_db_try_save_libs="$LIBS"

    svn_check_berkeley_db_major=$1
    svn_check_berkeley_db_minor=$2
    svn_check_berkeley_db_patch=$3
    svn_berkeley_db_lib_name=$4

    LIBS="$LIBS -l$svn_berkeley_db_lib_name"

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

  LIBS="$svn_lib_berkeley_db_try_save_libs"
  ]
)
