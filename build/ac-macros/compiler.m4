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
dnl SVN_CFLAGS_ADD_IFELSE(option, success, failure)
dnl
dnl   Check if the C compiler accepts $option. If it does, prepend it
dnl   to CFLAGS and execute $success; otherwise execute $failure.
dnl
dnl SVN_CXXFLAGS_ADD_IFELSE(option, success, failure)
dnl
dnl   Like SVN_CFLAGS_ADD_IFELSE, but for C++ and CXXFLAGS.
dnl
dnl SVN_PROG_CC: Customized replacement for AC_PROG_CC
dnl SVN_PROG_CXX: Customized replacement for AC_PROG_CXX

AC_DEFUN([_SVN_XXFLAGS_ADD_IFELSE],
[
  _svn_xxflags__save="[$][$3]"
  AC_LANG_PUSH([$1])
  AC_MSG_CHECKING([if [$][$2] accepts $4])
  [$3]="$4 [$][$3]"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])],[
    AC_MSG_RESULT([yes])
    $5
  ],[
    AC_MSG_RESULT([no])
    [$3]="$_svn_xxflags__save"
    $6
  ])
  AC_LANG_POP([$1])
])

AC_DEFUN([SVN_CFLAGS_ADD_IFELSE],
  [_SVN_XXFLAGS_ADD_IFELSE([C],[CC],[CFLAGS],[$1],[$2],[$3])])

AC_DEFUN([SVN_CXXFLAGS_ADD_IFELSE],
  [_SVN_XXFLAGS_ADD_IFELSE([C++],[CXX],[CXXFLAGS],[$1],[$2],[$3])])


AC_DEFUN([SVN_CC_MODE_SETUP],
[
  CFLAGS_KEEP="$CFLAGS"
  CFLAGS=""

  dnl Find flags to force C90 mode
                dnl gcc and clang
  SVN_CFLAGS_ADD_IFELSE([-std=c90],[],[
    SVN_CFLAGS_ADD_IFELSE([-std=c89],[],[
      SVN_CFLAGS_ADD_IFELSE([-ansi])
    ])
  ])

  CMODEFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS_KEEP"
  AC_SUBST(CMODEFLAGS)
  AC_SUBST(CMAINTAINERFLAGS)

  dnl Tell clang to not accept unknown warning flags
  SVN_CFLAGS_ADD_IFELSE([-Werror=unknown-warning-option])
])


AC_DEFUN([SVN_CXX_MODE_SETUP],
[
  CXXFLAGS_KEEP="$CXXFLAGS"
  CXXFLAGS=""

  dnl Find flags to force C++98 mode
                dnl g++ and clang++
  SVN_CXXFLAGS_ADD_IFELSE([-std=c++98])

  CXXMODEFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS_KEEP"
  AC_SUBST(CXXMODEFLAGS)
  AC_SUBST(CXXMAINTAINERFLAGS)

  dnl Tell clang++ to not accept unknown warning flags
  SVN_CXXFLAGS_ADD_IFELSE([-Werror=unknown-warning-option])
])
