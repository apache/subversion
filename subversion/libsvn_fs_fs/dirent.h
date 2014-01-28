/* dirent.h : utilities for normalization-independent path lookup
 *
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
 */

#ifndef SVN_LIBSVN_FS__DIRENT_H
#define SVN_LIBSVN_FS__DIRENT_H

#include "fs.h"

/* Array item used for directory contents. */
typedef struct svn_fs_fs__dirent_t
{
  /* The wrapped public directory entry.
   *
   * It must always be the first member of this structure, so that a
   * pointer to an svn_fs_fs__dirent_t can be reinterpreted as a
   * pointer to an svn_fs_dirent_t.
   */
  svn_fs_dirent_t dirent;

  /* The directory entry key.
   *
   * When normalized lookup is disabled, or dirent.name is already
   * normalized, this the same pointer value as dirent.name.
   * Otherwise, it is its normalized form.
   */
  const char *key;
} svn_fs_fs__dirent_t;


/* Given a directory entry with a valid DIRENT->dirent.name, set
   DIRENT->key according to specification. DIRENT *must* be allocated
   in RESULT_POOL, and DIRENT->key will be, too.

   Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__set_dirent_key(svn_fs_fs__dirent_t *dirent,
                          svn_boolean_t normalized,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

#endif /* SVN_LIBSVN_FS__DIRENT_H */
