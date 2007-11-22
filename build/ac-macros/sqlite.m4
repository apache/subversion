dnl   SVN_LIB_SQLITE(wanted_regex, recommended_ver, url)
dnl
dnl   Search for a suitable version of sqlite.  wanted_regex is a
dnl   regular expression used in a Bourne shell switch/case statement
dnl   to match versions of sqlite that can be used.  recommended_ver is the
dnl   recommended version of sqlite, which is not necessarily the latest
dnl   released version of sqlite that exists.  url is the URL of the
dnl   recommended version of sqlite.
dnl
dnl   If a --with-sqlite=PREFIX option is passed search for a suitable
dnl   sqlite installed on the system.  In this case ignore any sqlite/
dnl   subdir within the source tree.
dnl
dnl   If no --with-sqlite option is passed look first for a sqlite/ subdir.
dnl   If a sqlite/ subdir exists and is the wrong version exit with a
dnl   failure.  If no sqlite/ subdir is present search for a sqlite installed
dnl   on the system.
dnl
dnl   If the search for sqlite fails, set svn_lib_sqlite to no, otherwise set
dnl   it to yes.

AC_DEFUN(SVN_LIB_SQLITE,
[
  SQLITE_ALLOWED_LIST="$1"
  SQLITE_RECOMMENDED_VER="$2"
  SQLITE_URL="$3"
  SQLITE_PKGNAME="sqlite3"

  AC_MSG_NOTICE([checking sqlite library])

  AC_ARG_WITH(sqlite,
              AS_HELP_STRING([--with-sqlite=PREFIX],
                             [Use installed SQLite library.]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-sqlite requires an argument.])
    else
      sqlite_dir="$withval"
    fi

    SVN_SQLITE_CONFIG($sqlite_dir)
  ],
  [
    # no --with-sqlite switch, and no sqlite subdir, look in PATH
    AC_PATH_PROG(pkg_config,pkg-config)
    if test -x "$pkg_config"; then
      AC_MSG_CHECKING([sqlite library version (via pkg-config)])
      sqlite_version=`$pkg_config $SQLITE_PKGNAME --modversion --silence-errors`
      case $sqlite_version in
        $SQLITE_ALLOWED_LIST)
          AC_MSG_RESULT([$sqlite_version])
          svn_lib_sqlite="yes"
          SVN_SQLITE_INCLUDES="`$pkg_config $SQLITE_PKGNAME --cflags`"
          SVN_SQLITE_LIBS="`$pkg_config $SQLITE_PKGNAME --libs`"
          AC_CHECK_LIB(sqlite3, sqlite3_threadsafe,
                       [threadsafety_runtime_check_avail=yes])
          ;;
        *)
          AC_MSG_RESULT([none or unsupported $sqlite_version])
          ;;
      esac
    fi

    if test -z "$svn_lib_sqlite"; then
      SVN_SQLITE_CONFIG("")
      if test -z "$svn_lib_sqlite"; then
        AC_MSG_WARN([SQLite not found - libsvn_ra_local will be disabled.])
        SVN_DOWNLOAD_SQLITE
      fi
    fi
  ])

  if test "$threadsafety_runtime_check_avail" = "yes"; then
    AC_DEFINE([SVN_HAVE_SQLITE_THREADSAFE_PREDICATE], [1],
      [Defined if SQLite 3.5+'s thread-safety runtime check is available.])
  fi

  AC_SUBST(SVN_SQLITE_INCLUDES)
  AC_SUBST(SVN_SQLITE_LIBS)
])

dnl SVN_SQLITE_CONFIG()
AC_DEFUN(SVN_SQLITE_CONFIG,
[
  sqlite_dir="$1"

  if test "$sqlite_dir" != "" && test -d "$sqlite_dir"; then
    save_CPPFLAGS="$CPPFLAGS"
    save_LDFLAGS="$LDFLAGS"
    CPPFLAGS="$CPPFLAGS -I$sqlite_dir/include"
    LDFLAGS="$CPPFLAGS -L$sqlite_dir/lib"
  fi

  AC_CHECK_HEADER(sqlite3.h,
    [ AC_CHECK_LIB(sqlite3, sqlite3_close,
      [
        svn_lib_sqlite="yes"
        if test "$sqlite_dir" != "" && test -d "$sqlite_dir"; then
          SVN_SQLITE_INCLUDES="-I$sqlite_dir/include"
          SVN_SQLITE_LIBS="-L$sqlite_dir/lib -lsqlite3"
        else
          SVN_SQLITE_LIBS="-lsqlite3"
        fi

        AC_CHECK_LIB(sqlite3, sqlite3_threadsafe,
                     [threadsafety_runtime_check_avail=yes])
      ])
    ])

  if test "$sqlite_dir" != "" && test -d "$sqlite_dir"; then
    CPPFLAGS="$save_CPPFLAGS"
    LDFLAGS="$save_LDFLAGS"
  fi
])

dnl SVN_DOWNLOAD_SQLITE()
dnl no sqlite found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_SQLITE,
[
  echo ""
  echo "An appropriate version of sqlite could not be found, so libsvn_ra_local"
  echo "will not be built.  If you want to build libsvn_ra_local, please either"
  echo "install sqlite ${SQLITE_RECOMMENDED_VER} on this system"
  echo ""
  echo "or"
  echo ""
  echo "get sqlite ${SQLITE_RECOMMENDED_VER} from:"
  echo "    ${SQLITE_URL}"
  echo "unpack the archive using tar/gunzip and rename the resulting"
  echo "directory from ./sqlite-${SQLITE_RECOMMENDED_VER}/ to ./sqlite/"
  echo ""
  AC_MSG_RESULT([no suitable sqlite found])
  svn_lib_sqlite="no"
])
