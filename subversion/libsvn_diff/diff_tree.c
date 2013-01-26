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

#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_types.h"

#include "private/svn_diff_tree.h"
#include "svn_private_config.h"

typedef struct tree_processor_t
{
  svn_diff_tree_processor_t tp;

  /* void *future_extension */
} tree_processor_t;


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
  tree_processor_t *wrapper;
  wrapper = apr_pcalloc(result_pool, sizeof(*wrapper));

  wrapper->tp.baton        = baton;

  wrapper->tp.dir_opened   = default_dir_opened;
  wrapper->tp.dir_added    = default_dir_added;
  wrapper->tp.dir_deleted  = default_dir_deleted;
  wrapper->tp.dir_changed  = default_dir_changed;
  wrapper->tp.dir_closed   = default_dir_closed;

  wrapper->tp.file_opened   = default_file_opened;
  wrapper->tp.file_added    = default_file_added;
  wrapper->tp.file_deleted  = default_file_deleted;
  wrapper->tp.file_changed  = default_file_changed;
  wrapper->tp.file_closed   = default_file_closed;

  wrapper->tp.node_absent   = default_node_absent;


  return &wrapper->tp;
}

struct reverse_tree_baton_t
{
  svn_diff_tree_processor_t *processor;
  const char *prefix_relpath;
};

static svn_error_t *
reverse_dir_opened(void **new_dir_baton,
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
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_opened(new_dir_baton, skip, skip_children,
                                    relpath,
                                    right_source, left_source,
                                    NULL /* copyfrom */,
                                    parent_dir_baton,
                                    rb->processor,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_added(const char *relpath,
                  const svn_diff_source_t *copyfrom_source,
                  const svn_diff_source_t *right_source,
                  /*const*/ apr_hash_t *copyfrom_props,
                  /*const*/ apr_hash_t *right_props,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_deleted(relpath,
                                     right_source,
                                     right_props,
                                     dir_baton,
                                     rb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_deleted(const char *relpath,
                    const svn_diff_source_t *left_source,
                    /*const*/ apr_hash_t *left_props,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_added(relpath,
                                   NULL,
                                   left_source,
                                   NULL,
                                   left_props,
                                   dir_baton,
                                   rb->processor,
                                   scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    const apr_array_header_t *prop_changes,
                    void *dir_baton,
                    const struct svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;
  apr_array_header_t *reversed_prop_changes = NULL;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  if (prop_changes)
    {
      SVN_ERR_ASSERT(left_props != NULL && right_props != NULL);
      SVN_ERR(svn_prop_diffs(&reversed_prop_changes, left_props, right_props,
                             scratch_pool));
    }

  SVN_ERR(rb->processor->dir_changed(relpath,
                                     right_source,
                                     left_source,
                                     right_props,
                                     left_props,
                                     reversed_prop_changes,
                                     dir_baton,
                                     rb->processor,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_closed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_closed(relpath,
                                    right_source,
                                    left_source,
                                    dir_baton,
                                    rb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_opened(void **new_file_baton,
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
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_opened(new_file_baton,
                                     skip,
                                     relpath,
                                     right_source,
                                     left_source,
                                     NULL /* copy_from */,
                                     dir_baton,
                                     rb->processor,
                                     result_pool,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_added(const char *relpath,
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
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_deleted(relpath,
                                      right_source,
                                      right_file,
                                      right_props,
                                      file_baton,
                                      rb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_deleted(const char *relpath,
                     const svn_diff_source_t *left_source,
                     const char *left_file,
                     /*const*/ apr_hash_t *left_props,
                     void *file_baton,
                     const svn_diff_tree_processor_t *processor,
                     apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_added(relpath,
                                    NULL /* copyfrom src */,
                                    left_source,
                                    NULL /* copyfrom file */,
                                    left_file,
                                    NULL /* copyfrom props */,
                                    left_props,
                                    file_baton,
                                    rb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_changed(const char *relpath,
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
  struct reverse_tree_baton_t *rb = processor->baton;
  apr_array_header_t *reversed_prop_changes = NULL;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  if (prop_changes)
    {
      SVN_ERR_ASSERT(left_props != NULL && right_props != NULL);
      SVN_ERR(svn_prop_diffs(&reversed_prop_changes, left_props, right_props,
                             scratch_pool));
    }

  SVN_ERR(rb->processor->file_changed(relpath,
                                      right_source,
                                      left_source,
                                      right_file,
                                      left_file,
                                      right_props,
                                      left_props,
                                      file_modified,
                                      reversed_prop_changes,
                                      file_baton,
                                      rb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_closed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_closed(relpath,
                                     right_source,
                                     left_source,
                                     file_baton,
                                     rb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_node_absent(const char *relpath,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->node_absent(relpath,
                                    dir_baton,
                                    processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}


const svn_diff_tree_processor_t *
svn_diff__tree_processor_reverse_create(svn_diff_tree_processor_t * processor,
                                        const char *prefix_relpath,
                                        apr_pool_t *result_pool)
{
  struct reverse_tree_baton_t *rb;
  svn_diff_tree_processor_t *reverse;

  rb = apr_pcalloc(result_pool, sizeof(*rb));
  rb->processor = processor;
  if (prefix_relpath)
    rb->prefix_relpath = apr_pstrdup(result_pool, prefix_relpath);

  reverse = svn_diff__tree_processor_create(rb, result_pool);

  reverse->dir_opened   = reverse_dir_opened;
  reverse->dir_added    = reverse_dir_added;
  reverse->dir_deleted  = reverse_dir_deleted;
  reverse->dir_changed  = reverse_dir_changed;
  reverse->dir_closed   = reverse_dir_closed;

  reverse->file_opened   = reverse_file_opened;
  reverse->file_added    = reverse_file_added;
  reverse->file_deleted  = reverse_file_deleted;
  reverse->file_changed  = reverse_file_changed;
  reverse->file_closed   = reverse_file_closed;

  reverse->node_absent   = reverse_node_absent;

  return reverse;
}

svn_diff_source_t *
svn_diff__source_create(svn_revnum_t revision,
                        apr_pool_t *result_pool)
{
  svn_diff_source_t *src = apr_pcalloc(result_pool, sizeof(*src));

  src->revision = revision;
  return src;
}
