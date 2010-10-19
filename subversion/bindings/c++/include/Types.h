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

template<typename T>
class Nullable : public std::pair<bool, T>
{
  public:
    inline Nullable(bool b, T t)
      : std::pair<bool, T>(b, t)
    { }
};

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
      m_data = data ? new RefCounter<T, DUP>(data) : NULL;
    }

    inline
    CStructWrapper(const CStructWrapper<T, DUP> &that)
    {
      m_data = that.m_data;
      if (m_data)
        m_data->inc_ref();
    }

    inline CStructWrapper<T, DUP>&
    operator= (const CStructWrapper<T, DUP> &that)
    {
      // Self assignment
      if (&that == this)
        return *this;
     
      if (m_data)
        {
          m_data->dec_ref();
          if (m_data->refs() == 0)
            delete m_data;
        }

      m_data = that.m_data;
      if (m_data)
        m_data->inc_ref();

      return *this;
    }

    inline ~CStructWrapper()
    {
      if (!m_data)
        return;

      m_data->dec_ref();
      if (m_data->refs() == 0)
        delete m_data;
    }

    inline const T& operator* () const
    { return *m_data->ptr(); }

    inline const T* operator-> () const
    { return m_data ? m_data->ptr() : NULL; }

    inline operator T const *() const
    { return m_data ? m_data->ptr() : NULL; }

  private:
    RefCounter<T, DUP> *m_data;
};

template<typename T1, typename T2>
inline Nullable<T1>
makeNullable(T2 t)
{
  return Nullable<T1>(t != NULL,
                      t != NULL ? T1(t) : T1());
}

} // namespace Private

#define GET_MEMBER(func_name, type, member)                         \
    inline type                                                     \
    func_name() const                                               \
    {                                                               \
      return type(m_obj->member);                                   \
    }

#define GET_MEMBER_STR(func_name, member)                           \
    inline Nullable<std::string>                                    \
    func_name() const                                               \
    {                                                               \
      return Private::makeNullable<std::string>(m_obj->member);     \
    }

#define GET_MEMBER_PTR(func_name, type, member)                     \
    inline type *                                                   \
    func_name() const                                               \
    {                                                               \
      if (m_obj->member != NULL)                                    \
        return new type(m_obj->member);                             \
      else                                                          \
        return NULL;                                                \
    }

#define GET_MEMBER_NATIVE(func_name, type, member)                  \
    inline type                                                     \
    func_name() const                                               \
    {                                                               \
      return m_obj->member;                                         \
    }

// C-struct wrapper classes
class CommitInfo
{
  public:
    explicit inline
    CommitInfo(const svn_commit_info_t *info)
      : m_obj(info)
    {
    }

    inline operator bool () const
    {
      return m_obj != NULL;
    }

    inline Revision
    getRevision() const
    {
      return Revision::getNumberRev(m_obj->revision);
    }

    GET_MEMBER(getAuthor, std::string, author)
    GET_MEMBER_STR(getPostCommitErr, post_commit_err)
    GET_MEMBER_STR(getReposRoot, repos_root)

  private:
    Private::CStructWrapper<svn_commit_info_t,
                            svn_commit_info_dup> m_obj;
};

class Lock
{
  public:
    explicit inline
    Lock(const svn_lock_t *lock)
      : m_obj(lock)
    {
    }

    inline operator bool () const
    {
      return m_obj != NULL;
    }

    GET_MEMBER(getPath, std::string, path)
    GET_MEMBER(getToken, std::string, token)
    GET_MEMBER(getOwner, std::string, owner)
    GET_MEMBER_STR(getComment, comment)
    GET_MEMBER_NATIVE(isDavComment, bool, is_dav_comment)
    GET_MEMBER_NATIVE(getCreationDate, apr_time_t, creation_date)
    GET_MEMBER_NATIVE(getExpirationDate, apr_time_t, expiration_date)

  private:
    Private::CStructWrapper<svn_lock_t, svn_lock_dup> m_obj;
};

class ClientNotifyInfo
{
  public:
    inline
    ClientNotifyInfo(const svn_wc_notify_t *notify)
      : m_obj(notify)
    {
    }

    inline operator bool () const
    {
      return m_obj != NULL;
    }

    // ### This is only temporary
    inline const svn_wc_notify_t *
    to_c() const
    {
      return &(*m_obj);
    }

  private:
    Private::CStructWrapper<svn_wc_notify_t, svn_wc_dup_notify> m_obj;
};

class Version
{
  public:
    inline
    Version(const svn_version_t *version)
      : m_obj(version)
    {
    }

    inline operator bool () const
    {
      return m_obj != NULL;
    }

    GET_MEMBER(getTag, std::string, tag)

  private:
    static svn_version_t *
    dup(const svn_version_t *version, apr_pool_t *pool);

    Private::CStructWrapper<svn_version_t, dup> m_obj;
};

#undef GET_MEMBER
#undef GET_MEMBER_STR
#undef GET_MEMBER_PTR
#undef GET_MEMBER_NATIVE

}


#endif // TYPES_H
