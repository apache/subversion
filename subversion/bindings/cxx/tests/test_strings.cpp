/*
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

#include <boost/test/unit_test.hpp>

#include <codecvt>
#include <cstdint>
#include <locale>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "private/svn_utf_private.h"
#include "../src/aprwrap.hpp"

namespace {
std::string to_utf8(const std::u32string& str)
{
  static const int32_t endiancheck = 0xa5cbbc5a;
  static const bool arch_big_endian =
    (reinterpret_cast<const char*>(&endiancheck)[sizeof(endiancheck) - 1] == '\x5a');

  apr::pool scratch_pool;
  const svn_string_t* utf8_string;

  auto err = svn_utf__utf32_to_utf8(
      &utf8_string,
      reinterpret_cast<const apr_int32_t*>(str.c_str()),
      str.size(), arch_big_endian, scratch_pool.get(), scratch_pool.get());
  if (err)
    {
      svn_error_clear(err);
      throw std::range_error("bad unicode code point");
    }
  return std::string(utf8_string->data, utf8_string->len);
}

template<typename C> struct codepoint;
template<> struct codepoint<void>
{
  using src_type = char32_t;
  static constexpr std::uint_least32_t min = 0;
  static constexpr std::uint_least32_t max = 0x10ffff;
  static constexpr std::uint_least32_t surrogate_min = 0xd800;
  static constexpr std::uint_least32_t surrogate_max = 0xdfff;
};

template<> struct codepoint<char32_t> : public codepoint<void>
{
  using dst_type = char32_t;
  static std::u32string convert(const std::u32string& str)
    {
      return str;
    };
};

template<> struct codepoint<char16_t> : public codepoint<void>
{
  using dst_type = char16_t;
  static std::u16string convert(const std::u32string& str)
    {
      std::wstring_convert<std::codecvt_utf8_utf16<dst_type>, dst_type> u;
      return u.from_bytes(to_utf8(str));
    }
};

template<> struct codepoint<wchar_t> : public codepoint<void>
{
  using dst_type = wchar_t;

#ifdef WIN32
  // Be conservative, use UCS-2 for wchar_t on Windows
  static_assert(sizeof(wchar_t) == sizeof(char16_t),
                "I thought we had 2-byte wide chars on Windows");
  static constexpr std::uint_least32_t max = 0xffff;
#endif

  static std::wstring convert(const std::u32string& str)
    {
#ifdef WIN32
      const auto from_utf8 =
        [](const std::string& sstr)
          {
            apr::pool scratch_pool;
            const wchar_t* result;
            auto err = svn_utf__win32_utf8_to_utf16(
                &result, sstr.c_str(), nullptr, scratch_pool.get());
            if (err)
              {
                svn_error_clear(err);
                throw std::range_error("bad conversion to utf16");
              }
            return std::wstring(result);
          }
#else
      std::wstring_convert<std::codecvt_utf8<dst_type>, dst_type> u;
      const auto from_utf8 = [&u](const std::string& sstr)
                               {
                                 return u.from_bytes(sstr);
                               };
#endif
      return from_utf8(to_utf8(str));
    }
};

// Generate random strings.
template<typename C>
inline std::vector<std::basic_string<C>> generate_string_data(int count)
{
  using cp = codepoint<C>;
  std::mt19937 mt{std::random_device()()};
  std::uniform_int_distribution<> cgen{typename cp::src_type(cp::min),
                                       typename cp::src_type(cp::max)};
  std::uniform_int_distribution<> lgen{7U, 31U};

  std::vector<std::basic_string<C>> result;
  result.reserve(count);

  for (int i = 0; i < count; ++i)
    {
      const unsigned len = lgen(mt);

      std::u32string val;
      val.reserve(len);

      for (int j = 0; j < len; ++j)
        {
        repeat:
          auto c = cgen(mt);
          if (c >= cp::surrogate_min && c <= cp::surrogate_max)
            goto repeat;
          val.push_back(c);
        }
      result.emplace_back(cp::convert(val));
    }
  return result;
}
} // anonymous namespace


#include "../src/private/strings_private.hpp"

#include "fixture_init.hpp"

namespace svn = ::apache::subversion::svnxx;
namespace detail = ::apache::subversion::svnxx::detail;

BOOST_AUTO_TEST_SUITE(strings,
                      * boost::unit_test::fixture<init>());

BOOST_AUTO_TEST_CASE(wstring_conversion_roundtrip)
{
  for (const auto& sample : generate_string_data<wchar_t>(100))
    BOOST_TEST((sample == detail::convert<wchar_t>(detail::convert(sample))));
}

BOOST_AUTO_TEST_CASE(u16string_conversion_roundtrip)
{
  for (const auto& sample : generate_string_data<char16_t>(100))
    BOOST_TEST((sample == detail::convert<char16_t>(detail::convert(sample))));
}

BOOST_AUTO_TEST_CASE(u32string_conversion_roundtrip)
{
  for (const auto& sample : generate_string_data<char32_t>(100))
    BOOST_TEST((sample == detail::convert<char32_t>(detail::convert(sample))));
}

BOOST_AUTO_TEST_CASE(nulchar)
{
  const std::string nulstr("\0", 1);
  const std::wstring wnulstr(L"\0", 1);
  const std::u16string u16nulstr(u"\0", 1);
  const std::u32string u32nulstr(U"\0", 1);

  BOOST_TEST(nulstr.size() == 1);
  BOOST_TEST(wnulstr.size() == 1);
  BOOST_TEST(u16nulstr.size() == 1);
  BOOST_TEST(u32nulstr.size() == 1);

  BOOST_TEST(detail::convert<wchar_t>(nulstr).size() == 1);
  BOOST_TEST(detail::convert<char16_t>(nulstr).size() == 1);
  BOOST_TEST(detail::convert<char32_t>(nulstr).size() == 1);

  BOOST_TEST((detail::convert<wchar_t>(nulstr) == wnulstr));
  BOOST_TEST((detail::convert<char16_t>(nulstr) == u16nulstr));
  BOOST_TEST((detail::convert<char32_t>(nulstr) == u32nulstr));

  BOOST_TEST(detail::convert(wnulstr).size() == 1);
  BOOST_TEST(detail::convert(u16nulstr).size() == 1);
  BOOST_TEST(detail::convert(u32nulstr).size() == 1);

  BOOST_TEST((detail::convert(wnulstr) == nulstr));
  BOOST_TEST((detail::convert(u16nulstr) == nulstr));
  BOOST_TEST((detail::convert(u32nulstr) == nulstr));
}

BOOST_AUTO_TEST_SUITE_END();
