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
 * @file VersionExtended.h
 * @brief Interface for the VersionExtended class
 */

#ifndef JAVAHL_VERSION_EXTENDED_H
#define JAVAHL_VERSION_EXTENDED_H

#include "SVNBase.h"
#include "svn_version.h"

class VersionExtended : public SVNBase
{
public:
  static VersionExtended *getCppObject(jobject jthis);
  static const VersionExtended *getCppObjectFromLinkedLib(jobject jthat);
  static const VersionExtended *getCppObjectFromLoadedLib(jobject jthat);
  static const VersionExtended *getCppObjectFromLinkedLibIterator(jobject jthat);
  static const VersionExtended *getCppObjectFromLoadedLibIterator(jobject jthat);

  virtual ~VersionExtended();
  virtual void dispose(jobject jthis);

  VersionExtended(bool verbose)
    : m_ext_info(svn_version_extended(verbose, pool.getPool()))
    {}

  const char *build_date() const
    { return svn_version_ext_build_date(m_ext_info); }

  const char *build_time() const
    { return svn_version_ext_build_time(m_ext_info); }

  const char *build_host() const
    { return svn_version_ext_build_host(m_ext_info); }

  const char *copyright() const
    { return svn_version_ext_copyright(m_ext_info); }

  const char *runtime_host() const
    { return svn_version_ext_runtime_host(m_ext_info); }

  const char *runtime_osname() const
    { return svn_version_ext_runtime_osname(m_ext_info); }

  const svn_version_ext_linked_lib_t *get_linked_lib(int index) const
    {
      const apr_array_header_t *const libs =
        svn_version_ext_linked_libs(m_ext_info);
      if (!libs || index < 0 || libs->nelts <= index)
        return NULL;
      return &APR_ARRAY_IDX(libs, index, svn_version_ext_linked_lib_t);
    }

  const svn_version_ext_loaded_lib_t *get_loaded_lib(int index) const
    {
      const apr_array_header_t *const libs =
        svn_version_ext_loaded_libs(m_ext_info);
      if (!libs || index < 0 || libs->nelts <= index)
        return NULL;
      return &APR_ARRAY_IDX(libs, index, svn_version_ext_loaded_lib_t);
    }

private:
  const svn_version_extended_t *m_ext_info;
};

#endif /* JAVAHL_VERSION_EXTENDED_H */
