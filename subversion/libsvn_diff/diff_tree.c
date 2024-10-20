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

svn_diff_source_t *
svn_diff__source_create(svn_revnum_t revision,
                        apr_pool_t *result_pool)
{
  svn_diff_source_t *src = apr_pcalloc(result_pool, sizeof(*src));

  src->revision = revision;
  return src;
}
