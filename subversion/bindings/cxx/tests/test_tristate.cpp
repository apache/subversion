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

#include "svnxx/tristate.hpp"
#include "../src/private/tristate-private.hpp"

namespace svn = ::apache::subversion::svnxx;
namespace detail = ::apache::subversion::svnxx::detail;

BOOST_AUTO_TEST_SUITE(tristate);

namespace {
constexpr auto T = svn::tristate(true);
constexpr auto F = svn::tristate(false);
constexpr auto X = svn::tristate::unknown();
} // anonymous namespace

BOOST_AUTO_TEST_CASE(constants)
{
  BOOST_TEST(!svn::tristate::unknown(T));
  BOOST_TEST(!svn::tristate::unknown(F));
  BOOST_TEST(svn::tristate::unknown(X));

  BOOST_TEST(bool(T));
  BOOST_TEST(!bool(!T));

  BOOST_TEST(!bool(F));
  BOOST_TEST(bool(!F));

  BOOST_TEST(!bool(X));
  BOOST_TEST(!bool(!X));
}

BOOST_AUTO_TEST_CASE(conversions)
{
  BOOST_TEST(detail::convert(T) == svn_tristate_true);
  BOOST_TEST(detail::convert(F) == svn_tristate_false);
  BOOST_TEST(detail::convert(X) == svn_tristate_unknown);

  BOOST_TEST(detail::convert(svn_tristate_true) == T);
  BOOST_TEST(detail::convert(svn_tristate_false) == F);
  BOOST_TEST(svn::tristate::unknown(detail::convert(svn_tristate_unknown)));
}

BOOST_AUTO_TEST_CASE(construct_true)
{
  constexpr auto state = svn::tristate(true);
  BOOST_TEST(!svn::tristate::unknown(state));
  BOOST_TEST(bool(state));
  BOOST_TEST(!bool(!state));
}

BOOST_AUTO_TEST_CASE(construct_false)
{
  constexpr auto state = svn::tristate(false);
  BOOST_TEST(!svn::tristate::unknown(state));
  BOOST_TEST(!bool(state));
  BOOST_TEST(bool(!state));
}

BOOST_AUTO_TEST_CASE(construct_unknown)
{
  constexpr auto state = svn::tristate::unknown();
  BOOST_TEST(svn::tristate::unknown(state));
  BOOST_TEST(!bool(state));
  BOOST_TEST(!bool(!state));
}

BOOST_AUTO_TEST_CASE(tristate_and_tristate)
{
  BOOST_TEST((T && T) == T);
  BOOST_TEST((T && F) == F);
  BOOST_TEST((F && T) == F);
  BOOST_TEST((F && F) == F);
  BOOST_TEST(svn::tristate::unknown(T && X));
  BOOST_TEST(svn::tristate::unknown(X && T));
  BOOST_TEST((F && X) == F);
  BOOST_TEST((X && F) == F);
  BOOST_TEST(svn::tristate::unknown(X && X));
}

BOOST_AUTO_TEST_CASE(tristate_and_bool)
{
  BOOST_TEST((T &&  true) == T);
  BOOST_TEST((T && false) == F);
  BOOST_TEST((F &&  true) == F);
  BOOST_TEST((F && false) == F);
  BOOST_TEST(svn::tristate::unknown(X && true));
  BOOST_TEST((X && false) == F);
}

BOOST_AUTO_TEST_CASE(bool_and_tristate)
{
  BOOST_TEST((true  && T) == T);
  BOOST_TEST((false && T) == F);
  BOOST_TEST((true  && F) == F);
  BOOST_TEST((false && F) == F);
  BOOST_TEST(svn::tristate::unknown(true && X));
  BOOST_TEST((false && X) == F);
}

BOOST_AUTO_TEST_CASE(tristate_and_number)
{
  BOOST_TEST((T &&  1) == T);
  BOOST_TEST((T &&  0) == F);
  BOOST_TEST((F && -1) == F);
  BOOST_TEST((F &&  0) == F);
  BOOST_TEST(svn::tristate::unknown(X &&  5));
  BOOST_TEST((X &&  0) == F);
}

BOOST_AUTO_TEST_CASE(number_and_tristate)
{

  BOOST_TEST((77 && T) == T);
  BOOST_TEST(( 0 && T) == F);
  BOOST_TEST((~0 && F) == F);
  BOOST_TEST(( 0 && F) == F);
  BOOST_TEST(svn::tristate::unknown(07 && X));
  BOOST_TEST(( 0 && X) == F);
}

BOOST_AUTO_TEST_CASE(tristate_or_tristate)
{
  BOOST_TEST((T || T) == T);
  BOOST_TEST((T || F) == T);
  BOOST_TEST((F || T) == T);
  BOOST_TEST((F || F) == F);
  BOOST_TEST((T || X) == T);
  BOOST_TEST((X || T) == T);
  BOOST_TEST(svn::tristate::unknown(F || X));
  BOOST_TEST(svn::tristate::unknown(X || F));
  BOOST_TEST(svn::tristate::unknown(X || X));
}

BOOST_AUTO_TEST_CASE(tristate_or_bool)
{
  BOOST_TEST((T ||  true) == T);
  BOOST_TEST((T || false) == T);
  BOOST_TEST((F ||  true) == T);
  BOOST_TEST((F || false) == F);
  BOOST_TEST((X ||  true) == T);
  BOOST_TEST(svn::tristate::unknown(X || false));
}

BOOST_AUTO_TEST_CASE(bool_or_tristate)
{
  BOOST_TEST((true  || T) == T);
  BOOST_TEST((false || T) == T);
  BOOST_TEST((true  || F) == T);
  BOOST_TEST((false || F) == F);
  BOOST_TEST((true  || X) == T);
  BOOST_TEST(svn::tristate::unknown(false || X));
}

BOOST_AUTO_TEST_CASE(tristate_or_number)
{
  BOOST_TEST((T ||  1) == T);
  BOOST_TEST((T ||  0) == T);
  BOOST_TEST((F || -1) == T);
  BOOST_TEST((F ||  0) == F);
  BOOST_TEST((X ||  5) == T);
  BOOST_TEST(svn::tristate::unknown(X || 0));
}

BOOST_AUTO_TEST_CASE(number_or_tristate)
{

  BOOST_TEST((77 || T) == T);
  BOOST_TEST(( 0 || T) == T);
  BOOST_TEST((~0 || F) == T);
  BOOST_TEST(( 0 || F) == F);
  BOOST_TEST((07 || X) == T);
  BOOST_TEST(svn::tristate::unknown(0 || X));
}

BOOST_AUTO_TEST_CASE(tristate_eq_tristate)
{
  BOOST_TEST((T == T) == T);
  BOOST_TEST((T == F) == F);
  BOOST_TEST(svn::tristate::unknown(T == X));
  BOOST_TEST((F == T) == F);
  BOOST_TEST((F == F) == T);
  BOOST_TEST(svn::tristate::unknown(F == X));
  BOOST_TEST(svn::tristate::unknown(X == T));
  BOOST_TEST(svn::tristate::unknown(X == F));
  BOOST_TEST(svn::tristate::unknown(X == X));
}

BOOST_AUTO_TEST_CASE(tristate_eq_bool)
{
  BOOST_TEST((T ==  true) == T);
  BOOST_TEST((T == false) == F);
  BOOST_TEST((F ==  true) == F);
  BOOST_TEST((F == false) == T);
  BOOST_TEST(svn::tristate::unknown(X == true));
  BOOST_TEST(svn::tristate::unknown(X == false));
}

BOOST_AUTO_TEST_CASE(bool_eq_tristate)
{
  BOOST_TEST((true  == T) == T);
  BOOST_TEST((false == T) == F);
  BOOST_TEST((true  == F) == F);
  BOOST_TEST((false == F) == T);
  BOOST_TEST(svn::tristate::unknown(true  == X));
  BOOST_TEST(svn::tristate::unknown(false == X));
}

BOOST_AUTO_TEST_CASE(tristate_neq_tristate)
{
  BOOST_TEST((T != T) == F);
  BOOST_TEST((T != F) == T);
  BOOST_TEST(svn::tristate::unknown(T != X));
  BOOST_TEST((F != T) == T);
  BOOST_TEST((F != F) == F);
  BOOST_TEST(svn::tristate::unknown(F != X));
  BOOST_TEST(svn::tristate::unknown(X != T));
  BOOST_TEST(svn::tristate::unknown(X != F));
  BOOST_TEST(svn::tristate::unknown(X != X));
}

BOOST_AUTO_TEST_CASE(tristate_neq_bool)
{
  BOOST_TEST((T !=  true) == F);
  BOOST_TEST((T != false) == T);
  BOOST_TEST((F !=  true) == T);
  BOOST_TEST((F != false) == F);
  BOOST_TEST(svn::tristate::unknown(X != true));
  BOOST_TEST(svn::tristate::unknown(X != false));
}

BOOST_AUTO_TEST_CASE(bool_neq_tristate)
{
  BOOST_TEST((true  != T) == F);
  BOOST_TEST((false != T) == T);
  BOOST_TEST((true  != F) == T);
  BOOST_TEST((false != F) == F);
  BOOST_TEST(svn::tristate::unknown(true  != X));
  BOOST_TEST(svn::tristate::unknown(false != X));
}

BOOST_AUTO_TEST_SUITE_END();
