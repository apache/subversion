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

#include "private/revision_private.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace impl {

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
