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

namespace {
constexpr auto T = svn::tristate(true);
constexpr auto F = svn::tristate(false);
constexpr auto X = svn::tristate::unknown();
} // anonymous namespace

BOOST_AUTO_TEST_SUITE(tristate);

BOOST_AUTO_TEST_CASE(constants)
{
  BOOST_TEST(!svn::unknown(T));
  BOOST_TEST(!svn::unknown(F));
  BOOST_TEST(svn::unknown(X));

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
  BOOST_TEST(svn::unknown(detail::convert(svn_tristate_unknown)));
}

BOOST_AUTO_TEST_CASE(construct_true)
{
  constexpr auto state = svn::tristate(true);
  BOOST_TEST(!svn::unknown(state));
  BOOST_TEST(bool(state));
  BOOST_TEST(!bool(!state));
}

BOOST_AUTO_TEST_CASE(construct_false)
{
  constexpr auto state = svn::tristate(false);
  BOOST_TEST(!svn::unknown(state));
  BOOST_TEST(!bool(state));
  BOOST_TEST(bool(!state));
}

BOOST_AUTO_TEST_CASE(construct_unknown)
{
  constexpr auto state = svn::tristate::unknown();
  BOOST_TEST(svn::unknown(state));
  BOOST_TEST(!bool(state));
  BOOST_TEST(!bool(!state));
}

BOOST_AUTO_TEST_CASE(tristate_and_tristate)
{
  BOOST_TEST((T && T) == T);
  BOOST_TEST((T && F) == F);
  BOOST_TEST((F && T) == F);
  BOOST_TEST((F && F) == F);
  BOOST_TEST(svn::unknown(T && X));
  BOOST_TEST(svn::unknown(X && T));
  BOOST_TEST((F && X) == F);
  BOOST_TEST((X && F) == F);
  BOOST_TEST(svn::unknown(X && X));
}

BOOST_AUTO_TEST_CASE(tristate_and_bool)
{
  BOOST_TEST((T &&  true) == T);
  BOOST_TEST((T && false) == F);
  BOOST_TEST((F &&  true) == F);
  BOOST_TEST((F && false) == F);
  BOOST_TEST(svn::unknown(X && true));
  BOOST_TEST((X && false) == F);
}

BOOST_AUTO_TEST_CASE(bool_and_tristate)
{
  BOOST_TEST((true  && T) == T);
  BOOST_TEST((false && T) == F);
  BOOST_TEST((true  && F) == F);
  BOOST_TEST((false && F) == F);
  BOOST_TEST(svn::unknown(true && X));
  BOOST_TEST((false && X) == F);
}

BOOST_AUTO_TEST_CASE(tristate_and_number)
{
  BOOST_TEST((T &&  1) == T);
  BOOST_TEST((T &&  0) == F);
  BOOST_TEST((F && -1) == F);
  BOOST_TEST((F &&  0) == F);
  BOOST_TEST(svn::unknown(X &&  5));
  BOOST_TEST((X &&  0) == F);
}

BOOST_AUTO_TEST_CASE(number_and_tristate)
{

  BOOST_TEST((77 && T) == T);
  BOOST_TEST(( 0 && T) == F);
  BOOST_TEST((~0 && F) == F);
  BOOST_TEST(( 0 && F) == F);
  BOOST_TEST(svn::unknown(07 && X));
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
  BOOST_TEST(svn::unknown(F || X));
  BOOST_TEST(svn::unknown(X || F));
  BOOST_TEST(svn::unknown(X || X));
}

BOOST_AUTO_TEST_CASE(tristate_or_bool)
{
  BOOST_TEST((T ||  true) == T);
  BOOST_TEST((T || false) == T);
  BOOST_TEST((F ||  true) == T);
  BOOST_TEST((F || false) == F);
  BOOST_TEST((X ||  true) == T);
  BOOST_TEST(svn::unknown(X || false));
}

BOOST_AUTO_TEST_CASE(bool_or_tristate)
{
  BOOST_TEST((true  || T) == T);
  BOOST_TEST((false || T) == T);
  BOOST_TEST((true  || F) == T);
  BOOST_TEST((false || F) == F);
  BOOST_TEST((true  || X) == T);
  BOOST_TEST(svn::unknown(false || X));
}

BOOST_AUTO_TEST_CASE(tristate_or_number)
{
  BOOST_TEST((T ||  1) == T);
  BOOST_TEST((T ||  0) == T);
  BOOST_TEST((F || -1) == T);
  BOOST_TEST((F ||  0) == F);
  BOOST_TEST((X ||  5) == T);
  BOOST_TEST(svn::unknown(X || 0));
}

BOOST_AUTO_TEST_CASE(number_or_tristate)
{

  BOOST_TEST((77 || T) == T);
  BOOST_TEST(( 0 || T) == T);
  BOOST_TEST((~0 || F) == T);
  BOOST_TEST(( 0 || F) == F);
  BOOST_TEST((07 || X) == T);
  BOOST_TEST(svn::unknown(0 || X));
}

BOOST_AUTO_TEST_CASE(tristate_eq_tristate)
{
  BOOST_TEST((T == T) == T);
  BOOST_TEST((T == F) == F);
  BOOST_TEST(svn::unknown(T == X));
  BOOST_TEST((F == T) == F);
  BOOST_TEST((F == F) == T);
  BOOST_TEST(svn::unknown(F == X));
  BOOST_TEST(svn::unknown(X == T));
  BOOST_TEST(svn::unknown(X == F));
  BOOST_TEST(svn::unknown(X == X));
}

BOOST_AUTO_TEST_CASE(tristate_eq_bool)
{
  BOOST_TEST((T ==  true) == T);
  BOOST_TEST((T == false) == F);
  BOOST_TEST((F ==  true) == F);
  BOOST_TEST((F == false) == T);
  BOOST_TEST(svn::unknown(X == true));
  BOOST_TEST(svn::unknown(X == false));
}

BOOST_AUTO_TEST_CASE(bool_eq_tristate)
{
  BOOST_TEST((true  == T) == T);
  BOOST_TEST((false == T) == F);
  BOOST_TEST((true  == F) == F);
  BOOST_TEST((false == F) == T);
  BOOST_TEST(svn::unknown(true  == X));
  BOOST_TEST(svn::unknown(false == X));
}

BOOST_AUTO_TEST_CASE(tristate_neq_tristate)
{
  BOOST_TEST((T != T) == F);
  BOOST_TEST((T != F) == T);
  BOOST_TEST(svn::unknown(T != X));
  BOOST_TEST((F != T) == T);
  BOOST_TEST((F != F) == F);
  BOOST_TEST(svn::unknown(F != X));
  BOOST_TEST(svn::unknown(X != T));
  BOOST_TEST(svn::unknown(X != F));
  BOOST_TEST(svn::unknown(X != X));
}

BOOST_AUTO_TEST_CASE(tristate_neq_bool)
{
  BOOST_TEST((T !=  true) == F);
  BOOST_TEST((T != false) == T);
  BOOST_TEST((F !=  true) == T);
  BOOST_TEST((F != false) == F);
  BOOST_TEST(svn::unknown(X != true));
  BOOST_TEST(svn::unknown(X != false));
}

BOOST_AUTO_TEST_CASE(bool_neq_tristate)
{
  BOOST_TEST((true  != T) == F);
  BOOST_TEST((false != T) == T);
  BOOST_TEST((true  != F) == T);
  BOOST_TEST((false != F) == F);
  BOOST_TEST(svn::unknown(true  != X));
  BOOST_TEST(svn::unknown(false != X));
}

BOOST_AUTO_TEST_SUITE_END();


#ifdef SVNXX_USE_BOOST
namespace {
constexpr auto boost_T = boost::tribool(true);
constexpr auto boost_F = boost::tribool(false);
constexpr auto boost_X = boost::tribool(boost::indeterminate);
} // anonymous namespace

BOOST_AUTO_TEST_SUITE(tristate_tribool);

BOOST_AUTO_TEST_CASE(conversion_to_tribool)
{
  boost::tribool state;
  BOOST_TEST((state = T) == boost_T);
  BOOST_TEST((state = F) == boost_F);
  BOOST_TEST(boost::indeterminate(X));
}

BOOST_AUTO_TEST_CASE(conversion_from_tribool)
{
  svn::tristate state(false);   // Note: no public default constructor.
  BOOST_TEST((state = boost_T) == T);
  BOOST_TEST((state = boost_F) == F);
  BOOST_TEST(svn::unknown(boost_X));
}

BOOST_AUTO_TEST_CASE(tristate_and_tribool)
{
  BOOST_TEST((T && boost_T) == T);
  BOOST_TEST((T && boost_F) == F);
  BOOST_TEST((F && boost_T) == F);
  BOOST_TEST((F && boost_F) == F);
  BOOST_TEST(svn::unknown(T && boost_X));
  BOOST_TEST(svn::unknown(X && boost_T));
  BOOST_TEST((F && boost_X) == F);
  BOOST_TEST((X && boost_F) == F);
  BOOST_TEST(svn::unknown(X && boost_X));
}

BOOST_AUTO_TEST_CASE(tribool_and_tristate)
{
  BOOST_TEST((boost_T && T) == T);
  BOOST_TEST((boost_T && F) == F);
  BOOST_TEST((boost_F && T) == F);
  BOOST_TEST((boost_F && F) == F);
  BOOST_TEST(svn::unknown(boost_T && X));
  BOOST_TEST(svn::unknown(boost_X && T));
  BOOST_TEST((boost_F && X) == F);
  BOOST_TEST((boost_X && F) == F);
  BOOST_TEST(svn::unknown(boost_X && X));
}

BOOST_AUTO_TEST_CASE(tristate_or_tribool)
{
  BOOST_TEST((T || boost_T) == T);
  BOOST_TEST((T || boost_F) == T);
  BOOST_TEST((F || boost_T) == T);
  BOOST_TEST((F || boost_F) == F);
  BOOST_TEST((T || boost_X) == T);
  BOOST_TEST((X || boost_T) == T);
  BOOST_TEST(svn::unknown(F || boost_X));
  BOOST_TEST(svn::unknown(X || boost_F));
  BOOST_TEST(svn::unknown(X || boost_X));
}

BOOST_AUTO_TEST_CASE(tribool_or_tristate)
{
  BOOST_TEST((boost_T || T) == T);
  BOOST_TEST((boost_T || F) == T);
  BOOST_TEST((boost_F || T) == T);
  BOOST_TEST((boost_F || F) == F);
  BOOST_TEST((boost_T || X) == T);
  BOOST_TEST((boost_X || T) == T);
  BOOST_TEST(svn::unknown(boost_F || X));
  BOOST_TEST(svn::unknown(boost_X || F));
  BOOST_TEST(svn::unknown(boost_X || X));
}

BOOST_AUTO_TEST_CASE(tristate_eq_tribool)
{
  BOOST_TEST((T == boost_T) == T);
  BOOST_TEST((T == boost_F) == F);
  BOOST_TEST(svn::unknown(T == boost_X));
  BOOST_TEST((F == boost_T) == F);
  BOOST_TEST((F == boost_F) == T);
  BOOST_TEST(svn::unknown(F == boost_X));
  BOOST_TEST(svn::unknown(X == boost_T));
  BOOST_TEST(svn::unknown(X == boost_F));
  BOOST_TEST(svn::unknown(X == boost_X));
}

BOOST_AUTO_TEST_CASE(tribool_eq_tristate)
{
  BOOST_TEST((boost_T == T) == T);
  BOOST_TEST((boost_T == F) == F);
  BOOST_TEST(svn::unknown(boost_T == X));
  BOOST_TEST((boost_F == T) == F);
  BOOST_TEST((boost_F == F) == T);
  BOOST_TEST(svn::unknown(boost_F == X));
  BOOST_TEST(svn::unknown(boost_X == T));
  BOOST_TEST(svn::unknown(boost_X == F));
  BOOST_TEST(svn::unknown(boost_X == X));
}

BOOST_AUTO_TEST_SUITE_END();
#endif // SVNXX_USE_BOOST
