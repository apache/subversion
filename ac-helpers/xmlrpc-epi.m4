dnl   SVN_LIB_XMLRPC_EPI
dnl
dnl   Search for a useable version of XMLRPC-EPI in a number of
dnl   common places.
dnl
dnl   If we find a useable version, set CPPFLAGS and LIBS as
dnl   appropriate, and set the shell variable `svn_lib_xmlrpc_epi' to
dnl   `yes'.  Otherwise, set `svn_lib_xmlrpc_epi' to `no'.
dnl
dnl   This macro also checks for the `--with-xmlrpc-epi=PATH' flag;
dnl   if given, the macro will use the PATH specified, and the
dnl   configuration script will die if it can't find the library.  If
dnl   the user gives the `--without-xmlrpc-epi' flag, the entire
dnl   search is skipped.
dnl
dnl   We cache the results of individual searches under particular
dnl   prefixes, not the overall result of whether we found XMLRPC-EPI.
dnl   That way, the user can re-run the configure script with
dnl   different --with-xmlrpc-epi switch values, without interference
dnl   from the cache.


AC_DEFUN(SVN_LIB_XMLRPC_EPI,
[
  xmlrpc_version=0.51
  xmlrpc_api_no=20020623

  dnl  Process the `with-xmlrpc-epi' switch.  We set `status' to one
  dnl  of the following values:
  dnl    `required' --- the user specified that they did want to use
  dnl        XMLRPC-EPI, so abort the configuration if we cannot find it.
  dnl    `if-found' --- search for XMLRPC-EPI in the usual places;
  dnl        if we cannot find it, just do not build the code that
  dnl        depends on it.
  dnl    `skip' --- Do not look for XMLRPC-EPI, and do not build the
  dnl        code that depends on it.
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
  dnl  You'll notice that the value of the `--with-xmlrpc-epi' switch is a
  dnl  place spec.

  AC_ARG_WITH(xmlrpc-epi,
  [  --with-xmlrpc-epi=PATH
	   Find the XMLRPC-EPI header and library in 'PATH/include' and
	   'PATH/lib'.  If PATH is of the form 'HEADER:LIB', then search
	   for header files in HEADER, and the library in LIB.  If you omit
	   the '=PATH' part completely, the configure script will search
	   for XMLRPC-EPI in a number of standard places.

	   The Subversion ra_pipe implementation requires XMLRPC-EPI.  If
	   you specify '--without-xmlrpc-epi', it will not be built.
           Otherwise, the configure script builds it if and
	   only if it can find a new enough version installed, or if a
           copy of XMLRPC-EPI exists in the subversion tree as subdir
           'xmlrpc'.],
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
    # No --with-xmlrpc-epi option:
    #
    # Check to see if a db directory exists in the build directory.
    # If it does then we will be using the XMLRPC-EPI version
    # from the source tree. We can't test it since it is not built
    # yet, so we have to assume it is the correct version.

    AC_MSG_CHECKING([for built-in XMLRPC-EPI])

    if test -d xmlrpc; then
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
    xmlrpcdir=`cd xmlrpc/src ; pwd`
    SVN_XMLRPC_INCLUDES="-I$xmlrpcdir"
    svn_lib_xmlrpc_epi=yes
    SVN_XMLRPC_LIBS="-L$dbdir -lxmlrpc"
  elif test "$status" = "skip"; then
    svn_lib_xmlrpc_epi=no
  else

    if test "$places" = "search"; then
      places="std /usr/local /usr"
    fi
    # Now `places' is guaranteed to be a list of place specs we should
    # search, no matter what flags the user passed.

    # Save the original values of the flags we tweak.
    SVN_LIB_XMLRPC_EPI_save_libs="$LIBS"
    SVN_LIB_XMLRPC_EPI_save_cppflags="$CPPFLAGS"

    # The variable `found' is the prefix under which we've found
    # XMLRPC-EPI, or `not' if we haven't found it anywhere yet.
    found=not
    for place in $places; do

      LIBS="$SVN_LIB_XMLRPC_EPI_save_libs"
      CPPFLAGS="$SVN_LIB_XMLRPC_EPI_save_cppflags"
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

      # We generate a separate cache variable for each prefix we
      # search under.  That way, we avoid caching information that
      # changes if the user runs `configure' with a different set of
      # switches.
      changequote(,)
      cache_id="`echo svn_cv_lib_xmlrpc_epi_in_${place} \
                 | sed -e 's/[^a-zA-Z0-9_]/_/g'`"
      changequote([,])
      dnl We can't use AC_CACHE_CHECK here, because that won't print out
      dnl the value of the computed cache variable properly.
      AC_MSG_CHECKING([for XMLRPC-EPI in $description])
      AC_CACHE_VAL($cache_id,
        [
	  SVN_LIB_XMLRPC_EPI_TRY()
          eval "$cache_id=$svn_have_xmlrpc_epi"
        ])
      result="`eval echo '$'$cache_id`"
      AC_MSG_RESULT($result)

      # If we found it, no need to search any more.
      if test "`eval echo '$'$cache_id`" = "yes"; then
	found="$place"
	break
      fi

    done

    # Restore the original values of the flags we tweak.
    LIBS="$SVN_LIB_XMLRPC_EPI_save_libs"
    CPPFLAGS="$SVN_LIB_XMLRPC_EPI_save_cppflags"

    case "$found" in
      "not" )
	if test "$status" = "required"; then
	  AC_MSG_ERROR([Could not find XMLRPC-EPI $1.$2.$3.])
	fi
	svn_lib_xmlrpc_epi=no
      ;;
      "std" )
        SVN_XMLRPC_INCLUDES=
        SVN_XMLRPC_LIBS=-lxmlrpc
        svn_lib_xmlrpc_epi=yes
      ;;
      *":"* )
	header="`echo $found | sed -e 's/:.*$//'`"
	lib="`echo $found | sed -e 's/^.*://'`"
        SVN_XMLRPC_INCLUDES="-I$header"
        SVN_XMLRPC_LIBS="-L$lib -lxmlrpc"
        svn_lib_xmlrpc_epi=yes
      ;;
      * )
        SVN_XMLRPC_INCLUDES="-I$found/include"
        SVN_XMLRPC_LIBS="-L$found/lib -lxmlrpc"
	svn_lib_xmlrpc_epi=yes
      ;;
    esac
  fi
])


dnl   SVN_LIB_XMLRPC_EPI_TRY()
dnl
dnl   A subroutine of SVN_LIB_XMLRPC_EPI.
dnl
dnl   Check that a new-enough version of XMLRPC-EPI is installed.
dnl
dnl   Set the shell variable `svn_have_xmlrpc_epi' to `yes' if we found
dnl   an appropriate version installed, or `no' otherwise.
dnl
dnl   This macro uses the XMLRPC-EPI #define XMLRPC_API_NO to
dnl   find the version.  If the library installed doesn't have this
dnl   this #define, this macro assumes it is too old.

AC_DEFUN(SVN_LIB_XMLRPC_EPI_TRY,
  [
    AC_TRY_RUN(
      [
#include <stdio.h>
#include "xmlrpc.h"
main ()
{
#if XMLRPC_API_NO == $xmlrpc_api_no
  exit(0);
#endif
  exit(1);
}
      ],
      [svn_have_xmlrpc_epi=yes],
      [svn_have_xmlrpc_epi=no],
      [svn_have_xmlrpc_epi=yes]
    )
  ]
)
