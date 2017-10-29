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
dnl  SVN_PY3C
dnl
dnl  Check configure options and assign variables related to
dnl  the py3c library.
dnl

AC_DEFUN(SVN_PY3C,
[
  py3c_found=no
  py3c_skip=no

  AC_ARG_WITH(py3c,AS_HELP_STRING([--with-py3c=PREFIX],
                                  [py3c python extension compatibility library]),
  [
    if test "$withval" = "yes"; then
      py3c_skip=no
    elif test "$withval" = "no"; then
      py3c_skip=yes
    else
      py3c_skip=no
      py3c_prefix="$withval"
    fi
  ])

  if test "$py3c_skip" = "yes"; then
    AC_MSG_ERROR([subversion swig python bindings require py3c])
  fi

  if test -n "$py3c_prefix"; then
    AC_MSG_NOTICE([py3c library configuration via prefix])
    save_cppflags="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS -I$py3c_prefix/include"
    AC_CHECK_HEADERS(py3c.h,[
        py3c_found="yes"
        SVN_PY3C_INCLUDES="-I$py3c_prefix/include"
    ])
    CPPFLAGS="$save_cppflags"
  else
    SVN_PY3C_PKG_CONFIG()

    if test "$py3c_found" = "no"; then
      AC_MSG_NOTICE([py3c library configuration])
      AC_CHECK_HEADER(py3c.h, [
        py3c_found="yes"
      ])
    fi
  fi

  if test "$py3c_found" = "no"; then
    AC_MSG_ERROR([subversion swig python bindings require py3c])
  fi

  AC_SUBST(SVN_PY3C_INCLUDES)
])

dnl SVN_PY3C_PKG_CONFIG()
dnl Use pkg-config to try and detect and configure py3c
AC_DEFUN(SVN_PY3C_PKG_CONFIG,
[
  AC_MSG_NOTICE([py3c library configuration via pkg-config])
  if test -n "$PKG_CONFIG"; then
    AC_MSG_CHECKING([for py3c library])
    if $PKG_CONFIG py3c --exists; then
      AC_MSG_RESULT([yes])
      py3c_found=yes
      SVN_PY3C_INCLUDES=`$PKG_CONFIG py3c --cflags`
    else
      AC_MSG_RESULT([no])
    fi
  fi
])
