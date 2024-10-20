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

#include "svn_error.h"
#include "svn_types.h"

#include "private/svn_diff_tree.h"

/* Processor baton for the tee tree processor */
struct tee_baton_t
{
  const svn_diff_tree_processor_t *p1;
  const svn_diff_tree_processor_t *p2;
};

/* Wrapper baton for file and directory batons in the tee processor */
struct tee_node_baton_t
{
  void *baton1;
  void *baton2;
};

static svn_error_t *
tee_dir_opened(void **new_dir_baton,
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
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *pb = parent_dir_baton;
  struct tee_node_baton_t *nb = apr_pcalloc(result_pool, sizeof(*nb));

  SVN_ERR(tb->p1->dir_opened(&(nb->baton1),
                             skip,
                             skip_children,
                             relpath,
                             left_source,
                             right_source,
                             copyfrom_source,
                             pb ? pb->baton1 : NULL,
                             tb->p1,
                             result_pool,
                             scratch_pool));

  SVN_ERR(tb->p2->dir_opened(&(nb->baton2),
                             skip,
                             skip_children,
                             relpath,
                             left_source,
                             right_source,
                             copyfrom_source,
                             pb ? pb->baton2 : NULL,
                             tb->p2,
                             result_pool,
                             scratch_pool));

  *new_dir_baton = nb;

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_added(const char *relpath,
              const svn_diff_source_t *copyfrom_source,
              const svn_diff_source_t *right_source,
              /*const*/ apr_hash_t *copyfrom_props,
              /*const*/ apr_hash_t *right_props,
              void *dir_baton,
              const svn_diff_tree_processor_t *processor,
              apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_added(relpath,
                            copyfrom_source,
                            right_source,
                            copyfrom_props,
                            right_props,
                            db->baton1,
                            tb->p1,
                            scratch_pool));

  SVN_ERR(tb->p2->dir_added(relpath,
                            copyfrom_source,
                            right_source,
                            copyfrom_props,
                            right_props,
                            db->baton2,
                            tb->p2,
                            scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_deleted(const char *relpath,
                const svn_diff_source_t *left_source,
                /*const*/ apr_hash_t *left_props,
                void *dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_deleted(relpath,
                              left_source,
                              left_props,
                              db->baton1,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->dir_deleted(relpath,
                              left_source,
                              left_props,
                              db->baton2,
                              tb->p2,
                              scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    const apr_array_header_t *prop_changes,
                    void *dir_baton,
                    const struct svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_changed(relpath,
                              left_source,
                              right_source,
                              left_props,
                              right_props,
                              prop_changes,
                              db->baton1,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->dir_changed(relpath,
                              left_source,
                              right_source,
                              left_props,
                              right_props,
                              prop_changes,
                              db->baton2,
                              tb->p2,
                              scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_closed(const char *relpath,
               const svn_diff_source_t *left_source,
               const svn_diff_source_t *right_source,
               void *dir_baton,
               const svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_closed(relpath,
                             left_source,
                             right_source,
                             db->baton1,
                             tb->p1,
                             scratch_pool));

  SVN_ERR(tb->p2->dir_closed(relpath,
                             left_source,
                             right_source,
                             db->baton2,
                             tb->p2,
                             scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_opened(void **new_file_baton,
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
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *pb = dir_baton;
  struct tee_node_baton_t *nb = apr_pcalloc(result_pool, sizeof(*nb));

  SVN_ERR(tb->p1->file_opened(&(nb->baton1),
                              skip,
                              relpath,
                              left_source,
                              right_source,
                              copyfrom_source,
                              pb ? pb->baton1 : NULL,
                              tb->p1,
                              result_pool,
                              scratch_pool));

  SVN_ERR(tb->p2->file_opened(&(nb->baton2),
                              skip,
                              relpath,
                              left_source,
                              right_source,
                              copyfrom_source,
                              pb ? pb->baton2 : NULL,
                              tb->p2,
                              result_pool,
                              scratch_pool));

  *new_file_baton = nb;

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_added(const char *relpath,
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
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_added(relpath,
                             copyfrom_source,
                             right_source,
                             copyfrom_file,
                             right_file,
                             copyfrom_props,
                             right_props,
                             fb->baton1,
                             tb->p1,
                             scratch_pool));

  SVN_ERR(tb->p2->file_added(relpath,
                             copyfrom_source,
                             right_source,
                             copyfrom_file,
                             right_file,
                             copyfrom_props,
                             right_props,
                             fb->baton2,
                             tb->p2,
                             scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const char *left_file,
                 /*const*/ apr_hash_t *left_props,
                 void *file_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_deleted(relpath,
                               left_source,
                               left_file,
                               left_props,
                               fb->baton1,
                               tb->p1,
                               scratch_pool));

  SVN_ERR(tb->p2->file_deleted(relpath,
                               left_source,
                               left_file,
                               left_props,
                               fb->baton2,
                               tb->p2,
                               scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_changed(const char *relpath,
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
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_changed(relpath,
                               left_source,
                               right_source,
                               left_file,
                               right_file,
                               left_props,
                               right_props,
                               file_modified,
                               prop_changes,
                               fb->baton1,
                               tb->p1,
                               scratch_pool));

  SVN_ERR(tb->p2->file_changed(relpath,
                               left_source,
                               right_source,
                               left_file,
                               right_file,
                               left_props,
                               right_props,
                               file_modified,
                               prop_changes,
                               fb->baton2,
                               tb->p2,
                               scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_closed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_closed(relpath,
                              left_source,
                              right_source,
                              fb->baton1,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->file_closed(relpath,
                              left_source,
                              right_source,
                              fb->baton2,
                              tb->p2,
                              scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_node_absent(const char *relpath,
                void *dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->node_absent(relpath,
                              db ? db->baton1 : NULL,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->node_absent(relpath,
                              db ? db->baton2 : NULL,
                              tb->p2,
                              scratch_pool));

  return SVN_NO_ERROR;
}

const svn_diff_tree_processor_t *
svn_diff__tree_processor_tee_create(const svn_diff_tree_processor_t *processor1,
                                    const svn_diff_tree_processor_t *processor2,
                                    apr_pool_t *result_pool)
{
  struct tee_baton_t *tb = apr_pcalloc(result_pool, sizeof(*tb));
  svn_diff_tree_processor_t *tee;
  tb->p1 = processor1;
  tb->p2 = processor2;

  tee = svn_diff__tree_processor_create(tb, result_pool);

  tee->dir_opened    = tee_dir_opened;
  tee->dir_added     = tee_dir_added;
  tee->dir_deleted   = tee_dir_deleted;
  tee->dir_changed   = tee_dir_changed;
  tee->dir_closed    = tee_dir_closed;
  tee->file_opened   = tee_file_opened;
  tee->file_added    = tee_file_added;
  tee->file_deleted  = tee_file_deleted;
  tee->file_changed  = tee_file_changed;
  tee->file_closed   = tee_file_closed;
  tee->node_absent   = tee_node_absent;

  return tee;
}
