/*
 * copy.c:  copy/move wrappers around wc 'copy' functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
               apr_pool_t *pool)
{
  svn_node_kind_t src_kind, dst_kind;
  svn_stringbuf_t *unused, *parent = dst_path, *basename;

  /* Verify that SRC_PATH exists. */
  SVN_ERR (svn_io_check_path (src_path, &src_kind, pool));
  if (src_kind == svn_node_none)
    return svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
                              "path `%s' does not exist.", src_path->data);

  /* If DST_PATH does not exist, then its basename will become a new
     file or dir added to its parent (possibly an implicit '.').  If
     DST_PATH is a dir, then SRC_PATH's basename will become a new
     file or dir within DST_PATH itself.  Else if it's a file, just
     error out. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind == svn_node_none)
    svn_path_split (dst_path, &parent, &basename, svn_path_local_style, pool);
  else if (dst_kind == svn_node_dir)
    svn_path_split (src_path, &unused, &basename, svn_path_local_style, pool);
  else
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_path->data);

  /* Perform the copy and (optionally) delete. */
  SVN_ERR (svn_wc_copy (src_path, parent, basename, pool));
  if (is_move)
    SVN_ERR (svn_wc_delete (src_path, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
repos_to_repos_copy (svn_stringbuf_t *src_url, 
                     svn_revnum_t src_rev, 
                     svn_stringbuf_t *dst_url, 
                     svn_client_auth_baton_t *auth_baton,
                     svn_stringbuf_t *message,
                     svn_boolean_t is_move,
                     apr_pool_t *pool)
{
  void *root_baton;
  svn_revnum_t youngest;
  svn_stringbuf_t *basename, *unused;
  svn_stringbuf_t *top_url, *src_rel, *dst_rel;
  void *ra_baton, *sess, *cb_baton;
  svn_ra_plugin_t *ra_lib;
  svn_ra_callbacks_t *ra_callbacks;
  svn_node_kind_t src_kind, dst_kind;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  apr_array_header_t *src_pieces = NULL, *dst_pieces = NULL;
  svn_stringbuf_t *piece;
  int i = 0;
  void **batons;
  void *baton;

  /* We have to open our session to the longest path common to both
     SRC_URL and DST_URL in the repository so we can do existence
     checks on both paths, and so we can operate on both paths in the
     case of a move. */
  top_url = svn_path_get_longest_ancestor (src_url, dst_url,
                                           svn_path_url_style, pool);

  /* Get the portions of the SRC and DST URLs that are relative to
     TOP_URL. */
  src_rel = svn_path_is_child (top_url, src_url, svn_path_local_style, pool);
  if (src_rel)
    {
      src_pieces = svn_path_decompose (src_rel, svn_path_url_style, pool);
      if ((! src_pieces) || (! src_pieces->nelts))
        return svn_error_createf 
          (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
           "error decomposing relative path `%s'", src_rel->data);
    }

  dst_rel = svn_path_is_child (top_url, dst_url, svn_path_local_style, pool);
  if (dst_rel)
    {
      dst_pieces = svn_path_decompose (dst_rel, svn_path_url_style, pool);
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

  /* Get the client callbacks for auth stuffs. */
  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton, auth_baton, 
                                         top_url, TRUE, TRUE, pool));
  SVN_ERR (ra_lib->open (&sess, top_url, ra_callbacks, cb_baton, pool));
  SVN_ERR (ra_lib->get_latest_revnum (sess, &youngest));

  /* Use YOUNGEST for copyfrom args if not provided. */
  if (! SVN_IS_VALID_REVNUM (src_rev))
    src_rev = youngest;

  
  /* Verify that SRC_URL exists in the repository. */
  SVN_ERR (ra_lib->check_path (&src_kind, sess,
                               src_rel ? src_rel->data : NULL, src_rev));
  if (src_kind == svn_node_none)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
       "path `%s' does not exist in revision `%ld'", src_url->data, src_rev);

  /* Figure out the basename that will result from this operation. */
  SVN_ERR (ra_lib->check_path (&dst_kind, sess, 
                               dst_rel ? dst_rel->data : NULL, youngest));
  if (dst_kind == svn_node_none)
    {
      svn_path_split (dst_url, &unused, &basename, svn_path_local_style, pool);
      dst_pieces->nelts--; /* hack - where's apr_array_pop()? */
    }
  else if (dst_kind == svn_node_dir)
    svn_path_split (src_url, &unused, &basename, svn_path_local_style, pool);
  else
    return svn_error_createf (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_url->data);

  /* Fetch RA commit editor. */
  SVN_ERR (ra_lib->get_commit_editor
           (sess, &editor, &edit_baton, message, NULL, NULL, NULL, NULL));

  /* Drive that editor, baby! */
  SVN_ERR (editor->open_root (edit_baton, youngest, &root_baton));

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
          SVN_ERR (editor->open_directory (piece, batons[i], 
                                           youngest, &(batons[i + 1])));
          i++;
        }
    }
  /* Add our file/dir with copyfrom history. */
  if (src_kind == svn_node_dir)
    {
      SVN_ERR (editor->add_directory (basename, batons[i], src_url,
                                      src_rev, &baton));
      SVN_ERR (editor->close_directory (baton));
    }
  else
    {
      SVN_ERR (editor->add_file (basename, batons[i], src_url,
                                 src_rev, &baton));
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
          SVN_ERR (editor->open_directory (piece, batons[i],
                                           youngest, &(batons[i + 1])));
          i++;
        }
          
      /* Delete SRC. */
      piece = (((svn_stringbuf_t **)(src_pieces)->elts)[i]);
      SVN_ERR (editor->delete_entry (piece, batons[i]));

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

  return SVN_NO_ERROR;
}


static svn_error_t *
wc_to_repos_copy (svn_stringbuf_t *src_path, 
                  svn_stringbuf_t *dst_url, 
                  svn_client_auth_baton_t *auth_baton,
                  svn_stringbuf_t *message,
                  apr_pool_t *pool)
{
  abort();
  return SVN_NO_ERROR;
}


static svn_error_t *
repos_to_wc_copy (svn_stringbuf_t *src_url,
                  svn_revnum_t src_rev,
                  svn_stringbuf_t *dst_path, 
                  svn_client_auth_baton_t *auth_baton,
                  svn_stringbuf_t *message,
                  const svn_delta_edit_fns_t *before_editor,
                  void *before_edit_baton,
                  const svn_delta_edit_fns_t *after_editor,
                  void *after_edit_baton,
                  apr_pool_t *pool)
{
  void *ra_baton, *sess, *cb_baton;
  svn_ra_plugin_t *ra_lib;
  svn_ra_callbacks_t *ra_callbacks;
  svn_node_kind_t src_kind, dst_kind;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, src_url->data, pool));

  /* Get the client callbacks for auth stuffs. */
  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton, auth_baton, 
                                         src_url, TRUE, TRUE, pool));
  SVN_ERR (ra_lib->open (&sess, src_url, ra_callbacks, cb_baton, pool));
      
  /* Verify that SRC_URL exists in the repository. */
  SVN_ERR (ra_lib->check_path (&src_kind, sess, "", src_rev));
  if (src_kind == svn_node_none)
    {
      if (SVN_IS_VALID_REVNUM (src_rev))
        return svn_error_createf
          (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
           "path `%s' not found in revision `%ld'", src_url->data, src_rev);
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
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind == svn_node_dir)
    {
      svn_stringbuf_t *unused, *basename;
      svn_path_split (src_url, &unused, &basename, svn_path_url_style, pool);

      /* We shouldn't affect the caller's dst_path, so dup first and
         then extend. */
      dst_path = svn_stringbuf_dup (dst_path, pool);
      svn_path_add_component (dst_path, basename, svn_path_local_style);
    }
  else if (dst_kind != svn_node_none)  /* must be a file */
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_path->data);

  /* Now that dst_path has possibly been reset, check that there's
     nothing in the way of the upcoming checkout. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
                              "`%s' is in the way", dst_path->data);

  /* ### todo: We won't always punt on non-dir src like this. */
  if (src_kind != svn_node_dir)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
       "can't copy non-directory `%s' to a wc yet", src_url->data);
    
  /* Get a checkout editor and wrap it. */
  SVN_ERR (svn_wc_get_checkout_editor (dst_path,
                                       src_url,
                                       src_rev,
                                       1,
                                       &editor,
                                       &edit_baton,
                                       pool));

  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton,
                         after_editor, after_edit_baton, pool);

  /* Check out the new tree.  The parent dir will get no entry, so
     it will be as if the new tree isn't really there yet. */
  SVN_ERR (ra_lib->do_checkout (sess, src_rev, 1, editor, edit_baton));

  /* Switch the tree over to the new ancestry, incidentally adding an
     entry in parent.  See long comment in svn_wc_add()'s doc string
     about whether svn_wc_add() is appropriate for this. */
  SVN_ERR (svn_wc_add (dst_path, src_url, src_rev, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
setup_copy (svn_stringbuf_t *src_path,
            svn_revnum_t src_rev,
            svn_stringbuf_t *dst_path,
            svn_client_auth_baton_t *auth_baton,
            svn_stringbuf_t *message,
            const svn_delta_edit_fns_t *before_editor,
            void *before_edit_baton,
            const svn_delta_edit_fns_t *after_editor,
            void *after_edit_baton,
            svn_boolean_t is_move,
            apr_pool_t *pool)
{
  svn_boolean_t src_is_url, dst_is_url;
  svn_string_t path_str;

  /* Are either of our paths URLs? */
  path_str.data = src_path->data;
  path_str.len = src_path->len;
  src_is_url = svn_path_is_url (&path_str);
  path_str.data = dst_path->data;
  path_str.len = dst_path->len;
  dst_is_url = svn_path_is_url (&path_str);

  /* Disallow moves between the working copy and the repository. */
  if (is_move)
    {
      if (SVN_IS_VALID_REVNUM (src_rev))
        return svn_error_create 
          (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
           "move operations are only allowed on the HEAD revision");

      if (src_is_url == dst_is_url)
        {
          if (svn_path_is_child (src_path, dst_path, svn_path_url_style, pool))
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
          return svn_error_create 
            (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
             "no support for repos <--> working copy moves");
        }
    }

  /* Make sure our log_msg is non-NULL. */
  if (! message)
    message = svn_stringbuf_create ("", pool);

  /* Now, call the right handler for the operation. */
  if ((! src_is_url) && (! dst_is_url))
    SVN_ERR (wc_to_wc_copy (src_path, dst_path, is_move, pool));

  else if ((! src_is_url) && (dst_is_url))
    SVN_ERR (wc_to_repos_copy (src_path, dst_path, 
                               auth_baton, message, pool));

  else if ((src_is_url) && (! dst_is_url))
    SVN_ERR (repos_to_wc_copy (src_path, src_rev, dst_path, auth_baton,
                               message,
                               before_editor, before_edit_baton,
                               after_editor, after_edit_baton,
                               pool));

  else
    SVN_ERR (repos_to_repos_copy (src_path, src_rev, dst_path, auth_baton,
                                  message, is_move, pool));

  return SVN_NO_ERROR;
}


/* Public Interfaces */

svn_error_t *
svn_client_copy (svn_stringbuf_t *src_path,
                 svn_revnum_t src_rev,
                 svn_stringbuf_t *dst_path,
                 svn_client_auth_baton_t *auth_baton,
                 svn_stringbuf_t *message,
                 const svn_delta_edit_fns_t *before_editor,
                 void *before_edit_baton,
                 const svn_delta_edit_fns_t *after_editor,
                 void *after_edit_baton,
                 apr_pool_t *pool)
{
  return setup_copy (src_path, src_rev, dst_path, auth_baton, message,
                     before_editor, before_edit_baton,
                     after_editor, after_edit_baton,
                     FALSE /* is_move */, pool);
}


svn_error_t *
svn_client_move (svn_stringbuf_t *src_path,
                 svn_revnum_t src_rev,
                 svn_stringbuf_t *dst_path,
                 svn_client_auth_baton_t *auth_baton,
                 svn_stringbuf_t *message,
                 apr_pool_t *pool)
{
  return setup_copy (src_path, src_rev, dst_path, auth_baton, message,
                     NULL, NULL,  /* no before_editor, before_edit_baton */
                     NULL, NULL,  /* no after_editor, after_edit_baton */
                     TRUE /* is_move */, pool);
}







/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
