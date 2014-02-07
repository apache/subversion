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

#include <algorithm>
#include <stdexcept>

#include "../src/aprwrap.hpp"

#include <gmock/gmock.h>

//
// Pools
//

TEST(Pools, InitializeGlobalPool)
{
  APR::Pool pool;
  EXPECT_THAT(pool.get(), testing::NotNull());
  EXPECT_THAT(apr_pool_parent_get(pool.get()), testing::NotNull());
}

TEST(Pools, CreateSubpool)
{
  APR::Pool pool;
  APR::Pool subpool(&pool);
  EXPECT_EQ(pool.get(), apr_pool_parent_get(subpool.get()));
}

TEST(Pools, TypedAllocate)
{
  APR::Pool pool;
  const unsigned char* buffer = pool.alloc<unsigned char>(1);
  EXPECT_THAT(buffer, testing::NotNull());
}

// N.B.: This test may pass randomly even if zero-filled allocation
// does not work correctly, since we cannot make assumptions about the
// values of uninitialized memory.
TEST(Pools, TypedAllocateZerofill)
{
  APR::Pool pool;
  static const std::size_t size = 32757;
  const unsigned char* buffer = pool.allocz<unsigned char>(size);
  ASSERT_THAT(buffer, testing::NotNull());
  EXPECT_EQ(size, std::count(buffer, buffer + size, 0));
}

//
// Array helper functions
//

namespace {
// Create a randomly-ordered array of constant strings.
apr_array_header_t* fill_array(APR::Pool& pool)
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

//
// Arrays
//

TEST(Arrays, CreateArray)
{
  typedef APR::Array<unsigned char> Array;

  APR::Pool pool;
  Array array(pool);

  EXPECT_THAT(array.array(), testing::NotNull());
  EXPECT_EQ(0, array.size());
  EXPECT_EQ(sizeof(unsigned char), sizeof(Array::value_type));
  EXPECT_EQ(sizeof(Array::value_type), array.array()->elt_size);
}

TEST(Arrays, WrapArray)
{
  typedef APR::Array<unsigned char> Array;

  APR::Pool pool;
  apr_array_header_t* apr_array =
    apr_array_make(pool.get(), 0, sizeof(Array::value_type));
  ASSERT_THAT(apr_array, testing::NotNull());

  Array array(apr_array);
  EXPECT_EQ(apr_array, array.array());
  EXPECT_EQ(0, array.size());
}

TEST(Arrays, RewrapTypeMismatch)
{
  typedef APR::Array<unsigned char> ByteArray;
  typedef APR::Array<int> IntArray;

  APR::Pool pool;
  EXPECT_THROW(ByteArray array(IntArray(pool).array()),
               std::invalid_argument);
}

TEST(Arrays, OutOfBounds)
{
  typedef APR::Array<unsigned char> Array;

  APR::Pool pool;
  Array array(pool);

  EXPECT_THROW(array.at(-1), std::out_of_range);
  EXPECT_THROW(array.at(array.size()), std::out_of_range);
}

TEST(Arrays, Indexing)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  EXPECT_STREQ(array[0], APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  EXPECT_STREQ(array[array.size() - 1], APR_ARRAY_IDX(array.array(),
                                                      array.array()->nelts - 1,
                                                      Array::value_type));
}

TEST(Arrays, CheckedIndexing)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  EXPECT_STREQ(array.at(0), APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  EXPECT_STREQ(array.at(array.size() - 1),
               APR_ARRAY_IDX(array.array(), array.array()->nelts - 1,
                             Array::value_type));
}

TEST(Arrays, Iteration)
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
        EXPECT_STREQ(value, APR_ARRAY_IDX(m_raw_array, m_index,
                                          Array::value_type));
        ++m_index;
        return true;
      }

  private:
    Array::size_type m_index;
    apr_array_header_t* m_raw_array;
  } callback(array.array());

  array.iterate(callback);
}

TEST(Arrays, ConstIteration)
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
        EXPECT_STREQ(value, APR_ARRAY_IDX(m_raw_array, m_index,
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

TEST(Arrays, Push)
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

  EXPECT_EQ(point + 3, array.size());
  EXPECT_STREQ(first, array[0]);
  EXPECT_STREQ(last, array[point - 1]);
  EXPECT_STREQ("octavius", array[point]);
  EXPECT_STREQ("decimus", array[array.size() - 1]);
}

TEST(Arrays, Pop)
{
  typedef APR::Array<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  for (Array::size_type i = 0, z = array.size(); i <= z; ++i)
    {
      const char** last = (!array.array()->nelts ? NULL
                           : &APR_ARRAY_IDX(array.array(),
                                            array.array()->nelts - 1,
                                            const char*));
      EXPECT_EQ(last, array.pop());
    }
}

//
// ConstArrays
//

TEST(ConstArrays, WrapArray)
{
  typedef APR::ConstArray<unsigned char> Array;

  APR::Pool pool;
  const apr_array_header_t* apr_array =
    apr_array_make(pool.get(), 0, sizeof(Array::value_type));
  ASSERT_THAT(apr_array, testing::NotNull());

  Array array(apr_array);
  EXPECT_EQ(apr_array, array.array());
  EXPECT_EQ(0, array.size());
}

TEST(ConstArrays, RewrapTypeMismatch)
{
  typedef APR::ConstArray<unsigned char> ByteArray;
  typedef APR::Array<int> IntArray;

  APR::Pool pool;
  EXPECT_THROW(ByteArray array(IntArray(pool).array()),
               std::invalid_argument);
}

TEST(ConstArrays, OutOfBounds)
{
  typedef APR::ConstArray<unsigned char> Array;

  APR::Pool pool;
  Array array = Array(APR::Array<Array::value_type>(pool));

  EXPECT_THROW(array.at(-1), std::out_of_range);
  EXPECT_THROW(array.at(array.size()), std::out_of_range);
}

TEST(ConstArrays, Indexing)
{
  typedef APR::ConstArray<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  EXPECT_STREQ(array[0], APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  EXPECT_STREQ(array[array.size() - 1], APR_ARRAY_IDX(array.array(),
                                                      array.array()->nelts - 1,
                                                      Array::value_type));
}

TEST(ConstArrays, CheckedIndexing)
{
  typedef APR::ConstArray<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  EXPECT_STREQ(array.at(0), APR_ARRAY_IDX(array.array(), 0, Array::value_type));
  EXPECT_STREQ(array.at(array.size() - 1),
               APR_ARRAY_IDX(array.array(), array.array()->nelts - 1,
                             Array::value_type));
}

TEST(ConstArrays, Iteration)
{
  typedef APR::ConstArray<const char*> Array;

  APR::Pool pool;
  Array array(fill_array(pool));

  struct Iteration : public Array::Iteration
  {
    Iteration(const  apr_array_header_t* raw_array)
      : m_index(0), m_raw_array(raw_array)
      {}

    bool operator()(const Array::value_type& value)
      {
        EXPECT_STREQ(value, APR_ARRAY_IDX(m_raw_array, m_index,
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

//
// Hash tables
//

TEST(Hashes, StringHash)
{
  typedef APR::Hash<char, const char> H;

  APR::Pool pool;
  H hash(pool);
  hash.set("aa", "a");
  hash.set("bbb", "b");
  hash.set("cccc", "c");

  EXPECT_EQ(3, hash.size());
  EXPECT_STREQ("a", hash.get("aa"));
  EXPECT_STREQ("b", hash.get("bbb"));
  EXPECT_STREQ("c", hash.get("cccc"));
}

TEST(Hashes, FixedStringHash)
{
  // The point of this test is to verify that the key-length parameter
  // of the template actually limits the length of the keys.
  typedef APR::Hash<char, const char, 2> H;

  APR::Pool pool;
  H hash(pool);
  hash.set("aa&qux", "a");
  hash.set("bb#foo", "b");
  hash.set("cc@bar", "c");

  EXPECT_EQ(3, hash.size());
  EXPECT_STREQ("a", hash.get("aa%foo"));
  EXPECT_STREQ("b", hash.get("bb*bar"));
  EXPECT_STREQ("c", hash.get("cc$qux"));
}

TEST(Hashes, Delete)
{
  typedef APR::Hash<char, const char> H;

  APR::Pool pool;
  H hash(pool);
  hash.set("aa", "a");
  hash.set("bbb", "b");
  hash.set("cccc", "c");

  hash.del("bbb");

  EXPECT_EQ(2, hash.size());
  EXPECT_STREQ("a", hash.get("aa"));
  EXPECT_STREQ("c", hash.get("cccc"));
}

TEST(Hashes, Iterate)
{
  typedef APR::Hash<char, const char> H;

  APR::Pool pool;
  H hash(pool);
  hash.set("aa", "a");
  hash.set("bbb", "b");
  hash.set("cccc", "c");

  struct C : public H::Iteration
  {
    H& m_hash;
    explicit C(H& hashref) : m_hash(hashref) {}

    bool operator()(const H::Key& key, H::value_type value)
      {
        EXPECT_STREQ(value, m_hash.get(key));
        return true;
      }
  } callback(hash);

  hash.iterate(callback, pool);
}
