/*
 * workqueue.c :  manipulating work queue items
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_subst.h"
#include "svn_hash.h"
#include "svn_io.h"

#include "wc.h"
#include "wc_db.h"
#include "workqueue.h"
#include "adm_files.h"
#include "translate.h"

#include "svn_private_config.h"
#include "private/svn_skel.h"


/* Workqueue operation names.  */
#define OP_BASE_REMOVE "base-remove"
#define OP_FILE_COMMIT "file-commit"
#define OP_FILE_INSTALL "file-install"
#define OP_FILE_REMOVE "file-remove"
#define OP_FILE_MOVE "file-move"
#define OP_FILE_COPY_TRANSLATED "file-translate"
#define OP_SYNC_FILE_FLAGS "sync-file-flags"
#define OP_PREJ_INSTALL "prej-install"
#define OP_RECORD_FILEINFO "record-fileinfo"
#define OP_TMP_SET_TEXT_CONFLICT_MARKERS "tmp-set-text-conflict-markers"
#define OP_TMP_SET_PROPERTY_CONFLICT_MARKER "tmp-set-property-conflict-marker"
#define OP_POSTUPGRADE "postupgrade"

/* To be removed */
#define OP_DELETION_POSTCOMMIT "deletion-postcommit"
#define OP_POSTCOMMIT "postcommit"

/* For work queue debugging. Generates output about its operation.  */
/* #define DEBUG_WORK_QUEUE */


struct work_item_dispatch {
  const char *name;
  svn_error_t *(*func)(svn_wc__db_t *db,
                       const svn_skel_t *work_item,
                       const char *wri_abspath,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool);
};


/* ### forward declaration for this. Temporary hack so that a work item
   ### can be constructed within another handler and dispatched
   ### immediately. in most normal cases, appending a work item to the
   ### queue should be fine. but for now... not so much. */
static svn_error_t *
dispatch_work_item(svn_wc__db_t *db,
                   const char *wri_abspath,
                   const svn_skel_t *work_item,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool);


static svn_error_t *
sync_file_flags(svn_wc__db_t *db,
                const char *local_abspath,
                apr_pool_t *scratch_pool)
{
  svn_boolean_t did_set;

  SVN_ERR(svn_wc__maybe_set_read_only(&did_set, db, local_abspath,
                                      scratch_pool));
  if (!did_set)
    SVN_ERR(svn_io_set_file_read_write(local_abspath, FALSE, scratch_pool));

  SVN_ERR(svn_wc__maybe_set_executable(&did_set, db, local_abspath,
                                       scratch_pool));

  if (!did_set)
    SVN_ERR(svn_io_set_file_executable(local_abspath, FALSE, FALSE,
                                       scratch_pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
get_and_record_fileinfo(svn_wc__db_t *db,
                        const char *local_abspath,
                        svn_boolean_t ignore_enoent,
                        apr_pool_t *scratch_pool)
{
  const svn_io_dirent2_t *dirent;

  SVN_ERR(svn_io_stat_dirent(&dirent, local_abspath, ignore_enoent,
                             scratch_pool, scratch_pool));

  if (dirent->kind == svn_node_none)
    {
      /* Skip file not found if ignore_enoent */
      return SVN_NO_ERROR;
    }

  return svn_error_return(svn_wc__db_global_record_fileinfo(
                            db, local_abspath,
                            dirent->filesize, dirent->mtime,
                            scratch_pool));
}


/* ------------------------------------------------------------------------ */
/* OP_REMOVE_BASE  */

/* Removes a BASE_NODE and all it's data, leaving any adds and copies as is.
   Do this as a depth first traversal to make sure than any parent still exists
   on error conditions.

   ### This function needs review for 4th tree behavior.*/
static svn_error_t *
remove_base_node(svn_wc__db_t *db,
                 const char *local_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t base_status, wrk_status;
  svn_wc__db_kind_t base_kind, wrk_kind;
  svn_boolean_t have_base, have_work;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(svn_wc__db_read_info(&wrk_status, &wrk_kind, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL,
                               &have_base, NULL, &have_work,
                               db, local_abspath, scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(have_base); /* Verified in caller and _base_get_children() */

  if (wrk_status == svn_wc__db_status_normal
      || wrk_status == svn_wc__db_status_not_present
      || wrk_status == svn_wc__db_status_absent)
    {
      base_status = wrk_status;
      base_kind = wrk_kind;
    }
  else
    SVN_ERR(svn_wc__db_base_get_info(&base_status, &base_kind, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     db, local_abspath,
                                     scratch_pool, scratch_pool));

  /* Children first */
  if (base_kind == svn_wc__db_kind_dir
      && base_status == svn_wc__db_status_normal)
    {
      const apr_array_header_t *children;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      SVN_ERR(svn_wc__db_base_get_children(&children, db, local_abspath,
                                           scratch_pool, iterpool));

      for (i = 0; i < children->nelts; i++)
        {
          const char *child_name = APR_ARRAY_IDX(children, i, const char *);
          const char *child_abspath;

          svn_pool_clear(iterpool);

          child_abspath = svn_dirent_join(local_abspath, child_name, iterpool);

          SVN_ERR(remove_base_node(db, child_abspath, cancel_func, cancel_baton,
                                   iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  if (base_status == svn_wc__db_status_normal
      && wrk_status != svn_wc__db_status_added
      && wrk_status != svn_wc__db_status_excluded)
    {
      if (wrk_status != svn_wc__db_status_deleted
          && (base_kind == svn_wc__db_kind_file
              || base_kind == svn_wc__db_kind_symlink))
        {
          SVN_ERR(svn_io_remove_file2(local_abspath, TRUE, scratch_pool));
        }
      else if (base_kind == svn_wc__db_kind_dir
               && wrk_status != svn_wc__db_status_deleted)
        {
          svn_error_t *err = svn_io_dir_remove_nonrecursive(local_abspath,
                                                            scratch_pool);

          if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
                      || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)
                      || APR_STATUS_IS_ENOTEMPTY(err->apr_err)))
            svn_error_clear(err);
          else
            SVN_ERR(err);
        }
    }

  SVN_ERR(svn_wc__db_base_remove(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

/* Process the OP_REMOVE_BASE work item WORK_ITEM.
 * See svn_wc__wq_build_remove_base() which generates this work item.
 * Implements (struct work_item_dispatch).func. */

static svn_error_t *
run_base_remove(svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  svn_boolean_t keep_not_present;
  svn_revnum_t revision;
  const char *repos_relpath, *repos_root_url, *repos_uuid;
  svn_wc__db_kind_t kind;
  apr_int64_t val;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));
  SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));
  keep_not_present = (val != 0);

  if (keep_not_present)
    {
      SVN_ERR(svn_wc__db_base_get_info(NULL, &kind, &revision, &repos_relpath,
                                       &repos_root_url, &repos_uuid, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL,
                                       db, local_abspath,
                                       scratch_pool, scratch_pool));
    }

  SVN_ERR(remove_base_node(db, local_abspath,
                           cancel_func, cancel_baton,
                           scratch_pool));

  if (keep_not_present)
    {
      SVN_ERR(svn_wc__db_base_add_not_present_node(db, local_abspath,
                                                   repos_relpath,
                                                   repos_root_url,
                                                   repos_uuid,
                                                   revision,
                                                   kind,
                                                   NULL,
                                                   NULL,
                                                   scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__wq_build_base_remove(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_boolean_t keep_not_present,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_int(keep_not_present, *work_item, result_pool);
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_BASE_REMOVE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_DELETION_POSTCOMMIT  */

/* Process the OP_DELETION_POSTCOMMIT work item WORK_ITEM.
 * See svn_wc__wq_add_deletion_postcommit() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_deletion_postcommit(svn_wc__db_t *db,
                        const svn_skel_t *work_item,
                        const char *wri_abspath,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_abspath;
  svn_revnum_t new_revision;
  svn_boolean_t no_unlock;
  svn_wc__db_kind_t kind;
  apr_int64_t val;

  /* ### warning: this code has not been vetted for running multiple times  */

  /* We need a NUL-terminated path, so copy it out of the skel.  */
  local_abspath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));
  new_revision = (svn_revnum_t)val;
  SVN_ERR(svn_skel__parse_int(&val, arg1->next->next, scratch_pool));
  no_unlock = (val != 0);

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, scratch_pool));

  /* ### the section below was ripped out of log.c::log_do_committed().
     ### it needs to be rewritten into wc-ng terms.  */

    {
      const char *repos_relpath;
      const char *repos_root_url;
      const char *repos_uuid;
      svn_revnum_t parent_revision;

      /* Get hold of repository info, if we are going to need it,
         before deleting the file, */
      SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, &parent_revision, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL,
                                       db, svn_dirent_dirname(local_abspath,
                                                              scratch_pool),
                                       scratch_pool, scratch_pool));
      if (new_revision > parent_revision)
        SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath, &repos_root_url,
                                           &repos_uuid, db, local_abspath,
                                           scratch_pool, scratch_pool));

      /* We're deleting a file, and we can safely remove files from
         revision control without screwing something else up.  */
      SVN_ERR(svn_wc__internal_remove_from_revision_control(
                db, local_abspath,
                FALSE, FALSE, cancel_func, cancel_baton, scratch_pool));

      /* If the parent entry's working rev 'lags' behind new_rev...
         ### Maybe we should also add a not-present node if the
         ### deleted node was switched? */
      if (new_revision > parent_revision)
        {
          /* ...then the parent's revision is now officially a
             lie;  therefore, it must remember the file as being
             'deleted' for a while.  Create a new, uninteresting
             ghost entry:  */
          SVN_ERR(svn_wc__db_base_add_not_present_node(
                    db, local_abspath,
                    repos_relpath, repos_root_url, repos_uuid,
                    new_revision, kind,
                    NULL, NULL,
                    scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_POSTCOMMIT  */


/* FILE_ABSPATH is the new text base of the newly-committed versioned file,
 * in repository-normal form (aka "detranslated" form).  Adjust the working
 * file accordingly.
 *
 * If eol and/or keyword translation would cause the working file to
 * change, then overwrite the working file with a translated copy of
 * the new text base (but only if the translated copy differs from the
 * current working file -- if they are the same, do nothing, to avoid
 * clobbering timestamps unnecessarily).
 *
 * Set the working file's executability according to its svn:executable
 * property, or, if REMOVE_EXECUTABLE is TRUE, set it to not executable.
 *
 * Set the working file's read-only attribute according to its properties
 * and lock status (see svn_wc__maybe_set_read_only()), or, if
 * REMOVE_READ_ONLY is TRUE, set it to writable.
 *
 * If the working file was re-translated or had its executability or
 * read-only state changed,
 * then set OVERWROTE_WORKING to TRUE.  If the working file isn't
 * touched at all, then set to FALSE.
 *
 * Use SCRATCH_POOL for any temporary allocation.
 */
static svn_error_t *
install_committed_file(svn_boolean_t *overwrote_working,
                       svn_wc__db_t *db,
                       const char *file_abspath,
                       svn_boolean_t remove_executable,
                       svn_boolean_t remove_read_only,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t same, did_set;
  const char *tmp_wfile;
  svn_boolean_t special;

  /* start off assuming that the working file isn't touched. */
  *overwrote_working = FALSE;

  /* In the commit, newlines and keywords may have been
   * canonicalized and/or contracted... Or they may not have
   * been.  It's kind of hard to know.  Here's how we find out:
   *
   *    1. Make a translated tmp copy of the committed text base,
   *       translated according to the versioned file's properties.
   *       Or, if no committed text base exists (the commit must have
   *       been a propchange only), make a translated tmp copy of the
   *       working file.
   *    2. Compare the translated tmpfile to the working file.
   *    3. If different, copy the tmpfile over working file.
   *
   * This means we only rewrite the working file if we absolutely
   * have to, which is good because it avoids changing the file's
   * timestamp unless necessary, so editors aren't tempted to
   * reread the file if they don't really need to.
   */

  /* Copy and translate the new base-to-be file (if found, else the working
   * file) from repository-normal form to working form, writing a new
   * temporary file if any translation was actually done.  Set TMP_WFILE to
   * the translated file's path, which may be the source file's path if no
   * translation was done.  Set SAME to indicate whether the new working
   * text is the same as the old working text (or TRUE if it's a special
   * file). */
  {
    const char *tmp = file_abspath;

    /* Copy and translate, if necessary. The output file will be deleted at
     * scratch_pool cleanup.
     * ### That's not quite safe: we might rename the file and then maybe
     * its path will get re-used for another temp file before pool clean-up.
     * Instead, we should take responsibility for deleting it. */
    SVN_ERR(svn_wc__internal_translated_file(&tmp_wfile, tmp, db,
                                             file_abspath,
                                             SVN_WC_TRANSLATE_FROM_NF,
                                             cancel_func, cancel_baton,
                                             scratch_pool, scratch_pool));

    /* If the translation is a no-op, the text base and the working copy
     * file contain the same content, because we use the same props here
     * as were used to detranslate from working file to text base.
     *
     * In that case: don't replace the working file, but make sure
     * it has the right executable and read_write attributes set.
     */

    SVN_ERR(svn_wc__get_translate_info(NULL, NULL,
                                       NULL,
                                       &special,
                                       db, file_abspath, NULL,
                                       scratch_pool, scratch_pool));
    /* ### Should this be a strcmp()? */
    if (! special && tmp != tmp_wfile)
      SVN_ERR(svn_io_files_contents_same_p(&same, tmp_wfile,
                                           file_abspath, scratch_pool));
    else
      same = TRUE;
  }

  if (! same)
    {
      SVN_ERR(svn_io_file_rename(tmp_wfile, file_abspath, scratch_pool));
      *overwrote_working = TRUE;
    }

  /* ### should be using OP_SYNC_FILE_FLAGS, or an internal version of
     ### that here. do we need to set *OVERWROTE_WORKING?  */

  if (remove_executable)
    {
      /* No need to chmod -x on a new file: new files don't have it. */
      if (same)
        SVN_ERR(svn_io_set_file_executable(file_abspath,
                                           FALSE, /* chmod -x */
                                           FALSE, scratch_pool));
      /* ### We should avoid setting 'overwrote_working' here if we didn't
       * change the executability. */
      *overwrote_working = TRUE; /* entry needs wc-file's timestamp  */
    }
  else
    {
      /* Set the working file's execute bit if props dictate. */
      SVN_ERR(svn_wc__maybe_set_executable(&did_set, db, file_abspath,
                                           scratch_pool));
      if (did_set)
        /* okay, so we didn't -overwrite- the working file, but we changed
           its timestamp, which is the point of returning this flag. :-) */
        *overwrote_working = TRUE;
    }

  if (remove_read_only)
    {
      /* No need to make a new file read_write: new files already are. */
      if (same)
        SVN_ERR(svn_io_set_file_read_write(file_abspath, FALSE,
                                           scratch_pool));
      /* ### We should avoid setting 'overwrote_working' here if we didn't
       * change the read-only-ness. */
      *overwrote_working = TRUE; /* entry needs wc-file's timestamp  */
    }
  else
    {
      SVN_ERR(svn_wc__maybe_set_read_only(&did_set, db, file_abspath,
                                          scratch_pool));
      if (did_set)
        /* okay, so we didn't -overwrite- the working file, but we changed
           its timestamp, which is the point of returning this flag. :-) */
        *overwrote_working = TRUE;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
process_commit_file_install(svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_boolean_t remove_executable,
                       svn_boolean_t set_read_write,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t overwrote_working;

  /* Install the new file, which may involve expanding keywords.
     A copy of this file should have been dropped into our `tmp/text-base'
     directory during the commit process.  Part of this process
     involves recording the textual timestamp for this entry.  We'd like
     to just use the timestamp of the working file, but it is possible
     that at some point during the commit, the real working file might
     have changed again. 
   */

  SVN_ERR(install_committed_file(&overwrote_working, db,
                                 local_abspath,
                                 remove_executable, set_read_write,
                                 cancel_func, cancel_baton,
                                 scratch_pool));

  /* We will compute and modify the size and timestamp */
  if (overwrote_working)
    {
      apr_finfo_t finfo;

      SVN_ERR(svn_io_stat(&finfo, local_abspath,
                          APR_FINFO_MIN | APR_FINFO_LINK, scratch_pool));
      SVN_ERR(svn_wc__db_global_record_fileinfo(db, local_abspath,
                                                finfo.size, finfo.mtime,
                                                scratch_pool));
    }
  else
    {
      svn_boolean_t modified;

      /* The working copy file hasn't been overwritten.  We just
         removed the recorded size and modification time from the nodes
         record by calling svn_wc__db_global_commit().

         Now we have some file in our working copy that might be what
         we just committed, but we are not certain at this point.

         We still have a write lock here, so we check if the file is
         what we expect it to be and if it is the right file we update
         the recorded information. (If it isn't we keep the null data).

         Instead of reimplementing all this here, we just call a function
         that already does implement this when it notices that we have the
         right kind of lock (and we ignore the result)
       */
      SVN_ERR(svn_wc__internal_file_modified_p(&modified, NULL, NULL,
                                               db, local_abspath,
                                               FALSE, FALSE, scratch_pool));
    }
  return SVN_NO_ERROR;
}

/* Set the base version of the node LOCAL_ABSPATH to be the same as its
 * working version currently is:
 *
 * - Remove children deleted as part of a replacement (if it's a dir)
 * - Install the new base props
 * - Install the new tree state
 * - Install the new base text (if it's a file)
 * - Adjust the parent (if it's a dir)
 * */
static svn_error_t *
log_do_committed(svn_wc__db_t *db,
                 const char *local_abspath,
                 svn_revnum_t new_revision,
                 svn_revnum_t changed_rev,
                 apr_time_t changed_date,
                 const char *changed_author,
                 const svn_checksum_t *new_checksum,
                 apr_hash_t *new_dav_cache,
                 svn_boolean_t keep_changelist,
                 svn_boolean_t no_unlock,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  apr_pool_t *pool = scratch_pool;
  svn_wc__db_kind_t kind;
  svn_wc__db_status_t status;
  svn_boolean_t prop_mods;

  /* ### this gets the *intended* kind. for now, this also matches any
     ### potential BASE kind since we cannot change kinds.  */
  SVN_ERR(svn_wc__db_read_info(
            &status, &kind, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, &prop_mods,
            NULL, NULL, NULL,
            db, local_abspath,
            scratch_pool, scratch_pool));

  /* We should never be running a commit on a not-present node. If we see
     this, then it (probably) means that a prior run has deleted this node,
     and left the not-present behind. There isn't anything more to do.  */
  if (status == svn_wc__db_status_not_present)
    return SVN_NO_ERROR;

  /* We shouldn't be in this function for deleted nodes. They are handled
     by other processes.  */
  SVN_ERR_ASSERT(status != svn_wc__db_status_deleted);

  /*** Mark the committed item committed-to-date ***/

  /* If it's a file, install the tree changes and the file's text. */
  if (kind == svn_wc__db_kind_file
      || kind == svn_wc__db_kind_symlink)
    {
      svn_skel_t *work_item;

      SVN_ERR(svn_wc__wq_build_file_commit(&work_item,
                                           db, local_abspath,
                                           prop_mods,
                                           pool, pool));

      SVN_ERR(svn_wc__db_global_commit(db, local_abspath,
                                       new_revision, changed_rev,
                                       changed_date, changed_author,
                                       new_checksum,
                                       NULL /* new_children */,
                                       new_dav_cache,
                                       keep_changelist,
                                       no_unlock,
                                       work_item,
                                       pool));

      return SVN_NO_ERROR;
    }

  /* It's not a file, so it's a directory. */

  SVN_ERR(svn_wc__db_global_commit(db, local_abspath,
                                   new_revision, changed_rev,
                                   changed_date, changed_author,
                                   NULL /* new_checksum */,
                                   NULL /* new_children */,
                                   new_dav_cache,
                                   keep_changelist,
                                   no_unlock,
                                   NULL /* work_items */,
                                   pool));

  return SVN_NO_ERROR;
}


/* Process the OP_POSTCOMMIT work item WORK_ITEM.
 * See svn_wc__wq_add_postcommit() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_postcommit(svn_wc__db_t *db,
               const svn_skel_t *work_item,
               const char *wri_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const svn_skel_t *arg5 = work_item->children->next->next->next->next->next;
  const char *local_abspath;
  svn_revnum_t new_revision, changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const svn_checksum_t *new_checksum;
  apr_hash_t *new_dav_cache;
  svn_boolean_t keep_changelist, no_unlock;
  svn_error_t *err;
  apr_int64_t val;

  local_abspath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));
  new_revision = (svn_revnum_t)val;
  SVN_ERR(svn_skel__parse_int(&val, arg1->next->next, scratch_pool));
  changed_date = (apr_time_t)val;
  if (arg1->next->next->next->len == 0)
    changed_author = NULL;
  else
    changed_author = apr_pstrmemdup(scratch_pool,
                                    arg1->next->next->next->data,
                                    arg1->next->next->next->len);
  if (arg5->len == 0)
    {
      new_checksum = NULL;
    }
  else
    {
      const char *data = apr_pstrmemdup(scratch_pool, arg5->data, arg5->len);
      SVN_ERR(svn_checksum_deserialize(&new_checksum, data,
                                       scratch_pool, scratch_pool));
    }
  if (arg5->next->is_atom)
    new_dav_cache = NULL;
  else
    SVN_ERR(svn_skel__parse_proplist(&new_dav_cache, arg5->next,
                                     scratch_pool));
  SVN_ERR(svn_skel__parse_int(&val, arg5->next->next, scratch_pool));
  keep_changelist = (val != 0);

  /* Before r927056, this WQ item didn't have this next field.  Catch any
   * attempt to run this code on a WC having a stale WQ item in it. */
  SVN_ERR_ASSERT(arg5->next->next->next != NULL);

  if (arg5->next->next->next)
    {
      SVN_ERR(svn_skel__parse_int(&val, arg5->next->next->next,
                                  scratch_pool));
      no_unlock = (val != 0);
    }
  else
    no_unlock = TRUE;

  if (arg5->next->next->next->next)
    {
      SVN_ERR(svn_skel__parse_int(&val, arg5->next->next->next->next,
                                  scratch_pool));
      changed_rev = (svn_revnum_t)val;
    }
  else
    changed_rev = new_revision; /* Behavior before fixing issue #3676 */

  err = log_do_committed(db, local_abspath,
                         new_revision, changed_rev, changed_date,
                         changed_author, new_checksum, new_dav_cache,
                         keep_changelist, no_unlock,
                         cancel_func, cancel_baton,
                         scratch_pool);
  if (err)
    return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, err,
                             _("Error processing post-commit work for '%s'"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_FILE_COMMIT  */
static svn_error_t *
run_file_commit(svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  svn_boolean_t remove_executable;
  svn_boolean_t set_read_write;
  apr_int64_t v;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  SVN_ERR(svn_skel__parse_int(&v, arg1->next, scratch_pool));
  set_read_write = (v != 0);

  SVN_ERR(svn_skel__parse_int(&v, arg1->next->next, scratch_pool));
  remove_executable = (v != 0);

  return svn_error_return(
                process_commit_file_install(db, local_abspath,
                                            remove_executable,
                                            set_read_write,
                                            cancel_func, cancel_baton,
                                            scratch_pool));
}

svn_error_t *
svn_wc__wq_build_file_commit(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_boolean_t props_mod,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_boolean_t remove_executable = FALSE;
  svn_boolean_t set_read_write = FALSE;
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  {
    /* Examine propchanges here before installing the new properties in BASE
       If the executable prop was -deleted-, remember this by setting
       REMOVE_EXECUTABLE so that we can later tell install_committed_file() so.
       The same applies to the needs-lock property, remembered by
       setting SET_READ_WRITE. */

    int i;
    apr_array_header_t *propchanges;

    SVN_ERR(svn_wc__internal_propdiff(&propchanges, NULL, db, local_abspath,
                                      scratch_pool, scratch_pool));

    for (i = 0; i < propchanges->nelts; i++)
      {
        svn_prop_t *propchange = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

        if ((! strcmp(propchange->name, SVN_PROP_EXECUTABLE))
            && (propchange->value == NULL))
          remove_executable = TRUE;
        else if ((! strcmp(propchange->name, SVN_PROP_NEEDS_LOCK))
                 && (propchange->value == NULL))
          set_read_write = TRUE;
      }
  }

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_int(remove_executable, *work_item, result_pool);
  svn_skel__prepend_int(set_read_write, *work_item, result_pool);
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  svn_skel__prepend_str(OP_FILE_COMMIT, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */
/* OP_POSTUPGRADE  */

static svn_error_t *
run_postupgrade(svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__wipe_postupgrade(wri_abspath, FALSE,
                                   cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__wq_build_postupgrade(svn_skel_t **work_item,
                             apr_pool_t *result_pool)
{
  *work_item = svn_skel__make_empty_list(result_pool);

  svn_skel__prepend_str(OP_POSTUPGRADE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_FILE_INSTALL */

/* Process the OP_FILE_INSTALL work item WORK_ITEM.
 * See svn_wc__wq_build_file_install() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_install(svn_wc__db_t *db,
                 const svn_skel_t *work_item,
                 const char *wri_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const svn_skel_t *arg4 = arg1->next->next->next;
  const char *local_relpath;
  const char *local_abspath;
  svn_boolean_t use_commit_times;
  svn_boolean_t record_fileinfo;
  svn_boolean_t special;
  svn_stream_t *src_stream;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  const char *temp_dir_abspath;
  svn_stream_t *dst_stream;
  const char *dst_abspath;
  apr_int64_t val;
  const char *wcroot_abspath;
  const char *source_abspath;
  const svn_checksum_t *checksum;
  apr_hash_t *props;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));
  use_commit_times = (val != 0);
  SVN_ERR(svn_skel__parse_int(&val, arg1->next->next, scratch_pool));
  record_fileinfo = (val != 0);

  SVN_ERR(svn_wc__db_read_node_install_info(&wcroot_abspath, NULL, NULL,
                                            &checksum, NULL, &props,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));

  if (arg4 != NULL)
    {
      /* Use the provided path for the source.  */
      local_relpath = apr_pstrmemdup(scratch_pool, arg4->data, arg4->len);
      SVN_ERR(svn_wc__db_from_relpath(&source_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR_ASSERT(checksum != NULL);
      SVN_ERR(svn_wc__db_pristine_get_future_path(&source_abspath,
                                                  wcroot_abspath,
                                                  checksum,
                                                  scratch_pool, scratch_pool));
    }

  SVN_ERR(svn_stream_open_readonly(&src_stream, source_abspath,
                                   scratch_pool, scratch_pool));

  /* Fetch all the translation bits.  */
  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special, db, local_abspath, props,
                                     scratch_pool, scratch_pool));
  if (special)
    {
      /* When this stream is closed, the resulting special file will
         atomically be created/moved into place at LOCAL_ABSPATH.  */
      SVN_ERR(svn_subst_create_specialfile(&dst_stream, local_abspath,
                                           scratch_pool, scratch_pool));

      /* Copy the "repository normal" form of the special file into the
         special stream.  */
      SVN_ERR(svn_stream_copy3(src_stream, dst_stream,
                               cancel_func, cancel_baton,
                               scratch_pool));

      /* No need to set exec or read-only flags on special files.  */
      return SVN_NO_ERROR;
    }

  if (svn_subst_translation_required(style, eol, keywords,
                                     FALSE /* special */,
                                     TRUE /* force_eol_check */))
    {
      /* Wrap it in a translating (expanding) stream.  */
      src_stream = svn_subst_stream_translated(src_stream, eol,
                                               TRUE /* repair */,
                                               keywords,
                                               TRUE /* expand */,
                                               scratch_pool);
    }

  /* Where is the Right Place to put a temp file in this working copy?  */
  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir_abspath,
                                         db, wcroot_abspath,
                                         scratch_pool, scratch_pool));

  /* Translate to a temporary file. We don't want the user seeing a partial
     file, nor let them muck with it while we translate. We may also need to
     get its TRANSLATED_SIZE before the user can monkey it.  */
  SVN_ERR(svn_stream_open_unique(&dst_stream, &dst_abspath,
                                 temp_dir_abspath,
                                 svn_io_file_del_none,
                                 scratch_pool, scratch_pool));

  /* Copy from the source to the dest, translating as we go. This will also
     close both streams.  */
  SVN_ERR(svn_stream_copy3(src_stream, dst_stream,
                           cancel_func, cancel_baton,
                           scratch_pool));

  /* ### post-commit feature: avoid overwrite if same as working file.  */

  /* All done. Move the file into place.  */
  /* ### fix this. we should delay the rename.  */
  SVN_ERR(svn_io_file_rename(dst_abspath, local_abspath, scratch_pool));

  /* Tweak the on-disk file according to its properties.  */
  if (props
      && (apr_hash_get(props, SVN_PROP_NEEDS_LOCK, APR_HASH_KEY_STRING)
          || apr_hash_get(props, SVN_PROP_EXECUTABLE, APR_HASH_KEY_STRING)))
    {
      SVN_ERR(sync_file_flags(db, local_abspath, scratch_pool));
    }

  if (use_commit_times)
    {
      apr_time_t changed_date;

      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, &changed_date, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

      if (changed_date)
        SVN_ERR(svn_io_set_file_affected_time(changed_date,
                                              local_abspath,
                                              scratch_pool));
    }

  /* ### this should happen before we rename the file into place.  */
  if (record_fileinfo)
    {
      SVN_ERR(get_and_record_fileinfo(db, local_abspath,
                                      FALSE /* ignore_enoent */,
                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_file_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *source_abspath,
                              svn_boolean_t use_commit_times,
                              svn_boolean_t record_fileinfo,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  /* If a SOURCE_ABSPATH was provided, then put it into the skel. If this
     value is not provided, then the file's pristine contents will be used.  */
  if (source_abspath != NULL)
    {
      SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                    source_abspath, result_pool, scratch_pool));

      svn_skel__prepend_str(local_relpath, *work_item, result_pool);
    }

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_int(record_fileinfo, *work_item, result_pool);
  svn_skel__prepend_int(use_commit_times, *work_item, result_pool);
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_FILE_INSTALL, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_FILE_REMOVE  */

/* Process the OP_FILE_REMOVE work item WORK_ITEM.
 * See svn_wc__wq_build_file_remove() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_remove(svn_wc__db_t *db,
                 const svn_skel_t *work_item,
                 const char *wri_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  /* Remove the path, no worrying if it isn't there.  */
  return svn_error_return(svn_io_remove_file2(local_abspath, TRUE,
                                              scratch_pool));
}


svn_error_t *
svn_wc__wq_build_file_remove(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_FILE_REMOVE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_FILE_MOVE  */

/* Process the OP_FILE_MOVE work item WORK_ITEM.
 * See svn_wc__wq_build_file_move() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_move(svn_wc__db_t *db,
                 const svn_skel_t *work_item,
                 const char *wri_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *src_abspath, *dst_abspath;
  const char *local_relpath;
  svn_error_t *err;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&src_abspath, db, wri_abspath, local_relpath,
                                  scratch_pool, scratch_pool));
  local_relpath = apr_pstrmemdup(scratch_pool, arg1->next->data,
                                 arg1->next->len);
  SVN_ERR(svn_wc__db_from_relpath(&dst_abspath, db, wri_abspath, local_relpath,
                                  scratch_pool, scratch_pool));

  /* Use svn_io_file_move() instead of svn_io_file_rename() to allow cross
     device copies. We should not fail in the workqueue. */

  err = svn_io_file_move(src_abspath, dst_abspath, scratch_pool);

  /* If the source is not found, we assume the wq op is already handled */
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    svn_error_clear(err);
  else
    SVN_ERR(err);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_file_move(svn_skel_t **work_item,
                           svn_wc__db_t *db,
                           const char *wri_abspath,
                           const char *src_abspath,
                           const char *dst_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  /* File must exist */
  SVN_ERR(svn_io_check_path(src_abspath, &kind, result_pool));

  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("'%s' not found"),
                             svn_dirent_local_style(src_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath, dst_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath, src_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  svn_skel__prepend_str(OP_FILE_MOVE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_FILE_COPY_TRANSLATED */

/* Process the OP_FILE_COPY_TRANSLATED work item WORK_ITEM.
 * See run_file_copy_translated() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_copy_translated(svn_wc__db_t *db,
                         const svn_skel_t *work_item,
                         const char *wri_abspath,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_abspath, *src_abspath, *dst_abspath;
  const char *local_relpath;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t special;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->next->data,
                               arg1->next->len);
  SVN_ERR(svn_wc__db_from_relpath(&src_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->next->next->data,
                                arg1->next->next->len);
  SVN_ERR(svn_wc__db_from_relpath(&dst_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special,
                                     db, local_abspath, NULL,
                                     scratch_pool, scratch_pool));

  SVN_ERR(svn_subst_copy_and_translate4(src_abspath, dst_abspath,
                                        eol, TRUE /* repair */,
                                        keywords, TRUE /* expand */,
                                        special,
                                        cancel_func, cancel_baton,
                                        scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_file_copy_translated(svn_skel_t **work_item,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      const char *src_abspath,
                                      const char *dst_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  const char *local_relpath;

  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  /* File must exist */
  SVN_ERR(svn_io_check_path(src_abspath, &kind, result_pool));

  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("'%s' not found"),
                             svn_dirent_local_style(src_abspath,
                                                    scratch_pool));

  /* ### Once we move to a central DB we should try making
     ### src_abspath, dst_abspath and info_abspath relative from
     ### the WCROOT of info_abspath. */

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath, dst_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath, src_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath, 
                                local_abspath, result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  svn_skel__prepend_str(OP_FILE_COPY_TRANSLATED, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_SYNC_FILE_FLAGS  */

/* Process the OP_SYNC_FILE_FLAGS work item WORK_ITEM.
 * See svn_wc__wq_build_sync_file_flags() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_sync_file_flags(svn_wc__db_t *db,
                    const svn_skel_t *work_item,
                    const char *wri_abspath,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  return svn_error_return(sync_file_flags(db, local_abspath, scratch_pool));
}


svn_error_t *
svn_wc__wq_build_sync_file_flags(svn_skel_t **work_item,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_SYNC_FILE_FLAGS, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_PREJ_INSTALL  */

static svn_error_t *
run_prej_install(svn_wc__db_t *db,
                 const svn_skel_t *work_item,
                 const char *wri_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  const svn_skel_t *conflict_skel;
  const char *tmp_prejfile_abspath;
  const char *prejfile_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));
  if (arg1->next != NULL)
    conflict_skel = arg1->next;
  else
    SVN_ERR_MALFUNCTION();  /* ### wc_db can't provide it ... yet.  */

  /* Construct a property reject file in the temporary area.  */
  SVN_ERR(svn_wc__create_prejfile(&tmp_prejfile_abspath,
                                  db, local_abspath,
                                  conflict_skel,
                                  scratch_pool, scratch_pool));

  /* Get the (stored) name of where it should go.  */
  SVN_ERR(svn_wc__get_prejfile_abspath(&prejfile_abspath, db, local_abspath,
                                       scratch_pool, scratch_pool));
  SVN_ERR_ASSERT(prejfile_abspath != NULL);

  /* ... and atomically move it into place.  */
  SVN_ERR(svn_io_file_rename(tmp_prejfile_abspath,
                             prejfile_abspath,
                             scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_prej_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              svn_skel_t *conflict_skel,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  /* ### gotta have this, today  */
  SVN_ERR_ASSERT(conflict_skel != NULL);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  if (conflict_skel != NULL)
    svn_skel__prepend(conflict_skel, *work_item);
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_PREJ_INSTALL, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_RECORD_FILEINFO  */


static svn_error_t *
run_record_fileinfo(svn_wc__db_t *db,
                    const svn_skel_t *work_item,
                    const char *wri_abspath,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  apr_time_t set_time = 0;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);

  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  if (arg1->next)
    {
      apr_int64_t val;

      SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));
      set_time = (apr_time_t)val;
    }

  if (set_time != 0)
    {
      svn_node_kind_t kind;
      svn_boolean_t is_special;

      /* Do not set the timestamp on special files. */
      SVN_ERR(svn_io_check_special_path(local_abspath, &kind, &is_special,
                                        scratch_pool));

      /* Don't set affected time when local_abspath does not exist or is
         a special file */
      if (kind == svn_node_file && !is_special)
        SVN_ERR(svn_io_set_file_affected_time(set_time, local_abspath,
                                              scratch_pool));

      /* Note that we can't use the value we get here for recording as the
         filesystem might have a different timestamp granularity */
    }


  return svn_error_return(get_and_record_fileinfo(db, local_abspath,
                                                  TRUE /* ignore_enoent */,
                                                  scratch_pool));
}


svn_error_t *
svn_wc__wq_build_record_fileinfo(svn_skel_t **work_item,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_time_t set_time,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  if (set_time)
   svn_skel__prepend_int(set_time, *work_item, result_pool);

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_RECORD_FILEINFO, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_TMP_SET_TEXT_CONFLICT_MARKERS  */


static svn_error_t *
run_set_text_conflict_markers(svn_wc__db_t *db,
                    const svn_skel_t *work_item,
                    const char *wri_abspath,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  const char *old_abspath = NULL;
  const char *new_abspath = NULL;
  const char *wrk_abspath = NULL;

  local_relpath = apr_pstrmemdup(scratch_pool, arg->data, arg->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                           : NULL;

  if (local_relpath)
    {
      SVN_ERR(svn_wc__db_from_relpath(&old_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }

  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                           : NULL;
  if (local_relpath)
    {
      SVN_ERR(svn_wc__db_from_relpath(&new_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }

  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                           : NULL;

  if (local_relpath)
    {
      SVN_ERR(svn_wc__db_from_relpath(&wrk_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }

  return svn_error_return(
          svn_wc__db_temp_op_set_text_conflict_marker_files(db,
                                                            local_abspath,
                                                            old_abspath,
                                                            new_abspath,
                                                            wrk_abspath,
                                                            scratch_pool));
}


svn_error_t *
svn_wc__wq_tmp_build_set_text_conflict_markers(svn_skel_t **work_item,
                                               svn_wc__db_t *db,
                                               const char *local_abspath,
                                               const char *old_abspath,
                                               const char *new_abspath,
                                               const char *wrk_abspath,
                                               apr_pool_t *result_pool,
                                               apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Abspaths in the workqueue won't work if the WC is moved. */
  if (wrk_abspath)
    SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                  wrk_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(wrk_abspath ? local_relpath : "",
                        *work_item, result_pool);

  if (new_abspath)
    SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                  new_abspath, result_pool, scratch_pool));
  svn_skel__prepend_str(new_abspath ? local_relpath : "",
                        *work_item, result_pool);

  if (old_abspath)
    SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                  old_abspath, result_pool, scratch_pool));
  svn_skel__prepend_str(old_abspath ? local_relpath : "",
                        *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_TMP_SET_TEXT_CONFLICT_MARKERS, *work_item,
                        result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_TMP_SET_PROPERTY_CONFLICT_MARKER  */

static svn_error_t *
run_set_property_conflict_marker(svn_wc__db_t *db,
                                 const svn_skel_t *work_item,
                                 const char *wri_abspath,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  const char *prej_abspath = NULL;

  local_relpath = apr_pstrmemdup(scratch_pool, arg->data, arg->len);

  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));
                                   

  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                          : NULL;

  if (local_relpath)
    SVN_ERR(svn_wc__db_from_relpath(&prej_abspath, db, wri_abspath,
                                    local_relpath,
                                    scratch_pool, scratch_pool));

  return svn_error_return(
          svn_wc__db_temp_op_set_property_conflict_marker_file(db,
                                                                local_abspath,
                                                                prej_abspath,
                                                                scratch_pool));
}

svn_error_t *
svn_wc__wq_tmp_build_set_property_conflict_marker(svn_skel_t **work_item,
                                                  svn_wc__db_t *db,
                                                  const char *local_abspath,
                                                  const char *prej_abspath,
                                                  apr_pool_t *result_pool,
                                                  apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (prej_abspath)
    SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                  prej_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(prej_abspath ? local_relpath : "",
                        *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_TMP_SET_PROPERTY_CONFLICT_MARKER, *work_item,
                        result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static const struct work_item_dispatch dispatch_table[] = {
  { OP_FILE_COMMIT, run_file_commit },
  { OP_FILE_INSTALL, run_file_install },
  { OP_FILE_REMOVE, run_file_remove },
  { OP_FILE_MOVE, run_file_move },
  { OP_FILE_COPY_TRANSLATED, run_file_copy_translated },
  { OP_SYNC_FILE_FLAGS, run_sync_file_flags },
  { OP_PREJ_INSTALL, run_prej_install },
  { OP_RECORD_FILEINFO, run_record_fileinfo },
  { OP_BASE_REMOVE, run_base_remove },
  { OP_TMP_SET_TEXT_CONFLICT_MARKERS, run_set_text_conflict_markers },
  { OP_TMP_SET_PROPERTY_CONFLICT_MARKER, run_set_property_conflict_marker },

  /* Upgrade steps */
  { OP_POSTUPGRADE, run_postupgrade },

  /* To be removed (probably on next format bump) */
  { OP_DELETION_POSTCOMMIT, run_deletion_postcommit },
  { OP_POSTCOMMIT, run_postcommit },

  /* Sentinel.  */
  { NULL }
};


static svn_error_t *
dispatch_work_item(svn_wc__db_t *db,
                   const char *wri_abspath,
                   const svn_skel_t *work_item,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  const struct work_item_dispatch *scan;

  /* Scan the dispatch table for a function to handle this work item.  */
  for (scan = &dispatch_table[0]; scan->name != NULL; ++scan)
    {
      if (svn_skel__matches_atom(work_item->children, scan->name))
        {

#ifdef DEBUG_WORK_QUEUE
          SVN_DBG(("dispatch: operation='%s'\n", scan->name));
#endif
          SVN_ERR((*scan->func)(db, work_item, wri_abspath,
                                cancel_func, cancel_baton,
                                scratch_pool));
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
                               svn_dirent_local_style(wri_abspath,
                                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *wri_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

#ifdef DEBUG_WORK_QUEUE
  SVN_DBG(("wq_run: wri='%s'\n", wri_abspath));
#endif

  while (TRUE)
    {
      apr_uint64_t id;
      svn_skel_t *work_item;

      /* Stop work queue processing, if requested. A future 'svn cleanup'
         should be able to continue the processing.  */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc__db_wq_fetch(&id, &work_item, db, wri_abspath,
                                  iterpool, iterpool));
      if (work_item == NULL)
        break;

      SVN_ERR(dispatch_work_item(db, wri_abspath, work_item,
                                 cancel_func, cancel_baton, iterpool));

      /* The work item finished without error. Mark it completed.  */
      SVN_ERR(svn_wc__db_wq_completed(db, wri_abspath, id, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_skel_t *
svn_wc__wq_merge(svn_skel_t *work_item1,
                 svn_skel_t *work_item2,
                 apr_pool_t *result_pool)
{
  /* If either argument is NULL, then just return the other.  */
  if (work_item1 == NULL)
    return work_item2;
  if (work_item2 == NULL)
    return work_item1;

  /* We have two items. Figure out how to join them.  */
  if (SVN_WC__SINGLE_WORK_ITEM(work_item1))
    {
      if (SVN_WC__SINGLE_WORK_ITEM(work_item2))
        {
          /* Both are singular work items. Construct a list, then put
             both work items into it (in the proper order).  */

          svn_skel_t *result = svn_skel__make_empty_list(result_pool);

          svn_skel__prepend(work_item2, result);
          svn_skel__prepend(work_item1, result);
          return result;
        }

      /* WORK_ITEM2 is a list of work items. We can simply shove WORK_ITEM1
         in the front to keep the ordering.  */
      svn_skel__prepend(work_item1, work_item2);
      return work_item2;
    }
  /* WORK_ITEM1 is a list of work items.  */

  if (SVN_WC__SINGLE_WORK_ITEM(work_item2))
    {
      /* Put WORK_ITEM2 onto the end of the WORK_ITEM1 list.  */
      svn_skel__append(work_item1, work_item2);
      return work_item1;
    }

  /* We have two lists of work items. We need to chain all of the work
     items into one big list. We will leave behind the WORK_ITEM2 skel,
     as we only want its children.  */
  svn_skel__append(work_item1, work_item2->children);
  return work_item1;
}
