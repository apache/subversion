/*
 * copy.c:  copy/move wrappers around wc 'copy' functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include <assert.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"


/*** Code. ***/

/*
 * if (not exist src_path)
 *   return ERR_BAD_SRC error
 *
 * if (exist dst_path)
 *   {
 *     if (dst_path is directory)
 *       copy src_path into dst_path as basename (src_path)
 *     else
 *       return ERR_OBSTRUCTION error
 *   }
 * else
 *   copy src_path into parent_of_dst_path as basename (dst_path)
 *
 * if (this is a move)
 *   delete src_path
 */


/* Copy SRC_PATH into DST_PATH as DST_BASENAME, deleting SRC_PATH
   afterwards if IS_MOVE is TRUE.  Use POOL for all necessary
   allocations. */
static svn_error_t *
wc_to_wc_copy (svn_stringbuf_t *src_path,
               svn_stringbuf_t *dst_path,
               svn_boolean_t is_move,
               svn_boolean_t force,
               svn_wc_notify_func_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  svn_node_kind_t src_kind, dst_kind;
  svn_stringbuf_t *unused, *parent = dst_path, *basename;

  /* Verify that SRC_PATH exists. */
  SVN_ERR (svn_io_check_path (src_path->data, &src_kind, pool));
  if (src_kind == svn_node_none)
    return svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
                              "path `%s' does not exist.", src_path->data);

  /* If DST_PATH does not exist, then its basename will become a new
     file or dir added to its parent (possibly an implicit '.').  If
     DST_PATH is a dir, then SRC_PATH's basename will become a new
     file or dir within DST_PATH itself.  Else if it's a file, just
     error out. */
  SVN_ERR (svn_io_check_path (dst_path->data, &dst_kind, pool));
  if (dst_kind == svn_node_none)
    svn_path_split (dst_path, &parent, &basename, pool);
  else if (dst_kind == svn_node_dir)
    svn_path_split (src_path, &unused, &basename, pool);
  else
    return svn_error_createf (SVN_ERR_ENTRY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_path->data);

  if (is_move && !force)
    {
      /* Ensure there are no "awkward" files. */
      SVN_ERR_W (svn_client__can_delete (src_path, pool),
                 "Pass --force to override this restriction");
    }

  /* Perform the copy and (optionally) delete. */
  SVN_ERR (svn_wc_copy (src_path, parent, basename,
                        notify_func, notify_baton, pool));
  if (is_move)
    {
      SVN_ERR (svn_wc_delete (src_path,
                              notify_func, notify_baton, pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
repos_to_repos_copy (svn_client_commit_info_t **commit_info,
                     svn_stringbuf_t *src_url, 
                     const svn_client_revision_t *src_revision, 
                     svn_stringbuf_t *dst_url, 
                     svn_client_auth_baton_t *auth_baton,
                     svn_stringbuf_t *message,
                     svn_boolean_t is_move,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *top_url, *src_rel, *dst_rel, *basename, *unused;
  apr_array_header_t *src_pieces = NULL, *dst_pieces = NULL;
  svn_revnum_t youngest;
  void *ra_baton, *sess;
  svn_ra_plugin_t *ra_lib;
  svn_node_kind_t src_kind, dst_kind;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *root_baton, *baton;
  void **batons;
  int i = 0;
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  svn_revnum_t src_revnum;
  svn_stringbuf_t *piece, *telepath;

  /* ### TODO:  Currently, this function will violate the depth-first
     rule of editors when doing a move of something up into one of its
     grandparent directories, such as:

        svn mv http://server/repos/dir1/dir2/file http://server/repos/dir1

     While it seems to work just fine, we might want to evaluate this
     from a purely "correctness" standpoint.
  */

  /* We have to open our session to the longest path common to both
     SRC_URL and DST_URL in the repository so we can do existence
     checks on both paths, and so we can operate on both paths in the
     case of a move. */
  top_url = svn_path_get_longest_ancestor (src_url, dst_url, pool);

  /* Get the portions of the SRC and DST URLs that are relative to
     TOP_URL. */
  src_rel = svn_path_is_child (top_url, src_url, pool);
  if (src_rel)
    {
      src_pieces = svn_path_decompose (src_rel, pool);
      if ((! src_pieces) || (! src_pieces->nelts))
        return svn_error_createf 
          (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
           "error decomposing relative path `%s'", src_rel->data);
    }

  dst_rel = svn_path_is_child (top_url, dst_url, pool);
  if (dst_rel)
    {
      dst_pieces = svn_path_decompose (dst_rel, pool);
      if ((! dst_pieces) || (! dst_pieces->nelts))
        return svn_error_createf 
          (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
           "error decomposing relative path `%s'", dst_rel->data);
    }

  /* Allocate room for the root baton, the pieces of the
     source's or destination's path, and the destination itself. */
  {
    int num, num2;
    num = src_pieces ? src_pieces->nelts : 0;
    if (((num2 = (dst_pieces ? dst_pieces->nelts : 0))) > num)
      num = num2;

    batons = apr_palloc (pool, sizeof (void *) * (num + 2));
  }

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, top_url->data, pool));

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files or store the auth data. */
  SVN_ERR (svn_client__open_ra_session (&sess, ra_lib, top_url, NULL,
                                        NULL, FALSE, FALSE, TRUE, 
                                        auth_baton, pool));

  /* Pass null for the path, to ensure error if trying to get a
     revision based on the working copy. */
  SVN_ERR (svn_client__get_revision_number
           (&src_revnum, ra_lib, sess, src_revision, NULL, pool));

  SVN_ERR (ra_lib->get_latest_revnum (sess, &youngest));

  /* Use YOUNGEST for copyfrom args if not provided. */
  if (! SVN_IS_VALID_REVNUM (src_revnum))
    src_revnum = youngest;
  
  /* Verify that SRC_URL exists in the repository. */
  SVN_ERR (ra_lib->check_path (&src_kind, sess,
                               src_rel ? src_rel->data : NULL, src_revnum));
  if (src_kind == svn_node_none)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
       "path `%s' does not exist in revision `%" SVN_REVNUM_T_FMT "'",
       src_url->data, src_revnum);

  /* Figure out the basename that will result from this operation. */
  SVN_ERR (ra_lib->check_path (&dst_kind, sess, 
                               dst_rel ? dst_rel->data : NULL, youngest));
  if (dst_kind == svn_node_none)
    {
      svn_path_split (dst_url, &unused, &basename, pool);
      dst_pieces->nelts--; /* hack - where's apr_array_pop()? */
    }
  else if (dst_kind == svn_node_dir)
    svn_path_split (src_url, &unused, &basename, pool);
  else
    return svn_error_createf (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_url->data);

  /* Fetch RA commit editor. */
  SVN_ERR (ra_lib->get_commit_editor (sess, &editor, &edit_baton,
                                      &committed_rev,
                                      &committed_date,
                                      &committed_author,
                                      message));

  /* Initialize telepath to an empty string that's as big as our
     biggest relative target (we don't want to have to realloc this
     thing. */
  if (src_rel->len > dst_rel->len)
    telepath = svn_stringbuf_dup (src_rel, pool);
  else
    telepath = svn_stringbuf_dup (dst_rel, pool);
  svn_stringbuf_setempty (telepath);

  /* Drive that editor, baby! */
  SVN_ERR (editor->open_root (edit_baton, youngest, pool, &root_baton));

  /* Stuff the root baton here for convenience. */
  batons[i] = root_baton;

  /* Open directories down to the place where we need to make our
     copy. */
  if (dst_pieces && dst_pieces->nelts)
    {
      /* open_directory() all the way down to DST's parent. */
      while (i < dst_pieces->nelts)
        {
          piece = (((svn_stringbuf_t **)(dst_pieces)->elts)[i]);
          svn_path_add_component (telepath, piece);
          SVN_ERR (editor->open_directory (telepath->data, batons[i], 
                                           youngest, pool, &(batons[i + 1])));
          i++;
        }
    }
  /* Add our file/dir with copyfrom history. */
  svn_path_add_component (telepath, basename);
  if (src_kind == svn_node_dir)
    {
      SVN_ERR (editor->add_directory (telepath->data, batons[i], src_url->data,
                                      src_revnum, pool, &baton));
      SVN_ERR (editor->close_directory (baton));
    }
  else
    {
      SVN_ERR (editor->add_file (telepath->data, batons[i], src_url->data,
                                 src_revnum, pool, &baton));
      SVN_ERR (editor->close_file (baton));
    }

  /* Now, close up all those batons (except the root
     baton). */
  while (i)
    {
      SVN_ERR (editor->close_directory (batons[i]));
      batons[i--] = NULL;
    }

  /* If this was a move, we need to remove the SRC_URL. */
  svn_stringbuf_setempty (telepath);
  if (is_move)
    {
      /* If SRC_PIECES is NULL, we're trying to move a directory into
         itself (or one of its chidren...we should have caught that by
         now). */
      assert (src_pieces != NULL);

      /* open_directory() all the way down to SRC's parent. */
      while (i < (src_pieces->nelts - 1))
        {
          piece = (((svn_stringbuf_t **)(src_pieces)->elts)[i]);
          svn_path_add_component (telepath, piece);
          SVN_ERR (editor->open_directory (telepath->data, batons[i], 
                                           youngest, pool, &(batons[i + 1])));
          i++;
        }
          
      /* Delete SRC. */
      piece = (((svn_stringbuf_t **)(src_pieces)->elts)[i]);
      svn_path_add_component (telepath, piece);
      SVN_ERR (editor->delete_entry (telepath->data, SVN_INVALID_REVNUM, 
                                     batons[i], pool));

      /* Now, close up all those batons (except the root
         baton). */
      while (i)
        {
          SVN_ERR (editor->close_directory (batons[i--]));
        }
    }

  /* Turn off the lights, close up the shop, and go home. */
  SVN_ERR (editor->close_directory (batons[0]));
  SVN_ERR (editor->close_edit (edit_baton));

  /* Fill in the commit_info structure. */
  *commit_info = svn_client__make_commit_info (committed_rev,
                                               committed_author,
                                               committed_date,
                                               pool);

  SVN_ERR (ra_lib->close (sess));

  return SVN_NO_ERROR;
}



static svn_error_t *
unlock_dirs (apr_hash_t *locked_dirs,
             apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Split if there's nothing to be done. */
  if (! locked_dirs)
    return SVN_NO_ERROR;

  /* Clean up any locks. */
  for (hi = apr_hash_first (pool, locked_dirs); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_stringbuf_t *strkey;

      apr_hash_this (hi, &key, &keylen, &val);
      strkey = svn_stringbuf_ncreate ((const char *)key, keylen, pool);
      SVN_ERR (svn_wc_unlock (strkey, pool));
    }

  return SVN_NO_ERROR;
}  


static svn_error_t *
remove_tmpfiles (apr_hash_t *tempfiles,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Split if there's nothing to be done. */
  if (! tempfiles)
    return SVN_NO_ERROR;

  /* Clean up any tempfiles. */
  for (hi = apr_hash_first (pool, tempfiles); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_node_kind_t kind;

      apr_hash_this (hi, &key, &keylen, &val);
      SVN_ERR (svn_io_check_path ((const char *)key, &kind, pool));
      if (kind == svn_node_file)
        SVN_ERR (svn_io_remove_file ((const char *)key, pool));
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
reconcile_errors (svn_error_t *commit_err,
                  svn_error_t *unlock_err,
                  svn_error_t *cleanup_err,
                  apr_pool_t *pool)
{
  svn_error_t *err;

  /* Early release (for good behavior). */
  if (! (commit_err || unlock_err || cleanup_err))
    return SVN_NO_ERROR;

  /* If there was a commit error, start off our error chain with
     that. */
  if (commit_err)
    {
      commit_err = svn_error_quick_wrap 
        (commit_err, "Commit failed (details follow):");
      err = commit_err;
    }

  /* Else, create a new "general" error that will head off the errors
     that follow. */
  else
    err = svn_error_create (SVN_ERR_GENERAL, 0, NULL, pool,
                            "Commit succeeded, but other errors follow:");

  /* If there was an unlock error... */
  if (unlock_err)
    {
      /* Wrap the error with some headers. */
      unlock_err = svn_error_quick_wrap 
        (unlock_err, "Error unlocking locked dirs (details follow):");

      /* Append this error to the chain. */
      svn_error_compose (err, unlock_err);
    }

  /* If there was a cleanup error... */
  if (cleanup_err)
    {
      /* Wrap the error with some headers. */
      cleanup_err = svn_error_quick_wrap 
        (cleanup_err, "Error in post-commit clean-up (details follow):");

      /* Append this error to the chain. */
      svn_error_compose (err, cleanup_err);
    }

  return err;
}



static svn_error_t *
wc_to_repos_copy (svn_client_commit_info_t **commit_info,
                  svn_stringbuf_t *src_path, 
                  svn_stringbuf_t *dst_url, 
                  svn_client_auth_baton_t *auth_baton,
                  svn_stringbuf_t *message,
                  const svn_delta_editor_t *before_editor,
                  void *before_edit_baton,
                  const svn_delta_editor_t *after_editor,
                  void *after_edit_baton,
                  apr_pool_t *pool)
{
  svn_stringbuf_t *anchor, *target, *parent, *basename;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_node_kind_t src_kind, dst_kind;
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  apr_hash_t *committables, *locked_dirs, *tempfiles = NULL;
  apr_array_header_t *commit_items;
  svn_error_t *cmt_err = NULL, *unlock_err = NULL, *cleanup_err = NULL;
  svn_boolean_t commit_in_progress = FALSE;
  svn_stringbuf_t *base_path, *base_url;

  /* Check the SRC_PATH. */
  SVN_ERR (svn_io_check_path (src_path->data, &src_kind, pool));

  /* Split the SRC_PATH into a parent and basename. */
  svn_path_split (src_path, &parent, &basename, pool);
  if (svn_path_is_empty (parent))
    parent = svn_stringbuf_create (".", pool);

  /* Split the DST_URL into an anchor and target. */
  svn_path_split (dst_url, &anchor, &target, pool);

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, anchor->data, pool));

  /* Open an RA session for the anchor URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, anchor, parent,
                                        NULL, TRUE, TRUE, TRUE, 
                                        auth_baton, pool));

  /* Figure out the basename that will result from this operation. */
  SVN_ERR (ra_lib->check_path (&dst_kind, session, target->data,
                               SVN_INVALID_REVNUM));
  
  /* Close the RA session.  We'll re-open it after we've figured out
     the right URL to open. */
  SVN_ERR (ra_lib->close (session));
  session = NULL;

  /* BASE_URL defaults to DST_URL. */
  base_url = svn_stringbuf_dup (dst_url, pool);
  if (dst_kind == svn_node_none)
    {
      /* DST_URL doesn't exist under it's parent URL, so the URL we
         will be creating is DST_URL. */
    }
  else if (dst_kind == svn_node_dir)
    {
      /* DST_URL is an existing directory URL.  The URL we will be
         creating, then, is DST_URL+BASENAME. */
      svn_path_add_component (base_url, basename);
    }
  else
    {
      /* DST_URL is an existing file, which can't be overwritten or
         used as a container, so error out. */
      return svn_error_createf (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, pool,
                                "file `%s' already exists.", dst_url->data);
    }

  /* Get the absolute path of the WC path. */
  SVN_ERR (svn_path_get_absolute (&base_path, src_path, pool));

  /* Crawl the working copy for commit items. */
  if ((cmt_err = svn_client__get_copy_committables (&committables, 
                                                    &locked_dirs,
                                                    base_url,
                                                    base_path,
                                                    pool)))
    goto cleanup;

  /* ### todo: There should be only one hash entry, which currently
     has a hacked name until we have the entries files storing
     canonical repository URLs.  Then, the hacked name can go away and
     be replaced with a entry->repos (or whereever the entry's
     canonical repos URL is stored). */
  if (! ((commit_items = apr_hash_get (committables, 
                                       SVN_CLIENT__SINGLE_REPOS_NAME, 
                                       APR_HASH_KEY_STRING))))
    goto cleanup;

  /* Sort and condense our COMMIT_ITEMS. */
  if ((cmt_err = svn_client__condense_commit_items (&base_url, 
                                                    commit_items, 
                                                    pool)))
    goto cleanup;

  /* Open an RA session to BASE_URL. */
  if ((cmt_err = svn_client__open_ra_session (&session, ra_lib, base_url, NULL,
                                              commit_items, TRUE, TRUE, TRUE,
                                              auth_baton, pool)))
    goto cleanup;

  /* Fetch RA commit editor, giving it svn_wc_process_committed(). */
  if ((cmt_err = ra_lib->get_commit_editor (session, &editor, &edit_baton, 
                                            &committed_rev, &committed_date, 
                                            &committed_author, message)))
    goto cleanup;

  /* Make a note that we have a commit-in-progress. */
  commit_in_progress = TRUE;

  /* Wrap the resulting editor with BEFORE and AFTER editors. */
  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton, 
                         after_editor, after_edit_baton, pool);

  /* Perform the commit. */
  cmt_err = svn_client__do_commit (base_url, commit_items, editor, edit_baton, 
                                   NULL, NULL, 0,
                                   &tempfiles, pool);

  commit_in_progress = FALSE;

  /* Sleep for one second to ensure timestamp integrity. */
  apr_sleep (APR_USEC_PER_SEC * 1);

 cleanup:
  /* Abort the commit if it is still in progress. */
  if (commit_in_progress)
    editor->abort_edit (edit_baton); /* ignore return value */

  /* We were committing to RA, so close the session. */
  if (session)
    ra_lib->close (session);

  /* Unlock any remaining locked dirs. */
  if (locked_dirs)
    unlock_err = unlock_dirs (locked_dirs, pool);

  /* Remove any outstanding temporary text-base files. */
  if (tempfiles)
    cleanup_err = remove_tmpfiles (tempfiles, pool);

  /* Fill in the commit_info structure */
  *commit_info = svn_client__make_commit_info (committed_rev, 
                                               committed_author, 
                                               committed_date, pool);

  return reconcile_errors (cmt_err, unlock_err, cleanup_err, pool);
}


static svn_error_t *
repos_to_wc_copy (svn_stringbuf_t *src_url,
                  const svn_client_revision_t *src_revision,
                  svn_stringbuf_t *dst_path, 
                  svn_client_auth_baton_t *auth_baton,
                  const svn_delta_editor_t *before_editor,
                  void *before_edit_baton,
                  const svn_delta_editor_t *after_editor,
                  void *after_edit_baton,
                  svn_wc_notify_func_t notify_func,
                  void *notify_baton,
                  apr_pool_t *pool)
{
  void *ra_baton, *sess;
  svn_ra_plugin_t *ra_lib;
  svn_node_kind_t src_kind, dst_kind;
  svn_revnum_t src_revnum;

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, src_url->data, pool));

  /* Open a repository session to the given URL. We do not (yet) have a
     working copy, so we don't have a corresponding path and tempfiles
     cannot go into the admin area. We do want to store the resulting
     auth data, though, once the WC is built. */
  SVN_ERR (svn_client__open_ra_session (&sess, ra_lib, src_url, NULL,
                                        NULL, TRUE, FALSE, TRUE, 
                                        auth_baton, pool));
      
  /* Pass null for the path, to ensure error if trying to get a
     revision based on the working copy. */
  SVN_ERR (svn_client__get_revision_number
           (&src_revnum, ra_lib, sess, src_revision, NULL, pool));

  /* Verify that SRC_URL exists in the repository. */
  SVN_ERR (ra_lib->check_path (&src_kind, sess, "", src_revnum));
  if (src_kind == svn_node_none)
    {
      if (SVN_IS_VALID_REVNUM (src_revnum))
        return svn_error_createf
          (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
           "path `%s' not found in revision `%" SVN_REVNUM_T_FMT "'",
           src_url->data, src_revnum);
      else
        return svn_error_createf
          (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
           "path `%s' not found in head revision", src_url->data);
    }

  /* There are two interfering sets of cases to watch out for here:
   *
   * First set:
   *
   *   1) If DST_PATH does not exist, then great.  We're going to
   *      create a new entry in its parent.
   *   2) If it does exist, then it must be a directory and we're
   *      copying to a new entry inside that dir (the entry's name is
   *      the basename of SRC_URL).
   *
   * But while that's all going on, we must also remember:
   *
   *   A) If SRC_URL is a directory in the repository, we can check
   *      it out directly, no problem.
   *   B) If SRC_URL is a file, we have to manually get the editor
   *      started, since there won't be a root to open.
   *
   * I'm going to ignore B for the moment, and implement cases 1 and
   * 2 under A.
   */

  /* First, figure out about dst. */
  SVN_ERR (svn_io_check_path (dst_path->data, &dst_kind, pool));
  if (dst_kind == svn_node_dir)
    {
      svn_stringbuf_t *unused, *basename;
      svn_path_split (src_url, &unused, &basename, pool);

      /* We shouldn't affect the caller's dst_path, so dup first and
         then extend. */
      dst_path = svn_stringbuf_dup (dst_path, pool);
      svn_path_add_component (dst_path, basename);
    }
  else if (dst_kind != svn_node_none)  /* must be a file */
    return svn_error_createf (SVN_ERR_ENTRY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_path->data);

  /* Now that dst_path has possibly been reset, check that there's
     nothing in the way of the upcoming checkout. */
  SVN_ERR (svn_io_check_path (dst_path->data, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
                              "`%s' is in the way", dst_path->data);

  if (src_kind == svn_node_dir)
    {    
      const svn_delta_editor_t *editor;
      void *edit_baton;

      /* Get a checkout editor and wrap it. */
      SVN_ERR (svn_wc_get_checkout_editor (dst_path, src_url, src_revnum, 1,
                                           &editor, &edit_baton, pool));
      
      svn_delta_wrap_editor (&editor, &edit_baton,
                             before_editor, before_edit_baton,
                             editor, edit_baton,
                             after_editor, after_edit_baton, pool);

      /* Check out the new tree.  The parent dir will get no entry, so
         it will be as if the new tree isn't really there yet. */
      SVN_ERR (ra_lib->do_checkout (sess, src_revnum, 1, 
                                    editor,
                                    edit_baton));

      if (! SVN_IS_VALID_REVNUM(src_revnum))
        {
          /* If we just checked out from the "head" revision, that's fine,
             but we don't want to pass a '-1' as a copyfrom_rev to
             svn_wc_add().  That function will dump it right into the
             entry, and when we try to commit later on, the
             'add-dir-with-history' step will be -very- unhappy; it only
             accepts specific revisions.
             
             On the other hand, we *could* say that -1 is a legitimate
             copyfrom_rev, but I think that's bogus.  Somebody made a copy
             from a particular revision;  if they wait a long time to
             commit, it would be terrible if the copied happened from a
             newer revision!! */
          
          /* We just did a checkout; whatever revision we just got, that
             should be the copyfrom_revision when we commit later. */
          svn_wc_entry_t *d_entry;
          SVN_ERR (svn_wc_entry (&d_entry, dst_path, FALSE, pool));
          src_revnum = d_entry->revision;
        }

    } /* end directory case */

  else if (src_kind == svn_node_file)
    {
      apr_status_t status;
      svn_stream_t *fstream;
      apr_file_t *fp;
      svn_revnum_t fetched_rev = 0;
      apr_hash_t *props;
      apr_hash_index_t *hi;
      
      /* Open DST_PATH for writing. */
      status = apr_file_open (&fp, dst_path->data, (APR_CREATE | APR_WRITE),
                              APR_OS_DEFAULT, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "failed to open file '%s' for writing.",
                                  dst_path->data);

      /* Create a generic stream that operates on this file.  */
      fstream = svn_stream_from_aprfile (fp, pool);
      
      /* Have the RA layer 'push' data at this stream.  We pass a
         relative path of "", because we opened SRC_URL, which is
         already the full URL to the file. */         
      SVN_ERR (ra_lib->get_file (sess, "", src_revnum, fstream, 
                                 &fetched_rev, &props));

      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi)) 
        {
          const void *key;
          void *val;
          int len;
          enum svn_prop_kind kind;

          apr_hash_this (hi, &key, NULL, &val);

          /* We only want to set 'normal' props.  For now, we're
             ignoring any wc props (they're not needed when we commit
             an addition), and we're ignoring entry props (they're
             written to the entries file as part of the post-commit
             processing).  */
          kind = svn_property_kind (&len, key);
          if (kind == svn_prop_regular_kind)
            SVN_ERR (svn_wc_prop_set (key, val, dst_path->data, pool));
        }

      /* Close the file. */
      status = apr_file_close (fp);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "failed to close file '%s'.",
                                  dst_path->data);   
     
      /* Also, if SRC_REVNUM is invalid ('head'), then FETCHED_REV is now
         equal to the revision that was actually retrieved.  This is
         the value we want to use as 'copyfrom_rev' in the call to
         svn_wc_add() below. */
      if (! SVN_IS_VALID_REVNUM (src_revnum))
        src_revnum = fetched_rev;
    }

  /* Free the RA session. */
  SVN_ERR (ra_lib->close (sess));
      
  /* Schedule the new item for addition-with-history.

     If the new item is a directory, the URLs will be recursively
     rewritten, wcprops removed, and everything marked as 'copied'.
     See comment in svn_wc_add()'s doc about whether svn_wc_add is the
     appropriate place for this. */
  SVN_ERR (svn_wc_add (dst_path, src_url, src_revnum,
                       notify_func, notify_baton, pool));


  return SVN_NO_ERROR;
}


static svn_error_t *
setup_copy (svn_client_commit_info_t **commit_info,
            svn_stringbuf_t *src_path,
            const svn_client_revision_t *src_revision,
            svn_stringbuf_t *dst_path,
            svn_client_auth_baton_t *auth_baton,
            svn_client_get_commit_log_t log_msg_func,
            void *log_msg_baton,
            const svn_delta_editor_t *before_editor,
            void *before_edit_baton,
            const svn_delta_editor_t *after_editor,
            void *after_edit_baton,
            svn_boolean_t is_move,
            svn_boolean_t force,
            svn_wc_notify_func_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  svn_boolean_t src_is_url, dst_is_url;
  svn_string_t path_str;
  svn_stringbuf_t *message;

  /* Are either of our paths URLs? */
  path_str.data = src_path->data;
  path_str.len = src_path->len;
  src_is_url = svn_path_is_url (&path_str);
  path_str.data = dst_path->data;
  path_str.len = dst_path->len;
  dst_is_url = svn_path_is_url (&path_str);

  if (is_move)
    {
      if (src_is_url == dst_is_url)
        {
          if (svn_path_is_child (src_path, dst_path, pool))
            return svn_error_createf
              (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
               "cannot move path '%s' into its own child '%s'",
               src_path->data, dst_path->data);
          if (svn_stringbuf_compare (src_path, dst_path))
            return svn_error_createf
              (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
               "cannot move path '%s' into itself",
               src_path->data);
        }
      else
        {
          /* Disallow moves between the working copy and the repository. */
          return svn_error_create 
            (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
             "no support for repos <--> working copy moves");
        }

      /* It doesn't make sense to specify revisions in a move. */

      /* ### todo: this check could fail wrongly.  For example,
         someone could pass in an svn_client_revision_number that just
         happens to be the HEAD.  It's fair enough to punt then, IMHO,
         and just demand that the user not specify a revision at all;
         beats mucking up this function with RA calls and such. */ 
      if ((src_revision->kind != svn_client_revision_unspecified)
          || (src_revision->kind != svn_client_revision_unspecified))
        {
          return svn_error_create
            (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
             "cannot specify revisions with move operations");
        }
    }

  /* Create a new commit item and add it to the array. */
  if (dst_is_url && log_msg_func)
    {
      svn_client_commit_item_t *item;
      apr_array_header_t *commit_items 
        = apr_array_make (pool, 1, sizeof (item));
      
      item = apr_pcalloc (pool, sizeof (*item));
      item->url = svn_stringbuf_dup (dst_path, pool);
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      (*((svn_client_commit_item_t **) apr_array_push (commit_items))) 
        = item;
      
      SVN_ERR ((*log_msg_func) (&message, commit_items, 
                                log_msg_baton, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = svn_stringbuf_create ("", pool);

  /* Now, call the right handler for the operation. */
  if ((! src_is_url) && (! dst_is_url))
    SVN_ERR (wc_to_wc_copy (src_path, dst_path, is_move, force,
                            notify_func, notify_baton,
                            pool));

  else if ((! src_is_url) && (dst_is_url))
    SVN_ERR (wc_to_repos_copy (commit_info, src_path, dst_path, 
                               auth_baton, message, 
                               before_editor, before_edit_baton,
                               after_editor, after_edit_baton,
                               pool));

  else if ((src_is_url) && (! dst_is_url))
    SVN_ERR (repos_to_wc_copy (src_path, src_revision, 
                               dst_path, auth_baton,
                               before_editor, before_edit_baton,
                               after_editor, after_edit_baton,
                               notify_func, notify_baton,
                               pool));

  else
    SVN_ERR (repos_to_repos_copy (commit_info, src_path, src_revision,
                                  dst_path, auth_baton, message, is_move,
                                  pool));

  return SVN_NO_ERROR;
}



/* Public Interfaces */

svn_error_t *
svn_client_copy (svn_client_commit_info_t **commit_info,
                 svn_stringbuf_t *src_path,
                 const svn_client_revision_t *src_revision,
                 svn_stringbuf_t *dst_path,
                 svn_client_auth_baton_t *auth_baton,
                 svn_client_get_commit_log_t log_msg_func,
                 void *log_msg_baton,
                 const svn_delta_editor_t *before_editor,
                 void *before_edit_baton,
                 const svn_delta_editor_t *after_editor,
                 void *after_edit_baton,
                 svn_wc_notify_func_t notify_func,
                 void *notify_baton,
                 apr_pool_t *pool)
{
  return setup_copy (commit_info, 
                     src_path, src_revision, dst_path, auth_baton, 
                     log_msg_func, log_msg_baton,
                     before_editor, before_edit_baton,
                     after_editor, after_edit_baton,
                     FALSE /* is_move */,
                     TRUE /* force, set to avoid deletion check */,
                     notify_func, notify_baton,
                     pool);
}


svn_error_t *
svn_client_move (svn_client_commit_info_t **commit_info,
                 svn_stringbuf_t *src_path,
                 const svn_client_revision_t *src_revision,
                 svn_stringbuf_t *dst_path,
                 svn_boolean_t force,
                 svn_client_auth_baton_t *auth_baton,
                 svn_client_get_commit_log_t log_msg_func,
                 void *log_msg_baton,
                 svn_wc_notify_func_t notify_func,
                 void *notify_baton,
                 apr_pool_t *pool)
{
  return setup_copy (commit_info,
                     src_path, src_revision, dst_path, auth_baton,
                     log_msg_func, log_msg_baton,
                     NULL, NULL,  /* no before_editor, before_edit_baton */
                     NULL, NULL,  /* no after_editor, after_edit_baton */
                     TRUE /* is_move */,
                     force,
                     notify_func, notify_baton,
                     pool);
}







/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
