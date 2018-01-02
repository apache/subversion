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
dnl The default is simply to link with -lutf8proc.
dnl
dnl The user can specify --with-utf8proc=PREFIX to look in PREFIX or
dnl --with-utf8proc=internal to use the internal copy of the utf8proc
dnl code.

AC_DEFUN(SVN_UTF8PROC,
[
  AC_ARG_WITH([utf8proc],
    [AS_HELP_STRING([--with-utf8proc=PREFIX|internal],
                    [look for utf8proc in PREFIX or use the internal code])],
    [
      if test "$withval" = internal; then
        utf8proc_prefix=internal
      elif test "$withval" = yes; then
        utf8proc_prefix=std
      else
        utf8proc_prefix="$withval"
      fi
    ],
    [utf8proc_prefix=std])

  if test "$utf8proc_prefix" = "internal"; then
    AC_MSG_NOTICE([using internal utf8proc])
    AC_DEFINE([SVN_INTERNAL_UTF8PROC], [1],
               [Define to use internal UTF8PROC code])
  else
    if test "$utf8proc_prefix" = "std"; then
      SVN_UTF8PROC_STD
    else
      SVN_UTF8PROC_PREFIX
    fi
    if test "$utf8proc_found" != "yes"; then
      AC_MSG_ERROR([Subversion requires UTF8PROC])
    fi
  fi
  AC_SUBST(SVN_UTF8PROC_INCLUDES)
  AC_SUBST(SVN_UTF8PROC_LIBS)
])

AC_DEFUN(SVN_UTF8PROC_STD,
[
  AC_MSG_NOTICE([utf8proc configuration without pkg-config])
  AC_CHECK_LIB(utf8proc, utf8proc_version, [
     utf8proc_found=yes
     SVN_UTF8PROC_LIBS="-lutf8proc"
  ])
])

AC_DEFUN(SVN_UTF8PROC_PREFIX,
[
  AC_MSG_NOTICE([utf8proc configuration via prefix])
  save_cppflags="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS -I$utf8proc_prefix/include"
  save_ldflags="$LDFLAGS"
  LDFLAGS="$LDFLAGS -L$utf8proc_prefix/lib"
  AC_CHECK_LIB(utf8proc, utf8proc_version, [
    utf8proc_found=yes
    SVN_UTF8PROC_INCLUDES="-I$utf8proc_prefix/include"
    SVN_UTF8PROC_LIBS="`SVN_REMOVE_STANDARD_LIB_DIRS(-L$utf8proc_prefix/lib)` -lutf8proc"
  ])
  LDFLAGS="$save_ldflags"
  CPPFLAGS="$save_cppflags"
])
