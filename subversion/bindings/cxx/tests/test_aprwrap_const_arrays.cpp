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

#include "test_aprwrap_array_helpers.hpp"

BOOST_AUTO_TEST_SUITE(aprwrap_const_arrayw,
                      * boost::unit_test::fixture<init>());

BOOST_AUTO_TEST_CASE(wrap_array)
{
  typedef APR::ConstArray<unsigned char> Array;

  apr::pool pool;
  const apr_array_header_t* apr_array =
    apr_array_make(pool.get(), 0, sizeof(Array::value_type));
  BOOST_TEST_REQUIRE(apr_array != nullptr);

  Array array(apr_array);
  BOOST_TEST(array.array() == apr_array);
  BOOST_TEST(array.size() == 0);
}

BOOST_AUTO_TEST_CASE(rewrap_type_mismatch)
{
  typedef APR::ConstArray<unsigned char> ByteArray;
  typedef APR::Array<int> IntArray;

  apr::pool pool;
  BOOST_CHECK_THROW(ByteArray array(IntArray(pool).array()),
                    std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(out_of_bounds)
{
  typedef APR::ConstArray<unsigned char> Array;

  apr::pool pool;
  Array array = Array(APR::Array<Array::value_type>(pool));

  BOOST_CHECK_THROW(array.at(-1), std::out_of_range);
  BOOST_CHECK_THROW(array.at(array.size()), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(indexing)
{
  typedef APR::ConstArray<const char*> Array;

  apr::pool pool;
  Array array(fill_array(pool));

  BOOST_TEST(array[0] == APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  BOOST_TEST(array[array.size() - 1] == APR_ARRAY_IDX(array.array(),
                                                      array.array()->nelts - 1,
                                                      Array::value_type));
}

BOOST_AUTO_TEST_CASE(checked_indexing)
{
  typedef APR::ConstArray<const char*> Array;

  apr::pool pool;
  Array array(fill_array(pool));

  BOOST_TEST(array.at(0) == APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  BOOST_TEST(array.at(array.size() - 1) == APR_ARRAY_IDX(array.array(),
                                                         array.array()->nelts - 1,
                                                         Array::value_type));
}

BOOST_AUTO_TEST_CASE(iteration)
{
  typedef APR::ConstArray<const char*> Array;

  apr::pool pool;
  Array array(fill_array(pool));

  struct Iteration : public Array::Iteration
  {
    Iteration(const  apr_array_header_t* raw_array)
      : m_index(0), m_raw_array(raw_array)
      {}

    bool operator()(const Array::value_type& value)
      {
        BOOST_TEST(value == APR_ARRAY_IDX(m_raw_array, m_index,
                                          Array::value_type));
        ++m_index;
        return true;
      }

  private:
    Array::size_type m_index;
    const apr_array_header_t* m_raw_array;
  } callback(array.array());

  array.iterate(callback);
}

BOOST_AUTO_TEST_SUITE_END();
