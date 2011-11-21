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

  AC_ARG_WITH(zlib,AS_HELP_STRING([--with-zlib=PREFIX],
                                  [zlib compression library]),
  [
    if test "$withval" = "yes" ; then
      AC_CHECK_HEADER(zlib.h, [
        AC_CHECK_LIB(z, inflate, [zlib_found="builtin"])
      ])
    elif test "$withval" = "no" ; then
      AC_MSG_ERROR([cannot compile without zlib.])
    else
      AC_MSG_NOTICE([zlib library configuration])
      zlib_prefix=$withval
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS -I$zlib_prefix/include"
      AC_CHECK_HEADERS(zlib.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="$LDFLAGS -L$zlib_prefix/lib"
        AC_CHECK_LIB(z, inflate, [zlib_found="yes"])
        LDFLAGS="$save_ldflags"
      ])
      CPPFLAGS="$save_cppflags"
    fi
  ],
  [
    AC_CHECK_HEADER(zlib.h, [
      AC_CHECK_LIB(z, inflate, [zlib_found="builtin"])
    ])
  ])

  if test "$zlib_found" = "no"; then
    AC_MSG_ERROR([subversion requires zlib])
  fi

  if test "$zlib_found" = "yes"; then
    SVN_ZLIB_INCLUDES="-I$zlib_prefix/include"
    LDFLAGS="$LDFLAGS `SVN_REMOVE_STANDARD_LIB_DIRS(-L$zlib_prefix/lib)`"
  fi

  SVN_ZLIB_LIBS="-lz"

  AC_SUBST(SVN_ZLIB_INCLUDES)
  AC_SUBST(SVN_ZLIB_LIBS)
])
