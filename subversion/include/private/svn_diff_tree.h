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
 * @file svn_wc.h
 * @brief Generic diff handler. Replacing the old svn_wc_diff_callbacks4_t
 * infrastructure
 */

#ifndef SVN_DIFF_PROCESSOR_H
#define SVN_DIFF_PROCESSOR_H

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct svn_diff_source_t
{
  /* Always available */
  svn_revnum_t revision;

  /* Depending on the driver */
  const char *repos_relpath;
  const char *local_abspath;
} svn_diff_source_t;

/**
 * A callback vtable invoked by our diff-editors, as they receive diffs
 * from the server. 'svn diff' and 'svn merge' implement their own versions
 * of this vtable.
 *
 * All callbacks receive the processor and at least a parent baton. Forwarding
 * the processor allows future extensions to call into the old functions without
 * revving the entire API.
 *
 * Users must call svn_diff__tree_processor_create() to allow adding new
 * callbacks later. (E.g. when we decide how to add move support) These
 * extensions can then just call into other callbacks.
 *
 * @since New in 1.8.
 */
typedef struct svn_diff_tree_processor_t
{
  /** The value passed to svn_diff__tree_processor_create() as BATON.
   */
  void *baton; /* To avoid an additional in some places */

  /* Called before a directories children are processed.
   *
   * Set *SKIP_CHILDREN to TRUE, to skip calling callbacks for all
   * children.
   *
   * Set *SKIP to TRUE to skip calling the added, deleted, changed
   * or closed callback for this node only.
   */
  svn_error_t *
  (*dir_opened)(void **new_dir_baton,
                svn_boolean_t *skip,
                svn_boolean_t *skip_children,
                const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                const svn_diff_source_t *copyfrom_source,
                void *parent_dir_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool);

  /* Called after a directory and all its children are added
   */
  svn_error_t *
  (*dir_added)(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool);

  /* Called after all children of this node are reported as deleted.
   *
   * The default implementation calls dir_closed().
   */
  svn_error_t *
  (*dir_deleted)(const char *relpath,
                 const svn_diff_source_t *left_source,
                 /*const*/ apr_hash_t *left_props,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);

  /* Called instead of dir_closed() if the properties on the directory
   *  were modified.
   *
   * The default implementation calls dir_closed().
   */
  svn_error_t *
  (*dir_changed)(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);

  /* Called when a directory is closed without applying changes to
   * the directory itself.
   *
   * When dir_changed or dir_deleted are handled by the default implementation
   * they call dir_closed()
   */
  svn_error_t *
  (*dir_closed)(const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                void *dir_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool);

  /* Called before file_added(), file_deleted(), file_changed() and
     file_closed()
   */
  svn_error_t *
  (*file_opened)(void **new_file_baton,
                 svn_boolean_t *skip,
                 const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 const svn_diff_source_t *copyfrom_source,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool);

  /* Called after file_opened() for newly added and copied files */
  svn_error_t *
  (*file_added)(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool);

  /* Called after file_opened() for deleted or moved away files */
  svn_error_t *
  (*file_deleted)(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  /*const*/ apr_hash_t *left_props,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool);

  /* Called after file_opened() for changed files */
  svn_error_t *
  (*file_changed)(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool);

  /* Called after file_opened() for unmodified files */
  svn_error_t *
  (*file_closed)(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 void *file_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);

  /* Called when encountering a marker for an absent file or directory */
  svn_error_t *
  (*node_absent)(const char *relpath,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);
} svn_diff_tree_processor_t;

/**
 * Create a new svn_diff_tree_processor_t instance with all functions
 * set to a callback doing nothing but copying the parent baton to
 * the new baton.
 *
 * @since New in 1.8.
 */
svn_diff_tree_processor_t *
svn_diff__tree_processor_create(void *baton,
                                apr_pool_t *result_pool);

/**
 * Create a new svn_diff_tree_processor_t instance with all functions setup
 * to call into another svn_diff_tree_processor_t processor, but with all
 * adds and deletes inverted.
 *
 * @since New in 1.8.
 */ /* Used by libsvn clients repository diff */
const svn_diff_tree_processor_t *
svn_diff__tree_processor_reverse_create(const svn_diff_tree_processor_t * processor,
                                        const char *prefix_relpath,
                                        apr_pool_t *result_pool);

/**
 * Create a new svn_diff_tree_processor_t instance with all functions setup
 * to first call into processor for all paths equal to and below prefix_relpath.
 *
 * @since New in 1.8.
 */ /* Used by libsvn clients repository diff */
const svn_diff_tree_processor_t *
svn_diff__tree_processor_filter_create(const svn_diff_tree_processor_t *processor,
                                       const char *prefix_relpath,
                                       apr_pool_t *result_pool);


/**
 * Create a new svn_diff_tree_processor_t instance with all functions setup
 * to first call into processor1 and then processor2.
 *
 * This function is mostly a debug and migration helper.
 *
 * @since New in 1.8.
 */ /* Used by libsvn clients repository diff */
const svn_diff_tree_processor_t *
svn_diff__tree_processor_tee_create(const svn_diff_tree_processor_t *processor1,
                                    const svn_diff_tree_processor_t *processor2,
                                    apr_pool_t *result_pool);


svn_diff_source_t *
svn_diff__source_create(svn_revnum_t revision,
                        apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_DIFF_PROCESSOR_H */

