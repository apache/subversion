/*
 * auth.c:  routines that drive "authenticator" objects received from RA.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <apr_strings.h>
#include <apr_pools.h>
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_error.h"


/*-----------------------------------------------------------------------*/

/* Routines that understand specific authentication protocols, and how
   to drive the "authenticator" objects.  */


/* For the SVN_RA_AUTH_USERNAME method.

   This method is used only by `ra_local' right now. */
static svn_error_t *
authorize_username (void **session_baton,
                    svn_ra_plugin_t *ra_lib,
                    void *authenticator,
                    apr_pool_t *pool)
{
  apr_uid_t uid;
  apr_gid_t gid;
  apr_status_t status;
  char *username;

  svn_ra_username_authenticator_t *auth_obj =
    (svn_ra_username_authenticator_t *) authenticator;

  /* Set the username. */
  status = apr_current_userid (&uid, &gid, pool);
  if (status)
    return svn_error_createf(status, 0, NULL, pool,
                             "Error getting UID of client process.");
  
  status = apr_get_username (&username, uid, pool);
  if (status)
    return svn_error_createf(status, 0, NULL, pool,
                             "Error changing UID to username.");

  SVN_ERR (auth_obj->set_username (username, auth_obj->pbaton));

  /* Get the session baton. */
  SVN_ERR (auth_obj->authenticate (session_baton, auth_obj->pbaton));

  return SVN_NO_ERROR;
}





/* For the SVN_RA_AUTH_SIMPLE_PASSWORD method.

   This method is used only by `ra_local' right now. */
static svn_error_t *
authorize_simple_password (void **session_baton,
                           svn_ra_plugin_t *ra_lib,
                           svn_client_auth_info_callback_t cb,
                           void *cb_baton,
                           void *authenticator,
                           apr_pool_t *pool)
{
  /*  svn_ra_simple_password_authenticator_t *auth_obj =
      (svn_ra_simple_password_authenticator_t *) authenticator; */

  /* ### TODO

     If username/password not in working copy:
        use callback/baton:  have app prompt for both
        store them in working copy

     set username
     set password
     authenticate()
          
  */
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/* The main routine.  This routine should be used as a 'dispatcher'
   for the routines that actually understand the protocols behind each
   authentication method. */

svn_error_t *
svn_client_authenticate (void **session_baton,
                         svn_ra_plugin_t *ra_lib,
                         svn_stringbuf_t *repos_URL,
                         svn_client_auth_info_callback_t callback,
                         void *callback_baton,
                         apr_pool_t *pool)
{
  void *obj;

  /* Search for available authentication methods, moving from simplest
     to most complex. */

  
  /* Simple username-only authentication. */
  if (ra_lib->auth_methods & SVN_RA_AUTH_USERNAME)
    {
      SVN_ERR (ra_lib->get_authenticator (&obj, repos_URL,
                                          SVN_RA_AUTH_USERNAME, pool));
      
      SVN_ERR (authorize_username (session_baton, ra_lib, obj, pool));
    }

  /* Username and password authentication. */
  else if (ra_lib->auth_methods & SVN_RA_AUTH_SIMPLE_PASSWORD)
    {
      SVN_ERR (ra_lib->get_authenticator (&obj, repos_URL,
                                          SVN_RA_AUTH_SIMPLE_PASSWORD,
                                          pool));
      
      SVN_ERR (authorize_simple_password (session_baton, ra_lib,
                                          callback, callback_baton,
                                          obj, pool));
    }

  else
    {
      return 
        svn_error_create (SVN_ERR_RA_UNKNOWN_AUTH, 0, NULL, pool,
                          "all server authentication methods unrecognized."); 
    }

  return SVN_NO_ERROR;
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */


