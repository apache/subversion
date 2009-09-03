/*
 * node.c:  routines for getting information about nodes in the working copy.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

/* A note about these functions:

   We aren't really sure yet which bits of data libsvn_client needs about
   nodes.  In wc-1, we just grab the entry, and then use whatever we want
   from it.  Such a pattern is Bad.

   This file is intended to hold functions which retrieve specific bits of
   information about a node, and will hopefully give us a better idea about
   what data libsvn_client needs, and how to best provide that data in 1.7
   final.  As such, these functions should only be called from outside
   libsvn_wc; any internal callers are encouraged to use the appropriate
   information fetching function, such as svn_wc__db_read_info().
*/

#include <apr_pools.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_types.h"

#include "wc.h"
#include "lock.h"
#include "props.h"
#include "log.h"
#include "entries.h"
#include "wc_db.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_debug.h"


svn_error_t *
svn_wc__node_get_children(const apr_array_header_t **children,
                          svn_wc_context_t *wc_ctx,
                          const char *dir_abspath,
                          svn_boolean_t show_hidden,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const apr_array_header_t *rel_children;
  apr_array_header_t *childs;
  int i;

  SVN_ERR(svn_wc__db_read_children(&rel_children, wc_ctx->db, dir_abspath,
                                   scratch_pool, scratch_pool));

  childs = apr_array_make(result_pool, rel_children->nelts,
                             sizeof(const char *));
  for (i = 0; i < rel_children->nelts; i++)
    {
      const char *child_abspath = svn_dirent_join(dir_abspath,
                                                  APR_ARRAY_IDX(rel_children,
                                                                i,
                                                                const char *),
                                                  result_pool);

      /* Don't add hidden nodes to *CHILDREN if we don't want them. */
      if (!show_hidden)
        {
          svn_boolean_t child_is_hidden;

          SVN_ERR(svn_wc__db_node_hidden(&child_is_hidden, wc_ctx->db,
                                         child_abspath, scratch_pool));
          if (child_is_hidden)
            continue;
        }

      APR_ARRAY_PUSH(childs, const char *) = child_abspath;
    }

  *children = childs;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__node_get_repos_root(const char **repos_root_url,
                            svn_wc_context_t *wc_ctx,
                            const char * local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  
  err = svn_wc__db_read_info(NULL, NULL, NULL, NULL,
                             repos_root_url,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             wc_ctx->db, local_abspath, result_pool,
                             scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      *repos_root_url = NULL;
    }

  return err;
}

svn_error_t *
svn_wc__node_get_kind(svn_node_kind_t *kind,
                      svn_wc_context_t *wc_ctx,
                      const char *abspath,
                      svn_boolean_t show_hidden,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_kind_t db_kind;

  SVN_ERR(svn_wc__db_check_node(&db_kind, wc_ctx->db, abspath,
                                scratch_pool));
  switch (db_kind)
    {
      case svn_wc__db_kind_file:
        *kind = svn_node_file;
        break;
      case svn_wc__db_kind_dir:
        *kind = svn_node_dir;
        break;
      case svn_wc__db_kind_symlink:
        *kind = svn_node_file;
        break;
      default:
        *kind = svn_node_unknown;
    }

  /* If we found a svn_node_file or svn_node_dir, but it is hidden,
     then consider *KIND to be svn_node_none unless SHOW_HIDDEN is true. */
  if (! show_hidden
      && (*kind == svn_node_file || *kind == svn_node_dir))
    {
      svn_boolean_t hidden;

      SVN_ERR(svn_wc__db_node_hidden(&hidden, wc_ctx->db, abspath,
                                     scratch_pool));
      if (hidden)
        *kind = svn_node_none;
    }

  return SVN_NO_ERROR;
}

/* A recursive node-walker, helper for svn_wc__node_walk_children(). */
static svn_error_t *
walker_helper(svn_wc__db_t *db,
              const char *dir_abspath,
              const svn_wc__node_walk_callbacks_t *callbacks,
              void *walk_baton,
              svn_depth_t depth,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const apr_array_header_t *rel_children;
  apr_pool_t *iterpool;
  int i;

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_read_children(&rel_children, db, dir_abspath,
                                   scratch_pool, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < rel_children->nelts; i++)
    {
      const char *child_abspath;
      svn_wc__db_kind_t child_kind;
      
      svn_pool_clear(iterpool);

      /* See if someone wants to cancel this operation. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      child_abspath = svn_dirent_join(dir_abspath,
                                      APR_ARRAY_IDX(rel_children, i,
                                                    const char *),
                                      iterpool);

      SVN_ERR(svn_wc__db_read_info(NULL, &child_kind, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   db, child_abspath, iterpool, iterpool));

      /* Return the child, if appropriate.  (For a directory,
       * this is the first visit: as a child.) */
      if (child_kind == svn_wc__db_kind_file
            || depth >= svn_depth_immediates)
        {
          err = callbacks->found_node(child_abspath, walk_baton, iterpool);
          if (err)
            SVN_ERR(callbacks->handle_error(child_abspath, err, walk_baton,
                                            iterpool));
        }

      /* Recurse into this directory, if appropriate. */
      if (child_kind == svn_wc__db_kind_dir
            && depth >= svn_depth_immediates)
        {
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(walker_helper(db, child_abspath, callbacks, walk_baton,
                                depth_below_here, cancel_func, cancel_baton,
                                iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__node_walk_children(svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           const svn_wc__node_walk_callbacks_t *callbacks,
                           void *walk_baton,
                           svn_depth_t walk_depth,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_wc__db_kind_t kind;
  svn_depth_t depth;

  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, &depth, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  if (kind == svn_wc__db_kind_file || depth == svn_depth_exclude)
    {
      err = callbacks->found_node(local_abspath, walk_baton, scratch_pool);
      if (err)
        return svn_error_return(
          callbacks->handle_error(local_abspath, err, walk_baton,
                                  scratch_pool));

      return SVN_NO_ERROR;
    }

  if (kind == svn_wc__db_kind_dir)
    {
      /* Return the directory first, before starting recursion, since it
         won't get returned as part of the recursion. */
      err = callbacks->found_node(local_abspath, walk_baton, scratch_pool);
      if (err)
        SVN_ERR(callbacks->handle_error(local_abspath, err, walk_baton,
                                        scratch_pool));

      return svn_error_return(
        walker_helper(wc_ctx->db, local_abspath, callbacks, walk_baton,
                      walk_depth, cancel_func, cancel_baton, scratch_pool));
    }

  return svn_error_return(
    callbacks->handle_error(local_abspath,
                            svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                                  _("'%s' has an unrecognized node kind"),
                                  svn_dirent_local_style(local_abspath,
                                                         scratch_pool)),
                            walk_baton, scratch_pool));
}
