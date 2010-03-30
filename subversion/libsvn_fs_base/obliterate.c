/* obliterate.c : operations related to obliteration
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

#include <string.h>

#include <apr_tables.h>
#include <apr_pools.h>

#include "svn_fs.h"
#include "svn_pools.h"

#include "obliterate.h"
#include "fs.h"
#include "dag.h"
#include "trail.h"
#include "id.h"
#include "bdb/nodes-table.h"
#include "bdb/reps-table.h"
#include "bdb/strings-table.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"



/* Implementation:
 *   - read the existing rep
 *   - modify any members that need to change: just the txn_id
 *   - duplicate any members that need a deep copy
 *   - write out the local rep as a new rep
 *   - return the new rep's key (allocated in trail->pool)
 */
svn_error_t *
svn_fs_base__rep_dup(const char **new_key,
                     const char *new_txn_id,
                     const char *key,
                     trail_t *trail,
                     apr_pool_t *scratch_pool)
{
  representation_t *rep;

  SVN_ERR(svn_fs_bdb__read_rep(&rep, trail->fs, key, trail, scratch_pool));

  rep->txn_id = new_txn_id;

  /* Dup the strings and any recursively used representations */
  if (rep->kind == rep_kind_fulltext)
    {
      SVN_ERR(svn_fs_bdb__string_copy(trail->fs,
                                      &rep->contents.fulltext.string_key,
                                      rep->contents.fulltext.string_key,
                                      trail, scratch_pool));
    }
  else  /* rep_kind_delta */
    {
      apr_array_header_t *chunks = rep->contents.delta.chunks;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      /* Make a deep copy of the rep's delta information. For each "chunk" (aka
       * "window") in the parent rep, duplicate the chunk's delta string and
       * the chunk's rep. */
      for (i = 0; i < chunks->nelts; i++)
      {
        rep_delta_chunk_t *w = APR_ARRAY_IDX(chunks, i, rep_delta_chunk_t *);

        svn_pool_clear(iterpool);

        /* Pool usage: We must allocate these two new keys in a pool that
         * lives at least as long as 'rep'. ..._string_copy allocates in its
         * last arg; ...rep_dup() allocates in its 'trail' arg. */
        SVN_ERR(svn_fs_bdb__string_copy(trail->fs,
                                        &w->string_key, w->string_key,
                                        trail, scratch_pool));
        SVN_ERR(svn_fs_base__rep_dup(&w->rep_key, new_txn_id, w->rep_key,
                                     trail, iterpool));
        /* ### w->offset = calc_offset(w->rep_key); ??? */
      }
      svn_pool_destroy(iterpool);
    }

  SVN_ERR(svn_fs_bdb__write_new_rep(new_key, trail->fs, rep, trail, trail->pool));
  return SVN_NO_ERROR;
}

/*
 * ### Use svn_fs_base__dag_copy() instead?
 * ### Do we need to recurse in order to look for embedded references to
 *     OLD_TXN_ID even if the current node-rev was not created in txn
 *     OLD_TXN_ID?
 */
svn_error_t *
svn_fs_base__node_rev_dup(const svn_fs_id_t **new_id,
                          const svn_fs_id_t *old_id,
                          const char *new_txn_id,
                          const char *old_txn_id,
                          trail_t *trail,
                          apr_pool_t *scratch_pool)
{
  node_revision_t *noderev;

  /* We only want to dup a node-rev if it "belongs to" (was created in) the
   * txn we are replacing. If not, simply return the id. */
  if (strcmp(svn_fs_base__id_txn_id(old_id), old_txn_id) != 0)
    {
      *new_id = svn_fs_base__id_copy(old_id, trail->pool);
      return SVN_NO_ERROR;
    }

  /* Set ID2 to ID except with txn_id NEW_TXN_ID */
  *new_id = svn_fs_base__id_create(svn_fs_base__id_node_id(old_id),
                                   svn_fs_base__id_copy_id(old_id), new_txn_id,
                                   trail->pool);

  /* Dup the representation of its text or entries, and recurse to dup the
   * node-revs of any children. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, trail->fs, old_id, trail,
                                        scratch_pool));
  if (noderev->kind == svn_node_dir)
    {
      dag_node_t *parent_dag_node;
      apr_hash_t *entries;
      apr_hash_index_t *hi;

      /* Store the new parent node-rev so we can use dag functions on it */
      SVN_ERR(svn_fs_bdb__put_node_revision(trail->fs, *new_id, noderev, trail,
                                            scratch_pool));

      SVN_ERR(svn_fs_base__dag_get_node(&parent_dag_node, trail->fs,
                                        *new_id, trail, trail->pool));

      /* Get the children */
      SVN_ERR(svn_fs_base__dag_dir_entries(&entries, parent_dag_node, trail,
                                           scratch_pool));
      /* Caution: 'kind' of each child in 'entries' is 'svn_node_unknown'. */

      /* Dup the children (recursing) */
      if (entries)
        {
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);

          for (hi = apr_hash_first(scratch_pool, entries); hi;
               hi = apr_hash_next(hi))
            {
              const char *child_name = svn__apr_hash_index_key(hi);
              svn_fs_dirent_t *child_entry = svn__apr_hash_index_val(hi);
              const svn_fs_id_t *new_child_id;

              svn_pool_clear(iterpool);

              /* Pool usage: We are modifying stuff in 'parent_dag_node',
               * writing it to the DB immediately. None of these allocations
               * need to persist outside this loop. */

              /* Make a deep copy of the child node-rev. */
              SVN_ERR(svn_fs_base__node_rev_dup(&new_child_id, child_entry->id,
                                                new_txn_id, old_txn_id, trail,
                                                iterpool));

              /* Make the (new) parent node's rep refer to this new child. */
              SVN_ERR(svn_fs_base__dag_set_entry(parent_dag_node, child_name,
                                                 new_child_id, new_txn_id,
                                                 trail, iterpool));
              /* ### Use instead: svn_fs_base__dag_clone_child() ? */
            }
          svn_pool_destroy(iterpool);
        }
    }
  else if (noderev->kind == svn_node_file)
    {
      if (noderev->data_key)
        SVN_ERR(svn_fs_base__rep_dup(&noderev->data_key, new_txn_id,
                                     noderev->data_key, trail, scratch_pool));

      SVN_ERR(svn_fs_bdb__put_node_revision(trail->fs, *new_id, noderev, trail,
                                            scratch_pool));
    }
  else
    SVN_ERR_MALFUNCTION();

  return SVN_NO_ERROR;
}

