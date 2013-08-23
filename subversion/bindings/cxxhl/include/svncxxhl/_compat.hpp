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

// Configuration test: std::shared_ptr<> and friends
// Currently detects: clang++, g++, msvc-2010+
#ifndef SVN_CXXHL_HAVE_STD_SMART_PTRS
#  if   (defined(__clang__) && __cplusplus >= 201103L) \
     || (defined(__GNUC__) && defined(__GXX_EXPERIMENTAL_CXX0X__)) \
     || (defined(_MSC_VER) && _MSC_VER >= 1600)
#    define SVN_CXXHL_HAVE_STD_SMART_PTRS
#  endif  // config test: std::shared_ptr<>
#endif  // SVN_CXXHL_HAVE_STD_SMART_PTRS

// Configuration test: std::tr1::shared_ptr<> and friends
// Currently detects: clang++, g++
#ifndef SVN_CXXHL_HAVE_STD_SMART_PTRS
#  ifndef SVN_CXXHL_HAVE_STD_TR1_SMART_PTRS
#    if   defined(__GNUC__) \
       && (__GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ >= 1)
#      define SVN_CXXHL_HAVE_STD_TR1_SMART_PTRS
#    endif  // config test: std::tr1::shared_ptr<>
#  endif  // SVN_CXXHL_HAVE_STD_TR1_SMART_PTRS
#endif  // SVN_CXXHL_HAVE_STD_SMART_PTRS


#if defined(SVN_CXXHL_HAVE_STD_SMART_PTRS)

#include <memory>
namespace apache {
namespace subversion {
namespace cxxhl {
namespace compat {
using std::weak_ptr;
using std::shared_ptr;
using std::enable_shared_from_this;
} // namespace compat
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#elif defined(SVN_CXXHL_HAVE_STD_TR1_SMART_PTRS)

#include <tr1/memory>
namespace apache {
namespace subversion {
namespace cxxhl {
namespace compat {
using std::tr1::weak_ptr;
using std::tr1::shared_ptr;
using std::tr1::enable_shared_from_this;
} // namespace compat
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#else
// We need smart pointers from somewhere. If we cannot find them in
// ::std given known compiler characteristics, then try Boost as a
// last resort.

#define SVN_CXXHL_USING_BOOST
#include <boost/shared_ptr.hpp>
namespace apache {
namespace subversion {
namespace cxxhl {
namespace compat {
using boost::weak_ptr;
using boost::shared_ptr;
using boost::enable_shared_from_this;
} // namespace compat
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#endif  // SVN_CXXHL_HAVE_STD_SMART_PTRS

// Configuration test: noncopyable mixin.
#ifdef SVN_CXXHL_USING_BOOST

#include <boost/noncopyable.hpp>
namespace apache {
namespace subversion {
namespace cxxhl {
namespace compat {
using boost::noncopyable;
} // namespace compat
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#else  // !SVN_CXXHL_USING_BOOST

namespace apache {
namespace subversion {
namespace cxxhl {
namespace compat {
namespace noncopyable_
{
class noncopyable
{
protected:
  noncopyable() {}
  ~noncopyable() {}
private:
  noncopyable(const noncopyable&);
  noncopyable& operator=(const noncopyable&);
};
} // namespace noncopyable_
typedef noncopyable_::noncopyable noncopyable;
} // namespace compat
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#endif // SVN_CXXHL_USING_BOOST

#endif  // SVN_CXXHL_COMPAT_HPP
