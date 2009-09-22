/*
 * crop.c: Cropping the WC
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

#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc.h"
#include "lock.h"
#include "entries.h"

#include "svn_private_config.h"

/* Evaluate EXPR.  If it returns an error, return that error, unless
   the error's code is SVN_ERR_WC_LEFT_LOCAL_MOD, in which case clear
   the error and do not return. */
#define IGNORE_LOCAL_MOD(expr)                                   \
  do {                                                           \
    svn_error_t *__temp = (expr);                                \
    if (__temp)                                                  \
      {                                                          \
        if (__temp->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)        \
          svn_error_clear(__temp);                               \
        else                                                     \
          return __temp;                                         \
      }                                                          \
  } while (0)

/* Helper function that crops the children of the LOCAL_ABSPATH, under the
 * constraint of DEPTH. The DIR_PATH itself will never be cropped. The whole
 * subtree should have been locked.
 *
 * If NOTIFY_FUNC is not null, each file and ROOT of subtree will be reported
 * upon remove.
 */
static svn_error_t *
crop_children(svn_wc__db_t *db,
              const char *local_abspath,
              svn_depth_t depth,
              svn_wc_notify_func2_t notify_func,
              void *notify_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  const apr_array_header_t *children;
  svn_depth_t dir_depth;
  apr_pool_t *iterpool;
  int i;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  iterpool = svn_pool_create(pool);

  SVN_ERR_ASSERT(depth != svn_depth_exclude);

  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, &dir_depth,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               db, local_abspath, pool, iterpool));

  /* Update the depth of target first, if needed. */
  if (dir_depth > depth)
    {
      SVN_ERR(svn_wc__set_depth(db, local_abspath, depth, iterpool));
    }

  /* Looping over current directory's SVN entries: */
  SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath, pool,
                                   iterpool));

  for (i = 0; i < children->nelts; i++)
    {
      const char *child_name = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      svn_wc__db_kind_t kind;
      svn_depth_t child_depth;

      svn_pool_clear(iterpool);

      /* Get the next node */
      child_abspath = svn_dirent_join(local_abspath, child_name, iterpool);

      SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, &child_depth,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   db, child_abspath, iterpool, iterpool));

      if (kind == svn_wc__db_kind_file)
        {
          /* We currently crop on a directory basis. So don't worry about
             svn_depth_exclude here. And even we permit excluding a single
             file in the future, svn_wc_remove_from_revision_control() can
             also handle it. We only need to skip the notification in that
             case. */
          if (depth == svn_depth_empty)
            IGNORE_LOCAL_MOD(
              svn_wc__remove_from_revision_control_internal(
                                                   db,
                                                   child_abspath,
                                                   TRUE, /* destroy */
                                                   FALSE, /* instant error */
                                                   cancel_func, cancel_baton,
                                                   iterpool));
          else
            continue;

        }
      else if (kind == svn_wc__db_kind_dir)
        {
          if (child_depth == svn_depth_exclude)
            {
              /* Preserve the excluded node if the parent need it.
                 Anyway, don't report on excluded subdir, since they are
                 logically not exist. */
              if (depth < svn_depth_immediates)
                SVN_ERR(svn_wc__entry_remove(db, child_abspath, iterpool));
              continue;
            }
          else if (depth < svn_depth_immediates)
            {
              IGNORE_LOCAL_MOD(
                svn_wc__remove_from_revision_control_internal(
                                                     db,
                                                     child_abspath,
                                                     TRUE, /* destroy */
                                                     FALSE, /* instant error */
                                                     cancel_func,
                                                     cancel_baton,
                                                     iterpool));
            }
          else
            {
              SVN_ERR(crop_children(db,
                                    child_abspath,
                                    svn_depth_empty,
                                    notify_func,
                                    notify_baton,
                                    cancel_func,
                                    cancel_baton,
                                    iterpool));
              continue;
            }
        }
      else
        {
          return svn_error_createf
            (SVN_ERR_NODE_UNKNOWN_KIND, NULL, _("Unknown node kind for '%s'"),
             svn_dirent_local_style(child_abspath, iterpool));
        }

      if (notify_func)
        {
          svn_wc_notify_t *notify;
          notify = svn_wc_create_notify(child_abspath,
                                        svn_wc_notify_delete,
                                        iterpool);
          (*notify_func)(notify_baton, notify, iterpool);
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_crop_tree2(svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  svn_depth_t depth,
                  svn_wc_notify_func2_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;

  /* Only makes sense when the depth is restrictive. */
  if (depth == svn_depth_infinity)
    return SVN_NO_ERROR; /* Nothing to crop */
  if (!(depth >= svn_depth_exclude && depth < svn_depth_infinity))
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
      _("Can only crop a working copy with a restrictive depth"));

  {
    svn_boolean_t hidden;
    SVN_ERR(svn_wc__db_node_hidden(&hidden, db, local_abspath, scratch_pool));

    if (hidden)
      return svn_error_create(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("Can only crop directories"));
  }

  {
    svn_wc__db_status_t status;
    svn_wc__db_kind_t kind;
    svn_depth_t depth;

    SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, &depth, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool));

    if (kind != svn_wc__db_kind_dir)
      return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
        _("Can only crop directories"));

    if (status == svn_wc__db_status_deleted ||
        status == svn_wc__db_status_moved_away)
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("Cannot crop '%s': it is going to be removed "
                                 "from repository. Try commit instead"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));

    if (status == svn_wc__db_status_added ||
        status == svn_wc__db_status_copied ||
        status == svn_wc__db_status_moved_here)
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("Cannot crop '%s': it is to be added "
                                 "to the repository. Try commit instead"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
  }

  /* Crop the target itself if we are requested to. */
  if (depth == svn_depth_exclude)
    {
      const char *relpath;
      const char *uuid;

      if (svn_dirent_is_root(local_abspath, strlen(local_abspath)))
        return svn_error_createf
          (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
           _("Cannot exclude root directory"));

      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, &relpath, NULL, &uuid,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, db, local_abspath,
                                   scratch_pool, scratch_pool));

      /* This simulates the logic of svn_wc__check_wc_root(). */
      if (relpath != NULL)
        {
          /* If relpath is NULL, it is certainly not switched */
          const char *parent_relpath, *parent_uuid;
          const char *exp_relpath;
          svn_error_t *err;

          err = svn_wc__db_scan_base_repos(&parent_relpath, NULL, &parent_uuid,
                                           db,
                                           svn_dirent_dirname(local_abspath,
                                                              scratch_pool),
                                                              scratch_pool,
                                                              scratch_pool);

          if (err || strcmp(parent_uuid, uuid) != 0)
            {
              /* Probably fell off the top of the working copy?  */
              svn_error_clear(err);
              return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                       _("Cannot crop '%s': "
                                         "it is a working copy root"),
                                       svn_dirent_local_style(local_abspath,
                                                              scratch_pool));
            }

          exp_relpath = svn_relpath_join(parent_relpath,
                                         svn_dirent_basename(local_abspath,
                                                             NULL),
                                         scratch_pool);

          if (strcmp(relpath, exp_relpath) != 0)
            return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                     _("Cannot crop '%s': "
                                       "it is a switched path"),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));
        }

      SVN_ERR(svn_wc__set_depth(db, local_abspath, svn_depth_exclude,
                                scratch_pool));

      /* TODO(#2843): Do we need to restore the modified depth if the user
         cancel this operation? */
      IGNORE_LOCAL_MOD(
          svn_wc_remove_from_revision_control2(wc_ctx,
                                               local_abspath,
                                               TRUE, /* destroy */
                                               FALSE, /* instant error */
                                               cancel_func, cancel_baton,
                                               scratch_pool));

      if (notify_func)
        {
          svn_wc_notify_t *notify;
          notify = svn_wc_create_notify(local_abspath,
                                        svn_wc_notify_delete,
                                        scratch_pool);
          (*notify_func)(notify_baton, notify, scratch_pool);
        }
      return SVN_NO_ERROR;
    }

  return crop_children(db, local_abspath, depth,
                       notify_func, notify_baton,
                       cancel_func, cancel_baton, scratch_pool);
}
