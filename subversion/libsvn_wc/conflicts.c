/*
 * conflicts.c: routines for managing conflict data.
 *            NOTE: this code doesn't know where the conflict is
 *            actually stored.
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



#include <string.h>

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include <apr_errno.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_diff.h"

#include "wc.h"
#include "wc_db.h"
#include "conflicts.h"
#include "workqueue.h"

#include "private/svn_wc_private.h"
#include "private/svn_skel.h"

#include "svn_private_config.h"

svn_skel_t *
svn_wc__prop_conflict_skel_new(apr_pool_t *result_pool)
{
  svn_skel_t *operation = svn_skel__make_empty_list(result_pool);
  svn_skel_t *result = svn_skel__make_empty_list(result_pool);

  svn_skel__prepend(operation, result);
  return result;
}


static void
prepend_prop_value(const svn_string_t *value,
                   svn_skel_t *skel,
                   apr_pool_t *result_pool)
{
  svn_skel_t *value_skel = svn_skel__make_empty_list(result_pool);

  if (value != NULL)
    {
      const void *dup = apr_pmemdup(result_pool, value->data, value->len);

      svn_skel__prepend(svn_skel__mem_atom(dup, value->len, result_pool),
                        value_skel);
    }

  svn_skel__prepend(value_skel, skel);
}


svn_error_t *
svn_wc__prop_conflict_skel_add(
  svn_skel_t *skel,
  const char *prop_name,
  const svn_string_t *original_value,
  const svn_string_t *mine_value,
  const svn_string_t *incoming_value,
  const svn_string_t *incoming_base_value,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_skel_t *prop_skel = svn_skel__make_empty_list(result_pool);

  /* ### check that OPERATION has been filled in.  */

  /* See notes/wc-ng/conflict-storage  */
  prepend_prop_value(incoming_base_value, prop_skel, result_pool);
  prepend_prop_value(incoming_value, prop_skel, result_pool);
  prepend_prop_value(mine_value, prop_skel, result_pool);
  prepend_prop_value(original_value, prop_skel, result_pool);
  svn_skel__prepend_str(apr_pstrdup(result_pool, prop_name), prop_skel,
                        result_pool);
  svn_skel__prepend_str(SVN_WC__CONFLICT_KIND_PROP, prop_skel, result_pool);

  /* Now we append PROP_SKEL to the end of the provided conflict SKEL.  */
  svn_skel__append(skel, prop_skel);

  return SVN_NO_ERROR;
}




/*** Resolving a conflict automatically ***/

/* Conflict resolution involves removing the conflict files, if they exist,
   and clearing the conflict filenames from the entry.  The latter needs to
   be done whether or not the conflict files exist.

   PATH is the path to the item to be resolved, BASE_NAME is the basename
   of PATH, and CONFLICT_DIR is the access baton for PATH.  ORIG_ENTRY is
   the entry prior to resolution. RESOLVE_TEXT and RESOLVE_PROPS are TRUE
   if text and property conflicts respectively are to be resolved.

   If this call marks any conflict as resolved, set *DID_RESOLVE to true,
   else do not change *DID_RESOLVE.

   See svn_wc_resolved_conflict5() for how CONFLICT_CHOICE behaves.
*/
static svn_error_t *
resolve_conflict_on_node(svn_boolean_t *did_resolve,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_boolean_t resolve_text,
                         svn_boolean_t resolve_props,
                         svn_boolean_t resolve_tree,
                         svn_wc_conflict_choice_t conflict_choice,
                         svn_cancel_func_t cancel_func_t,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  const char *conflict_old = NULL;
  const char *conflict_new = NULL;
  const char *conflict_working = NULL;
  const char *prop_reject_file = NULL;
  int i;
  const apr_array_header_t *conflicts;
  svn_skel_t *work_items = NULL;
  svn_skel_t *work_item;

  *did_resolve = FALSE;

  SVN_ERR(svn_wc__db_read_conflicts(&conflicts, db, local_abspath,
                                    pool, pool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *desc;

      desc = APR_ARRAY_IDX(conflicts, i,
                           const svn_wc_conflict_description2_t*);

      if (desc->kind == svn_wc_conflict_kind_text)
        {
          conflict_old = desc->base_abspath;
          conflict_new = desc->their_abspath;
          conflict_working = desc->my_abspath;
        }
      else if (desc->kind == svn_wc_conflict_kind_property)
        prop_reject_file = desc->their_abspath;
    }

  if (resolve_text)
    {
      const char *auto_resolve_src;

      /* Handle automatic conflict resolution before the temporary files are
       * deleted, if necessary. */
      switch (conflict_choice)
        {
        case svn_wc_conflict_choose_base:
          auto_resolve_src = conflict_old;
          break;
        case svn_wc_conflict_choose_mine_full:
          auto_resolve_src = conflict_working;
          break;
        case svn_wc_conflict_choose_theirs_full:
          auto_resolve_src = conflict_new;
          break;
        case svn_wc_conflict_choose_merged:
          auto_resolve_src = NULL;
          break;
        case svn_wc_conflict_choose_theirs_conflict:
        case svn_wc_conflict_choose_mine_conflict:
          {
            if (conflict_old && conflict_working && conflict_new)
              {
                const char *temp_dir;
                svn_stream_t *tmp_stream = NULL;
                svn_diff_t *diff;
                svn_diff_conflict_display_style_t style =
                  conflict_choice == svn_wc_conflict_choose_theirs_conflict
                  ? svn_diff_conflict_display_latest
                  : svn_diff_conflict_display_modified;

                SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir, db,
                                                       local_abspath,
                                                       pool, pool));
                SVN_ERR(svn_stream_open_unique(&tmp_stream,
                                               &auto_resolve_src,
                                               temp_dir,
                                               svn_io_file_del_on_pool_cleanup,
                                               pool, pool));

                SVN_ERR(svn_diff_file_diff3_2(&diff,
                                              conflict_old,
                                              conflict_working,
                                              conflict_new,
                                              svn_diff_file_options_create(pool),
                                              pool));
                SVN_ERR(svn_diff_file_output_merge2(tmp_stream, diff,
                                                    conflict_old,
                                                    conflict_working,
                                                    conflict_new,
                                                    /* markers ignored */
                                                    NULL, NULL, NULL, NULL,
                                                    style,
                                                    pool));
                SVN_ERR(svn_stream_close(tmp_stream));
              }
            else
              auto_resolve_src = NULL;
            break;
          }
        default:
          return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                  _("Invalid 'conflict_result' argument"));
        }

      if (auto_resolve_src)
        {
          SVN_ERR(svn_wc__wq_build_file_copy_translated(
                    &work_item, db, local_abspath,
                    auto_resolve_src, local_abspath, pool, pool));
          work_items = svn_wc__wq_merge(work_items, work_item, pool);
        }
    }

  if (resolve_text)
    {
      svn_node_kind_t node_kind;

      /* Legacy behavior: Only report text conflicts as resolved when at least
         one conflict marker file exists.

         If not the UI shows the conflict as already resolved
         (and in this case we just remove the in-db conflict) */

      if (conflict_old)
        {
          SVN_ERR(svn_io_check_path(conflict_old, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   conflict_old,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }

      if (conflict_new)
        {
          SVN_ERR(svn_io_check_path(conflict_new, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   conflict_new,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }

      if (conflict_working)
        {
          SVN_ERR(svn_io_check_path(conflict_working, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   conflict_working,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }
    }
  if (resolve_props)
    {
      svn_node_kind_t node_kind;

      /* Legacy behavior: Only report property conflicts as resolved when the
         property reject file exists

         If not the UI shows the conflict as already resolved
         (and in this case we just remove the in-db conflict) */

      if (prop_reject_file)
        {
          SVN_ERR(svn_io_check_path(prop_reject_file, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   prop_reject_file,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }
    }
  if (resolve_tree)
    *did_resolve = TRUE;

  if (resolve_text || resolve_props || resolve_tree)
    {
      SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath,
                                          resolve_text, resolve_props,
                                          resolve_tree, work_items, pool));

      /* Run the work queue to remove conflict marker files. */
      SVN_ERR(svn_wc__wq_run(db, local_abspath,
                             cancel_func_t, cancel_baton,
                             pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__resolve_text_conflict(svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool)
{
  svn_boolean_t ignored_result;

  return svn_error_trace(resolve_conflict_on_node(
                           &ignored_result,
                           db, local_abspath,
                           TRUE /* resolve_text */,
                           FALSE /* resolve_props */,
                           FALSE /* resolve_tree */,
                           svn_wc_conflict_choose_merged,
                           NULL, NULL, /* cancel_func */
                           scratch_pool));
}


/* Baton for conflict_status_walker */
struct conflict_status_walker_baton
{
  svn_wc__db_t *db;
  svn_boolean_t resolve_text;
  const char *resolve_prop;
  svn_boolean_t resolve_tree;
  svn_wc_conflict_choice_t conflict_choice;
  svn_wc_conflict_resolver_func2_t conflict_func;
  void *conflict_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

/* Implements svn_wc_status4_t to walk all conflicts to resolve */
static svn_error_t *
conflict_status_walker(void *baton,
                       const char *local_abspath,
                       const svn_wc_status3_t *status,
                       apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton *cswb = baton;
  svn_wc__db_t *db = cswb->db;

  const apr_array_header_t *conflicts;
  apr_pool_t *iterpool;
  int i;
  svn_boolean_t resolved = FALSE;

  if (!status->conflicted)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_wc__db_read_conflicts(&conflicts, db, local_abspath,
                                    scratch_pool, iterpool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *cd;
      svn_boolean_t did_resolve;
      svn_wc_conflict_choice_t my_choice = cswb->conflict_choice;

      cd = APR_ARRAY_IDX(conflicts, i, const svn_wc_conflict_description2_t *);

      svn_pool_clear(iterpool);

      if (my_choice == svn_wc_conflict_choose_unspecified)
        {
          svn_wc_conflict_result_t *result;

          if (!cswb->conflict_func)
            return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                    _("No conflict-callback and no "
                                      "pre-defined conflict-choice provided"));

          SVN_ERR(cswb->conflict_func(&result, cd, cswb->conflict_baton,
                                      iterpool, iterpool));

          my_choice = result->choice;
        }


      if (my_choice == svn_wc_conflict_choose_postpone)
        continue;

      switch (cd->kind)
        {
          case svn_wc_conflict_kind_tree:
            if (!cswb->resolve_tree)
              break;

            /* For now, we only clear tree conflict information and resolve
             * to the working state. There is no way to pick theirs-full
             * or mine-full, etc. Throw an error if the user expects us
             * to be smarter than we really are. */
            if (my_choice != svn_wc_conflict_choose_merged)
              {
                return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                         NULL,
                                         _("Tree conflicts can only be "
                                           "resolved to 'working' state; "
                                           "'%s' not resolved"),
                                         svn_dirent_local_style(local_abspath,
                                                                iterpool));
              }

            SVN_ERR(resolve_conflict_on_node(&did_resolve,
                                             db,
                                             local_abspath,
                                             FALSE /* resolve_text */,
                                             FALSE /* resolve_props */,
                                             TRUE /* resolve_tree */,
                                             my_choice,
                                             cswb->cancel_func,
                                             cswb->cancel_baton,
                                             iterpool));

            resolved = TRUE;
            break;

          case svn_wc_conflict_kind_text:
            if (!cswb->resolve_text)
              break;

            SVN_ERR(resolve_conflict_on_node(&did_resolve,
                                             db,
                                             local_abspath,
                                             TRUE /* resolve_text */,
                                             FALSE /* resolve_props */,
                                             FALSE /* resolve_tree */,
                                             my_choice,
                                             cswb->cancel_func,
                                             cswb->cancel_baton,
                                             iterpool));

            if (did_resolve)
              resolved = TRUE;
            break;

          case svn_wc_conflict_kind_property:
            if (!cswb->resolve_prop)
              break;

            /* ### this is bogus. resolve_conflict_on_node() does not handle
               ### individual property resolution.  */
            if (*cswb->resolve_prop != '\0' &&
                strcmp(cswb->resolve_prop, cd->property_name) != 0)
              {
                break; /* Skip this property conflict */
              }


            /* We don't have property name handling here yet :( */
            SVN_ERR(resolve_conflict_on_node(&did_resolve,
                                             db,
                                             local_abspath,
                                             FALSE /* resolve_text */,
                                             TRUE /* resolve_props */,
                                             FALSE /* resolve_tree */,
                                             my_choice,
                                             cswb->cancel_func,
                                             cswb->cancel_baton,
                                             iterpool));

            if (did_resolve)
              resolved = TRUE;
            break;

          default:
            /* We can't resolve other conflict types */
            break;
        }
    }

  /* Notify */
  if (cswb->notify_func && resolved)
    cswb->notify_func(cswb->notify_baton,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_resolved,
                                           iterpool),
                      iterpool);

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__resolve_conflicts(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  svn_kind_t kind;
  svn_boolean_t conflicted;
  struct conflict_status_walker_baton cswb;

  /* ### the underlying code does NOT support resolving individual
     ### properties. bail out if the caller tries it.  */
  if (resolve_prop != NULL && *resolve_prop != '\0')
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            U_("Resolving a single property is not (yet) "
                               "supported."));

  /* ### Just a versioned check? */
  /* Conflicted is set to allow invoking on actual only nodes */
  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, &conflicted,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  /* When the implementation still used the entry walker, depth
     unknown was translated to infinity. */
  if (kind != svn_kind_dir)
    depth = svn_depth_empty;
  else if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  cswb.db = wc_ctx->db;
  cswb.resolve_text = resolve_text;
  cswb.resolve_prop = resolve_prop;
  cswb.resolve_tree = resolve_tree;
  cswb.conflict_choice = conflict_choice;

  cswb.conflict_func = conflict_func;
  cswb.conflict_baton = conflict_baton;

  cswb.cancel_func = cancel_func;
  cswb.cancel_baton = cancel_baton;

  cswb.notify_func = notify_func;
  cswb.notify_baton = notify_baton;

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_conflict_resolver_starting,
                                    scratch_pool),
                scratch_pool);

  SVN_ERR(svn_wc_walk_status(wc_ctx,
                             local_abspath,
                             depth,
                             FALSE /* get_all */,
                             FALSE /* no_ignore */,
                             TRUE /* ignore_text_mods */,
                             NULL /* ignore_patterns */,
                             conflict_status_walker, &cswb,
                             cancel_func, cancel_baton,
                             scratch_pool));

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_conflict_resolver_done,
                                    scratch_pool),
                scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_resolved_conflict5(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__resolve_conflicts(wc_ctx, local_abspath,
                                                   depth, resolve_text,
                                                   resolve_prop, resolve_tree,
                                                   conflict_choice,
                                                   NULL, NULL,
                                                   cancel_func, cancel_baton,
                                                   notify_func, notify_baton,
                                                   scratch_pool));
}
