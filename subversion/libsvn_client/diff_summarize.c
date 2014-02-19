/*
 * repos_diff_summarize.c -- The diff callbacks for summarizing
 * the differences of two repository versions
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

#include "svn_private_config.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "private/svn_wc_private.h"

#include "client.h"


/* Diff callbacks baton.  */
struct summarize_baton_t {
  apr_pool_t *baton_pool; /* For allocating skip_path */

  /* The target path of the diff, relative to the anchor; "" if target == anchor. */
  const char *original_target;
  const char *anchor_path;
  const char *skip_relpath;

  /* The summarize callback passed down from the API */
  svn_client_diff_summarize_func_t summarize_func;

  /* The summarize callback baton */
  void *summarize_func_baton;

  /* Which paths have a prop change. Key is a (const char *) path; the value
   * is any non-null pointer to indicate that this path has a prop change. */
  apr_hash_t *prop_changes;
};

/* Calculate skip_relpath from original_target and anchor_path */
static APR_INLINE void
ensure_skip_relpath(struct summarize_baton_t *b)
{
  if (b->skip_relpath)
    return;

  if (svn_path_is_url(b->original_target))
    {
      b->skip_relpath = svn_uri_skip_ancestor(b->anchor_path,
                                              b->original_target,
                                              b->baton_pool);
    }
  else
    {
      b->skip_relpath = svn_dirent_skip_ancestor(b->anchor_path,
                                                 b->original_target);
    }

  if (!b->skip_relpath)
    b->skip_relpath = "";
}

/* Call B->summarize_func with B->summarize_func_baton, passing it a
 * summary object composed from PATH (but made to be relative to the target
 * of the diff), SUMMARIZE_KIND, PROP_CHANGED (or FALSE if the action is an
 * add or delete) and NODE_KIND. */
static svn_error_t *
send_summary(struct summarize_baton_t *b,
             const char *path,
             svn_client_diff_summarize_kind_t summarize_kind,
             svn_boolean_t prop_changed,
             svn_node_kind_t node_kind,
             apr_pool_t *scratch_pool)
{
  svn_client_diff_summarize_t *sum = apr_pcalloc(scratch_pool, sizeof(*sum));

  SVN_ERR_ASSERT(summarize_kind != svn_client_diff_summarize_kind_normal
                 || prop_changed);

  /* PATH is relative to the anchor of the diff, but SUM->path needs to be
     relative to the target of the diff. */
  ensure_skip_relpath(b);

  sum->path = svn_relpath_skip_ancestor(b->skip_relpath, path);
  sum->summarize_kind = summarize_kind;
  if (summarize_kind == svn_client_diff_summarize_kind_modified
      || summarize_kind == svn_client_diff_summarize_kind_normal)
    sum->prop_changed = prop_changed;
  sum->node_kind = node_kind;

  SVN_ERR(b->summarize_func(sum, b->summarize_func_baton, scratch_pool));
  return SVN_NO_ERROR;
}

/* Are there any changes to relevant (normal) props in PROPCHANGES? */
static svn_boolean_t
props_changed(const apr_array_header_t *propchanges,
              apr_pool_t *scratch_pool)
{
  apr_array_header_t *props;

  svn_error_clear(svn_categorize_props(propchanges, NULL, NULL, &props,
                                       scratch_pool));
  return (props->nelts != 0);
}


static svn_error_t *
cb_dir_deleted(svn_wc_notify_state_t *state,
               svn_boolean_t *tree_conflicted,
               const char *path,
               void *diff_baton,
               apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = diff_baton;

  SVN_ERR(send_summary(b, path, svn_client_diff_summarize_kind_deleted,
                       FALSE, svn_node_dir, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
cb_file_deleted(svn_wc_notify_state_t *state,
                svn_boolean_t *tree_conflicted,
                const char *path,
                const char *tmpfile1,
                const char *tmpfile2,
                const char *mimetype1,
                const char *mimetype2,
                apr_hash_t *originalprops,
                void *diff_baton,
                apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = diff_baton;

  SVN_ERR(send_summary(b, path, svn_client_diff_summarize_kind_deleted,
                       FALSE, svn_node_file, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
cb_dir_added(svn_wc_notify_state_t *state,
             svn_boolean_t *tree_conflicted,
             svn_boolean_t *skip,
             svn_boolean_t *skip_children,
             const char *path,
             svn_revnum_t rev,
             const char *copyfrom_path,
             svn_revnum_t copyfrom_revision,
             void *diff_baton,
             apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
cb_dir_opened(svn_boolean_t *tree_conflicted,
              svn_boolean_t *skip,
              svn_boolean_t *skip_children,
              const char *path,
              svn_revnum_t rev,
              void *diff_baton,
              apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
cb_dir_closed(svn_wc_notify_state_t *contentstate,
              svn_wc_notify_state_t *propstate,
              svn_boolean_t *tree_conflicted,
              const char *path,
              svn_boolean_t dir_was_added,
              void *diff_baton,
              apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = diff_baton;
  svn_boolean_t prop_change;

  ensure_skip_relpath(b);
  if (! svn_relpath_skip_ancestor(b->skip_relpath, path))
    return SVN_NO_ERROR;

  prop_change = svn_hash_gets(b->prop_changes, path) != NULL;
  if (dir_was_added || prop_change)
    SVN_ERR(send_summary(b, path,
                         dir_was_added ? svn_client_diff_summarize_kind_added
                                       : svn_client_diff_summarize_kind_normal,
                         prop_change, svn_node_dir, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
cb_file_added(svn_wc_notify_state_t *contentstate,
              svn_wc_notify_state_t *propstate,
              svn_boolean_t *tree_conflicted,
              const char *path,
              const char *tmpfile1,
              const char *tmpfile2,
              svn_revnum_t rev1,
              svn_revnum_t rev2,
              const char *mimetype1,
              const char *mimetype2,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              const apr_array_header_t *propchanges,
              apr_hash_t *originalprops,
              void *diff_baton,
              apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = diff_baton;

  SVN_ERR(send_summary(b, path, svn_client_diff_summarize_kind_added,
                       props_changed(propchanges, scratch_pool),
                       svn_node_file, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
cb_file_opened(svn_boolean_t *tree_conflicted,
               svn_boolean_t *skip,
               const char *path,
               svn_revnum_t rev,
               void *diff_baton,
               apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
cb_file_changed(svn_wc_notify_state_t *contentstate,
                svn_wc_notify_state_t *propstate,
                svn_boolean_t *tree_conflicted,
                const char *path,
                const char *tmpfile1,
                const char *tmpfile2,
                svn_revnum_t rev1,
                svn_revnum_t rev2,
                const char *mimetype1,
                const char *mimetype2,
                const apr_array_header_t *propchanges,
                apr_hash_t *originalprops,
                void *diff_baton,
                apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = diff_baton;
  svn_boolean_t text_change = (tmpfile2 != NULL);
  svn_boolean_t prop_change = props_changed(propchanges, scratch_pool);

  if (text_change || prop_change)
    SVN_ERR(send_summary(b, path,
                         text_change ? svn_client_diff_summarize_kind_modified
                                     : svn_client_diff_summarize_kind_normal,
                         prop_change, svn_node_file, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
cb_dir_props_changed(svn_wc_notify_state_t *propstate,
                     svn_boolean_t *tree_conflicted,
                     const char *path,
                     svn_boolean_t dir_was_added,
                     const apr_array_header_t *propchanges,
                     apr_hash_t *original_props,
                     void *diff_baton,
                     apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = diff_baton;

  if (props_changed(propchanges, scratch_pool))
    svn_hash_sets(b->prop_changes, path, path);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_diff_summarize_callbacks(
                        const svn_diff_tree_processor_t **diff_processor,
                        const char ***anchor_path,
                        svn_client_diff_summarize_func_t summarize_func,
                        void *summarize_baton,
                        const char *original_target,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_wc_diff_callbacks4_t *cb = apr_palloc(result_pool, sizeof(*cb));
  struct summarize_baton_t *b = apr_pcalloc(result_pool, sizeof(*b));

  *anchor_path = &b->anchor_path;
  b->baton_pool = result_pool;
  b->summarize_func = summarize_func;
  b->summarize_func_baton = summarize_baton;
  b->original_target = apr_pstrdup(result_pool, original_target);
  b->prop_changes = apr_hash_make(result_pool);

  cb->file_opened = cb_file_opened;
  cb->file_changed = cb_file_changed;
  cb->file_added = cb_file_added;
  cb->file_deleted = cb_file_deleted;
  cb->dir_deleted = cb_dir_deleted;
  cb->dir_opened = cb_dir_opened;
  cb->dir_added = cb_dir_added;
  cb->dir_props_changed = cb_dir_props_changed;
  cb->dir_closed = cb_dir_closed;

  SVN_ERR(svn_wc__wrap_diff_callbacks(diff_processor, cb, b, TRUE,
                                      result_pool, scratch_pool));

  return SVN_NO_ERROR;
}
