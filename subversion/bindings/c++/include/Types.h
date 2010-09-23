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
 * @file Types.h
 * @brief Some helpful types
 */

#ifndef TYPES_H
#define TYPES_H

#include "Pool.h"
#include "Revision.h"

#include "svn_types.h"

#include <map>
#include <string>

namespace SVN
{

// Typedefs
typedef std::map<std::string, std::string> PropTable;


// C-struct wrapper classes
class CommitInfo
{
  private:
    Pool m_pool;
    svn_commit_info_t *m_info;

  public:
    inline
    CommitInfo(const svn_commit_info_t *info)
      : m_pool()
    {
      m_info = svn_commit_info_dup(info, m_pool.pool());
    }

    inline
    CommitInfo(const CommitInfo &that)
      : m_pool()
    {
      m_info = svn_commit_info_dup(that.m_info, m_pool.pool());
    }

    inline CommitInfo&
    operator=(const CommitInfo &that)
    {
      m_pool.clear();
      m_info = svn_commit_info_dup(that.m_info, m_pool.pool());
    }

    inline Revision
    getRevision()
    {
      return Revision::getNumberRev(m_info->revision);
    }

    inline std::string
    getAuthor()
    {
      return std::string(m_info->author);
    }

    inline std::string
    getPostCommitErr()
    {
      if (m_info->post_commit_err)
        return std::string(m_info->post_commit_err);
      else
        return std::string();
    }

    inline std::string
    getReposRoot()
    {
      if (m_info->repos_root)
        return std::string(m_info->repos_root);
      else
        return std::string();
    }
};

}


#endif // TYPES_H
