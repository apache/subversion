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
dnl  If configuring via prefix, the ac_cv_python_includes variable needs
dnl  to be set to the appropriate include configuration to build against
dnl  the correct Python C interface.
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
    AC_MSG_NOTICE([Skipping configure of py3c])
  else
    if test -n "$py3c_prefix"; then
      AC_MSG_NOTICE([py3c library configuration via prefix $py3c_prefix])

      dnl The standard Python headers are required to validate py3c.h
      if test "$ac_cv_python_includes" = "none"; then
        AC_MSG_WARN([py3c cannot be used without distutils module])
      fi

      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $ac_cv_python_includes -I$py3c_prefix/include"
      AC_CHECK_HEADERS(py3c.h,[
          py3c_found="yes"
          SVN_PY3C_INCLUDES="-I$py3c_prefix/include"
      ])
      CPPFLAGS="$save_cppflags"
    else
      SVN_PY3C_PKG_CONFIG()

      if test "$py3c_found" = "no"; then
        AC_MSG_NOTICE([py3c library configuration without pkg-config])

        dnl The standard Python headers are required to validate py3c.h
        if test "$ac_cv_python_includes" = "none"; then
          AC_MSG_WARN([py3c cannot be used without distutils module])
        fi

        save_cppflags="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $ac_cv_python_includes"
        AC_CHECK_HEADER(py3c.h, [
          py3c_found="yes"
        ])
        CPPFLAGS="$save_cppflags"
      fi
    fi
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
