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

#ifndef SVNXX_PRIVATE_REVISION_HPP
#define SVNXX_PRIVATE_REVISION_HPP

#include <stdexcept>

#include "svnxx/revision.hpp"

#include "svn_opt.h"

namespace apache {
namespace subversion {
namespace svnxx {
namespace impl {

/**
 * Convert @a kind to an svn_opt_revision_kind.
 */
inline constexpr svn_opt_revision_kind convert(revision::kind kind)
{
#ifndef SVN_DEBUG
  return svn_opt_revision_kind(kind);
#else
  // switch in constexpr is allowed in C++14 but not in C++11,
  // so we have to use a series of ternary operators.
  return (kind == revision::kind::unspecified
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_unspecified
             ? throw std::range_error("convert(svn::revision::kind::unspecified)")
             : svn_opt_revision_kind(kind))
          :
          kind == revision::kind::number
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_number
             ? throw std::range_error("convert(svn::revision::kind::number)")
             : svn_opt_revision_kind(kind))
          :
          kind == revision::kind::date
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_date
             ? throw std::range_error("convert(svn::revision::kind::date)")
             : svn_opt_revision_kind(kind))
          :
          kind == revision::kind::committed
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_committed
             ? throw std::range_error("convert(svn::revision::kind::committed)")
             : svn_opt_revision_kind(kind))
          :
          kind == revision::kind::previous
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_previous
             ? throw std::range_error("convert(svn::revision::kind::previous)")
             : svn_opt_revision_kind(kind))
          :
          kind == revision::kind::base
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_base
             ? throw std::range_error("convert(svn::revision::kind::base)")
             : svn_opt_revision_kind(kind))
          :
          kind == revision::kind::working
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_working
             ? throw std::range_error("convert(svn::revision::kind::working)")
             : svn_opt_revision_kind(kind))
          :
          kind == revision::kind::head
          ? (svn_opt_revision_kind(kind) != svn_opt_revision_head
             ? throw std::range_error("convert(svn::revision::kind::head)")
             : svn_opt_revision_kind(kind))
          :
          throw std::range_error("convert: unknown svn::revision::kind"));
#endif
}

/**
 * Convert @a kind to an svn::revision::kind.
 */
inline constexpr revision::kind convert(svn_opt_revision_kind kind)
{
#ifndef SVN_DEBUG
  return revision::kind(kind);
#else
  // switch in constexpr is allowed in C++14 but not in C++11,
  // so we have to use a series of ternary operators.
  return (kind ==svn_opt_revision_unspecified
          ? (kind != svn_opt_revision_kind(revision::kind::unspecified)
             ? throw std::range_error("convert(svn_opt_revision_unspecified)")
             : revision::kind(kind))
          :
          kind == svn_opt_revision_number
          ? (kind != svn_opt_revision_kind(revision::kind::number)
             ? throw std::range_error("convert(svn_opt_revision_number)")
             : revision::kind(kind))
          :
          kind == svn_opt_revision_date
          ? (kind != svn_opt_revision_kind(revision::kind::date)
             ? throw std::range_error("convert(svn_opt_revision_date)")
             : revision::kind(kind))
          :
          kind == svn_opt_revision_committed
          ? (kind != svn_opt_revision_kind(revision::kind::committed)
             ? throw std::range_error("convert(svn_opt_revision_committed)")
             : revision::kind(kind))
          :
          kind == svn_opt_revision_previous
          ? (kind != svn_opt_revision_kind(revision::kind::previous)
             ? throw std::range_error("convert(svn_opt_revision_previous)")
             : revision::kind(kind))
          :
          kind == svn_opt_revision_base
          ? (kind != svn_opt_revision_kind(revision::kind::base)
             ? throw std::range_error("convert(svn_opt_revision_base)")
             : revision::kind(kind))
          :
          kind == svn_opt_revision_working
          ? (kind != svn_opt_revision_kind(revision::kind::working)
             ? throw std::range_error("convert(svn_opt_revision_working)")
             : revision::kind(kind))
          :
          kind == svn_opt_revision_head
          ? (kind != svn_opt_revision_kind(revision::kind::head)
             ? throw std::range_error("convert(svn_opt_revision_head)")
             : revision::kind(kind))
          :
          throw std::range_error("convert: unknown svn_opt_revision_kind"));
#endif
}

/**
 * Convert @a rev to an svn_opt_revision_t.
 */
svn_opt_revision_t convert(const revision& rev);

/**
 * Convert @a rev to an svn::revision.
 */
revision convert(const svn_opt_revision_t& rev);

} // namespace impl
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_REVISION_HPP
