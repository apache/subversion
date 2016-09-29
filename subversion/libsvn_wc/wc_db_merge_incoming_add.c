/*
 * wc_db_merge_incoming_add.c: merge incoming adds during conflict resolution
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

/* This file implements a diff-tree processor which is driven by the conflict
 * resolver in libsvn_client to resolve an "incoming directory add vs. local
 * directory add" tree conflict raised during a merge operation.
 *
 * We use a diff-tree processor because our standard merge operation
 * is not set up for merges where the merge-source anchor is itself an
 * added directory (i.e. does not exist on one side of the diff).
 * The standard merge will only merge additions of children of a path
 * that exists across the entire revision range being merged.
 * But in the add vs. add case, the merge-left side does not yet exist.
 *
 * The diff-tree processor merges an incoming directory tree into an
 * existing directory tree in the working copy.
 */

/* ==================================================================== */


/*** Includes. ***/

#include "svn_types.h"
#include "svn_dirent_uri.h"
#include "svn_wc.h"

#include "private/svn_diff_tree.h"

#include "libsvn_wc/wc_db.h"

struct merge_newly_added_dir_baton {
  const char *target_abspath;
  svn_wc__db_t *db;
};

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_dir_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               apr_hash_t *copyfrom_props,
               apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;

  SVN_DBG(("%s: %s\n", __func__, relpath));
  if (copyfrom_source)
    SVN_DBG(("%s: copyfrom source: %s@%lu\n", __func__,
      copyfrom_source->repos_relpath, copyfrom_source->revision));
  SVN_DBG(("%s: right source: %s@%lu\n", __func__,
    right_source->repos_relpath, right_source->revision));

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_changed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 apr_hash_t *left_props,
                 apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;

  SVN_DBG(("%s: %s\n", __func__, relpath));
  SVN_DBG(("%s: left source: %s@%lu\n", __func__,
    left_source->repos_relpath, left_source->revision));
  SVN_DBG(("%s: right source: %s@%lu\n", __func__,
    right_source->repos_relpath, right_source->revision));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_dir_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 apr_hash_t *left_props,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;

  SVN_DBG(("%s: %s\n", __func__, relpath));
  SVN_DBG(("%s: left source: %s@%lu\n", __func__,
    left_source->repos_relpath, left_source->revision));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                apr_hash_t *copyfrom_props,
                apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;
  const char *local_abspath;
  svn_node_kind_t on_disk_kind;
  svn_node_kind_t db_kind;

  local_abspath = svn_dirent_join(b->target_abspath, relpath, scratch_pool);

  SVN_ERR(svn_io_check_path(local_abspath, &on_disk_kind, scratch_pool));

  if (on_disk_kind != svn_node_none)
    {
      SVN_DBG(("%s: obstructed: %s\n", __func__, local_abspath));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__db_read_kind(&db_kind, b->db, local_abspath,
                               TRUE, FALSE, FALSE, scratch_pool));
  if (db_kind != svn_node_none && db_kind != svn_node_unknown)
    {
      SVN_DBG(("%s: tree conflict: %s\n", __func__, local_abspath));
      return SVN_NO_ERROR;
    }

  SVN_DBG(("%s: %s (%s: %s)\n", __func__, relpath,
      svn_node_kind_to_word(on_disk_kind), local_abspath));
  if (copyfrom_source)
    SVN_DBG(("%s: copyfrom source: %s@%lu\n", __func__,
      copyfrom_source->repos_relpath, copyfrom_source->revision));
  SVN_DBG(("%s: right source: %s@%lu\n", __func__,
    right_source->repos_relpath, right_source->revision));
  SVN_DBG(("%s: right file: %s\n", __func__, right_file));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  apr_hash_t *left_props,
                  apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;

  SVN_DBG(("%s: %s\n", __func__, relpath));
  SVN_DBG(("%s: left source: %s@%lu\n", __func__,
    left_source->repos_relpath, left_source->revision));
  SVN_DBG(("%s: right source: %s@%lu\n", __func__,
    right_source->repos_relpath, right_source->revision));
  SVN_DBG(("%s: right file: %s\n", __func__, right_file));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  apr_hash_t *left_props,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;

  SVN_DBG(("%s: %s\n", __func__, relpath));
  SVN_DBG(("%s: left source: %s@%lu\n", __func__,
    left_source->repos_relpath, left_source->revision));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_get_merge_incoming_add_diff_processor(
  const svn_diff_tree_processor_t **diff_processor,
  void **diff_processor_baton,
  const char *target_abspath,
  const char *prefix_relpath,
  svn_boolean_t reverse_merge,
  svn_wc__db_t *db,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_diff_tree_processor_t *processor;
  struct merge_newly_added_dir_baton *baton;

  baton = apr_pcalloc(result_pool, sizeof(*baton));
  baton->target_abspath = target_abspath;
  baton->db = db;

  processor = svn_diff__tree_processor_create(baton, scratch_pool);
  processor->dir_added = diff_dir_added;
  processor->dir_changed = diff_dir_changed;
  processor->dir_deleted = diff_dir_deleted;
  processor->file_added = diff_file_added;
  processor->file_changed = diff_file_changed;
  processor->file_deleted = diff_file_deleted;

  *diff_processor = processor;
  if (reverse_merge)
    *diff_processor = svn_diff__tree_processor_reverse_create(*diff_processor,
                                                              NULL,
                                                              scratch_pool);

  /* Filter the first path component using a filter processor, until we fixed
     the diff processing to handle this directly */
  *diff_processor = svn_diff__tree_processor_filter_create(
                     *diff_processor, prefix_relpath, scratch_pool);

  *diff_processor_baton = baton;

  return SVN_NO_ERROR;
}
