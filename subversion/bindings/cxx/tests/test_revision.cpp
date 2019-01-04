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

#include <array>

#include "../src/private/revision_private.hpp"

namespace svn = ::apache::subversion::svnxx;
namespace impl = ::apache::subversion::svnxx::impl;

BOOST_AUTO_TEST_SUITE(revision);

BOOST_AUTO_TEST_CASE(convert_to_kind)
{
  using kind = svn::revision::kind;
  BOOST_TEST((impl::convert(kind::unspecified) == svn_opt_revision_unspecified));
  BOOST_TEST((impl::convert(kind::number)      == svn_opt_revision_number));
  BOOST_TEST((impl::convert(kind::date)        == svn_opt_revision_date));
  BOOST_TEST((impl::convert(kind::committed)   == svn_opt_revision_committed));
  BOOST_TEST((impl::convert(kind::previous)    == svn_opt_revision_previous));
  BOOST_TEST((impl::convert(kind::base)        == svn_opt_revision_base));
  BOOST_TEST((impl::convert(kind::working)     == svn_opt_revision_working));
  BOOST_TEST((impl::convert(kind::head)        == svn_opt_revision_head));
}

BOOST_AUTO_TEST_CASE(convert_from_kind)
{
  using kind = svn::revision::kind;
  BOOST_TEST((impl::convert(svn_opt_revision_unspecified) == kind::unspecified));
  BOOST_TEST((impl::convert(svn_opt_revision_number)      == kind::number));
  BOOST_TEST((impl::convert(svn_opt_revision_date)        == kind::date));
  BOOST_TEST((impl::convert(svn_opt_revision_committed)   == kind::committed));
  BOOST_TEST((impl::convert(svn_opt_revision_previous)    == kind::previous));
  BOOST_TEST((impl::convert(svn_opt_revision_base)        == kind::base));
  BOOST_TEST((impl::convert(svn_opt_revision_working)     == kind::working));
  BOOST_TEST((impl::convert(svn_opt_revision_head)        == kind::head));
}

BOOST_AUTO_TEST_CASE(roundtrip_conversions)
{
  using kind = svn::revision::kind;
  using usec = svn::revision::usec;

  std::array<svn::revision, 11> data = {
    svn::revision(),
    svn::revision(kind::unspecified),
    svn::revision(kind::committed),
    svn::revision(kind::previous),
    svn::revision(kind::base),
    svn::revision(kind::working),
    svn::revision(kind::head),
    svn::revision(svn::revnum::invalid),
    svn::revision(svn::revnum(7)),
    svn::revision(svn::revision::time<usec>()),
    svn::revision(svn::revision::time<usec>(usec{11})),
  };

  for (const auto& r : data)
    BOOST_TEST((impl::convert(impl::convert(r)) == r));
}

BOOST_AUTO_TEST_CASE(preconditions)
{
  using kind = svn::revision::kind;
  BOOST_CHECK_THROW(svn::revision{kind::number}, std::invalid_argument);
  BOOST_CHECK_THROW(svn::revision{kind::date}, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(postconditions_kind)
{
  using kind = svn::revision::kind;
  BOOST_TEST((svn::revision(kind::unspecified).get_kind() == kind::unspecified));
  BOOST_TEST((svn::revision(kind::committed)  .get_kind() == kind::committed));
  BOOST_TEST((svn::revision(kind::previous)   .get_kind() == kind::previous));
  BOOST_TEST((svn::revision(kind::base)       .get_kind() == kind::base));
  BOOST_TEST((svn::revision(kind::working)    .get_kind() == kind::working));
  BOOST_TEST((svn::revision(kind::head)       .get_kind() == kind::head));
}

BOOST_AUTO_TEST_CASE(postconditions_default)
{
  using kind = svn::revision::kind;
  using usec = svn::revision::usec;

  const auto r = svn::revision();
  BOOST_TEST((r.get_kind() == kind::unspecified));
  BOOST_CHECK_THROW(r.get_number(), std::logic_error);
  BOOST_CHECK_THROW(r.get_date<usec>(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(postconditions_number)
{
  using kind = svn::revision::kind;
  using usec = svn::revision::usec;

  const auto r = svn::revision(svn::revnum::invalid);
  BOOST_TEST((r.get_kind() == kind::number));
  BOOST_TEST((r.get_number() == svn::revnum::invalid));
  BOOST_CHECK_THROW(r.get_date<usec>(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(postconditions_date)
{
  using kind = svn::revision::kind;
  using usec = svn::revision::usec;

  const auto r = svn::revision(svn::revision::time<usec>());
  BOOST_TEST((r.get_kind() == kind::date));
  BOOST_TEST((r.get_date<usec>() == svn::revision::time<usec>()));
  BOOST_CHECK_THROW(r.get_number(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(assignment)
{
  using kind = svn::revision::kind;
  using clock = std::chrono::system_clock;
  const auto timestamp = clock::now();

  svn::revision r;
  BOOST_TEST((r.get_kind() == kind::unspecified));

  r = svn::revision(kind::previous);
  BOOST_TEST((r.get_kind() == kind::previous));

  r = svn::revision(svn::revnum(0));
  BOOST_TEST((r.get_kind() == kind::number));
  BOOST_TEST((r.get_number() == svn::revnum(0)));

  r = svn::revision(timestamp);
  BOOST_TEST((r.get_kind() == kind::date));
  BOOST_TEST((r.get_date<clock::duration>() == timestamp));
  BOOST_TEST((r.get_date<svn::revision::usec>() == timestamp));
}

// TODO: Add tests for !=, <, >, <= and >=

BOOST_AUTO_TEST_SUITE_END();
