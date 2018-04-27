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
dnl  SVN_LIB_Z
dnl
dnl  Check configure options and assign variables related to
dnl  the zlib library.
dnl

AC_DEFUN(SVN_LIB_Z,
[
  zlib_found=no
  zlib_skip=no

  AC_ARG_WITH(zlib,AS_HELP_STRING([--with-zlib=PREFIX],
                                  [zlib compression library]),
  [
    if test "$withval" = "yes"; then
      zlib_skip=no
    elif test "$withval" = "no"; then
      zlib_skip=yes
    else
      zlib_skip=no
      zlib_prefix="$withval"
    fi
  ])

  if test "$zlib_skip" = "yes"; then
    AC_MSG_ERROR([subversion requires zlib])
  fi

  if test -n "$zlib_prefix"; then
    AC_MSG_NOTICE([zlib library configuration via prefix])
    save_cppflags="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS -I$zlib_prefix/include"
    AC_CHECK_HEADERS(zlib.h,[
      save_ldflags="$LDFLAGS"
      LDFLAGS="$LDFLAGS -L$zlib_prefix/lib"
      AC_CHECK_LIB(z, inflate, [
        zlib_found="yes"
        SVN_ZLIB_INCLUDES="-I$zlib_prefix/include"
        SVN_ZLIB_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS(-L$zlib_prefix/lib)` -lz"
      ])
      LDFLAGS="$save_ldflags"
    ])
    CPPFLAGS="$save_cppflags"
  else
    SVN_ZLIB_PKG_CONFIG()

    if test "$zlib_found" = "no"; then
      AC_MSG_NOTICE([zlib library configuration])
      AC_CHECK_HEADER(zlib.h, [
        AC_CHECK_LIB(z, inflate, [
          zlib_found="builtin"
          SVN_ZLIB_LIBS="-lz"
        ])
      ])
    fi
  fi
 
  if test "$zlib_found" = "no"; then
    AC_MSG_ERROR([subversion requires zlib])
  fi

  AC_SUBST(SVN_ZLIB_INCLUDES)
  AC_SUBST(SVN_ZLIB_LIBS)
])

dnl SVN_ZLIB_PKG_CONFIG()
dnl Use pkg-config to try and detect and configure zlib
AC_DEFUN(SVN_ZLIB_PKG_CONFIG,
[
  AC_MSG_NOTICE([zlib library configuration via pkg-config])
  if test -n "$PKG_CONFIG"; then
    AC_MSG_CHECKING([for zlib library])
    if $PKG_CONFIG zlib --exists; then
      AC_MSG_RESULT([yes])
      zlib_found=yes
      SVN_ZLIB_INCLUDES=`$PKG_CONFIG zlib --cflags`
      SVN_ZLIB_LIBS=`$PKG_CONFIG zlib --libs`
      SVN_ZLIB_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS($SVN_ZLIB_LIBS)`"
    else
      AC_MSG_RESULT([no])
    fi
  fi
])
