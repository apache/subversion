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

BOOST_AUTO_TEST_SUITE(aprwrap_arrays,
                      * boost::unit_test::fixture<init>());

BOOST_AUTO_TEST_CASE(create_array)
{
  typedef APR::Array<unsigned char> Array;

  APR::Pool pool;
  Array array(pool);

  BOOST_TEST(array.array() != nullptr);
  BOOST_TEST(array.size() == 0);
  BOOST_TEST(sizeof(Array::value_type) == sizeof(unsigned char));
  BOOST_TEST(array.array()->elt_size == sizeof(Array::value_type));
}

BOOST_AUTO_TEST_CASE(wrap_array)
{
  typedef APR::Array<unsigned char> Array;

  APR::Pool pool;
  apr_array_header_t* apr_array =
    apr_array_make(pool.get(), 0, sizeof(Array::value_type));
  BOOST_TEST_REQUIRE(apr_array != nullptr);

  Array array(apr_array);
  BOOST_TEST(array.array() == apr_array);
  BOOST_TEST(array.size() == 0);
}

BOOST_AUTO_TEST_CASE(rewrap_type_mismatch)
{
  typedef APR::Array<unsigned char> ByteArray;
  typedef APR::Array<int> IntArray;

  APR::Pool pool;
  BOOST_CHECK_THROW(ByteArray array(IntArray(pool).array()),
                    std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(out_of_bounds)
{
  typedef APR::Array<unsigned char> Array;

  APR::Pool pool;
  Array array(pool);

  BOOST_CHECK_THROW(array.at(-1), std::out_of_range);
  BOOST_CHECK_THROW(array.at(array.size()), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(indexing)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  BOOST_TEST(array[0] == APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  BOOST_TEST(array[array.size() - 1] == APR_ARRAY_IDX(array.array(),
                                                      array.array()->nelts - 1,
                                                      Array::value_type));
}

BOOST_AUTO_TEST_CASE(checked_indexing)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  BOOST_TEST(array.at(0) == APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  BOOST_TEST(array.at(array.size() - 1) == APR_ARRAY_IDX(array.array(),
                                                         array.array()->nelts - 1,
                                                         Array::value_type));
}

BOOST_AUTO_TEST_CASE(iteration)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  struct Iteration : public Array::Iteration
  {
    Iteration(apr_array_header_t* raw_array)
      : m_index(0), m_raw_array(raw_array)
      {}

    bool operator()(Array::value_type& value)
      {
        BOOST_TEST(value == APR_ARRAY_IDX(m_raw_array, m_index,
                                          Array::value_type));
        ++m_index;
        return true;
      }

  private:
    Array::size_type m_index;
    apr_array_header_t* m_raw_array;
  } callback(array.array());

  array.iterate(callback);

  // TODO: Fix iteration to take std::function.
  // Array::size_type index = 0;
  // apr_array_header_t* raw_array = array.array();
  // array.iterate(
  //     [&index, &raw_array](Array::value_type& value) {
  //       BOOST_TEST(value == APR_ARRAY_IDX(raw_array, index, Array::value_type));
  //       ++index;
  //       return true;
  //     });
}

BOOST_AUTO_TEST_CASE(const_iteration)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  struct Iteration : public Array::ConstIteration
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

BOOST_AUTO_TEST_CASE(push)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  const Array::size_type point = array.size();
  const Array::value_type first = array[0];
  const Array::value_type last = array[point - 1];

  array.push("octavius");
  array.push("nonus");
  array.push("decimus");

  BOOST_TEST(array.size() == point + 3);
  BOOST_TEST(array[0] == first);
  BOOST_TEST(array[point - 1] == last);
  BOOST_TEST(array[point] == "octavius");
  BOOST_TEST(array[array.size() - 1] == "decimus");
}

BOOST_AUTO_TEST_CASE(pop)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  for (Array::size_type i = 0, z = array.size(); i <= z; ++i)
    {
      const char** last = (!array.array()->nelts ? nullptr
                           : &APR_ARRAY_IDX(array.array(),
                                            array.array()->nelts - 1,
                                            Array::value_type));
      BOOST_TEST(array.pop() == last);
    }
}

BOOST_AUTO_TEST_SUITE_END();
