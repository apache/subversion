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
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <apr_strings.h>
#include <apr_pools.h>
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_error.h"
#include "svn_io.h"


/*-----------------------------------------------------------------------*/

/* Routines that understand specific authentication protocols, and how
   to drive the "authenticator" objects.  */


/* Helper routine/baton to store auth info in working copy. */
typedef struct svn_auth_info_baton_t 
{
  svn_stringbuf_t *username;
  svn_stringbuf_t *password;
  svn_stringbuf_t *path;
  apr_pool_t *pool;
} svn_auth_info_baton_t;

static svn_error_t *
store_auth_info (void *baton)
{
  enum svn_node_kind kind;
  svn_auth_info_baton_t *aibt = (svn_auth_info_baton_t *) baton;

  /* Sanity check */
  SVN_ERR (svn_io_check_path (aibt->path, &kind, aibt->pool));
  if (kind != svn_node_dir)
    return SVN_NO_ERROR;

  /* If present, recursively store the username. */
  if (aibt->username)
    SVN_ERR (svn_wc_set_auth_file (aibt->path, TRUE,
                                   SVN_CLIENT_AUTH_USERNAME, 
                                   aibt->username, aibt->pool));

  /* If present, recursively store the password. */
  if (aibt->password)
    SVN_ERR (svn_wc_set_auth_file (aibt->path, TRUE,
                                   SVN_CLIENT_AUTH_PASSWORD, 
                                   aibt->password, aibt->pool));

  return SVN_NO_ERROR;
}


/* For the SVN_RA_AUTH_USERNAME method.

   This method is used only by `ra_local' right now. */
static svn_error_t *
authorize_username (void **session_baton,
                    svn_ra_plugin_t *ra_lib,
                    svn_stringbuf_t *path,
                    svn_client_auth_t *auth_obj,
                    const void *authenticator,
                    void *auth_baton,
                    apr_pool_t *pool)
{
  apr_uid_t uid;
  apr_gid_t gid;
  apr_status_t status;
  svn_error_t *err;
  svn_stringbuf_t *username;
  svn_boolean_t need_to_store = FALSE;

  svn_ra_username_authenticator_t *auth_vtable =
    (svn_ra_username_authenticator_t *) authenticator;

  /* Try to get username: */

  /* 1. From the application (e.g. commandline args) */
  if (auth_obj->username)
    {
      username = svn_stringbuf_create (auth_obj->username, pool);
      need_to_store = TRUE;
    }
  /* 2. From the working copy */
  else 
    {
      err = svn_wc_get_auth_file (path, SVN_CLIENT_AUTH_USERNAME,
                                  &username, pool);
      if (err)
        {
          /* 3. Else, just use the process owner. */
          char *un;
          status = apr_current_userid (&uid, &gid, pool);
          if (status)
            return svn_error_createf(status, 0, NULL, pool,
                                     "Error getting UID of client process.");
  
          status = apr_get_username (&un, uid, pool);
          if (status)
            return svn_error_createf(status, 0, NULL, pool,
                                     "Error changing UID to username.");

          username = svn_stringbuf_create (un, pool);        
          need_to_store = TRUE;
        }
      else if (err)
        return err;
    }

  /* Send username to the RA layer. */
  SVN_ERR (auth_vtable->set_username (username->data, auth_baton));

  /* Get (and implicitly return) the session baton. */
  SVN_ERR (auth_vtable->authenticate (session_baton, auth_baton));

  if (need_to_store)
    {
      /* Return a callback and baton that will allow the client
         routine to store the auth info in PATH's admin area. */
      svn_auth_info_baton_t *abaton = apr_pcalloc (pool, sizeof(*abaton));
      abaton->username = username;
      abaton->password = NULL;  /* no password to store. */
      abaton->path = path;
      abaton->pool = pool;

      auth_obj->storage_callback = store_auth_info;
      auth_obj->storage_baton = abaton;
    }
  else
    /* No storage needed */
    auth_obj->storage_callback = NULL;

  return SVN_NO_ERROR;
}




/* For the SVN_RA_AUTH_SIMPLE_PASSWORD method.

   This method is used only by `ra_dav' right now. */
static svn_error_t *
authorize_simple_password (void **session_baton,
                           svn_ra_plugin_t *ra_lib,
                           svn_stringbuf_t *path,
                           svn_client_auth_t *auth_obj,
                           const void *authenticator,
                           void *auth_baton,
                           apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stringbuf_t *username, *password;
  svn_boolean_t need_to_store = FALSE;
  svn_ra_simple_password_authenticator_t *auth_vtable =
    (svn_ra_simple_password_authenticator_t *) authenticator;
  
  /* Try to get username: */

  /* 1. From the application (e.g. commandline args) */
  if (auth_obj->username)
    {
      username = svn_stringbuf_create (auth_obj->username, pool);
      need_to_store = TRUE;
    }
  /* 2. From the working copy */
  else 
    {
      err = svn_wc_get_auth_file (path, SVN_CLIENT_AUTH_USERNAME,
                                  &username, pool);
      if (err)
        {
          /* 3. From the user directly, by prompting */
          char *answer;
          SVN_ERR (auth_obj->prompt_callback (&answer, 
                                              "Username: ", 0, 
                                              auth_obj->prompt_baton, pool));
          username = svn_stringbuf_create (answer, pool);
          need_to_store = TRUE;
        }
      else if (err)
        return err;
    }

  /* Try to get password: */

  /* 1. From the application (e.g. commandline args) */
  if (auth_obj->password)
    {
      password = svn_stringbuf_create (auth_obj->password, pool);
      need_to_store = TRUE;
    }
  /* 2. From the working copy */
  else 
    {
      err = svn_wc_get_auth_file (path, SVN_CLIENT_AUTH_PASSWORD,
                                  &password, pool);
      if (err)
        {
          /* 3. From the user directly, by prompting */
          char *answer;
          SVN_ERR (auth_obj->prompt_callback (&answer, 
                                              "Password: ", 1, 
                                              auth_obj->prompt_baton, pool));
          password = svn_stringbuf_create (answer, pool);
          need_to_store = TRUE;
        }
      else if (err)
        return err;
    }

  /* Send username/password to the RA layer. */
  SVN_ERR (auth_vtable->set_username (username->data, auth_baton));
  SVN_ERR (auth_vtable->set_password (password->data, auth_baton));
  
  /* Get (and implicitly return) the session baton. */
  SVN_ERR (auth_vtable->authenticate (session_baton, auth_baton));

  /* If we had to display a prompt to the user... */
  if (need_to_store)
    {
      /* Return a callback and baton that will allow the client
         routine to store the auth info in PATH's admin area. */
      svn_auth_info_baton_t *abaton = apr_pcalloc (pool, sizeof(*abaton));
      abaton->username = username;
      abaton->password = password;
      abaton->path = path;
      abaton->pool = pool;

      auth_obj->storage_callback = store_auth_info;
      auth_obj->storage_baton = abaton;
    }
  else
    /* No storage needed */
    auth_obj->storage_callback = NULL;

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
                         svn_stringbuf_t *path,
                         svn_client_auth_t *auth_obj,
                         apr_pool_t *pool)
{
  const void *obj;
  void *auth_baton;

  /* Search for available authentication methods, moving from simplest
     to most complex. */

  
  /* Simple username-only authentication. */
  if (ra_lib->auth_methods & SVN_RA_AUTH_USERNAME)
    {
      SVN_ERR (ra_lib->get_authenticator (&obj, &auth_baton, repos_URL,
                                          SVN_RA_AUTH_USERNAME, /* method */
                                          pool));
      
      SVN_ERR (authorize_username (session_baton,
                                   ra_lib, path, auth_obj,
                                   obj, auth_baton, pool));
    }

  /* Username and password authentication. */
  else if (ra_lib->auth_methods & SVN_RA_AUTH_SIMPLE_PASSWORD)
    {
      SVN_ERR (ra_lib->get_authenticator (&obj, &auth_baton, repos_URL,
                                          SVN_RA_AUTH_SIMPLE_PASSWORD,
                                          pool));
      
      SVN_ERR (authorize_simple_password (session_baton,
                                          ra_lib, path, auth_obj,
                                          obj, auth_baton, pool));
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


