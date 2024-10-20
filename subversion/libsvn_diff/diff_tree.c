/*
 * diff_tree.c :  default diff tree processor
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

#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>

#include <assert.h>

#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_types.h"

#include "private/svn_diff_tree.h"
#include "svn_private_config.h"

static svn_error_t *
default_dir_opened(void **new_dir_baton,
                   svn_boolean_t *skip,
                   svn_boolean_t *skip_children,
                   const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   const svn_diff_source_t *copyfrom_source,
                   void *parent_dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  *new_dir_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_added(const char *relpath,
                  const svn_diff_source_t *copyfrom_source,
                  const svn_diff_source_t *right_source,
                  /*const*/ apr_hash_t *copyfrom_props,
                  /*const*/ apr_hash_t *right_props,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->dir_closed(relpath, NULL, right_source,
                                dir_baton, processor,
                                scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_deleted(const char *relpath,
                    const svn_diff_source_t *left_source,
                    /*const*/ apr_hash_t *left_props,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->dir_closed(relpath, left_source, NULL,
                                dir_baton, processor,
                                scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    const apr_array_header_t *prop_changes,
                    void *dir_baton,
                    const struct svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->dir_closed(relpath,
                                left_source, right_source,
                                dir_baton,
                                processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_closed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_opened(void **new_file_baton,
                    svn_boolean_t *skip,
                    const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    const svn_diff_source_t *copyfrom_source,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  *new_file_baton = dir_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_added(const char *relpath,
                   const svn_diff_source_t *copyfrom_source,
                   const svn_diff_source_t *right_source,
                   const char *copyfrom_file,
                   const char *right_file,
                   /*const*/ apr_hash_t *copyfrom_props,
                   /*const*/ apr_hash_t *right_props,
                   void *file_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->file_closed(relpath,
                                 NULL, right_source,
                                 file_baton, processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_deleted(const char *relpath,
                     const svn_diff_source_t *left_source,
                     const char *left_file,
                     /*const*/ apr_hash_t *left_props,
                     void *file_baton,
                     const svn_diff_tree_processor_t *processor,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->file_closed(relpath,
                                 left_source, NULL,
                                 file_baton, processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_changed(const char *relpath,
                     const svn_diff_source_t *left_source,
                     const svn_diff_source_t *right_source,
                     const char *left_file,
                     const char *right_file,
                     /*const*/ apr_hash_t *left_props,
                     /*const*/ apr_hash_t *right_props,
                     svn_boolean_t file_modified,
                     const apr_array_header_t *prop_changes,
                     void *file_baton,
                     const svn_diff_tree_processor_t *processor,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->file_closed(relpath,
                                 left_source, right_source,
                                 file_baton, processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_closed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
default_node_absent(const char *relpath,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

svn_diff_tree_processor_t *
svn_diff__tree_processor_create(void *baton,
                                apr_pool_t *result_pool)
{
  svn_diff_tree_processor_t *tp = apr_pcalloc(result_pool, sizeof(*tp));

  tp->baton        = baton;

  tp->dir_opened   = default_dir_opened;
  tp->dir_added    = default_dir_added;
  tp->dir_deleted  = default_dir_deleted;
  tp->dir_changed  = default_dir_changed;
  tp->dir_closed   = default_dir_closed;

  tp->file_opened  = default_file_opened;
  tp->file_added   = default_file_added;
  tp->file_deleted = default_file_deleted;
  tp->file_changed = default_file_changed;
  tp->file_closed  = default_file_closed;

  tp->node_absent  = default_node_absent;

  return tp;
}

struct copy_as_changed_baton_t
{
  const svn_diff_tree_processor_t *processor;
};

static svn_error_t *
copy_as_changed_dir_opened(void **new_dir_baton,
                           svn_boolean_t *skip,
                           svn_boolean_t *skip_children,
                           const char *relpath,
                           const svn_diff_source_t *left_source,
                           const svn_diff_source_t *right_source,
                           const svn_diff_source_t *copyfrom_source,
                           void *parent_dir_baton,
                           const svn_diff_tree_processor_t *processor,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (!left_source && copyfrom_source)
    {
      assert(right_source != NULL);

      left_source = copyfrom_source;
      copyfrom_source = NULL;
    }

  SVN_ERR(cb->processor->dir_opened(new_dir_baton, skip, skip_children,
                                    relpath,
                                    left_source, right_source,
                                    copyfrom_source,
                                    parent_dir_baton,
                                    cb->processor,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_added(const char *relpath,
                          const svn_diff_source_t *copyfrom_source,
                          const svn_diff_source_t *right_source,
                          /*const*/ apr_hash_t *copyfrom_props,
                          /*const*/ apr_hash_t *right_props,
                          void *dir_baton,
                          const svn_diff_tree_processor_t *processor,
                          apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (copyfrom_source)
    {
      apr_array_header_t *propchanges;
      SVN_ERR(svn_prop_diffs(&propchanges, right_props, copyfrom_props,
                             scratch_pool));
      SVN_ERR(cb->processor->dir_changed(relpath,
                                         copyfrom_source,
                                         right_source,
                                         copyfrom_props,
                                         right_props,
                                         propchanges,
                                         dir_baton,
                                         cb->processor,
                                         scratch_pool));
    }
  else
    {
      SVN_ERR(cb->processor->dir_added(relpath,
                                       copyfrom_source,
                                       right_source,
                                       copyfrom_props,
                                       right_props,
                                       dir_baton,
                                       cb->processor,
                                       scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_deleted(const char *relpath,
                            const svn_diff_source_t *left_source,
                            /*const*/ apr_hash_t *left_props,
                            void *dir_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->dir_deleted(relpath,
                                     left_source,
                                     left_props,
                                     dir_baton,
                                     cb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_changed(const char *relpath,
                            const svn_diff_source_t *left_source,
                            const svn_diff_source_t *right_source,
                            /*const*/ apr_hash_t *left_props,
                            /*const*/ apr_hash_t *right_props,
                            const apr_array_header_t *prop_changes,
                            void *dir_baton,
                            const struct svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->dir_changed(relpath,
                                     left_source,
                                     right_source,
                                     left_props,
                                     right_props,
                                     prop_changes,
                                     dir_baton,
                                     cb->processor,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_closed(const char *relpath,
                           const svn_diff_source_t *left_source,
                           const svn_diff_source_t *right_source,
                           void *dir_baton,
                           const svn_diff_tree_processor_t *processor,
                           apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->dir_closed(relpath,
                                    left_source,
                                    right_source,
                                    dir_baton,
                                    cb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_opened(void **new_file_baton,
                            svn_boolean_t *skip,
                            const char *relpath,
                            const svn_diff_source_t *left_source,
                            const svn_diff_source_t *right_source,
                            const svn_diff_source_t *copyfrom_source,
                            void *dir_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (!left_source && copyfrom_source)
    {
      assert(right_source != NULL);

      left_source = copyfrom_source;
      copyfrom_source = NULL;
    }

  SVN_ERR(cb->processor->file_opened(new_file_baton,
                                     skip,
                                     relpath,
                                     left_source,
                                     right_source,
                                     copyfrom_source,
                                     dir_baton,
                                     cb->processor,
                                     result_pool,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_added(const char *relpath,
                           const svn_diff_source_t *copyfrom_source,
                           const svn_diff_source_t *right_source,
                           const char *copyfrom_file,
                           const char *right_file,
                           /*const*/ apr_hash_t *copyfrom_props,
                           /*const*/ apr_hash_t *right_props,
                           void *file_baton,
                           const svn_diff_tree_processor_t *processor,
                           apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (copyfrom_source)
    {
      apr_array_header_t *propchanges;
      svn_boolean_t same;
      SVN_ERR(svn_prop_diffs(&propchanges, right_props, copyfrom_props,
                             scratch_pool));

      /* "" is sometimes a marker for just modified (E.g. no-textdeltas),
         and it is certainly not a file */
      if (*copyfrom_file && *right_file)
        {
          SVN_ERR(svn_io_files_contents_same_p(&same, copyfrom_file,
                                               right_file, scratch_pool));
        }
      else
        same = FALSE;

      SVN_ERR(cb->processor->file_changed(relpath,
                                          copyfrom_source,
                                          right_source,
                                          copyfrom_file,
                                          right_file,
                                          copyfrom_props,
                                          right_props,
                                          !same,
                                          propchanges,
                                          file_baton,
                                          cb->processor,
                                          scratch_pool));
    }
  else
    {
      SVN_ERR(cb->processor->file_added(relpath,
                                        copyfrom_source,
                                        right_source,
                                        copyfrom_file,
                                        right_file,
                                        copyfrom_props,
                                        right_props,
                                        file_baton,
                                        cb->processor,
                                        scratch_pool));
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_deleted(const char *relpath,
                             const svn_diff_source_t *left_source,
                             const char *left_file,
                             /*const*/ apr_hash_t *left_props,
                             void *file_baton,
                             const svn_diff_tree_processor_t *processor,
                             apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->file_deleted(relpath,
                                      left_source,
                                      left_file,
                                      left_props,
                                      file_baton,
                                      cb->processor,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_changed(const char *relpath,
                             const svn_diff_source_t *left_source,
                             const svn_diff_source_t *right_source,
                             const char *left_file,
                             const char *right_file,
                             /*const*/ apr_hash_t *left_props,
                             /*const*/ apr_hash_t *right_props,
                             svn_boolean_t file_modified,
                             const apr_array_header_t *prop_changes,
                             void *file_baton,
                             const svn_diff_tree_processor_t *processor,
                             apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->file_changed(relpath,
                                      left_source,
                                      right_source,
                                      left_file,
                                      right_file,
                                      left_props,
                                      right_props,
                                      file_modified,
                                      prop_changes,
                                      file_baton,
                                      cb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_closed(const char *relpath,
                            const svn_diff_source_t *left_source,
                            const svn_diff_source_t *right_source,
                            void *file_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->file_closed(relpath,
                                     left_source,
                                     right_source,
                                     file_baton,
                                     cb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_node_absent(const char *relpath,
                            void *dir_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->node_absent(relpath,
                                    dir_baton,
                                    cb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}


const svn_diff_tree_processor_t *
svn_diff__tree_processor_copy_as_changed_create(
                        const svn_diff_tree_processor_t * processor,
                        apr_pool_t *result_pool)
{
  struct copy_as_changed_baton_t *cb;
  svn_diff_tree_processor_t *filter;

  cb = apr_pcalloc(result_pool, sizeof(*cb));
  cb->processor = processor;

  filter = svn_diff__tree_processor_create(cb, result_pool);
  filter->dir_opened   = copy_as_changed_dir_opened;
  filter->dir_added    = copy_as_changed_dir_added;
  filter->dir_deleted  = copy_as_changed_dir_deleted;
  filter->dir_changed  = copy_as_changed_dir_changed;
  filter->dir_closed   = copy_as_changed_dir_closed;

  filter->file_opened   = copy_as_changed_file_opened;
  filter->file_added    = copy_as_changed_file_added;
  filter->file_deleted  = copy_as_changed_file_deleted;
  filter->file_changed  = copy_as_changed_file_changed;
  filter->file_closed   = copy_as_changed_file_closed;

  filter->node_absent   = copy_as_changed_node_absent;

  return filter;
}

svn_diff_source_t *
svn_diff__source_create(svn_revnum_t revision,
                        apr_pool_t *result_pool)
{
  svn_diff_source_t *src = apr_pcalloc(result_pool, sizeof(*src));

  src->revision = revision;
  return src;
}
