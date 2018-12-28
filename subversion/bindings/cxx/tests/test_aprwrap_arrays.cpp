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

#include <algorithm>
#include <stdexcept>

#include "../src/aprwrap.hpp"

#include "fixture_init.hpp"

BOOST_AUTO_TEST_SUITE(aprwrap_arrays,
                      * boost::unit_test::fixture<init>());

namespace {
// Create a randomly-ordered array of constant strings.
apr_array_header_t* fill_array(apr::pool& pool)
{
  apr_array_header_t* a = apr_array_make(pool.get(), 0, sizeof(const char*));
  APR_ARRAY_PUSH(a, const char*) = "primus";
  APR_ARRAY_PUSH(a, const char*) = "secundus";
  APR_ARRAY_PUSH(a, const char*) = "tertius";
  APR_ARRAY_PUSH(a, const char*) = "quartus";
  APR_ARRAY_PUSH(a, const char*) = "quintus";
  APR_ARRAY_PUSH(a, const char*) = "sextus";
  APR_ARRAY_PUSH(a, const char*) = "septimus";
  std::random_shuffle(&APR_ARRAY_IDX(a, 0, const char*),
                      &APR_ARRAY_IDX(a, a->nelts, const char*));
  return a;
}
} // anonymous namespace

BOOST_AUTO_TEST_CASE(create_array)
{
  typedef apr::array<unsigned char> array;

  apr::pool pool;
  array a(pool);

  BOOST_TEST(a.get_array() != nullptr);
  BOOST_TEST(a.size() == 0);
  BOOST_TEST(sizeof(array::value_type) == sizeof(unsigned char));
  BOOST_TEST(a.get_array()->elt_size == sizeof(array::value_type));
}

BOOST_AUTO_TEST_CASE(wrap_array)
{
  typedef apr::array<unsigned char> array;

  apr::pool pool;
  apr_array_header_t* apr_array =
    apr_array_make(pool.get(), 0, sizeof(array::value_type));
  BOOST_TEST_REQUIRE(apr_array != nullptr);

  array a(apr_array);
  BOOST_TEST(a.get_array() == apr_array);
  BOOST_TEST(a.size() == 0);
}

BOOST_AUTO_TEST_CASE(rewrap_type_mismatch)
{
  typedef apr::array<unsigned char> byte_array;
  typedef apr::array<int> int_array;

  apr::pool pool;
  BOOST_CHECK_THROW(byte_array{int_array(pool).get_array()},
                    std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(out_of_bounds)
{
  typedef apr::array<unsigned char> array;

  apr::pool pool;
  array a(pool);

  BOOST_CHECK_THROW(a.at(-1), std::out_of_range);
  BOOST_CHECK_THROW(a.at(a.size()), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(indexing)
{
  typedef apr::array<const char*> array;

  apr::pool pool;
  array a(fill_array(pool));

  BOOST_TEST(a[0] == APR_ARRAY_IDX(a.get_array(), 0, array::value_type));
  BOOST_TEST(a[a.size() - 1] == APR_ARRAY_IDX(a.get_array(),
                                              a.get_array()->nelts - 1,
                                              array::value_type));
}

BOOST_AUTO_TEST_CASE(checked_indexing)
{
  typedef apr::array<const char*> array;

  apr::pool pool;
  array a(fill_array(pool));

  BOOST_TEST(a.at(0) == APR_ARRAY_IDX(a.get_array(), 0, array::value_type));
  BOOST_TEST(a.at(a.size() - 1) == APR_ARRAY_IDX(a.get_array(),
                                                 a.get_array()->nelts - 1,
                                                 array::value_type));
}

BOOST_AUTO_TEST_CASE(iteration)
{
  typedef apr::array<const char*> array;

  apr::pool pool;
  array a(fill_array(pool));

  const auto raw_array = a.get_array();
  array::size_type index = 0;
  for (auto& value : a)
    {
      BOOST_TEST(value == APR_ARRAY_IDX(raw_array, index, array::value_type));
      ++index;
    }
}

BOOST_AUTO_TEST_CASE(const_iteration)
{
  typedef apr::array<const char*> array;

  apr::pool pool;
  const array a(fill_array(pool));

  const auto raw_array = a.get_array();
  array::size_type index = 0;
  for (const auto& value : a)
    {
      BOOST_TEST(value == APR_ARRAY_IDX(raw_array, index, array::value_type));
      ++index;
    }
}

BOOST_AUTO_TEST_CASE(push)
{
  typedef apr::array<const char*> array;

  apr::pool pool;
  array a(fill_array(pool));

  const array::size_type point = a.size();
  const array::value_type first = a[0];
  const array::value_type last = a[point - 1];

  a.push("octavius");
  a.push("nonus");
  a.push("decimus");

  BOOST_TEST(a.size() == point + 3);
  BOOST_TEST(a[0] == first);
  BOOST_TEST(a[point - 1] == last);
  BOOST_TEST(a[point] == "octavius");
  BOOST_TEST(a[a.size() - 1] == "decimus");
}

BOOST_AUTO_TEST_CASE(pop)
{
  typedef apr::array<const char*> array;

  apr::pool pool;
  array a(fill_array(pool));

  for (array::size_type i = 0, z = a.size(); i <= z; ++i)
    {
      const char** last = (!a.get_array()->nelts ? nullptr
                           : &APR_ARRAY_IDX(a.get_array(),
                                            a.get_array()->nelts - 1,
                                            array::value_type));
      BOOST_TEST(a.pop() == last);
    }
}

BOOST_AUTO_TEST_SUITE_END();
