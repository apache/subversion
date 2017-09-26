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
dnl  Search for a suitable version of serf. min_major_num,
dnl  min_minor_num, and min_micro_num are used to determine
dnl  if the serf library is at least that version.
dnl
dnl  If a --with-serf option (no argument) or --with-serf=yes
dnl  option is passed, then a search for serf on the system will be
dnl  performed with pkg-config.  If --with-serf=yes was actually passed
dnl  then we error if we can't actually find serf.
dnl
dnl  If a --with-serf=PREFIX option is passed search for a suitable
dnl  serf installed on the system under that PREFIX.  First we will
dnl  try to find a pc file for serf under the prefix or directly
dnl  in the prefix (allowing the path that the serf-?.pc file to be
dnl  passed to configure if the pc file is in a non-standard location)
dnl  and then use pkg-config to determine the options to use that library.
dnl  If pkg-confg can't provide us the options to use that library fall
dnl  back on trying to use the guess the options based on just the prefix.
dnl  We will error if we can't find serf.
dnl
dnl  If a --with-serf=no option is passed then no search will be
dnl  conducted.
dnl
dnl  If the search for serf fails, set svn_lib_serf to no, otherwise set
dnl  it to yes.
dnl

AC_DEFUN(SVN_LIB_SERF,
[
  serf_found=no
  serf_required=no
  serf_skip=no

  serf_check_major="$1"
  serf_check_minor="$2"
  serf_check_patch="$3"
  serf_check_version="$1.$2.$3"

  AC_ARG_WITH(serf,AS_HELP_STRING([--with-serf=PREFIX],
                                  [Serf HTTP client library (enabled by default if found)]),
  [
    if test "$withval" = "yes" ; then
      serf_required=yes 
    elif test "$withval" = "no" ; then
      serf_skip=yes 
    else
      serf_required=yes
      serf_prefix="$withval"
    fi
  ])

  if test "$serf_skip" = "no" ; then
    SVN_SERF_PKG_CONFIG()
    if test -n "$serf_prefix" && test "$serf_found" = "no" ; then
      SVN_SERF_PREFIX_CONFIG()
    fi
  
    AC_MSG_CHECKING([was serf enabled])
    if test "$serf_found" = "yes"; then
      AC_MSG_RESULT([yes])
    else 
      AC_MSG_RESULT([no]) 
      SVN_DOWNLOAD_SERF() 
      if test "$serf_required" = "yes"; then
        AC_MSG_ERROR([Serf was explicitly enabled but an appropriate version was not found.])
      fi
    fi
  fi

  svn_lib_serf=$serf_found

  AC_SUBST(SVN_SERF_INCLUDES)
  AC_SUBST(SVN_SERF_LIBS)
])

dnl SVN_SERF_PREFIX_CONFIG()
dnl Use user provided prefix to try and detect and configure serf
AC_DEFUN(SVN_SERF_PREFIX_CONFIG,
[
  AC_MSG_NOTICE([serf library configuration via prefix])
  serf_required=yes
  for serf_major in serf-2 serf-1; do
    if ! test -d $serf_prefix/include/$serf_major; then continue; fi
    save_cppflags="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES -I$serf_prefix/include/$serf_major"
    AC_CHECK_HEADERS(serf.h,[
      save_ldflags="$LDFLAGS"
      LDFLAGS="$LDFLAGS `SVN_REMOVE_STANDARD_LIB_DIRS(-L$serf_prefix/lib)`"
      AC_CHECK_LIB($serf_major, serf_context_create,[
        AC_TRY_COMPILE([
#include <stdlib.h>
#include "serf.h"
],[
#if ! SERF_VERSION_AT_LEAST($serf_check_major, $serf_check_minor, $serf_check_patch)
#error Serf version too old: need $serf_check_version
#endif
], [serf_found=yes], [AC_MSG_WARN([Serf version too old: need $serf_check_version])
      serf_found=no])], ,
    $SVN_APRUTIL_LIBS $SVN_APR_LIBS -lz)
    LDFLAGS="$save_ldflags"])
    CPPFLAGS="$save_cppflags"
    test $serf_found = yes && break
  done

  if test $serf_found = "yes"; then
    SVN_SERF_INCLUDES="-I$serf_prefix/include/$serf_major"
    if test -e "$serf_prefix/lib/lib$serf_major.la"; then
      SVN_SERF_LIBS="$serf_prefix/lib/lib$serf_major.la"
    else
      SVN_SERF_LIBS="-l$serf_major"
      LDFLAGS="$LDFLAGS `SVN_REMOVE_STANDARD_LIB_DIRS(-L$serf_prefix/lib)`"
    fi
  fi
])

dnl SVN_SERF_PKG_CONFIG()
dnl Use pkg-config to try and detect and configure serf
AC_DEFUN(SVN_SERF_PKG_CONFIG,
[
  AC_MSG_NOTICE([serf library configuration via pkg-config])
  if test -n "$PKG_CONFIG"; then
    for serf_major in serf-2 serf-1; do
      AC_MSG_CHECKING([for $serf_major library])
      if test -n "$serf_prefix" ; then
        dnl User provided a prefix so we try to find the pc file under
        dnl the prefix.  PKG_CONFIG_PATH isn't useful for this because
        dnl we want to make sure that we get the library in the prefix
        dnl the user specifies and we want to allow the prefix path to
        dnl point at the path for the pc file is in (if it's in some
        dnl other path than $serf_prefix/lib/pkgconfig).
        if test -e "$serf_prefix/$serf_major.pc" ; then
          serf_pc_arg="$serf_prefix/$serf_major.pc"
        elif test -e "$serf_prefix/lib/pkgconfig/$serf_major.pc" ; then
          serf_pc_arg="$serf_prefix/lib/pkgconfig/$serf_major.pc"
        else
          AC_MSG_RESULT([no])
          continue
        fi
      else
        serf_pc_arg="$serf_major"
      fi
      if $PKG_CONFIG $serf_pc_arg --exists; then
        AC_MSG_RESULT([yes])
        AC_MSG_CHECKING([serf library version])
        SERF_VERSION=`$PKG_CONFIG $serf_pc_arg --modversion`
        AC_MSG_RESULT([$SERF_VERSION])
        AC_MSG_CHECKING([serf version is suitable])
        if $PKG_CONFIG $serf_pc_arg --atleast-version=$serf_check_version; then
          AC_MSG_RESULT([yes])
          serf_found=yes
          SVN_SERF_INCLUDES=[`$PKG_CONFIG $serf_pc_arg --cflags | $SED -e 's/ -D[^ ]*//g' -e 's/^-D[^ ]*//g'`]
          SVN_SERF_LIBS=`$PKG_CONFIG $serf_pc_arg --libs-only-l` 
          dnl don't use --libs-only-L because then we might miss some options
          LDFLAGS=["$LDFLAGS `$PKG_CONFIG $serf_pc_arg --libs | $SED -e 's/-l[^ ]*//g'`"]
          break
        else
          AC_MSG_RESULT([no])
          AC_MSG_WARN([Serf version too old: need $serf_check_version])
        fi        
      else
        AC_MSG_RESULT([no])
      fi
    done 
  fi
])

dnl SVN_DOWNLOAD_SERF()
dnl no serf found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_SERF,
[
  echo ""
  echo "An appropriate version of serf could not be found, so libsvn_ra_serf"
  echo "will not be built.  If you want to build libsvn_ra_serf, please"
  echo "install serf $serf_check_version or newer."
  echo ""
])
