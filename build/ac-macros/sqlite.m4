dnl   SVN_LIB_SQLITE(minimum_ver, recommended_ver, url)
dnl
dnl   Search for a suitable version of sqlite.  minimum_ver is a
dnl   version string which is the lowest suitable version we can use.
dnl   recommended_ver is the recommended version of sqlite, which is
dnl   not necessarily the latest version released.  url is the URL of
dnl   the recommended version of sqlite.
dnl
dnl   If a --with-sqlite=PREFIX option is passed search for a suitable
dnl   sqlite installed on the system.  In this case ignore any sqlite/
dnl   subdir within the source tree.
dnl
dnl   If no --with-sqlite option is passed look first build/sqlite3.c,
dnl   which should be the amalgamated version of the source distribution.
dnl   If the amalgamation exists and is the wrong version exit with a
dnl   failure.  If no sqlite/ subdir is present search for a sqlite installed
dnl   on the system.
dnl
dnl   If the search for sqlite fails, set svn_lib_sqlite to no, otherwise set
dnl   it to yes.

AC_DEFUN(SVN_LIB_SQLITE,
[
  SQLITE_MINIMUM_VER="$1"
  SQLITE_RECOMMENDED_VER="$2"
  SQLITE_URL="$3"
  SQLITE_PKGNAME="sqlite3"

  AC_MSG_NOTICE([checking sqlite library])

  AC_ARG_WITH(sqlite,
              AS_HELP_STRING([--with-sqlite=PREFIX],
                          [Use installed SQLite library or amalgamation file.]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-sqlite requires an argument.])
    else
      sqlite_dir="$withval"
    fi

    if test -d $sqlite_dir; then
      dnl pointed at an sqlite installation
      SVN_SQLITE_DIR_CONFIG($sqlite_dir)
    else
      dnl pointed at the amalgamation file
      SVN_SQLITE_FILE_CONFIG($sqlite_dir)
    fi

    if test -z "$svn_lib_sqlite"; then
      AC_MSG_WARN([no suitable sqlite found in $sqlite_dir])
      SVN_DOWNLOAD_SQLITE
    fi
  ],
  [
    dnl no --with-sqlite switch, and no sqlite subdir, look in PATH
    SVN_SQLITE_PKG_CONFIG

    if test -z "$svn_lib_sqlite"; then
      dnl check the "standard" location of /usr
      SVN_SQLITE_DIR_CONFIG(/usr)
    fi

    if test -z "$svn_lib_sqlite"; then
      dnl finally, see if the sqlite amalgamation exists
      SVN_SQLITE_FILE_CONFIG(`pwd`/sqlite-amalgamation/sqlite3.c)
    fi

    if test -z "$svn_lib_sqlite"; then
      SVN_DOWNLOAD_SQLITE
    fi
  ])

  AC_SUBST(SVN_SQLITE_INCLUDES)
  AC_SUBST(SVN_SQLITE_LIBS)
])

dnl SVN_SQLITE_PKG_CONFIG
dnl
dnl Look for sqlite in PATH using pkg-config.
AC_DEFUN(SVN_SQLITE_PKG_CONFIG,
[
    AC_PATH_PROG(pkg_config,pkg-config)
    if test -x "$pkg_config"; then
      AC_MSG_CHECKING([sqlite library version (via pkg-config)])
      sqlite_version=`$pkg_config $SQLITE_PKGNAME --modversion --silence-errors`

      if test -n "$sqlite_version"; then
        SVN_SQLITE_VERNUM_PARSE(sqlite_version)
     
        if test "$sqlite_ver_num" -ge "$sqlite_min_ver_num"; then
          AC_MSG_RESULT([$sqlite_version])
          svn_lib_sqlite="yes"
          SVN_SQLITE_INCLUDES="`$pkg_config $SQLITE_PKGNAME --cflags`"
          SVN_SQLITE_LIBS="`$pkg_config $SQLITE_PKGNAME --libs`"
        else
          AC_MSG_RESULT([none or unsupported $sqlite_version])
        fi
      fi
    fi

    if test -z "$svn_lib_sqlite"; then
      AC_MSG_RESULT(no)
    else
      AC_MSG_RESULT([$svn_lib_sqlite])
    fi
])

dnl SVN_SQLITE_DIR_CONFIG(sqlite_dir)
dnl
dnl Check to see if we've got an appropriate sqlite library at sqlite_dir.
dnl If we don't, fail.
AC_DEFUN(SVN_SQLITE_DIR_CONFIG,
[
  sqlite_dir="$1"

  AC_MSG_CHECKING([sqlite library in $sqlite_dir])
  echo ""

  dnl if the header file doesn't exist, fail
  sqlite_header=$sqlite_dir/include/sqlite3.h
  if test ! -e $sqlite_header; then
    echo "header not found in $sqlite_dir/include"
    SVN_DOWNLOAD_SQLITE
  fi

  AC_CHECK_HEADER(sqlite3.h,
    [
      AC_MSG_CHECKING([sqlite library version (via header)])
      sqlite_version=`python build/getversion.py SQLITE $sqlite_header`
      SVN_SQLITE_VERNUM_PARSE(sqlite_version)
     
      if test "$sqlite_ver_num" -ge "$sqlite_min_ver_num"; then
        AC_MSG_RESULT([$sqlite_version])
        AC_CHECK_LIB(sqlite3, sqlite3_close,
        [
          svn_lib_sqlite="yes"
          if test -d "$sqlite_dir"; then
            SVN_SQLITE_INCLUDES="-I$sqlite_dir/include"
            SVN_SQLITE_LIBS="-L$sqlite_dir/lib -lsqlite3"
          else
            SVN_SQLITE_LIBS="-lsqlite3"
          fi
        ])
      else
        AC_MSG_RESULT([none or unsupported $sqlite_version])
      fi
    ])
])

dnl SVN_SQLITE_FILE_CONFIG(sqlite_file)
dnl
dnl Check to see if we've got an appropriate sqlite amalgamation file
dnl at sqlite_file.  If not, fail.
AC_DEFUN(SVN_SQLITE_FILE_CONFIG,
[
  sqlite_amalg="$1"
  if test ! -e $sqlite_amalg; then
    echo "amalgamation not found at $sqlite_amalg"
  else
    AC_MSG_CHECKING([sqlite amalgamation file])
    sqlite_version=`python build/getversion.py SQLITE $sqlite_amalg`
    SVN_SQLITE_VERNUM_PARSE(sqlite_version)

    if test "$sqlite_ver_num" -ge "$sqlite_min_ver_num"; then
      AC_MSG_RESULT([$sqlite_version])
      AC_DEFINE(SVN_SQLITE_INLINE, 1,
                [Defined if svn should use the amalgamated version of sqlite])
      SVN_SQLITE_INCLUDES="-I`dirname $sqlite_amalg`"
      svn_lib_sqlite="yes"
    else
      AC_MSG_RESULT([none or unsupported $sqlite_version])
    fi
  fi
])

dnl Parse a x.y.z version string into a number
AC_DEFUN(SVN_SQLITE_VERNUM_PARSE,
[
  ver_str="$1"
  sqlite_major=`expr $sqlite_version : '\([[0-9]]*\)'`
  sqlite_minor=`expr $sqlite_version : '[[0-9]]*\.\([[0-9]]*\)'`
  sqlite_micro=`expr $sqlite_version : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
  if test -z "$sqlite_micro"; then
    sqlite_micro=0
  fi
  sqlite_ver_num=`expr $sqlite_major \* 1000000 \
                    \+ $sqlite_minor \* 1000 \
                    \+ $sqlite_micro`

  sqlite_min_major=`expr $SQLITE_MINIMUM_VER : '\([[0-9]]*\)'`
  sqlite_min_minor=`expr $SQLITE_MINIMUM_VER : '[[0-9]]*\.\([[0-9]]*\)'`
  sqlite_min_micro=`expr $SQLITE_MINIMUM_VER : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
  sqlite_min_ver_num=`expr $sqlite_min_major \* 1000000 \
                        \+ $sqlite_min_minor \* 1000 \
                        \+ $sqlite_min_micro`
])

dnl SVN_DOWNLOAD_SQLITE()
dnl no sqlite found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_SQLITE,
[
  echo ""
  echo "An appropriate version of sqlite could not be found.  We recommmend"
  echo "${SQLITE_RECOMMENDED_VER}, but require at least ${SQLITE_MINIMUM_VER}."
  echo "Please either install a newer sqlite on this system"
  echo ""
  echo "or"
  echo ""
  echo "get the sqlite ${SQLITE_RECOMMENDED_VER} amalgamation from:"
  echo "    ${SQLITE_URL}"
  echo "unpack the archive using tar/gunzip and copy sqlite3.c from the"
  echo "resulting directory to ./build/sqlite3.c"
  echo "This file also ships as part of the subversion-deps distribution."
  echo ""
  AC_MSG_ERROR([Subversion requires SQLite])
])
