/*
 * copy.c:  wc 'copy' functionality.
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

#include <string.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc.h"
#include "workqueue.h"
#include "adm_files.h"
#include "props.h"
#include "translate.h"
#include "entries.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* Make a copy of the filesystem node (or tree if RECURSIVE) at
   SRC_ABSPATH under a temporary name in the directory
   TMPDIR_ABSPATH and return the absolute path of the copy in
   *DST_ABSPATH.  Return the node kind of SRC_ABSPATH in *KIND.  If
   SRC_ABSPATH doesn't exist then set *DST_ABSPATH to NULL to indicate
   that no copy was made. */
static svn_error_t *
copy_to_tmpdir(const char **dst_abspath,
               svn_node_kind_t *kind,
               const char *src_abspath,
               const char *tmpdir_abspath,
               svn_boolean_t recursive,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  svn_boolean_t is_special;
  svn_io_file_del_t delete_when;

  SVN_ERR(svn_io_check_special_path(src_abspath, kind, &is_special,
                                    scratch_pool));
  if (*kind == svn_node_none)
    {
      *dst_abspath = NULL;
      return SVN_NO_ERROR;
    }
  else if (*kind == svn_node_unknown)
    {
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("Source '%s' is unexpected kind"),
                               svn_dirent_local_style(src_abspath,
                                                      scratch_pool));
    }
  else if (*kind == svn_node_dir || is_special)
    delete_when = svn_io_file_del_on_close;
  else /* the default case: (*kind == svn_node_file) */
    delete_when = svn_io_file_del_none;

  /* ### Do we need a pool cleanup to remove the copy?  We can't use
     ### svn_io_file_del_on_pool_cleanup above because a) it won't
     ### handle the directory case and b) we need to be able to remove
     ### the cleanup before queueing the move work item. */

  /* Set DST_ABSPATH to a temporary unique path.  If *KIND is file, leave a
     file there and then overwrite it; otherwise leave no node on disk at
     that path.  In the latter case, something else might use that path
     before we get around to using it a moment later, but never mind. */
  SVN_ERR(svn_io_open_unique_file3(NULL, dst_abspath, tmpdir_abspath,
                                   delete_when, scratch_pool, scratch_pool));

  if (*kind == svn_node_dir)
    {
      if (recursive)
        SVN_ERR(svn_io_copy_dir_recursively(src_abspath,
                                            tmpdir_abspath,
                                            svn_dirent_basename(*dst_abspath,
                                                                scratch_pool),
                                            TRUE, /* copy_perms */
                                            cancel_func, cancel_baton,
                                            scratch_pool));
      else
        SVN_ERR(svn_io_dir_make(*dst_abspath, APR_OS_DEFAULT, scratch_pool));
    }
  else if (!is_special)
    SVN_ERR(svn_io_copy_file(src_abspath, *dst_abspath, TRUE, /* copy_perms */
                             scratch_pool));
  else
    SVN_ERR(svn_io_copy_link(src_abspath, *dst_abspath, scratch_pool));


  return SVN_NO_ERROR;
}


/* If SRC_ABSPATH and DST_ABSPATH use different pristine stores, copy the
   pristine text of SRC_ABSPATH (if there is one) into the pristine text
   store connected to DST_ABSPATH.  This will only happen when copying into
   a separate WC such as an external directory.
 */
static svn_error_t *
copy_pristine_text_if_necessary(svn_wc__db_t *db,
                                const char *src_abspath,
                                const char *dst_abspath,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                apr_pool_t *scratch_pool)
{
  const svn_checksum_t *checksum;

  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &checksum, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               db, src_abspath,
                               scratch_pool, scratch_pool));
  if (checksum)
    {
      svn_boolean_t present;
      svn_stream_t *src_pristine, *tmp_pristine;
      const char *tmp_pristine_abspath;
      const svn_checksum_t *sha1_checksum, *md5_checksum;
      const char *tmpdir_abspath;

      /* If it's already in DST_ABSPATH's pristine store, we're done. */
      SVN_ERR(svn_wc__db_pristine_check(&present, db, dst_abspath, checksum,
                                        scratch_pool));
      if (present)
        return SVN_NO_ERROR;

      if (checksum->kind == svn_checksum_md5)
        {
          md5_checksum = checksum;
          SVN_ERR(svn_wc__db_pristine_get_sha1(&sha1_checksum, db,
                                               src_abspath, checksum,
                                               scratch_pool, scratch_pool));
        }
      else
        {
          sha1_checksum = checksum;
          SVN_ERR(svn_wc__db_pristine_get_md5(&md5_checksum, db,
                                              src_abspath, checksum,
                                              scratch_pool, scratch_pool));
        }

      SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmpdir_abspath, db, dst_abspath,
                                             scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_pristine_read(&src_pristine, NULL, db,
                                       src_abspath, sha1_checksum,
                                       scratch_pool, scratch_pool));
      SVN_ERR(svn_stream_open_unique(&tmp_pristine, &tmp_pristine_abspath,
                                     tmpdir_abspath, svn_io_file_del_none,
                                     scratch_pool, scratch_pool));
      SVN_ERR(svn_stream_copy3(src_pristine, tmp_pristine,
                               cancel_func, cancel_baton,
                               scratch_pool));
      SVN_ERR(svn_wc__db_pristine_install(db, tmp_pristine_abspath,
                                          sha1_checksum, md5_checksum,
                                          scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Copy the versioned node SRC_ABSPATH in DB to the path DST_ABSPATH in DB.

   This is a specific variant of copy_versioned_file and copy_versioned_dir
   specifically handling deleted nodes.
 */
static svn_error_t *
copy_deleted_node(svn_wc__db_t *db,
                  const char *src_abspath,
                  const char *dst_abspath,
                  const char *dst_op_root_abspath,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_wc_notify_func2_t notify_func,
                  void *notify_baton,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_op_copy(db, src_abspath, dst_abspath, dst_op_root_abspath,
                             NULL, scratch_pool));

  /* Don't recurse on children while all we do is creating not-present
     children */

  return SVN_NO_ERROR;
}

/* Copy the versioned file SRC_ABSPATH in DB to the path DST_ABSPATH in DB.
   If METADATA_ONLY is true, copy only the versioned metadata,
   otherwise copy both the versioned metadata and the filesystem node (even
   if it is the wrong kind, and recursively if it is a dir).

   This also works for versioned symlinks that are stored in the db as
   svn_wc__db_kind_file with svn:special set. */
static svn_error_t *
copy_versioned_file(svn_wc__db_t *db,
                    const char *src_abspath,
                    const char *dst_abspath,
                    const char *dst_op_root_abspath,
                    svn_boolean_t metadata_only,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    svn_wc_notify_func2_t notify_func,
                    void *notify_baton,
                    apr_pool_t *scratch_pool)
{
  svn_skel_t *work_items = NULL;
  const char *dir_abspath = svn_dirent_dirname(dst_abspath, scratch_pool);
  const char *tmpdir_abspath;

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmpdir_abspath, db, dst_abspath,
                                         scratch_pool, scratch_pool));

  /* In case we are copying from one WC to another (e.g. an external dir),
     ensure the destination WC has a copy of the pristine text. */
  SVN_ERR(copy_pristine_text_if_necessary(db, src_abspath, dst_abspath,
                                          cancel_func, cancel_baton,
                                          scratch_pool));

  /* Prepare a temp copy of the filesystem node.  It is usually a file, but
     copy recursively if it's a dir. */
  if (!metadata_only)
    {
      const char *tmp_dst_abspath;
      svn_node_kind_t disk_kind;

      SVN_ERR(copy_to_tmpdir(&tmp_dst_abspath, &disk_kind, src_abspath,
                             tmpdir_abspath,
                             TRUE, /* recursive */
                             cancel_func, cancel_baton, scratch_pool));

      if (tmp_dst_abspath)
        {
          svn_skel_t *work_item;

          /* Remove 'read-only' from the destination file; it's a local add. */
            {
              const svn_string_t *needs_lock;
              SVN_ERR(svn_wc__internal_propget(&needs_lock, db, src_abspath,
                                               SVN_PROP_NEEDS_LOCK,
                                               scratch_pool, scratch_pool));
              if (needs_lock)
                SVN_ERR(svn_io_set_file_read_write(tmp_dst_abspath,
                                                   FALSE, scratch_pool));
            }

          SVN_ERR(svn_wc__wq_build_file_move(&work_item, db, dir_abspath,
                                             tmp_dst_abspath, dst_abspath,
                                             scratch_pool, scratch_pool));
          work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);

          if (disk_kind == svn_node_file)
            {
              svn_boolean_t modified;

              /* It's faster to look for mods on the source now, as
                 the timestamp might match, than to examine the
                 destination later as the destination timestamp will
                 never match. */
              SVN_ERR(svn_wc__internal_file_modified_p(&modified,
                                                       db, src_abspath,
                                                       FALSE, scratch_pool));
              if (!modified)
                {
                  SVN_ERR(svn_wc__wq_build_record_fileinfo(&work_item,
                                                           db, dst_abspath, 0,
                                                           scratch_pool,
                                                           scratch_pool));
                  work_items = svn_wc__wq_merge(work_items, work_item,
                                                scratch_pool);
                }
            }
        }
    }

  /* Copy the (single) node's metadata, and move the new filesystem node
     into place. */
  SVN_ERR(svn_wc__db_op_copy(db, src_abspath, dst_abspath, dst_op_root_abspath,
                             work_items, scratch_pool));
  SVN_ERR(svn_wc__wq_run(db, dir_abspath,
                         cancel_func, cancel_baton, scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(dst_abspath, svn_wc_notify_add,
                               scratch_pool);
      notify->kind = svn_node_file;
      (*notify_func)(notify_baton, notify, scratch_pool);
    }
  return SVN_NO_ERROR;
}

/* Copy the versioned dir SRC_ABSPATH in DB to the path DST_ABSPATH in DB,
   recursively.  If METADATA_ONLY is true, copy only the versioned metadata,
   otherwise copy both the versioned metadata and the filesystem nodes (even
   if they are the wrong kind, and including unversioned children). */
static svn_error_t *
copy_versioned_dir(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   const char *dst_op_root_abspath,
                   svn_boolean_t metadata_only,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   svn_wc_notify_func2_t notify_func,
                   void *notify_baton,
                   apr_pool_t *scratch_pool)
{
  svn_skel_t *work_items = NULL;
  const char *dir_abspath = svn_dirent_dirname(dst_abspath, scratch_pool);
  const char *tmpdir_abspath;
  const apr_array_header_t *versioned_children;
  apr_hash_t *disk_children;
  svn_node_kind_t disk_kind;
  apr_pool_t *iterpool;
  int i;

  /* Prepare a temp copy of the single filesystem node (usually a dir). */
  if (!metadata_only)
    {
      const char *tmp_dst_abspath;

      SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmpdir_abspath, db,
                                             dst_abspath,
                                             scratch_pool, scratch_pool));

      SVN_ERR(copy_to_tmpdir(&tmp_dst_abspath, &disk_kind, src_abspath,
                             tmpdir_abspath, FALSE, /* recursive */
                             cancel_func, cancel_baton, scratch_pool));
      if (tmp_dst_abspath)
        {
          svn_skel_t *work_item;

          SVN_ERR(svn_wc__wq_build_file_move(&work_item, db, dir_abspath,
                                             tmp_dst_abspath, dst_abspath,
                                             scratch_pool, scratch_pool));
          work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
        }
    }

  /* Copy the (single) node's metadata, and move the new filesystem node
     into place. */
  SVN_ERR(svn_wc__db_op_copy(db, src_abspath, dst_abspath, dst_op_root_abspath,
                             work_items, scratch_pool));
  SVN_ERR(svn_wc__wq_run(db, dir_abspath,
                         cancel_func, cancel_baton, scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(dst_abspath, svn_wc_notify_add,
                               scratch_pool);
      notify->kind = svn_node_dir;
      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  if (!metadata_only && disk_kind == svn_node_dir)
    /* All filesystem children, versioned and unversioned.  We're only
       interested in their names, so we can pass TRUE as the only_check_type
       param. */
    SVN_ERR(svn_io_get_dirents3(&disk_children, src_abspath, TRUE,
                                scratch_pool, scratch_pool));
  else
    disk_children = NULL;

  /* Copy all the versioned children */
  SVN_ERR(svn_wc__db_read_children(&versioned_children, db, src_abspath,
                                   scratch_pool, scratch_pool));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < versioned_children->nelts; ++i)
    {
      const char *child_name, *child_src_abspath, *child_dst_abspath;
      svn_wc__db_status_t child_status;
      svn_wc__db_kind_t child_kind;
      svn_boolean_t op_root;

      svn_pool_clear(iterpool);
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      child_name = APR_ARRAY_IDX(versioned_children, i, const char *);
      child_src_abspath = svn_dirent_join(src_abspath, child_name, iterpool);
      child_dst_abspath = svn_dirent_join(dst_abspath, child_name, iterpool);

      SVN_ERR(svn_wc__db_read_info(&child_status, &child_kind, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, &op_root, NULL, NULL,
                                   NULL, NULL, NULL,
                                   db, child_src_abspath,
                                   iterpool, iterpool));

      if (op_root)
        SVN_ERR(svn_wc__db_op_copy_shadowed_layer(db,
                                                  child_src_abspath,
                                                  child_dst_abspath,
                                                  scratch_pool));

      if (child_status == svn_wc__db_status_normal
          || child_status == svn_wc__db_status_added)
        {
          /* We have more work to do than just changing the DB */
         if (child_kind == svn_wc__db_kind_file)
            SVN_ERR(copy_versioned_file(db,
                                        child_src_abspath, child_dst_abspath,
                                        dst_op_root_abspath,
                                        metadata_only,
                                        cancel_func, cancel_baton, NULL, NULL,
                                        iterpool));
          else if (child_kind == svn_wc__db_kind_dir)
            SVN_ERR(copy_versioned_dir(db,
                                       child_src_abspath, child_dst_abspath,
                                       dst_op_root_abspath,
                                       metadata_only,
                                       cancel_func, cancel_baton, NULL, NULL,
                                       iterpool));
          else
            return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                                     _("cannot handle node kind for '%s'"),
                                     svn_dirent_local_style(child_src_abspath,
                                                            scratch_pool));
        }
      else if (child_status == svn_wc__db_status_deleted
          || child_status == svn_wc__db_status_not_present
          || child_status == svn_wc__db_status_excluded)
        {
          /* This will be copied as some kind of deletion. Don't touch
             any actual files */
          SVN_ERR(copy_deleted_node(db,
                                    child_src_abspath, child_dst_abspath,
                                    dst_op_root_abspath,
                                    cancel_func, cancel_baton, NULL, NULL,
                                    iterpool));
        }
      else
        {
          SVN_ERR_ASSERT(child_status == svn_wc__db_status_absent);

          return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                   _("Cannot copy '%s' excluded by server"),
                                   svn_dirent_local_style(src_abspath,
                                                          iterpool));
        }

      if (disk_children
          && (child_status == svn_wc__db_status_normal
              || child_status == svn_wc__db_status_added))
        {
          /* Remove versioned child as it has been handled */
          apr_hash_set(disk_children, child_name, APR_HASH_KEY_STRING, NULL);
        }
    }

  /* Copy all the remaining filesystem children, which are unversioned. */
  if (disk_children)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, disk_children); hi;
           hi = apr_hash_next(hi))
        {
          const char *name = svn__apr_hash_index_key(hi);
          const char *unver_src_abspath, *unver_dst_abspath;
          const char *tmp_dst_abspath;

          if (svn_wc_is_adm_dir(name, iterpool))
            continue;

          svn_pool_clear(iterpool);
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          unver_src_abspath = svn_dirent_join(src_abspath, name, iterpool);
          unver_dst_abspath = svn_dirent_join(dst_abspath, name, iterpool);

          SVN_ERR(copy_to_tmpdir(&tmp_dst_abspath, &disk_kind,
                                 unver_src_abspath, tmpdir_abspath,
                                 TRUE, /* recursive */
                                 cancel_func, cancel_baton, iterpool));
          if (tmp_dst_abspath)
            {
              svn_skel_t *work_item;
              SVN_ERR(svn_wc__wq_build_file_move(&work_item, db, dir_abspath,
                                                 tmp_dst_abspath,
                                                 unver_dst_abspath,
                                                 iterpool, iterpool));
              SVN_ERR(svn_wc__db_wq_add(db, dst_abspath, work_item,
                                        iterpool));
            }

        }
      SVN_ERR(svn_wc__wq_run(db, dst_abspath, cancel_func, cancel_baton,
                             scratch_pool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Public Interface */

svn_error_t *
svn_wc_copy3(svn_wc_context_t *wc_ctx,
             const char *src_abspath,
             const char *dst_abspath,
             svn_boolean_t metadata_only,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_wc__db_kind_t src_db_kind;
  const char *dstdir_abspath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  dstdir_abspath = svn_dirent_dirname(dst_abspath, scratch_pool);

  /* Ensure DSTDIR_ABSPATH belongs to the same repository as SRC_ABSPATH;
     throw an error if not. */
  {
    svn_wc__db_status_t src_status, dstdir_status;
    const char *src_repos_root_url, *dst_repos_root_url;
    const char *src_repos_uuid, *dst_repos_uuid;
    svn_error_t *err;

    err = svn_wc__db_read_info(&src_status, &src_db_kind, NULL, NULL,
                               &src_repos_root_url, &src_repos_uuid, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               db, src_abspath, scratch_pool, scratch_pool);

    if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
      {
        /* Replicate old error code and text */
        svn_error_clear(err);
        return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                 _("'%s' is not under version control"),
                                 svn_dirent_local_style(src_abspath,
                                                        scratch_pool));
      }
    else
      SVN_ERR(err);

    SVN_ERR(svn_wc__db_read_info(&dstdir_status, NULL, NULL, NULL,
                                 &dst_repos_root_url, &dst_repos_uuid, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL,
                                 db, dstdir_abspath, scratch_pool, scratch_pool));

    if (!src_repos_root_url)
      {
        if (src_status == svn_wc__db_status_added)
          SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, NULL,
                                           &src_repos_root_url,
                                           &src_repos_uuid, NULL, NULL, NULL,
                                           NULL,
                                           db, src_abspath,
                                           scratch_pool, scratch_pool));
        else
          /* If not added, the node must have a base or we can't copy */
          SVN_ERR(svn_wc__db_scan_base_repos(NULL, &src_repos_root_url,
                                             &src_repos_uuid,
                                             db, src_abspath,
                                             scratch_pool, scratch_pool));
      }

    if (!dst_repos_root_url)
      {
        if (dstdir_status == svn_wc__db_status_added)
          SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, NULL,
                                           &dst_repos_root_url,
                                           &dst_repos_uuid, NULL, NULL, NULL,
                                           NULL,
                                           db, dstdir_abspath,
                                           scratch_pool, scratch_pool));
        else
          /* If not added, the node must have a base or we can't copy */
          SVN_ERR(svn_wc__db_scan_base_repos(NULL, &dst_repos_root_url,
                                             &dst_repos_uuid,
                                             db, dstdir_abspath,
                                             scratch_pool, scratch_pool));
      }

    if (strcmp(src_repos_root_url, dst_repos_root_url) != 0
        || strcmp(src_repos_uuid, dst_repos_uuid) != 0)
      return svn_error_createf(
         SVN_ERR_WC_INVALID_SCHEDULE, NULL,
         _("Cannot copy to '%s', as it is not from repository '%s'; "
           "it is from '%s'"),
         svn_dirent_local_style(dst_abspath, scratch_pool),
         src_repos_root_url, dst_repos_root_url);

    if (dstdir_status == svn_wc__db_status_deleted)
      return svn_error_createf(
         SVN_ERR_WC_INVALID_SCHEDULE, NULL,
         _("Cannot copy to '%s' as it is scheduled for deletion"),
         svn_dirent_local_style(dst_abspath, scratch_pool));
         /* ### should report dstdir_abspath instead of dst_abspath? */
  }

  /* TODO(#2843): Rework the error report. */
  /* Check if the copy target is missing or hidden and thus not exist on the
     disk, before actually doing the file copy. */
  {
    svn_wc__db_status_t dst_status;
    svn_error_t *err;

    err = svn_wc__db_read_info(&dst_status, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               db, dst_abspath, scratch_pool, scratch_pool);

    if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
      return svn_error_return(err);

    svn_error_clear(err);

    if (!err)
      switch (dst_status)
        {
          case svn_wc__db_status_excluded:
            return svn_error_createf(
                     SVN_ERR_ENTRY_EXISTS, NULL,
                     _("'%s' is already under version control "
                       "but is excluded."),
                     svn_dirent_local_style(dst_abspath, scratch_pool));
          case svn_wc__db_status_absent:
            return svn_error_createf(
                     SVN_ERR_ENTRY_EXISTS, NULL,
                     _("'%s' is already under version control"),
                     svn_dirent_local_style(dst_abspath, scratch_pool));

          case svn_wc__db_status_deleted:
          case svn_wc__db_status_not_present:
            break; /* OK to add */

          default:
            return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                               _("There is already a versioned item '%s'"),
                               svn_dirent_local_style(dst_abspath,
                                                      scratch_pool));
        }
  }

  /* Check that the target path is not obstructed, if required. */
  if (!metadata_only)
    {
      svn_node_kind_t dst_kind;

      /* (We need only to check the root of the copy, not every path inside
         copy_versioned_file/_dir.) */
      SVN_ERR(svn_io_check_path(dst_abspath, &dst_kind, scratch_pool));
      if (dst_kind != svn_node_none)
        return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                                 _("'%s' already exists and is in the way"),
                                 svn_dirent_local_style(dst_abspath,
                                                        scratch_pool));
    }

  if (src_db_kind == svn_wc__db_kind_file
      || src_db_kind == svn_wc__db_kind_symlink)
    {
      SVN_ERR(copy_versioned_file(db, src_abspath, dst_abspath, dst_abspath,
                                  metadata_only,
                                  cancel_func, cancel_baton,
                                  notify_func, notify_baton,
                                  scratch_pool));
    }
  else
    {
      SVN_ERR(copy_versioned_dir(db, src_abspath, dst_abspath, dst_abspath,
                                 metadata_only,
                                 cancel_func, cancel_baton,
                                 notify_func, notify_baton,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}
