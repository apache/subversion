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
dnl  SVN_LIB_GIT2
dnl
dnl  Check configure options and assign variables related to
dnl  the libgit2 library.
dnl

AC_DEFUN(SVN_LIB_GIT2,
[
  libgit2_found=no

  AC_ARG_WITH(libgit2,AS_HELP_STRING([--with-libgit2=PREFIX],
                                  [libgit2 library]),
  [
    if test "$withval" = "no" ; then
      AC_MSG_ERROR([cannot compile without libgit2.])
    else
      AC_MSG_NOTICE([libgit2 library configuration])
      libgit2_prefix=$withval
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS -I$libgit2_prefix/include"
      AC_CHECK_HEADERS(git2.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="$LDFLAGS -L$libgit2_prefix/lib"
        AC_CHECK_LIB(git2, git_libgit2_version, [libgit2_found="yes"])
        LDFLAGS="$save_ldflags"
      ])
      CPPFLAGS="$save_cppflags"
    fi
  ],
  [ ])

  if test "$libgit2_found" = "no"; then
    AC_MSG_ERROR([subversion requires libgit2])
  fi

  if test "$libgit2_found" = "yes"; then
    SVN_LIBGIT2_INCLUDES="-I$libgit2_prefix/include"
    LDFLAGS="$LDFLAGS `SVN_REMOVE_STANDARD_LIB_DIRS(-L$libgit2_prefix/lib)`"
  fi

  SVN_LIBGIT2_LIBS="-lgit2"

  AC_SUBST(SVN_LIBGIT2_INCLUDES)
  AC_SUBST(SVN_LIBGIT2_LIBS)
])
