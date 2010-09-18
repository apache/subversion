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
 *
 */

#include "Client.h"
#include "Pool.h"
#include "Utility.h"

#include "Common.h"

namespace SVN {

Client::Client()
  : m_pool()
{
  svn_error_clear(svn_client_create_context(&m_ctx, m_pool.pool()));
}

Client::~Client()
{
}

Version
Client::getVersion()
{
  return Version(svn_client_version());
}

void
Client::cat(std::ostream &stream, const std::string &path_or_url)
{
  cat(stream, path_or_url, Revision::HEAD, Revision::HEAD);
}

void
Client::cat(std::ostream &stream, const std::string &path_or_url,
    const Revision &peg_revision, const Revision &revision)
{
  Pool pool;
  svn_stream_t *out = Private::Utility::ostream_wrapper(stream, pool);

  SVN_CPP_ERR(svn_client_cat2(out, path_or_url.c_str(),
                              peg_revision.revision(),
                              revision.revision(), m_ctx, pool.pool()));
}

Revision
Client::checkout(const std::string &url, const std::string path)
{
  return checkout(url, path, Revision::HEAD, Revision::HEAD,
                  svn_depth_infinity, false, false);
}

Revision
Client::checkout(const std::string &url, const std::string path,
                 const Revision &peg_revision, const Revision &revision,
                 svn_depth_t depth, bool ignore_externals,
                 bool allow_unver_obstructions)
{
  Pool pool;
  svn_revnum_t result_rev;

  SVN_CPP_ERR(svn_client_checkout3(&result_rev, url.c_str(), path.c_str(),
                                   peg_revision.revision(),
                                   revision.revision(), depth,
                                   ignore_externals, allow_unver_obstructions,
                                   m_ctx, pool.pool()));
  return Revision::getNumberRev(result_rev);
}

}
