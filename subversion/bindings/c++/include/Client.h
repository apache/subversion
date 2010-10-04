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
#include "Revision.h"
#include "Pool.h"

#include "svn_client.h"

#include <ostream>
#include <string>
#include <vector>
#include <set>

namespace SVN
{

  class Client
  {
    public:
      /** The constructor. */
      Client();

      /** The destructor needs to be public. */
      virtual ~Client();

      /** The real work of the destructor.  Useful for "placement delete". */
      void dispose();

      inline void subscribeNotifier(Callback::ClientNotifier *notifier)
      {
        m_notifiers.insert(notifier);
      }
      inline void unsubscribeNotifier(Callback::ClientNotifier *notifier)
      {
        m_notifiers.erase(m_notifiers.find(notifier));
      }

      Version getVersion();

      inline void cat(std::ostream &stream, const std::string &path_or_url)
      {
        cat(stream, path_or_url, Revision::HEAD, Revision::HEAD);
      }
      void cat(std::ostream &stream, const std::string &path_or_url,
               const Revision &peg_revision, const Revision &revision);

      inline Revision checkout(const std::string &url, const std::string path)
      {
        return checkout(url, path, Revision::HEAD, Revision::HEAD,
                        svn_depth_infinity, false, false);
      }
      Revision checkout(const std::string &url, const std::string path,
                        const Revision &peg_revisio, const Revision &revision,
                        svn_depth_t depth, bool ignore_externals,
                        bool allow_unver_obstructions);

      inline void commit(const std::vector<std::string> &targets,
                         Callback::Commit &callback)
      {
        commit(targets, svn_depth_infinity, false, false,
               std::vector<std::string>(), PropTable(), callback);
      }
      void commit(const std::vector<std::string> &targets,
                  svn_depth_t depth, bool keep_locks, bool keep_changelists,
                  const std::vector<std::string> &changelists,
                  const PropTable &revprop_table,
                  Callback::Commit &callback);

    private:
      Pool m_pool;
      svn_client_ctx_t *m_ctx;
      
      std::set<Callback::ClientNotifier *> m_notifiers;

      static void notify_func2(void *baton,
                               const svn_wc_notify_t *notify,
                               apr_pool_t *pool);
      void notify(const ClientNotifyInfo &info);
  };
}

#endif // CLIENT_H
