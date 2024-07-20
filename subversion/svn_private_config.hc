/*
 * svn_private_config.hc : Template for svn_private_config.h for CMake.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* ==================================================================== */




#ifndef SVN_PRIVATE_CONFIG_HW
#define SVN_PRIVATE_CONFIG_HW


#define SVN_BUILD_HOST "@SVN_BUILD_HOST@"
#define SVN_BUILD_TARGET "@SVN_BUILD_TARGET@"

/* The minimal version of Berkeley DB we want */
#define SVN_FS_WANT_DB_MAJOR    4
#define SVN_FS_WANT_DB_MINOR    0
#define SVN_FS_WANT_DB_PATCH    14

/* Path separator for local filesystem */
#ifdef WIN32
#define SVN_PATH_LOCAL_SEPARATOR '\\'
#else
#define SVN_PATH_LOCAL_SEPARATOR '/'
#endif

/* Name of system's null device */
#ifdef WIN32
#define SVN_NULL_DEVICE_NAME "nul"
#else
#define SVN_NULL_DEVICE_NAME "/dev/null"
#endif

/* Defined to be the path to the installed binaries */
#define SVN_BINDIR "/usr/local/bin"



/* The default FS back-end type */
#define DEFAULT_FS_TYPE "fsfs"

/* The default HTTP library to use */
#define DEFAULT_HTTP_LIBRARY "serf"

/* Define to the Python/C API format character suitable for apr_int64_t */
#if defined(_WIN64)
#define SVN_APR_INT64_T_PYCFMT "l"
#elif defined(_WIN32)
#define SVN_APR_INT64_T_PYCFMT "L"
#endif

/* Setup gettext macros */
#define N_(x) x
#define U_(x) x
#define PACKAGE_NAME "subversion"

#ifdef ENABLE_NLS
#define SVN_LOCALE_RELATIVE_PATH "../share/locale"
#include <locale.h>
#include <libintl.h>
#define _(x) dgettext(PACKAGE_NAME, x)
#define Q_(x1, x2, n) dngettext(PACKAGE_NAME, x1, x2, n)
#define HAVE_BIND_TEXTDOMAIN_CODESET
#else
#define _(x) (x)
#define Q_(x1, x2, n) (((n) == 1) ? x1 : x2)
#define gettext(x) (x)
#define dgettext(domain, x) (x)
#endif

/* compiler hints */
#if defined(__GNUC__) && (__GNUC__ >= 3)
# define SVN__PREDICT_FALSE(x) (__builtin_expect(x, 0))
# define SVN__PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
# define SVN__PREDICT_FALSE(x) (x)
# define SVN__PREDICT_TRUE(x) (x)
#endif

#if defined(SVN_DEBUG)
# define SVN__FORCE_INLINE
# define SVN__PREVENT_INLINE
#elif defined(_MSC_VER)
# define SVN__FORCE_INLINE __forceinline
# define SVN__PREVENT_INLINE __declspec(noinline)
#elif defined(__GNUC__) && (__GNUC__ >= 3)
# define SVN__FORCE_INLINE APR_INLINE __attribute__ ((always_inline))
# define SVN__PREVENT_INLINE __attribute__ ((noinline))
#else
# define SVN__FORCE_INLINE APR_INLINE
# define SVN__PREVENT_INLINE
#endif

/* Macro used to specify that a variable is intentionally left unused.
   Suppresses compiler warnings about the variable being unused.  */
#define SVN_UNUSED(v) ( (void)(v) )

#endif /* SVN_PRIVATE_CONFIG_HW */

/* Inclusion of Berkeley DB header */
#ifdef SVN_WANT_BDB
#define APU_WANT_DB
#include <apu_want.h>
#endif
