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

BOOST_AUTO_TEST_SUITE(aprwrap_hashes,
                      * boost::unit_test::fixture<init>());

BOOST_AUTO_TEST_CASE(string_hash)
{
  typedef APR::Hash<char, const char> H;

  apr::pool pool;
  H hash(pool);
  hash.set("aa", "a");
  hash.set("bbb", "b");
  hash.set("cccc", "c");

  BOOST_TEST(hash.size() == 3);
  BOOST_TEST(hash.get("aa") == "a");
  BOOST_TEST(hash.get("bbb") == "b");
  BOOST_TEST(hash.get("cccc") == "c");
}

BOOST_AUTO_TEST_CASE(fixed_string_hash)
{
  // The point of this test is to verify that the key-length parameter
  // of the template actually limits the length of the keys.
  typedef APR::Hash<char, const char, 2> H;

  apr::pool pool;
  H hash(pool);
  hash.set("aa&qux", "a");
  hash.set("bb#foo", "b");
  hash.set("cc@bar", "c");

  BOOST_TEST(hash.size() == 3);
  BOOST_TEST(hash.get("aa%foo") == "a");
  BOOST_TEST(hash.get("bb*bar") == "b");
  BOOST_TEST(hash.get("cc$qux") == "c");
}

BOOST_AUTO_TEST_CASE(delete_element)
{
  typedef APR::Hash<char, const char> H;

  apr::pool pool;
  H hash(pool);
  hash.set("aa", "a");
  hash.set("bbb", "b");
  hash.set("cccc", "c");

  hash.del("bbb");

  BOOST_TEST(hash.size() == 2);
  BOOST_TEST(hash.get("aa") == "a");
  BOOST_TEST(hash.get("cccc") == "c");
}

BOOST_AUTO_TEST_CASE(iterate)
{
  typedef APR::Hash<char, const char> H;

  apr::pool pool;
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
        BOOST_TEST(value == m_hash.get(key));
        return true;
      }
  } callback(hash);

  hash.iterate(callback, pool);
}

BOOST_AUTO_TEST_SUITE_END();
