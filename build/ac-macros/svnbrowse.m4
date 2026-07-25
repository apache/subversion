dnl ===================================================================
dnl   Licensed to the Apache Software Foundation (ASF) under one
dnl   or more contributor license agreements.  See the NOTICE file
dnl   distributed with this work for additional information
dnl   regarding copyright ownership.  The ASF licenses this file
dnl   to you under the Apache License, Version 2.0 (the
dnl   "License"); you may not use this file except in compliance
dnl   with the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl   Unless required by applicable law or agreed to in writing,
dnl   software distributed under the License is distributed on an
dnl   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
dnl   KIND, either express or implied.  See the License for the
dnl   specific language governing permissions and limitations
dnl   under the License.
dnl ===================================================================
dnl
dnl  Configure svnbrowse

AC_DEFUN(SVN_SVNBROWSE,
[
    dnl disabled by default
    do_svnbrowse_build=no
    AC_ARG_ENABLE(svnbrowse,
    AS_HELP_STRING([--enable-svnbrowse],
                   [Enable building svnbrowse]),
    [
        if test "$enableval" = "yes" ; then
            AC_MSG_NOTICE([Enabling svnbrowse])
            do_svnbrowse_build=yes
        else
            dnl this is the default AC_MSG_NOTICE([Disabling svnbrowse])
            do_svnbrowse_build=no
        fi
    ])

    ncurses_skip=no
    AC_ARG_WITH(ncurses,
    AS_HELP_STRING([--with-ncurses=PREFIX],
                   [ncurses text-based user interface library]),
    [
        if test "$withval" = "yes"; then
            ncurses_skip=no
        elif test "$withval" = "no"; then
            ncurses_skip=yes
        else
            ncurses_skip=no
            ncurses_prefix="$withval"
        fi
    ])

    ncurses_found=no
    if test "$do_svnbrowse_build" = "yes" && test "$ncurses_skip" = "no"; then
        if test -n "$ncurses_prefix"; then
            SVN_NCURSES_PREFIX()
        else
            SVN_NCURSES_PKG_CONFIG()
        fi

        if test "$ncurses_found" = "no"; then
            dnl Make sure previously cached values don't leak here
            unset ac_cv_header_curses_h
            unset ac_cv_lib_ncurses_termattrs
            AC_MSG_NOTICE([ncurses library configuration])
            AC_CHECK_HEADER([curses.h],[
                AC_CHECK_LIB([ncurses],[termattrs],[
                    ncurses_found="builtin"
                    SVN_NCURSES_LIBS="-lncurses"
                ])
            ])
        fi

        if test "$ncurses_found" != "no"; then
            AC_MSG_CHECKING([ncurses version])
            save_cppflags="$CPPFLAGS"
            save_ldflags="$LDFLAGS"
            CPPFLAGS="$CPPFLAGS $SVN_NCURSES_INCLUDES"
            LDFLAGS="$LDFLAGS $SVN_NCURSES_LIBS"
            AC_RUN_IFELSE(
            [AC_LANG_SOURCE([[
            #include <stdio.h>
            #include <curses.h>
            int main(void) {
              printf("%s\n", NCURSES_VERSION);
              return 0;
            }]])],
            [],
            [echo "not available"],
            [echo "not available (cross compiling)"])
            CPPFLAGS="$save_cppflags"
            LDFLAGS="$save_ldflags"
        fi
    fi

    if test "$do_svnbrowse_build" = "yes"; then
        if test "$ncurses_found" = "no"; then
            AC_MSG_WARN([svnbrowse requires ncurses, disabling])
            SVN_BUILD_SVNBROWSE=false
        else
            SVN_BUILD_SVNBROWSE=true
        fi
    else
        SVN_BUILD_SVNBROWSE=false
    fi

    SVN_DOT_CLANGD([$SVN_NCURSES_INCLUDES])
    AC_SUBST(SVN_BUILD_SVNBROWSE)
    AC_SUBST(SVN_NCURSES_INCLUDES)
    AC_SUBST(SVN_NCURSES_LIBS)
])

AC_DEFUN(SVN_NCURSES_PREFIX,
[
    AC_MSG_NOTICE([ncurses library configuration via prefix])
    save_cppflags="$CPPFLAGS"
    ncurses_includes="-I$ncurses_prefix/include"
    CPPFLAGS="$CPPFLAGS $ncurses_includes"
    AC_CHECK_HEADER([curses.h],[
        save_ldflags="$LDFLAGS"
        ncurses_libs="`SVN_REMOVE_STANDARD_LIB_DIRS(-L$ncurses_prefix/lib)`"
        LDFLAGS="$LDFLAGS $ncurses_libs"
        AC_CHECK_LIB([ncurses],[termattrs],[
            ncurses_found="yes"
            SVN_NCURSES_INCLUDES="$ncurses_includes"
            SVN_NCURSES_LIBS="$ncurses_libs -lncurses"
        ])
        LDFLAGS="$save_ldflags"
    ])
    CPPFLAGS="$save_cppflags"
])

AC_DEFUN(SVN_NCURSES_PKG_CONFIG,
[
    AC_MSG_NOTICE([ncurses library configuration via pkg-config])
    if test -n "$PKG_CONFIG"; then
        AC_MSG_CHECKING([for ncurses library])
        if $PKG_CONFIG ncurses --exists; then
            AC_MSG_RESULT([yes])
            ncurses_found=yes
            ncurses_libs=`$PKG_CONFIG ncurses --libs`
            SVN_NCURSES_INCLUDES=`$PKG_CONFIG ncurses --cflags`
            SVN_NCURSES_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS($ncurses_libs)`"
        else
            AC_MSG_RESULT([no])
        fi
    fi
])
