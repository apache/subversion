/*
 * client.h :  shared stuff internal to the client library.
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


#ifndef CLIENT_H
#define CLIENT_H


#include <apr_pools.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra.h"
#include "svn_client.h"


/* ---------------------------------------------------------------- */

/*** RA callbacks ***/


/* This is the baton that we pass to RA->open(), and is associated with
   the callback table we provide to RA. */
typedef struct
{
  /* This is provided by the calling application for handling authentication
     information for this session. */
  svn_client_auth_baton_t *auth_baton;

  /* Holds the directory that corresponds to the REPOS_URL at RA->open()
     time. When callbacks specify a relative path, they are joined with
     this base directory. */
  svn_stringbuf_t *base_dir;

  /* Record whether we should store the user/pass into the WC */
  svn_boolean_t do_store;

  /* The pool to use for session-related items. */
  apr_pool_t *pool;

} svn_client__callback_baton_t;


/* Open an RA session, returning the session baton in SESSION_BATON. The
   RA library to use is specified by RA_LIB.

   The root of the session is specified by REPOS_URL and BASE_DIR.

   Additional control parameters:

      - DO_STORE indicates whether the RA layer should attempt to
        store authentication info.

      - USE_ADMIN indicates that the RA layer should create tempfiles
        in the administrative area instead of in the working copy itself.

   BASE_DIR may be NULL if the RA operation does not correspond to a
   working copy (in which case, DO_STORE and USE_ADMIN should both
   be FALSE).

   The calling application's authentication baton is provided in AUTH_BATON,
   and allocations related to this session are performed in POOL.
*/
svn_error_t * svn_client__open_ra_session (void **session_baton,
                                           const svn_ra_plugin_t *ra_lib,
                                           svn_stringbuf_t *repos_URL,
                                           svn_stringbuf_t *base_dir,
                                           svn_boolean_t do_store,
                                           svn_boolean_t use_admin,
                                           void *auth_baton,
                                           apr_pool_t *pool);


/* Retrieve an AUTHENTICATOR/AUTH_BATON pair from the client,
   which represents the protocol METHOD.  */
svn_error_t * svn_client__get_authenticator (void **authenticator,
                                             void **auth_baton,
                                             apr_uint64_t method,
                                             void *callback_baton,
                                             apr_pool_t *pool);


/* ---------------------------------------------------------------- */

/*** Checkout and update ***/

svn_error_t *
svn_client__checkout_internal (const svn_delta_edit_fns_t *before_editor,
                               void *before_edit_baton,
                               const svn_delta_edit_fns_t *after_editor,
                               void *after_edit_baton,
                               svn_stringbuf_t *path,
                               svn_stringbuf_t *xml_src,
                               svn_stringbuf_t *ancestor_path,
                               svn_revnum_t ancestor_revision,
                               svn_boolean_t recurse,
                               apr_pool_t *pool);


svn_error_t *
svn_client__update_internal (const svn_delta_edit_fns_t *before_editor,
                             void *before_edit_baton,
                             const svn_delta_edit_fns_t *after_editor,
                             void *after_edit_baton,
                             svn_stringbuf_t *path,
                             svn_stringbuf_t *xml_src,
                             svn_revnum_t ancestor_revision,
                             svn_boolean_t recurse,
                             apr_pool_t *pool);


/* ---------------------------------------------------------------- */

/*** Editor for repository diff ***/

/* Create an editor for a pure repository comparison, i.e. comparing one
   repository version against the other. TARGET can be a working copy path
   or an URL. REVISION is the start revision in the comparison.
   DIFF_CMD/DIFF_CMD_BATON define the diff callback for comparing two
   files. RA_LIB/RA_SESSION define the ra session for requesting file
   contents. The editor is returned in EDITOR/EDIT_BATON. */
svn_error_t *
svn_client__get_diff_editor (svn_stringbuf_t *target,
                             svn_wc_diff_cmd_t diff_cmd,
                             void *diff_cmd_baton,
                             svn_boolean_t recurse,
                             svn_ra_plugin_t *ra_lib,
                             void *ra_session, 
                             svn_revnum_t revision,
                             const svn_delta_edit_fns_t **editor,
                             void **edit_baton,
                             apr_pool_t *pool);

#endif /* CLIENT_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

