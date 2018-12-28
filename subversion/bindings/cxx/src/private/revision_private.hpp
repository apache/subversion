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

#include "svnxx/revision.hpp"

#include "svn_types.h"
#include "svn_opt.h"

namespace apache {
namespace subversion {
namespace svnxx {
namespace impl {

/**
 * Convert @a kind to an svn_opt_revision_kind.
 */
svn_opt_revision_kind convert(revision::kind kind);

/**
 * Convert @a kind to an svn::revision::kind.
 */
revision::kind convert(svn_opt_revision_kind kind);

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
