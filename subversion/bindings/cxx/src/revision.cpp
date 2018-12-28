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

#include <stdexcept>

#include "private/revision_private.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace impl {

svn_opt_revision_kind convert(revision::kind kind)
{
#ifdef SVN_DEBUG
  switch (kind)
    {
    case revision::kind::unspecified:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_unspecified)
        throw std::range_error("convert svn::revision::kind::unspecified");
      break;

    case revision::kind::number:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_number)
        throw std::range_error("convert svn::revision::kind::number");
      break;

    case revision::kind::date:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_date)
        throw std::range_error("convert svn::revision::kind::date");
      break;

    case revision::kind::committed:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_committed)
        throw std::range_error("convert svn::revision::kind::committed");
      break;

    case revision::kind::previous:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_previous)
        throw std::range_error("convert svn::revision::kind::previous");
      break;

    case revision::kind::base:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_base)
        throw std::range_error("convert svn::revision::kind::base");
      break;

    case revision::kind::working:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_working)
        throw std::range_error("convert svn::revision::kind::working");
      break;

    case revision::kind::head:
      if (svn_opt_revision_kind(kind) != svn_opt_revision_head)
        throw std::range_error("convert svn::revision::kind::head");
      break;

    default:
      throw std::range_error("unknown svn::revision::kind");
    }
#endif
  return svn_opt_revision_kind(kind);
}

revision::kind convert(svn_opt_revision_kind kind)
{
#ifdef SVN_DEBUG
  switch (kind)
    {
    case svn_opt_revision_unspecified:
      if (kind != svn_opt_revision_kind(revision::kind::unspecified))
        throw std::range_error("convert svn_opt_revision_unspecified");
      break;

    case svn_opt_revision_number:
      if (kind != svn_opt_revision_kind(revision::kind::number))
        throw std::range_error("convert svn_opt_revision_number");
      break;

    case svn_opt_revision_date:
      if (kind != svn_opt_revision_kind(revision::kind::date))
        throw std::range_error("convert svn_opt_revision_date");
      break;

    case svn_opt_revision_committed:
      if (kind != svn_opt_revision_kind(revision::kind::committed))
        throw std::range_error("convert svn_opt_revision_committed");
      break;

    case svn_opt_revision_previous:
      if (kind != svn_opt_revision_kind(revision::kind::previous))
        throw std::range_error("convert svn_opt_revision_previous");
      break;

    case svn_opt_revision_base:
      if (kind != svn_opt_revision_kind(revision::kind::base))
        throw std::range_error("convert svn_opt_revision_base");
      break;

    case svn_opt_revision_working:
      if (kind != svn_opt_revision_kind(revision::kind::working))
        throw std::range_error("convert svn_opt_revision_working");
      break;

    case svn_opt_revision_head:
      if (kind != svn_opt_revision_kind(revision::kind::head))
        throw std::range_error("convert svn_opt_revision_head");
      break;

    default:
      throw std::range_error("unknown svn_opt_revision_kind");
    }
#endif
  return revision::kind(kind);
}

svn_opt_revision_t convert(const revision& rev)
{
  svn_opt_revision_t result;
  result.kind = convert(rev.get_kind());
  if (result.kind == svn_opt_revision_number)
    result.value.number = svn_revnum_t(rev.get_number());
  else if (result.kind == svn_opt_revision_date)
    {
      // NOTE: We're assuming that the APR and C++ system_clock epochs
      //       are the same. This will be standardized in C++20.
      using usec = revision::usec;
      const auto usecs = (rev.get_date<usec>() - revision::time<usec>());
      result.value.date = usecs.count();
    }
  return result;
}

revision convert(const svn_opt_revision_t& rev)
{
  switch (rev.kind)
    {
    case svn_opt_revision_number:
      return revision(revision::number(rev.value.number));
    case svn_opt_revision_date:
      {
        // NOTE: We're assuming that the APR and C++ system_clock epochs
        //       are the same. This will be standardized in C++20.
        using usec = revision::usec;
        return revision(revision::time<usec>(usec{rev.value.date}));
      }
    default:
      return revision(convert(rev.kind));
    }
}

} // namespace impl
} // namespace svnxx
} // namespace subversion
} // namespace apache
