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
 * if (exist dst_path)
 *   {
 *     if (dst_path is directory)
 *       copy src_path into dst_path as basename (src_path)
 *     else
 *       return ERR_OBSTRUCTION error
 *   }
 * else
 *   {
 *     if (not exist parent_of_dst_path)
 *       return ERR_BAD_DST error
 *     else
 *       copy src_path into parent_of_dst_path as basename (dst_path)
 *   }
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

  /* Verify that DST_PATH is not an existing file. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind == svn_node_file)
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_path->data);

  /* If DST_PATH does not exist, then its basename will become a new
     file or dir added to its parent (possibly an implicit '.').  If
     DST_PATH is a dir, then SRC_PATH's basename will become a new
     file or dir within DST_PATH itself. */
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
#if 0
  void *root_baton;
  svn_stringbuf_t *final_parent, *final_basename;

  svn_stringbuf_t *top_url, *src_rel, *dst_rel;
  void *ra_baton, *sess, *cb_baton;
  svn_ra_plugin_t *ra_lib;
  svn_ra_callbacks_t *ra_callbacks;
  svn_node_kind_t src_kind, dst_kind;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;

  /* We have to open our session to the longest path common to both
     SRC_URL and DST_URL in the repository so we can do existence
     checks on both paths, and so we can operate on both paths in the
     case of a move. */
  top_url = svn_path_get_longest_ancestor (src_url, dst_url,
                                           svn_path_url_style, pool);
  src_rel = svn_path_is_child (top_url, src_url, svn_path_local_style, pool);
  dst_rel = svn_path_is_child (top_url, dst_url, svn_path_local_style, pool);

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, top_url->data, pool));

  /* Get the client callbacks for auth stuffs. */
  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton, auth_baton, 
                                         top_url, TRUE, TRUE, pool));
  SVN_ERR (ra_lib->open (&sess, top_url, ra_callbacks, cb_baton, pool));
      
  /* Verify that SRC_URL exists in the repository. */
  SVN_ERR (ra_lib->check_path (&src_kind, sess, src_rel, src_rev));
  if ((src_kind != svn_node_dir) && (src_kind != svn_node_file))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
       "path `%s' does not exist in revision `%ld'", src_url->data, src_rev);

  /* Make sure that DST_URL doesn't already exist as a file in
     the repository. */
  SVN_ERR (ra_lib->check_path (&dst_kind, sess, dst_rel, SVN_INVALID_REVNUM));
  if (dst_kind == svn_node_file)
    return svn_error_createf (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_url->data);

  /* ### todo:  What should happen below here?

     1.  Get a commit editor, anchored above SRC and DST
     2.  replace-dir() down DST (inclusive, if DST is a directory)
     3.  add-file/dir(copyfrom=src,rev) either
         - basename(DST) if DST doesn't exist, or 
         - basename(SRC) if DST does exist as a dir
     4.  close up all those batons and stuffs
     5.  if this is a move, replace-dir() back down to SRC's parent,
         then delete_entry(basename(SRC)), and close up batons again.
  */

#endif  /* 0 */
  abort();
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
                  apr_pool_t *pool)
{
  abort();
  return SVN_NO_ERROR;
}


static svn_error_t *
setup_copy (svn_stringbuf_t *src_path,
            svn_revnum_t src_rev,
            svn_stringbuf_t *dst_path,
            svn_client_auth_baton_t *auth_baton,
            svn_stringbuf_t *message,
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
  if (is_move && (src_is_url != dst_is_url))
    return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
                             "no support for repos <--> working copy moves");

  /* Now, call the right handler for the operation. */
  if ((! src_is_url) && (! dst_is_url))
    SVN_ERR (wc_to_wc_copy (src_path, dst_path, is_move, pool));

  else if ((! src_is_url) && (dst_is_url))
    SVN_ERR (wc_to_repos_copy (src_path, dst_path, auth_baton, message, pool));

  else if ((src_is_url) && (! dst_is_url))
    SVN_ERR (repos_to_wc_copy (src_path, src_rev, dst_path, auth_baton,
                               message, pool));

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
                 apr_pool_t *pool)
{
  return setup_copy (src_path, src_rev, dst_path, auth_baton, message,
                     FALSE, /* is_move */ pool);
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
                     TRUE, /* is_move */ pool);
}







/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
