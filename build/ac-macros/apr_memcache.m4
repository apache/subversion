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
dnl  SVN_LIB_APR_MEMCACHE
dnl
dnl  Check configure options and assign variables related to
dnl  the apr_memcache client library.
dnl  Sets svn_lib_apr_memcache to "yes" if memcache code is accessible
dnl  either from the standalone apr_memcache library or from apr-util.
dnl

AC_DEFUN(SVN_LIB_APR_MEMCACHE,
[
  apr_memcache_found=no

  AC_ARG_WITH(apr_memcache,AC_HELP_STRING([--with-apr_memcache=PREFIX],
                                  [Standalone apr_memcache client library]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-apr_memcache requires an argument.])
    else
      AC_MSG_NOTICE([looking for separate apr_memcache package])
      apr_memcache_prefix=$withval
      save_cppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES -I$apr_memcache_prefix/include/apr_memcache-0"
      AC_CHECK_HEADER(apr_memcache.h,[
        save_ldflags="$LDFLAGS"
        LDFLAGS="$LDFLAGS -L$apr_memcache_prefix/lib"
        AC_CHECK_LIB(apr_memcache, apr_memcache_create,
          [apr_memcache_found="standalone"])
        LDFLAGS="$save_ldflags"])
      CPPFLAGS="$save_cppflags"
    fi
  ], [
dnl   Try just looking in apr-util (>= 1.3 has it already).
    AC_MSG_NOTICE([looking for apr_memcache as part of apr-util])
    save_cppflags="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES $SVN_APRUTIL_INCLUDES"
    AC_CHECK_HEADER(apr_memcache.h,[
      save_ldflags="$LDFLAGS"
      LDFLAGS="$LDFLAGS $SVN_APRUTIL_LIBS"
      AC_CHECK_LIB(aprutil-1, apr_memcache_create,
        [apr_memcache_found="aprutil"])
      LDFLAGS="$save_ldflags"])
    CPPFLAGS="$save_cppflags"
   ])


  if test $apr_memcache_found = "standalone"; then
    SVN_APR_MEMCACHE_INCLUDES="-I$apr_memcache_prefix/include/apr_memcache-0"
    SVN_APR_MEMCACHE_LIBS="$apr_memcache_prefix/lib/libapr_memcache.la"
    svn_lib_apr_memcache=yes
  elif test $apr_memcache_found = "aprutil"; then
dnl We are already linking apr-util everywhere, so no special treatement needed.
    SVN_APR_MEMCACHE_INCLUDES=""
    SVN_APR_MEMCACHE_LIBS=""
    svn_lib_apr_memcache=yes
  elif test $apr_memcache_found = "reconfig"; then
    svn_lib_apr_memcache=yes
  else
    svn_lib_apr_memcache=no
  fi

  AC_SUBST(SVN_APR_MEMCACHE_INCLUDES)
  AC_SUBST(SVN_APR_MEMCACHE_LIBS)
])
