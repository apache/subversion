/*
 * diff_tree_debug.c :  a diff tree processor implementation, that writes
 *                      the operations it does to a given stream.
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

#include "svn_error.h"
#include "svn_io.h"
#include "svn_types.h"

#include "private/svn_diff_tree.h"
#include "svn_private_config.h"

#define INDENT_SIZE 2

typedef struct debug_diff_tree_baton_t
{
  int indent_level;

  svn_stream_t *out;
  const char *prefix;
} debug_diff_tree_baton_t;

static const char *
diff_source_to_string(const svn_diff_source_t *source,
                      apr_pool_t *result_pool)
{
  if (source)
    {
      return apr_psprintf(result_pool, "%s@r" "%" SVN_REVNUM_T_FMT,
                          source->repos_relpath, source->revision);
    }
  else
    {
      return apr_pstrdup(result_pool, "(null)");
    }
}

static svn_error_t *
write_indent(debug_diff_tree_baton_t *baton,
             apr_pool_t *scratch_pool)
{
  int i;

  SVN_ERR(svn_stream_puts(baton->out, baton->prefix));
  for (i = 0; i < baton->indent_level * INDENT_SIZE; ++i)
    SVN_ERR(svn_stream_puts(baton->out, " "));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_action(debug_diff_tree_baton_t *b,
             const char *action_name,
             const char *relpath,
             apr_pool_t *scratch_pool)
{
  SVN_ERR(write_indent(b, scratch_pool));
  SVN_ERR(svn_stream_printf(b->out, scratch_pool,
                            "%s('%s')\n",
                            action_name, relpath));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_action_property(debug_diff_tree_baton_t *b,
                      const char *name,
                      const char *value,
                      apr_pool_t *scratch_pool)
{
  SVN_ERR(write_indent(b, scratch_pool));
  SVN_ERR(svn_stream_printf(b->out, scratch_pool,
                            "| %s : %s\n",
                            name, value));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_action_property_source(debug_diff_tree_baton_t *b,
                             const char *name,
                             const svn_diff_source_t *value,
                             apr_pool_t *scratch_pool)
{
  return svn_error_trace(write_action_property(
    b, name, diff_source_to_string(value, scratch_pool), scratch_pool));
}

static svn_error_t *
debug_dir_opened(void **new_dir_baton,
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
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "dir_opened", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "copyfrom_source",
                                       copyfrom_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source", 
                                       right_source, scratch_pool));

  b->indent_level++;

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_dir_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "dir_added", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "copyfrom_source",
                                       copyfrom_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source",
                                       right_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_dir_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  /*const*/ apr_hash_t *left_props,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "dir_deleted", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));
  
  return SVN_NO_ERROR;
}

static svn_error_t *
debug_dir_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  const apr_array_header_t *prop_changes,
                  void *dir_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "dir_changed", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source",
                                       right_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_dir_closed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 void *dir_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  debug_diff_tree_baton_t *b = processor->baton;

  b->indent_level--;

  SVN_ERR(write_action(b, "dir_closed", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source",
                                       right_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_file_opened(void **new_file_baton,
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
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "file_opened", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source",
                                       right_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "copyfrom_source",
                                       copyfrom_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_file_added(const char *relpath,
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
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "file_added", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "copyfrom_source",
                                       copyfrom_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source", 
right_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_file_deleted(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const char *left_file,
                   /*const*/ apr_hash_t *left_props,
                   void *file_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "file_deleted", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_file_changed(const char *relpath,
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
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "file_changed", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source",
                                       right_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_file_closed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  void *file_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "file_closed", relpath, scratch_pool));
  SVN_ERR(write_action_property_source(b, "left_source",
                                       left_source, scratch_pool));
  SVN_ERR(write_action_property_source(b, "right_source",
                                       right_source, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
debug_node_absent(const char *relpath,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  debug_diff_tree_baton_t *b = processor->baton;

  SVN_ERR(write_action(b, "node_absent", relpath, scratch_pool));

  return SVN_NO_ERROR;
}

const svn_diff_tree_processor_t *
svn_diff__tree_processor_debug_create(svn_stream_t *out_stream,
                                      apr_pool_t *result_pool)
{
  debug_diff_tree_baton_t *b = apr_pcalloc(result_pool, sizeof(*b));

  b->indent_level = 0;
  b->out = out_stream;
  b->prefix = "DBG: ";

  svn_diff_tree_processor_t *debug =
    svn_diff__tree_processor_create(b, result_pool);

  debug->dir_opened   = debug_dir_opened;
  debug->dir_added    = debug_dir_added;
  debug->dir_deleted  = debug_dir_deleted;
  debug->dir_changed  = debug_dir_changed;
  debug->dir_closed   = debug_dir_closed;

  debug->file_opened  = debug_file_opened;
  debug->file_added   = debug_file_added;
  debug->file_deleted = debug_file_deleted;
  debug->file_changed = debug_file_changed;
  debug->file_closed  = debug_file_closed;

  debug->node_absent  = debug_node_absent;

  return debug;
}
