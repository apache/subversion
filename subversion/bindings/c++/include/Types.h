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

#include "svn_wc.h"
#include "svn_types.h"

#include <stdlib.h>
#include <map>
#include <string>
#include <iostream>

namespace SVN
{

// Typedefs
typedef std::map<std::string, std::string> PropTable;

namespace Private
{

// Magic
template<typename T, T *DUP(const T *, apr_pool_t *pool)>
class RefCounter
{
  public:
    inline
    RefCounter(const T *in)
      : m_pool(),
        count(1)
    {
      p = DUP(in, m_pool.pool());
    }

    inline void inc_ref() { ++count; }
    inline void dec_ref() { --count; }
    inline T const *ptr() { return p; }
    inline size_t refs() { return count; }

    // The default destructor will destroy the Pool, and deallocate our object

  private:
    Pool m_pool;
    size_t count;
    T *p;
};

template<typename T, T *DUP(const T *, apr_pool_t *pool)>
class CStructWrapper
{
  public:
    inline
    CStructWrapper(const T *data)
    {
      m_data = new RefCounter<T, DUP>(data);
    }

    inline
    CStructWrapper(const CStructWrapper<T, DUP> &that)
    {
      m_data = that.m_data;
      m_data->inc_ref();
    }

    inline CStructWrapper<T, DUP>&
    operator= (const CStructWrapper<T, DUP> &that)
    {
      // Self assignment
      if (&that == this)
        return *this;
      
      m_data->dec_ref();
      if (m_data->refs() == 0)
        delete m_data;
      m_data = that.m_data;
      m_data->inc_ref();

      return *this;
    }

    inline ~CStructWrapper()
    {
      m_data->dec_ref();
      if (m_data->refs() == 0)
        delete m_data;
    }

    inline const T& operator* () const { return *m_data->ptr(); }
    inline const T* operator-> () const { return m_data->ptr(); }
    inline operator T const *() const { return m_data->ptr(); }

  private:
    RefCounter<T, DUP> *m_data;
};

} // namespace Private

// C-struct wrapper classes
class CommitInfo
{
  public:
    explicit inline
    CommitInfo(const svn_commit_info_t *info)
      : m_info(info)
    {
    }

    inline operator bool () const
    {
      return m_info != NULL;
    }

    inline Revision
    getRevision() const
    {
      return Revision::getNumberRev(m_info->revision);
    }

    inline std::string
    getAuthor() const
    {
      return std::string(m_info->author);
    }

    inline std::string
    getPostCommitErr() const
    {
      if (m_info->post_commit_err)
        return std::string(m_info->post_commit_err);
      else
        return std::string();
    }

    inline std::string
    getReposRoot() const
    {
      if (m_info->repos_root)
        return std::string(m_info->repos_root);
      else
        return std::string();
    }

  private:
    Private::CStructWrapper<svn_commit_info_t,
                            svn_commit_info_dup> m_info;
};

class Lock
{
  public:
    explicit inline
    Lock(const svn_lock_t *lock)
      : m_lock(lock)
    {
    }

    inline operator bool () const
    {
      return m_lock != NULL;
    }

    inline std::string
    getPath() const
    {
      return std::string(m_lock->path);
    }

    inline std::string
    getToken() const
    {
      return std::string(m_lock->token);
    }

    inline std::string
    getOwner() const
    {
      return std::string(m_lock->owner);
    }

    inline std::string
    getComment() const
    {
      return std::string(m_lock->comment);
    }

    inline bool
    isDavComment() const
    {
      return m_lock->is_dav_comment;
    }

    inline apr_time_t
    getCreationDate() const
    {
      return m_lock->creation_date;
    }

    inline apr_time_t
    getExpirationDate() const
    {
      return m_lock->expiration_date;
    }

  private:
    Private::CStructWrapper<svn_lock_t, svn_lock_dup> m_lock;
};

class ClientNotifyInfo
{
  public:
    inline
    ClientNotifyInfo(const svn_wc_notify_t *notify)
      : m_notify(notify)
    {
    }

    inline operator bool () const
    {
      return m_notify != NULL;
    }

    // ### This is only temporary
    inline const svn_wc_notify_t *
    to_c() const
    {
      return &(*m_notify);
    }

  private:
    Private::CStructWrapper<svn_wc_notify_t, svn_wc_dup_notify> m_notify;
};

class Version
{
  public:
    inline
    Version(const svn_version_t *version)
      : m_version(version)
    {
    }

    inline operator bool () const
    {
      return m_version != NULL;
    }

    inline std::string
    getTag()
    {
      return std::string(m_version->tag);
    }

  private:
    static svn_version_t *
    dup(const svn_version_t *version, apr_pool_t *pool);

    Private::CStructWrapper<svn_version_t, dup> m_version;
};

}


#endif // TYPES_H
