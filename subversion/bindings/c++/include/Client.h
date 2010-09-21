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
 * @file Client.h
 * @brief Interface of the class Client
 */

#ifndef CLIENT_H
#define CLIENT_H

#include "Types.h"
#include "Common.h"
#include "Callback.h"
#include "Version.h"
#include "Revision.h"
#include "Pool.h"

#include "svn_client.h"

#include <ostream>
#include <string>
#include <vector>

namespace SVN
{

  class Client
  {
    private:
      Pool m_pool;
      svn_client_ctx_t *m_ctx;

    public:
      /** The constructor. */
      Client();

      /** The destructor needs to be public. */
      virtual ~Client();

      Version getVersion();

      void cat(std::ostream &stream, const std::string &path_or_url);
      void cat(std::ostream &stream, const std::string &path_or_url,
               const Revision &peg_revision, const Revision &revision);

      Revision checkout(const std::string &url, const std::string path);
      Revision checkout(const std::string &url, const std::string path,
                        const Revision &peg_revisio, const Revision &revision,
                        svn_depth_t depth, bool ignore_externals,
                        bool allow_unver_obstructions);

      void commit(const std::vector<std::string> &targets,
                  Callback::Commit &callback);
      void commit(const std::vector<std::string> &targets,
                  svn_depth_t depth, bool keep_locks, bool keep_changelists,
                  const std::vector<std::string> &changelists,
                  const PropTable &revprop_table,
                  Callback::Commit &callback);
  };
}

#endif // CLIENT_H
