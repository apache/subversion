/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#ifndef SVN_SWIG_SWIGUTIL_RB__PRE_RUBY_H
#define SVN_SWIG_SWIGUTIL_RB__PRE_RUBY_H

#if defined(SVN_SWIG_RUBY__CUSTOM_RUBY_CONFIG) && defined(_MSC_VER)
/* The standard install of Ruby on Windows 1.9 expects to be build with MINGW,
   a gcc compatible toolchain. Since 1.8 they removed much of the compatibility
   with Visual C++, but we can't build with MingW as that would break APR
   binary compatibility.
 */

#include <ruby/config.h>
#ifdef _MSC_VER
#endif

#undef NORETURN
#undef DEPRECATED
#undef FUNC_STDCALL
#undef FUNC_CDECL
#undef FUNC_FASTCALL
#undef RUBY_ALIAS_FUNCTION_TYPE
#undef RUBY_ALIAS_FUNCTION_VOID
#undef HAVE_GCC_ATOMIC_BUILTINS
#undef RUBY_FUNC_EXPORTED
#undef RUBY_FUNC_EXPORTED
#undef RUBY_EXTERN

#define NORETURN(x) __declspec(noreturn) x
#define RUBY_EXTERN extern __declspec(dllimport)

/* Yuck. But this makes ruby happy */
#undef pid_t
#undef uid_t
#undef gid_t
typedef  int pid_t;
typedef  int uid_t;
typedef  int gid_t;

#if !defined(__cplusplus) && !defined(inline)
#define inline __inline
#endif
typedef long ssize_t;

/* Don't define iovec when including APR */
#define APR_IOVEC_DEFINED

/* Undefine headers that aren't available in Visual C++, but config.h says
   are available. */
#undef HAVE_UNISTD_H

/* Visual C++ >= 2013 has <inttypes.h> */
#if _MSC_VER < 1800
#undef HAVE_INTTYPES_H
#endif

#endif /* defined(SVN_SWIG_RUBY__CUSTOM_RUBY_CONFIG) && defined(_MSC_VER) */

#endif /* SVN_SWIG_SWIGUTIL_RB__PRE_RUBY_H */
