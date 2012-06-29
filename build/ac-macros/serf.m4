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
dnl  SVN_LIB_SERF(min_major_num, min_minor_num, min_micro_num)
dnl
dnl  Check configure options and assign variables related to
dnl  the serf library.
dnl

AC_DEFUN(SVN_LIB_SERF,
[
  serf_found=no

  serf_check_major="$1"
  serf_check_minor="$2"
  serf_check_patch="$3"

  AC_ARG_WITH(serf,AS_HELP_STRING([--with-serf=PREFIX],
                                  [Serf HTTP client library]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-serf requires an argument.])
    elif test "$withval" != "no" ; then
      AC_MSG_NOTICE([serf library configuration])
      serf_prefix=$withval
      for serf_major in serf-2 serf-1; do
        if ! test -d $serf_prefix/include/$serf_major; then continue; fi
        save_cppflags="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES -I$serf_prefix/include/$serf_major"
        AC_CHECK_HEADERS(serf.h,[
          save_ldflags="$LDFLAGS"
          LDFLAGS="$LDFLAGS -L$serf_prefix/lib"
          AC_CHECK_LIB($serf_major, serf_context_create,[
            AC_TRY_COMPILE([
#include <stdlib.h>
#include "serf.h"
],[
#if ! SERF_VERSION_AT_LEAST($serf_check_major, $serf_check_minor, $serf_check_patch)
#error Serf version too old: need $serf_check_major.$serf_check_minor.$serf_check_patch
#endif
], [serf_found=yes], [AC_MSG_WARN([Serf version too old: need $serf_check_major.$serf_check_minor.$serf_check_patch])
          serf_found=no])], ,
            $SVN_APRUTIL_LIBS $SVN_APR_LIBS -lz)
          LDFLAGS="$save_ldflags"])
        CPPFLAGS="$save_cppflags"
        test $serf_found = yes && break
      done
    fi
  ])

  if test $serf_found = "yes"; then
    SVN_SERF_INCLUDES="-I$serf_prefix/include/$serf_major"
    if test -e "$serf_prefix/lib/lib$serf_major.la"; then
      SVN_SERF_LIBS="$serf_prefix/lib/lib$serf_major.la"
    else
      SVN_SERF_LIBS="-l$serf_major"
      LDFLAGS="$LDFLAGS -L$serf_prefix/lib"
    fi
  fi

  svn_lib_serf=$serf_found

  AC_SUBST(SVN_SERF_INCLUDES)
  AC_SUBST(SVN_SERF_LIBS)
])
