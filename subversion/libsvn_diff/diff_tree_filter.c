/*
 * diff_tree_filter.c :  filter diff tree processor
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
#include "svn_types.h"

#include "private/svn_diff_tree.h"
#include "svn_private_config.h"

struct filter_tree_baton_t
{
  const svn_diff_tree_processor_t *processor;
  const char *prefix_relpath;
};

static svn_error_t *
filter_dir_opened(void **new_dir_baton,
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
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);

  if (! relpath)
    {
      /* Skip work for this, but NOT for DESCENDANTS */
      *skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(fb->processor->dir_opened(new_dir_baton, skip, skip_children,
                                    relpath,
                                    left_source, right_source,
                                    copyfrom_source,
                                    parent_dir_baton,
                                    fb->processor,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_added(const char *relpath,
                 const svn_diff_source_t *copyfrom_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *copyfrom_props,
                 /*const*/ apr_hash_t *right_props,
                 void *dir_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_added(relpath,
                                   copyfrom_source,
                                   right_source,
                                   copyfrom_props,
                                   right_props,
                                   dir_baton,
                                   fb->processor,
                                   scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_deleted(const char *relpath,
                   const svn_diff_source_t *left_source,
                   /*const*/ apr_hash_t *left_props,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_deleted(relpath,
                                     left_source,
                                     left_props,
                                     dir_baton,
                                     fb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_changed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   /*const*/ apr_hash_t *left_props,
                   /*const*/ apr_hash_t *right_props,
                   const apr_array_header_t *prop_changes,
                   void *dir_baton,
                   const struct svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_changed(relpath,
                                     left_source,
                                     right_source,
                                     left_props,
                                     right_props,
                                     prop_changes,
                                     dir_baton,
                                     fb->processor,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_closed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_closed(relpath,
                                    left_source,
                                    right_source,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_opened(void **new_file_baton,
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
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);

  if (! relpath)
    {
      *skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(fb->processor->file_opened(new_file_baton,
                                     skip,
                                     relpath,
                                     left_source,
                                     right_source,
                                     copyfrom_source,
                                     dir_baton,
                                     fb->processor,
                                     result_pool,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_added(const char *relpath,
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
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_added(relpath,
                                    copyfrom_source,
                                    right_source,
                                    copyfrom_file,
                                    right_file,
                                    copyfrom_props,
                                    right_props,
                                    file_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_deleted(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const char *left_file,
                    /*const*/ apr_hash_t *left_props,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_deleted(relpath,
                                      left_source,
                                      left_file,
                                      left_props,
                                      file_baton,
                                      fb->processor,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_changed(const char *relpath,
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
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_changed(relpath,
                                      left_source,
                                      right_source,
                                      left_file,
                                      right_file,
                                      left_props,
                                      right_props,
                                      file_modified,
                                      prop_changes,
                                      file_baton,
                                      fb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_closed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   void *file_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_closed(relpath,
                                     left_source,
                                     right_source,
                                     file_baton,
                                     fb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_node_absent(const char *relpath,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->node_absent(relpath,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}


const svn_diff_tree_processor_t *
svn_diff__tree_processor_filter_create(const svn_diff_tree_processor_t * processor,
                                        const char *prefix_relpath,
                                        apr_pool_t *result_pool)
{
  struct filter_tree_baton_t *fb;
  svn_diff_tree_processor_t *filter;

  fb = apr_pcalloc(result_pool, sizeof(*fb));
  fb->processor = processor;
  if (prefix_relpath)
    fb->prefix_relpath = apr_pstrdup(result_pool, prefix_relpath);

  filter = svn_diff__tree_processor_create(fb, result_pool);

  filter->dir_opened   = filter_dir_opened;
  filter->dir_added    = filter_dir_added;
  filter->dir_deleted  = filter_dir_deleted;
  filter->dir_changed  = filter_dir_changed;
  filter->dir_closed   = filter_dir_closed;

  filter->file_opened   = filter_file_opened;
  filter->file_added    = filter_file_added;
  filter->file_deleted  = filter_file_deleted;
  filter->file_changed  = filter_file_changed;
  filter->file_closed   = filter_file_closed;

  filter->node_absent   = filter_node_absent;

  return filter;
}
