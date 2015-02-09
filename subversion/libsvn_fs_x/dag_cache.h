/* dag_cache.h : Interface to the DAG walker and node cache.
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

#ifndef SVN_LIBSVN_FS_X_DAG_CACHE_H
#define SVN_LIBSVN_FS_X_DAG_CACHE_H

#include "dag.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* In RESULT_POOL, create an instance of a DAG node cache. */
svn_fs_x__dag_cache_t*
svn_fs_x__create_dag_cache(apr_pool_t *result_pool);

/* Invalidate cache entries for PATH and any of its children.  This
   should *only* be called on a transaction root! */
svn_error_t *
svn_fs_x__invalidate_dag_cache(svn_fs_root_t *root,
                               const char *path,
                               apr_pool_t *scratch_pool);

/* Flag type used in svn_fs_x__dag_path_t to determine where the
   respective node got its copy ID from. */
typedef enum svn_fs_x__copy_id_inherit_t
{
  svn_fs_x__copy_id_inherit_unknown = 0,
  svn_fs_x__copy_id_inherit_self,
  svn_fs_x__copy_id_inherit_parent,
  svn_fs_x__copy_id_inherit_new

} svn_fs_x__copy_id_inherit_t;

/* Flags for svn_fs_x__get_dag_path.  */
typedef enum svn_fs_x__dag_path_flags_t {

  /* The last component of the PATH need not exist.  (All parent
     directories must exist, as usual.)  If the last component doesn't
     exist, simply leave the `node' member of the bottom parent_path
     component zero.  */
  svn_fs_x__dag_path_last_optional = 1,

  /* When this flag is set, don't bother to lookup the DAG node in
     our caches because we already tried this.  Ignoring this flag
     has no functional impact.  */
  svn_fs_x__dag_path_uncached = 2,

  /* The caller does not care about the parent node chain but only
     the final DAG node. */
  svn_fs_x__dag_path_node_only = 4,

  /* The caller wants a NULL path object instead of an error if the
     path cannot be found. */
  svn_fs_x__dag_path_allow_null = 8
} svn_fs_x__dag_path_flags_t;


/* A linked list representing the path from a node up to a root
   directory.  We use this for cloning, and for operations that need
   to deal with both a node and its parent directory.  For example, a
   `delete' operation needs to know that the node actually exists, but
   also needs to change the parent directory.  */
typedef struct svn_fs_x__dag_path_t
{
  /* A node along the path.  This could be the final node, one of its
     parents, or the root.  Every parent path ends with an element for
     the root directory.  */
  dag_node_t *node;

  /* The name NODE has in its parent directory.  This is zero for the
     root directory, which (obviously) has no name in its parent.  */
  char *entry;

  /* The parent of NODE, or zero if NODE is the root directory.  */
  struct svn_fs_x__dag_path_t *parent;

  /* The copy ID inheritance style. */
  svn_fs_x__copy_id_inherit_t copy_inherit;

  /* If copy ID inheritance style is copy_id_inherit_new, this is the
     path which should be implicitly copied; otherwise, this is NULL. */
  const char *copy_src_path;

} svn_fs_x__dag_path_t;

/* Open the node identified by PATH in ROOT, allocating in POOL.  Set
   *DAG_PATH_P to a path from the node up to ROOT.  The resulting
   **DAG_PATH_P value is guaranteed to contain at least one element,
   for the root directory.  PATH must be in canonical form.

   If resulting *PARENT_PATH_P will eventually be made mutable and
   modified, or if copy ID inheritance information is otherwise needed,
   IS_TXN_PATH must be set.  If IS_TXN_PATH is FALSE, no copy ID
   inheritance information will be calculated for the *PARENT_PATH_P chain.

   If FLAGS & open_path_last_optional is zero, return the error
   SVN_ERR_FS_NOT_FOUND if the node PATH refers to does not exist.  If
   non-zero, require all the parent directories to exist as normal,
   but if the final path component doesn't exist, simply return a path
   whose bottom `node' member is zero.  This option is useful for
   callers that create new nodes --- we find the parent directory for
   them, and tell them whether the entry exists already.

   The remaining bits in FLAGS are hints that allow this function
   to take shortcuts based on knowledge that the caller provides,
   such as the caller is not actually being interested in PARENT_PATH_P,
   but only in (*PARENT_PATH_P)->NODE.

   NOTE: Public interfaces which only *read* from the filesystem
   should not call this function directly, but should instead use
   svn_fs_x__get_dag_node().
*/
svn_error_t *
svn_fs_x__get_dag_path(svn_fs_x__dag_path_t **dag_path_p,
                       svn_fs_root_t *root,
                       const char *path,
                       int flags,
                       svn_boolean_t is_txn_path,
                       apr_pool_t *pool);

/* Open the node identified by PATH in ROOT.  Set DAG_NODE_P to the
   node we find, allocated in POOL.  Return the error
   SVN_ERR_FS_NOT_FOUND if this node doesn't exist.
 */
static svn_error_t *
svn_fs_x__get_dag_node(dag_node_t **dag_node_p,
                       svn_fs_root_t *root,
                       const char *path,
                       apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_DAG_CACHE_H */
