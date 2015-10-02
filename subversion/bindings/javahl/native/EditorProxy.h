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
 * @file EditorProxy.h
 * @brief Interface of all editor proxy classes
 */

#ifndef JAVAHL_EDITOR_PROXY_H
#define JAVAHL_EDITOR_PROXY_H

#include <memory>

#include "svn_delta.h"
#include "private/svn_editor.h"
#include "private/svn_delta_private.h"

/**
 * These callbacks are needed by the delta-to-Ev2 shims.
 */
struct EditorProxyCallbacks
{
  svn_delta__unlock_func_t m_unlock_func;
  svn_delta_fetch_props_func_t m_fetch_props_func;
  svn_delta_fetch_base_func_t m_fetch_base_func;
  struct svn_delta__extra_baton m_extra_baton;
  void* m_baton;
};

/**
 * This is a proxy object that translates Ev2 operations (possibly
 * implemented through shims) into calls to a Java editor
 * implementation.
 */
class EditorProxy
{
public:
  typedef std::auto_ptr<EditorProxy> UniquePtr;

  EditorProxy(jobject jeditor, apr_pool_t* edit_pool,
              const char* repos_root_url, const char* base_relpath,
              svn_cancel_func_t cancel_func, void* cancel_baton,
              const EditorProxyCallbacks& callbacks);
  ~EditorProxy();

  const svn_delta_editor_t* delta_editor() const
    {
      return m_delta_editor;
    }

  void* delta_baton() const
    {
      return m_delta_baton;
    }

private:
  EditorProxy(const EditorProxy&); // noncopyable

  static svn_error_t* cb_add_directory(void *baton,
                                       const char *relpath,
                                       const apr_array_header_t *children,
                                       apr_hash_t *props,
                                       svn_revnum_t replaces_rev,
                                       apr_pool_t *scratch_pool);
  static svn_error_t* cb_add_file(void *baton,
                                  const char *relpath,
                                  const svn_checksum_t *checksum,
                                  svn_stream_t *contents,
                                  apr_hash_t *props,
                                  svn_revnum_t replaces_rev,
                                  apr_pool_t *scratch_pool);
  static svn_error_t* cb_add_symlink(void *baton,
                                     const char *relpath,
                                     const char *target,
                                     apr_hash_t *props,
                                     svn_revnum_t replaces_rev,
                                     apr_pool_t *scratch_pool);
  static svn_error_t* cb_add_absent(void *baton,
                                    const char *relpath,
                                    svn_node_kind_t kind,
                                    svn_revnum_t replaces_rev,
                                    apr_pool_t *scratch_pool);
  static svn_error_t* cb_alter_directory(void *baton,
                                         const char *relpath,
                                         svn_revnum_t revision,
                                         const apr_array_header_t *children,
                                         apr_hash_t *props,
                                         apr_pool_t *scratch_pool);
  static svn_error_t* cb_alter_file(void *baton,
                                    const char *relpath,
                                    svn_revnum_t revision,
                                    const svn_checksum_t *checksum,
                                    svn_stream_t *contents,
                                    apr_hash_t *props,
                                    apr_pool_t *scratch_pool);
  static svn_error_t* cb_alter_symlink(void *baton,
                                       const char *relpath,
                                       svn_revnum_t revision,
                                       const char *target,
                                       apr_hash_t *props,
                                       apr_pool_t *scratch_pool);
  static svn_error_t* cb_delete(void *baton,
                                const char *relpath,
                                svn_revnum_t revision,
                                apr_pool_t *scratch_pool);
  static svn_error_t* cb_copy(void *baton,
                              const char *src_relpath,
                              svn_revnum_t src_revision,
                              const char *dst_relpath,
                              svn_revnum_t replaces_rev,
                              apr_pool_t *scratch_pool);
  static svn_error_t* cb_move(void *baton,
                              const char *src_relpath,
                              svn_revnum_t src_revision,
                              const char *dst_relpath,
                              svn_revnum_t replaces_rev,
                              apr_pool_t *scratch_pool);
  static svn_error_t* cb_complete(void *baton,
                                  apr_pool_t *scratch_pool);
  static svn_error_t* cb_abort(void *baton,
                               apr_pool_t *scratch_pool);

private:
  bool m_valid;
  jobject m_jeditor;            ///< Reference to Java editor implementation
  apr_pool_t* m_edit_pool;

  const char* m_repos_root_url; ///< The root of the repository
  const char* m_base_relpath;   ///< The root of the session within the repo
  bool m_found_paths;           ///< Returned paths are absolute

  svn_editor_t* m_editor;
  const svn_delta_editor_t* m_delta_editor;
  void* m_delta_baton;
  EditorProxyCallbacks m_proxy_callbacks;
};


#endif // JAVAHL_EDITOR_PROXY_H
