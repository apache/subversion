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

/* Fetch a vtable of CALLBACKS/CALLBACK_BATON suitable for passing
   to RA->open().  AUTH_BATON is originally provided by the calling
   application.  Do allocation in POOL.

   The calling libsvn_client routine customizes these callbacks, based
   on what it's about to do with the RA session:

      - PATH customizes the callbacks to operate on a specific path in
        the working copy.  

      - DO_STORE indicates whether the RA layer should attempt to
        store authentication info.

      - USE_ADMIN indicates that the RA layer should create tempfiles
        in the administrative area instead of in the working copy itself.

*/
svn_error_t *svn_client__get_ra_callbacks (svn_ra_callbacks_t **callbacks,
                                           void **callback_baton,
                                           svn_client_auth_baton_t *auth_baton,
                                           svn_stringbuf_t *path,
                                           svn_boolean_t do_store,
                                           svn_boolean_t use_admin,
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



#endif /* CLIENT_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

