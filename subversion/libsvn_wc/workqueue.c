/*
 * workqueue.c :  manipulating work queue items
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_subst.h"

#include "wc.h"
#include "wc_db.h"
#include "workqueue.h"
#include "entries.h"
#include "props.h"
#include "adm_files.h"
#include "translate.h"

#include "svn_private_config.h"
#include "private/svn_skel.h"


#define NOT_IMPLEMENTED() \
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.")


/* Workqueue operation names.  */
#define OP_REVERT "revert"
#define OP_PREPARE_REVERT_FILES "prep-rev-files"


struct work_item_dispatch {
  const char *name;
  svn_error_t *(*func)(svn_wc__db_t *db,
                       const svn_skel_t *work_item,
                       apr_pool_t *scratch_pool);
};


/* Ripped from the old loggy cp_and_translate operation.

   LOCAL_ABSPATH specifies the destination of the copy (typically the
   working file).

   SOURCE_ABSPATH specifies the source which is translated for
   installation as the working file.

   VERSIONED_ABSPATH specifies the versioned file holding the properties
   which specify the translation parameters.  */
static svn_error_t *
copy_and_translate(svn_wc__db_t *db,
                   const char *source_abspath,
                   const char *dest_abspath,
                   const char *versioned_abspath,
                   apr_pool_t *scratch_pool)
{
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t special;

  SVN_ERR(svn_wc__get_eol_style(&style, &eol, db, versioned_abspath,
                                scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__get_keywords(&keywords, db, versioned_abspath, NULL,
                               scratch_pool, scratch_pool));

  /* ### eventually, we will not be called for special files...  */
  SVN_ERR(svn_wc__get_special(&special, db, versioned_abspath,
                              scratch_pool));

  SVN_ERR(svn_subst_copy_and_translate3(
            source_abspath, dest_abspath,
            eol, TRUE,
            keywords, TRUE,
            special,
            scratch_pool));

  /* ### this is a problem. DEST_ABSPATH is not necessarily versioned.  */
  SVN_ERR(svn_wc__maybe_set_read_only(NULL, db, dest_abspath,
                                      scratch_pool));
  SVN_ERR(svn_wc__maybe_set_executable(NULL, db, dest_abspath,
                                       scratch_pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
move_if_present(const char *source_abspath,
                const char *dest_abspath,
                apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_io_file_rename(source_abspath, dest_abspath, scratch_pool);
  if (err)
    {
      if (!APR_STATUS_IS_ENOENT(err->apr_err))
        return svn_error_return(err);

      /* Not there. Maybe the node was moved in a prior run.  */
      svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_REVERT  */


static svn_error_t *
run_revert(svn_wc__db_t *db,
           const svn_skel_t *work_item,
           apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_boolean_t replaced;
  svn_wc__db_kind_t kind;
  svn_node_kind_t node_kind;
  const char *conflict_old;
  const char *conflict_new;
  const char *conflict_wrk;
  const char *prop_reject_file;
  const char *working_props_path;
  const char *parent_abspath;
  apr_uint64_t modify_flags = 0;
  svn_wc_entry_t tmp_entry;

  /* We need a NUL-terminated path, so copy it out of the skel.  */
  local_abspath = apr_pstrmemdup(scratch_pool,
                                 work_item->children->next->data,
                                 work_item->children->next->len);
  /* ### fix this code. validate.  */
  replaced = work_item->children->next->next->data[0] - '0';
  /* magic_changed is extracted further below.  */
  /* use_commit_times is extracted further below.  */

  /* NOTE: we can read KIND here since uncommitted kind changes are not
     (yet) allowed. If we read any conflict files, then we (obviously) have
     not removed them from the metadata (yet).  */
  SVN_ERR(svn_wc__db_read_info(
            NULL, &kind, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            &conflict_old, &conflict_new, &conflict_wrk, &prop_reject_file,
            NULL,
            db, local_abspath,
            scratch_pool, scratch_pool));

  /* Move the "revert" props over/on the "base" props.  */
  if (replaced)
    {
      const char *revert_props_path;
      const char *base_props_path;

      SVN_ERR(svn_wc__prop_path(&revert_props_path, local_abspath,
                                kind, svn_wc__props_revert, scratch_pool));
      SVN_ERR(svn_wc__prop_path(&base_props_path, local_abspath,
                                kind, svn_wc__props_base, scratch_pool));

      SVN_ERR(move_if_present(revert_props_path, base_props_path,
                              scratch_pool));
    }

  /* The "working" props contain changes. Nuke 'em from orbit.  */
  SVN_ERR(svn_wc__prop_path(&working_props_path, local_abspath,
                            kind, svn_wc__props_working, scratch_pool));
  SVN_ERR(svn_io_remove_file2(working_props_path, TRUE, scratch_pool));

  /* Deal with the working file, as needed.  */
  if (kind == svn_wc__db_kind_file)
    {
      svn_boolean_t magic_changed;
      svn_boolean_t reinstall_working;
      const char *text_base_path;

      SVN_ERR(svn_wc__text_base_path(&text_base_path, db, local_abspath,
                                     FALSE, scratch_pool));

      magic_changed = work_item->children->next->next->next->data[0] - '0';

      /* If there was a magic property change, then we'll reinstall the
         working-file to pick up any/all appropriate changes. If there was
         a replacement, then we definitely want to reinstall the working-file
         using the original base.  */
      reinstall_working = magic_changed || replaced;

      if (replaced)
        {
          const char *revert_base_path;
          svn_checksum_t *checksum;

          SVN_ERR(svn_wc__text_revert_path(&revert_base_path, db,
                                           local_abspath, scratch_pool));
          SVN_ERR(move_if_present(revert_base_path, text_base_path,
                                  scratch_pool));

          /* At this point, the regular text base has been restored (just
             now, or on a prior run). We need to recompute the checksum
             from that.

             ### in wc-1, this recompute only happened for add-with-history.
             ### need to investigate, but maybe the checksum was not touched
             ### for a simple replacing add? regardless, this recompute is
             ### always okay to do.  */
          SVN_ERR(svn_io_file_checksum2(&checksum, text_base_path,
                                        svn_checksum_md5, scratch_pool));
          tmp_entry.checksum = svn_checksum_to_cstring(checksum, scratch_pool);
          modify_flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
        }
      else if (!reinstall_working)
        {
          svn_node_kind_t check_kind;

          /* If the working file is missing, we need to reinstall it.  */
          SVN_ERR(svn_io_check_path(local_abspath, &check_kind,
                                    scratch_pool));
          reinstall_working = (check_kind == svn_node_none);

          if (!reinstall_working)
            {
              /* ### can we optimize this call? we already fetched some
                 ### info about the node. and *definitely* never want a
                 ### full file-scan.  */

              /* ### for now, just always reinstall. without some extra work,
                 ### we could end up in a situation where the file is copied
                 ### from the base, but then something fails immediately
                 ### after that. on the second time through here, we would
                 ### see the file is "the same" and fail to complete those
                 ### follow-on actions. in some future work, examine the
                 ### points of failure, and possibly precompue the
                 ### "reinstall_working" flag, or maybe do some follow-on
                 ### actions unconditionally.  */
#if 1
              reinstall_working = TRUE;
#endif
#if 0
              SVN_ERR(svn_wc__text_modified_internal_p(&reinstall_working,
                                                       db, local_abspath,
                                                       FALSE, FALSE,
                                                       scratch_pool));
#endif
            }
        }

      if (reinstall_working)
        {
          svn_boolean_t use_commit_times;
          apr_finfo_t finfo;

          /* Copy from the text base to the working file. The working file
             specifies the params for translation.  */
          SVN_ERR(copy_and_translate(db, text_base_path, local_abspath,
                                     local_abspath, scratch_pool));

          use_commit_times = (work_item->children->next->next->next
                              ->next->data[0] - '0');

          /* Possibly set the timestamp to last-commit-time, rather
             than the 'now' time that already exists. */
          if (use_commit_times)
            {
              apr_time_t changed_date;

              /* Note: OP_REVERT is not used for a pure addition. There will
                 always be a BASE node.  */
              SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               NULL, &changed_date, NULL,
                                               NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               db, local_abspath,
                                               scratch_pool, scratch_pool));
              if (changed_date)
                {
                  svn_boolean_t special;

                  /* ### skip this test once db_kind_symlink is in use.  */
                  SVN_ERR(svn_wc__get_special(&special, db, local_abspath,
                                              scratch_pool));
                  if (!special)
                    SVN_ERR(svn_io_set_file_affected_time(changed_date,
                                                          local_abspath,
                                                          scratch_pool));
                }
            }

          /* loggy_set_entry_timestamp_from_wc()  */
          SVN_ERR(svn_io_file_affected_time(&tmp_entry.text_time,
                                            local_abspath,
                                            scratch_pool));
          modify_flags |= SVN_WC__ENTRY_MODIFY_TEXT_TIME;

          /* loggy_set_entry_working_size_from_wc()  */
          SVN_ERR(svn_io_stat(&finfo, local_abspath,
                              APR_FINFO_MIN | APR_FINFO_LINK,
                              scratch_pool));
          tmp_entry.working_size = finfo.size;
          modify_flags |= SVN_WC__ENTRY_MODIFY_WORKING_SIZE;
        }
    }
  else if (kind == svn_wc__db_kind_symlink)
    {
      NOT_IMPLEMENTED();
    }

  if (kind == svn_wc__db_kind_dir)
    parent_abspath = local_abspath;
  else
    parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  /* ### in wc-ng: the following four blocks clear ACTUAL_NODE.  */
  if (conflict_old != NULL)
    {
      SVN_ERR(svn_io_remove_file2(svn_dirent_join(parent_abspath,
                                                  conflict_old,
                                                  scratch_pool),
                                  TRUE, scratch_pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;
      tmp_entry.conflict_old = NULL;
    }
  if (conflict_new != NULL)
    {
      SVN_ERR(svn_io_remove_file2(svn_dirent_join(parent_abspath,
                                                  conflict_new,
                                                  scratch_pool),
                                  TRUE, scratch_pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;
      tmp_entry.conflict_new = NULL;
    }
  if (conflict_wrk != NULL)
    {
      SVN_ERR(svn_io_remove_file2(svn_dirent_join(parent_abspath,
                                                  conflict_wrk,
                                                  scratch_pool),
                                  TRUE, scratch_pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
      tmp_entry.conflict_wrk = NULL;
    }
  if (prop_reject_file != NULL)
    {
      SVN_ERR(svn_io_remove_file2(svn_dirent_join(parent_abspath,
                                                  prop_reject_file,
                                                  scratch_pool),
                                  TRUE, scratch_pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;
      tmp_entry.prejfile = NULL;
    }

  /* Clean up the copied state for all replacements.  */
  if (replaced)
    {
      modify_flags |= (SVN_WC__ENTRY_MODIFY_COPIED
                       | SVN_WC__ENTRY_MODIFY_COPYFROM_URL
                       | SVN_WC__ENTRY_MODIFY_COPYFROM_REV);
      tmp_entry.copied = FALSE;
      tmp_entry.copyfrom_url = NULL;
      tmp_entry.copyfrom_rev = SVN_INVALID_REVNUM;
    }

  /* Reset schedule attribute to svn_wc_schedule_normal. It could already be
     "normal", but no biggy if this is a no-op.  */
  modify_flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
  tmp_entry.schedule = svn_wc_schedule_normal;

  /* We need the old school KIND...  */
  if (kind == svn_wc__db_kind_dir)
    {
      node_kind = svn_node_dir;
    }
  else
    {
      SVN_ERR_ASSERT(kind == svn_wc__db_kind_file
                     || kind == svn_wc__db_kind_symlink);
      node_kind = svn_node_file;
    }

  SVN_ERR(svn_wc__entry_modify2(db, local_abspath, node_kind, FALSE,
                                &tmp_entry, modify_flags,
                                scratch_pool));

  /* ### need to revert some bits in the parent stub. sigh.  */
  if (kind == svn_wc__db_kind_dir)
    {
      svn_boolean_t is_wc_root;

      /* There is no parent stub if we're at the root.  */
      SVN_ERR(svn_wc__check_wc_root(&is_wc_root, NULL, db, local_abspath,
                                    scratch_pool));
      if (!is_wc_root)
        {
          modify_flags = (SVN_WC__ENTRY_MODIFY_COPIED
                          | SVN_WC__ENTRY_MODIFY_COPYFROM_URL
                          | SVN_WC__ENTRY_MODIFY_COPYFROM_REV
                          | SVN_WC__ENTRY_MODIFY_SCHEDULE);
          tmp_entry.copied = FALSE;
          tmp_entry.copyfrom_url = NULL;
          tmp_entry.copyfrom_rev = SVN_INVALID_REVNUM;
          tmp_entry.schedule = svn_wc_schedule_normal;
          SVN_ERR(svn_wc__entry_modify2(db, local_abspath, svn_node_dir, TRUE,
                                        &tmp_entry, modify_flags,
                                        scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


/* For issue #2101, we need to deliver this error. When the wc-ng pristine
   handling comes into play, the issue should be fixed, and this code can
   go away.  */
static svn_error_t *
verify_pristine_present(svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_pool_t *scratch_pool)
{
  const char *base_abspath;
  svn_node_kind_t check_kind;

  /* Verify that one of the two text bases are present.  */
  SVN_ERR(svn_wc__text_base_path(&base_abspath, db, local_abspath, FALSE,
                                 scratch_pool));
  SVN_ERR(svn_io_check_path(base_abspath, &check_kind, scratch_pool));
  if (check_kind == svn_node_file)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__text_revert_path(&base_abspath, db, local_abspath,
                                   scratch_pool));
  SVN_ERR(svn_io_check_path(base_abspath, &check_kind, scratch_pool));
  if (check_kind == svn_node_file)
    return SVN_NO_ERROR;

  /* A real file must have either a regular or a revert text-base.
     If it has neither, we could be looking at the situation described
     in issue #2101, in which case all we can do is deliver the expected
     error.  */
  return svn_error_createf(APR_ENOENT, NULL,
                           _("Error restoring text for '%s'"),
                           svn_dirent_local_style(local_abspath,
                                                  scratch_pool));
}


/* Record a work item to revert LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_add_revert(svn_boolean_t *will_revert,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      svn_boolean_t use_commit_times,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  svn_boolean_t replaced;
  svn_boolean_t magic_changed = FALSE;

  SVN_ERR(svn_wc__db_read_info(
            &status, &kind, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL,
            db, local_abspath,
            scratch_pool, scratch_pool));

  /* Special handling for issue #2101.  */
  if (kind == svn_wc__db_kind_file)
    SVN_ERR(verify_pristine_present(db, local_abspath, scratch_pool));

  /* Gather a few items *before* the revert work-item has a chance to run.
     During its operation, this data could/will change, which means that a
     potential re-run of the work-item may gather incorrect values.  */

  SVN_ERR(svn_wc__internal_is_replaced(&replaced, db, local_abspath,
                                       scratch_pool));

  /* If a replacement has occurred, then a revert definitely happens.  */
  *will_revert = replaced;

  if (!replaced)
    {
      apr_hash_t *base_props;
      apr_hash_t *working_props;
      apr_array_header_t *prop_diffs;

      SVN_ERR(svn_wc__load_props(&base_props, &working_props, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool));
      SVN_ERR(svn_prop_diffs(&prop_diffs, working_props, base_props,
                             scratch_pool));
      magic_changed = svn_wc__has_magic_property(prop_diffs);

      if (prop_diffs->nelts > 0)
        {
          /* Property changes cause a revert to occur.  */
          *will_revert = TRUE;
        }
      else
        {
          /* There is nothing to do for NORMAL or ADDED nodes. Typically,
             we won't even be called for added nodes (since a revert
             simply removes it from version control), but it is possible
             that a parent replacement was turned from a replaced copy
             into a normal node, and the (broken) old ENTRY->COPIED logic
             then turns the copied children into typical ADDED nodes.
             Since the recursion has already started, these children are
             visited (unlike most added nodes).  */
          if (status != svn_wc__db_status_normal
              && status != svn_wc__db_status_added)
            {
              *will_revert = TRUE;
            }

          /* We may need to restore a missing working file.  */
          if (! *will_revert)
            {
              svn_node_kind_t on_disk;

              SVN_ERR(svn_io_check_path(local_abspath, &on_disk,
                                        scratch_pool));
              *will_revert = on_disk == svn_node_none;
            }

          if (! *will_revert)
            {
              /* ### there may be ways to simplify this test, rather than
                 ### doing file comparisons and junk... */
              SVN_ERR(svn_wc__text_modified_internal_p(will_revert,
                                                       db, local_abspath,
                                                       FALSE, FALSE,
                                                       scratch_pool));
            }
        }
    }

  /* Don't even bother to queue a work item if there is nothing to do.  */
  if (*will_revert)
    {
      svn_skel_t *work_item;
      char bools[3] = {
        replaced + '0',
        magic_changed + '0',
        use_commit_times + '0'
      };

      work_item = svn_skel__make_empty_list(scratch_pool);

      /* These skel atoms hold references to very transitory state, but
         we only need the work_item to survive for the duration of wq_add.  */
      svn_skel__prepend(svn_skel__mem_atom(&bools[2], 1, scratch_pool),
                        work_item);
      svn_skel__prepend(svn_skel__mem_atom(&bools[1], 1, scratch_pool),
                        work_item);
      svn_skel__prepend(svn_skel__mem_atom(&bools[0], 1, scratch_pool),
                        work_item);
      svn_skel__prepend(svn_skel__str_atom(local_abspath, scratch_pool),
                        work_item);
      svn_skel__prepend(svn_skel__str_atom(OP_REVERT, scratch_pool),
                        work_item);

      SVN_ERR(svn_wc__db_wq_add(db, local_abspath, work_item, scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_PREPARE_REVERT_FILES  */


static svn_error_t *
run_prepare_revert_files(svn_wc__db_t *db,
                         const svn_skel_t *work_item,
                         apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_wc__db_kind_t kind;
  const char *revert_prop_abspath;
  const char *base_prop_abspath;
  svn_node_kind_t on_disk;

  /* We need a NUL-terminated path, so copy it out of the skel.  */
  local_abspath = apr_pstrmemdup(scratch_pool,
                                 work_item->children->next->data,
                                 work_item->children->next->len);

  /* Rename the original text base over to the revert text base.  */
  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, scratch_pool));
  if (kind == svn_wc__db_kind_file)
    {
      const char *text_base;
      const char *text_revert;

      SVN_ERR(svn_wc__text_base_path(&text_base, db, local_abspath, FALSE,
                                     scratch_pool));
      SVN_ERR(svn_wc__text_revert_path(&text_revert, db, local_abspath,
                                       scratch_pool));

      SVN_ERR(move_if_present(text_base, text_revert, scratch_pool));
    }

  /* Set up the revert props.  */

  SVN_ERR(svn_wc__prop_path(&revert_prop_abspath, local_abspath, kind,
                            svn_wc__props_revert, scratch_pool));
  SVN_ERR(svn_wc__prop_path(&base_prop_abspath, local_abspath, kind,
                            svn_wc__props_base, scratch_pool));

  /* First: try to move any base properties to the revert location.  */
  SVN_ERR(move_if_present(base_prop_abspath, revert_prop_abspath,
                          scratch_pool));

  /* If no props exist at the revert location, then drop a set of empty
     props there. They are expected to be present.  */
  SVN_ERR(svn_io_check_path(revert_prop_abspath, &on_disk, scratch_pool));
  if (on_disk == svn_node_none)
    {
      SVN_ERR(svn_wc__write_properties(
                apr_hash_make(scratch_pool), revert_prop_abspath,
                NULL, NULL,
                scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_prepare_revert_files(svn_wc__db_t *db,
                                const char *local_abspath,
                                apr_pool_t *scratch_pool)
{
  svn_skel_t *work_item = svn_skel__make_empty_list(scratch_pool);

  /* These skel atoms hold references to very transitory state, but
     we only need the work_item to survive for the duration of wq_add.  */
  svn_skel__prepend(svn_skel__str_atom(local_abspath, scratch_pool),
                    work_item);
  svn_skel__prepend(svn_skel__str_atom(OP_PREPARE_REVERT_FILES, scratch_pool),
                    work_item);

  SVN_ERR(svn_wc__db_wq_add(db, local_abspath, work_item, scratch_pool));

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

static const struct work_item_dispatch dispatch_table[] = {
  { OP_REVERT, run_revert },
  { OP_PREPARE_REVERT_FILES, run_prepare_revert_files },

  /* Sentinel.  */
  { NULL }
};


svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *local_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  while (TRUE)
    {
      apr_uint64_t id;
      svn_skel_t *work_item;
      const struct work_item_dispatch *scan;

      /* Stop work queue processing, if requested. A future 'svn cleanup'
         should be able to continue the processing.  */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc__db_wq_fetch(&id, &work_item, db, local_abspath,
                                  iterpool, iterpool));
      if (work_item == NULL)
        {
          svn_pool_destroy(iterpool);
          return SVN_NO_ERROR;
        }

      /* Scan the dispatch table for a function to handle this work item.  */
      for (scan = &dispatch_table[0]; scan->name != NULL; ++scan)
        {
          if (svn_skel__matches_atom(work_item->children, scan->name))
            {
              SVN_ERR((*scan->func)(db, work_item, iterpool));
              break;
            }
        }

      if (scan->name == NULL)
        {
          /* We should know about ALL possible work items here. If we do not,
             then something is wrong. Most likely, some kind of format/code
             skew. There is nothing more we can do. Erasing or ignoring this
             work item could leave the WC in an even more broken state.

             Contrary to issue #1581, we cannot simply remove work items and
             continue, so bail out with an error.  */
          return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, NULL,
                                   _("Unrecognized work item in the queue "
                                     "associated with '%s'"),
                                   svn_dirent_local_style(local_abspath,
                                                          iterpool));
        }

      SVN_ERR(svn_wc__db_wq_completed(db, local_abspath, id, iterpool));
    }

  /* NOTREACHED */
}
