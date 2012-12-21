/**
 * @copyright
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
 * @endcopyright
 */

#ifndef __cplusplus
#error "This is a C++ header file."
#endif

#ifndef SVN_CXXHL_COMPAT_HPP
#define SVN_CXXHL_COMPAT_HPP

// Configuration test: std::shared_ptr<>
// Currently detects: clang++, g++, msvc-2010+
#ifndef SVN_CXXHL_HAVE_STD_SHARED_PTR
#  if   (defined(__clang__) && __cplusplus >= 201103L) \
     || (defined(__GNUC__) && defined(__GXX_EXPERIMENTAL_CXX0X__)) \
     || (defined(_MSC_VER) && _MSC_VER >= 1600)
#    define SVN_CXXHL_HAVE_STD_SHARED_PTR
#  endif  // config test: std::shared_ptr<>
#endif  // SVN_CXXHL_HAVE_STD_SHARED_PTR

// Configuration test: std::tr1::shared_ptr<>
// Currently detects: clang++, g++
#ifndef SVN_CXXHL_HAVE_STD_SHARED_PTR
#  ifndef SVN_CXXHL_HAVE_STD_TR1_SHARED_PTR
#    if   defined(__GNUC__) \
       && (__GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ >= 1)
#      define SVN_CXXHL_HAVE_STD_TR1_SHARED_PTR
#    endif  // config test: std::tr1::shared_ptr<>
#  endif  // SVN_CXXHL_HAVE_STD_TR1_SHARED_PTR
#endif  // SVN_CXXHL_HAVE_STD_SHARED_PTR


#if defined(SVN_CXXHL_HAVE_STD_SHARED_PTR)

#include <memory>
namespace subversion {
namespace cxxhl {
namespace compat {
using std::shared_ptr;
} // namespace compat
} // namespace cxxhl
} // namespace subversion

#elif defined(SVN_CXXHL_HAVE_STD_TR1_SHARED_PTR)

#include <tr1/memory>
namespace subversion {
namespace cxxhl {
namespace compat {
using std::tr1::shared_ptr;
} // namespace compat
} // namespace cxxhl
} // namespace subversion

#else
// We need shared_ptr<> from somewhere. If we cannot find it in ::std
// given known compiler characteristics, then try boost as a last
// resort.

#include <boost/shared_ptr.hpp>
namespace subversion {
namespace cxxhl {
namespace compat {
using boost::shared_ptr;
} // namespace compat
} // namespace cxxhl
} // namespace subversion

#endif  // SVN_CXXHL_HAVE_STD_SHARED_PTR

#endif  // SVN_CXXHL_COMPAT_HPP
