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

#ifndef SVNXX_PRIVATE_STRINGS_HPP
#define SVNXX_PRIVATE_STRINGS_HPP

#include <codecvt>
#include <cstdlib>
#include <locale>
#include <string>

namespace apache {
namespace subversion {
namespace svnxx {
namespace detail {

namespace {
// Define codecvt types for our various converters.
template<typename C> struct codecvt;
template<> struct codecvt<wchar_t> : public std::codecvt_utf8<wchar_t> {};
template<> struct codecvt<char16_t> : public std::codecvt_utf8_utf16<char16_t> {};
template<> struct codecvt<char32_t> : public std::codecvt_utf8<char32_t> {};

template<typename C> using converter = std::wstring_convert<codecvt<C>, C>;
} // anonymous namespace

/**
 * Convert a sequence of @c char's encoded as UTF-8 to a not-narroe string.
 */
template<typename C>
inline std::basic_string<C> convert(const char* cstr, std::size_t size)
{
  return converter<C>().from_bytes(cstr, cstr + size);
}

/**
 * Convert a nul-terminated string encoded as UTF-8 to a not-narrow string.
 */
template<typename C>
inline std::basic_string<C> convert(const char* cstr)
{
  return converter<C>().from_bytes(cstr);
}

/**
 * Convert a string encoded as UTF-8 to a not-narrow string.
 */
template<typename C>
inline std::basic_string<C> convert(const std::string& str)
{
  return converter<C>().from_bytes(str);
}

/**
 * Convert a sequence of @c C's to a string encoded as UTF-8.
 */
template<typename C>
inline std::string convert(const C* cstr, std::size_t size)
{
  return converter<C>().to_bytes(cstr, size);
}

/**
 * Convert a nul-terminated not-narrow string to a string encoded as UTF-8.
 */
template<typename C>
inline std::string convert(const C* cstr)
{
  return converter<C>().to_bytes(cstr);
}

/**
 * Convert a not-narrow string to a string encoded as UTF-8.
 */
template<typename C>
inline std::string convert(const std::basic_string<C>& str)
{
  return converter<C>().to_bytes(str);
}

} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_STRINGS_HPP
