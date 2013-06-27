/*
 * cleanup.c:  wrapper around wc cleanup functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_time.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "client.h"
#include "svn_props.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

struct remove_unversioned_items_baton
{
  svn_boolean_t remove_unversioned_items;
  svn_boolean_t remove_ignored_items;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
};

/* An implementation of svn_wc_status_func4_t. */
static svn_error_t *
remove_unversioned_items(void *baton,
                         const char *local_abspath,
                         const svn_wc_status3_t *status,
                         apr_pool_t *scratch_pool)
{
  struct remove_unversioned_items_baton *b = baton;
  svn_node_kind_t kind_on_disk;
  svn_wc_notify_t *notify;

  if (status->node_status == svn_wc_status_ignored)
    {
      if (!b->remove_ignored_items)
        return SVN_NO_ERROR;
    }
  else if (status->node_status == svn_wc_status_unversioned)
    {
      if (!b->remove_unversioned_items)
        return SVN_NO_ERROR;
    }
  else
    return SVN_NO_ERROR;

  SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
  switch (kind_on_disk)
    {
      case svn_node_file:
      case svn_node_symlink:
        SVN_ERR(svn_io_remove_file2(local_abspath, FALSE, scratch_pool));
        break;
      case svn_node_dir:
        SVN_ERR(svn_io_remove_dir2(local_abspath, FALSE,
                                   b->cancel_func, b->cancel_baton,
                                   scratch_pool));
        break;
      case svn_node_none:
      default:
        return SVN_NO_ERROR;
    }

  notify = svn_wc_create_notify(local_abspath, svn_wc_notify_delete,
                                scratch_pool);
  notify->kind = kind_on_disk;
  (*b->notify_func)(b->notify_baton, notify, scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_cleanup2(const char *path,
                    svn_boolean_t remove_unversioned_children,
                    svn_boolean_t remove_ignored_children,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_error_t *err;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  if (remove_unversioned_children || remove_ignored_children)
    {
      svn_boolean_t is_locked_here;
      svn_boolean_t is_locked;
      svn_boolean_t sqlite_exclusive;
      svn_config_t *cfg = ctx->config
                          ? svn_hash_gets(ctx->config,
                                          SVN_CONFIG_CATEGORY_CONFIG)
                          : NULL;

      /* Check if someone else owns a lock for LOCAL_ABSPATH. */
      SVN_ERR(svn_wc_locked2(&is_locked_here, &is_locked, ctx->wc_ctx,
                             local_abspath, scratch_pool));
      if (is_locked && !is_locked_here)
        return svn_error_createf(SVN_ERR_WC_LOCKED, NULL,
                                 _("Working copy at '%s' is already locked."),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));

      SVN_ERR(svn_config_get_bool(cfg, &sqlite_exclusive,
                                  SVN_CONFIG_SECTION_WORKING_COPY,
                                  SVN_CONFIG_OPTION_SQLITE_EXCLUSIVE,
                                  FALSE));
      if (sqlite_exclusive)
        {
          /* Close the db because svn_wc_cleanup3() will try to open it again,
           * which doesn't work if exclusive sqlite locking mode is enabled. */
          SVN_ERR(svn_wc__close_db(local_abspath, ctx->wc_ctx, scratch_pool));
        }
    }

  err = svn_wc_cleanup3(ctx->wc_ctx, local_abspath, ctx->cancel_func,
                        ctx->cancel_baton, scratch_pool);
  svn_io_sleep_for_timestamps(path, scratch_pool);
  if (err)
    return svn_error_trace(err);
 
  if (remove_unversioned_children || remove_ignored_children)
    {
      struct remove_unversioned_items_baton b;

      b.remove_unversioned_items = remove_unversioned_children;
      b.remove_ignored_items = remove_ignored_children;
      b.notify_func = ctx->notify_func2;
      b.notify_baton = ctx->notify_baton2;
      b.cancel_func = ctx->cancel_func;
      b.cancel_baton = ctx->cancel_baton;
      SVN_ERR(svn_wc_walk_status(ctx->wc_ctx, local_abspath,
                                 svn_depth_infinity,
                                 TRUE,  /* get all */
                                 remove_ignored_children,
                                 TRUE,  /* ignore textmods */
                                 NULL,  /* use default ignore patterns */
                                 remove_unversioned_items, &b,
                                 ctx->cancel_func,
                                 ctx->cancel_baton,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}
