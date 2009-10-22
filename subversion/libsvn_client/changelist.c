/*
 * changelist.c:  implementation of the 'changelist' command
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "client.h"
#include "private/svn_wc_private.h"


/* Entry-walker callback for svn_client_add_to_changelist() and
   svn_client_remove_from_changelist() below. */
struct set_cl_fe_baton
{
  svn_wc_adm_access_t *adm_access;
  const char *changelist; /* NULL if removing changelists */
  apr_hash_t *changelist_hash;
  svn_client_ctx_t *ctx;
  apr_pool_t *pool;
};


/* This function -- which implements the 'found_entry' vtable member
   of svn_wc_entry_callbacks2_t -- associates PATH (via its ENTRY)
   with a new changelist (passed along in BATON->changelist), so long
   as ENTRY is deemed a valid target of that association.  */
static svn_error_t *
set_entry_changelist(const char *path,
                     const svn_wc_entry_t *entry,
                     void *baton,
                     apr_pool_t *pool)
{
  struct set_cl_fe_baton *b = (struct set_cl_fe_baton *)baton;
  svn_wc_adm_access_t *adm_access;

  /* See if this entry passes our changelist filtering. */
  if (! SVN_WC__CL_MATCH(b->changelist_hash, entry))
    return SVN_NO_ERROR;

  /* We only care about files right now. */
  if (entry->kind != svn_node_file)
    {
      if ((strcmp(SVN_WC_ENTRY_THIS_DIR, entry->name) == 0)
          && (b->ctx->notify_func2))
        b->ctx->notify_func2(b->ctx->notify_baton2,
                             svn_wc_create_notify(path,
                                                  svn_wc_notify_skip,
                                                  pool),
                             pool);
      return SVN_NO_ERROR;
    }

  /* Get the ADM_ACCESS for our file's parent directory,
     specifically. */
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, b->adm_access,
                              svn_dirent_dirname(path, pool), pool));
  return svn_wc_set_changelist(path, b->changelist, adm_access,
                               b->ctx->cancel_func, b->ctx->cancel_baton,
                               b->ctx->notify_func2, b->ctx->notify_baton2,
                               pool);
}


static const svn_wc_entry_callbacks2_t set_cl_entry_callbacks =
  { set_entry_changelist, svn_client__default_walker_error_handler };


svn_error_t *
svn_client_add_to_changelist(const apr_array_header_t *paths,
                             const char *changelist,
                             svn_depth_t depth,
                             const apr_array_header_t *changelists,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  /* ### Someday this routine might use a different underlying API to
     ### to make the associations in a centralized database. */

  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *changelist_hash = NULL;
  int i;

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  for (i = 0; i < paths->nelts; i++)
    {
      struct set_cl_fe_baton seb;
      svn_wc_adm_access_t *adm_access;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(subpool);
      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path,
                                     TRUE, /* write lock */ -1, /* infinity */
                                     ctx->cancel_func, ctx->cancel_baton,
                                     subpool));

      seb.adm_access = adm_access;
      seb.changelist = changelist;
      seb.changelist_hash = changelist_hash;
      seb.ctx = ctx;
      seb.pool = subpool;
      SVN_ERR(svn_wc_walk_entries3(path, adm_access,
                                   &set_cl_entry_callbacks, &seb,
                                   depth, FALSE, /* no hidden entries */
                                   ctx->cancel_func, ctx->cancel_baton,
                                   subpool));

      SVN_ERR(svn_wc_adm_close2(adm_access, subpool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_remove_from_changelists(const apr_array_header_t *paths,
                                   svn_depth_t depth,
                                   const apr_array_header_t *changelists,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *pool)
{
  /* ### Someday this routine might use a different underlying API to
     ### to make the associations in a centralized database. */

  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *changelist_hash = NULL;
  int i;

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  for (i = 0; i < paths->nelts; i++)
    {
      struct set_cl_fe_baton seb;
      svn_wc_adm_access_t *adm_access;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(subpool);
      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path,
                                     TRUE, /* write lock */ -1, /* infinity */
                                     ctx->cancel_func, ctx->cancel_baton,
                                     subpool));

      seb.adm_access = adm_access;
      seb.changelist = NULL;
      seb.changelist_hash = changelist_hash;
      seb.ctx = ctx;
      seb.pool = subpool;
      SVN_ERR(svn_wc_walk_entries3(path, adm_access,
                                   &set_cl_entry_callbacks, &seb,
                                   depth, FALSE, /* no hidden entries */
                                   ctx->cancel_func, ctx->cancel_baton,
                                   subpool));

      SVN_ERR(svn_wc_adm_close2(adm_access, subpool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}



/* Entry-walker callback for svn_client_get_changelist() below. */
struct get_cl_fe_baton
{
  svn_changelist_receiver_t callback_func;
  void *callback_baton;
  apr_hash_t *changelists;
  apr_pool_t *pool;
};


static svn_error_t *
get_entry_changelist(const char *path,
                     const svn_wc_entry_t *entry,
                     void *baton,
                     apr_pool_t *pool)
{
  struct get_cl_fe_baton *b = (struct get_cl_fe_baton *)baton;

  /* If the entry has a changelist, and is a file or is the "this-dir"
     entry for directory, and the changelist matches one that we're
     looking for (or we aren't looking for any in particular)... */
  if (SVN_WC__CL_MATCH(b->changelists, entry)
      && ((entry->kind == svn_node_file)
          || ((entry->kind == svn_node_dir)
              && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0))))
    {
      /* ...then call the callback function. */
      SVN_ERR(b->callback_func(b->callback_baton, path,
                               entry->changelist, pool));
    }

  return SVN_NO_ERROR;
}


static const svn_wc_entry_callbacks2_t get_cl_entry_callbacks =
  { get_entry_changelist, svn_client__default_walker_error_handler };


svn_error_t *
svn_client_get_changelists(const char *path,
                           const apr_array_header_t *changelists,
                           svn_depth_t depth,
                           svn_changelist_receiver_t callback_func,
                           void *callback_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  struct get_cl_fe_baton geb;
  svn_wc_adm_access_t *adm_access;

  geb.callback_func = callback_func;
  geb.callback_baton = callback_baton;
  geb.pool = pool;
  if (changelists)
    SVN_ERR(svn_hash_from_cstring_keys(&(geb.changelists), changelists, pool));
  else
    geb.changelists = NULL;
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path,
                                 FALSE, /* no write lock */
                                 -1, /* levels to lock == infinity */
                                 ctx->cancel_func, ctx->cancel_baton, pool));
  SVN_ERR(svn_wc_walk_entries3(path, adm_access, &get_cl_entry_callbacks, &geb,
                               depth, FALSE, /* don't show hidden entries */
                               ctx->cancel_func, ctx->cancel_baton, pool));
  return svn_wc_adm_close2(adm_access, pool);
}
