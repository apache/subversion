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

#include "client.h"


/*-----------------------------------------------------------------------*/

/* Callback routines that RA libraries use to pull or store auth info. */


static svn_error_t *
get_username (char **username,
              void *auth_baton,
              apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stringbuf_t *uname;

  svn_client_auth_baton_t *ab = 
    (svn_client_auth_baton_t *) auth_baton;
  
  /* Does auth_baton already have the value, received from
     the application (probably from argv[])? */
  if (ab->username)
    *username = apr_pstrdup (pool, ab->username);
  
  else  /* else get it from file cached in working copy. */
    {
      err = svn_wc_get_auth_file (ab->path, 
                                  SVN_CLIENT_AUTH_USERNAME,
                                  &uname, pool);
      if (! err)
        *username = uname->data;
        
      else
        {
          /* No file cache?  Then just use the process owner. */
          char *un;
          apr_uid_t uid;
          apr_gid_t gid;
          apr_status_t status;
          
          status = apr_current_userid (&uid, &gid, pool);
          if (status)
            return 
              svn_error_createf(status, 0, NULL, pool,
                                "Error getting UID of process.");
          
          status = apr_get_username (&un, uid, pool);
          if (status)
            return svn_error_createf(status, 0, NULL, pool,
                                     "Error in UID->username.");
          *username = un;                       
        }
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
get_password (char **password,
              void *auth_baton,
              apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stringbuf_t *pword;

  svn_client_auth_baton_t *ab = 
    (svn_client_auth_baton_t *) auth_baton;
  
  /* Does auth_baton already have the value, received from
     the application (probably from argv[])? */
  if (ab->password)
    *password = apr_pstrdup (pool, ab->password);
  
  else  /* else get it from file cached in working copy. */
    {
      err = svn_wc_get_auth_file (ab->path, 
                                  SVN_CLIENT_AUTH_PASSWORD,
                                  &pword, pool);
      if (! err)
        *password = pword->data;
      
      else
        {
          /* No file cache?  Then prompt the user. */
          SVN_ERR (ab->prompt_callback (password, 
                                        "password: ",
                                        TRUE, /* don't echo to the screen */
                                        ab->prompt_baton, pool));
        }
    }
  
  return SVN_NO_ERROR;
}



static svn_error_t *
get_user_and_pass (char **username,
                   char **password,
                   void *auth_baton,
                   apr_pool_t *pool)
{
  SVN_ERR (get_username (username, auth_baton, pool));
  SVN_ERR (get_password (password, auth_baton, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
store_auth_info (const char *filename,
                 const char *data,
                 svn_stringbuf_t *wc_path,
                 apr_pool_t *pool)
{
  enum svn_node_kind kind;

  /* Sanity check -- store only in a directory. */
  SVN_ERR (svn_io_check_path (wc_path, &kind, pool));
  if (kind != svn_node_dir)
    return SVN_NO_ERROR;  /* ### is this really not an error? */

  /* Do a recursive store. */
  SVN_ERR (svn_wc_set_auth_file (wc_path, TRUE, filename, 
                                 svn_stringbuf_create (data, pool), pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
store_username (const char *username,
                void *auth_baton)
{
  svn_client_auth_baton_t *ab = 
    (svn_client_auth_baton_t *) auth_baton;
  
  return store_auth_info (SVN_CLIENT_AUTH_USERNAME, username,
                          ab->path, ab->pool);
}


static svn_error_t *
store_password (const char *password,
                void *auth_baton)
{
  svn_client_auth_baton_t *ab = 
    (svn_client_auth_baton_t *) auth_baton;
  
  return store_auth_info (SVN_CLIENT_AUTH_PASSWORD, password,
                          ab->path, ab->pool);
}



static svn_error_t *
store_user_and_pass (const char *username,
                     const char *password,
                     void *auth_baton)
{
  SVN_ERR (store_username (username, auth_baton));
  SVN_ERR (store_password (password, auth_baton));

  return SVN_NO_ERROR;
}



/* Retrieve an AUTHENTICATOR/AUTH_BATON pair from the client,
   which represents the protocol METHOD.  */
static svn_error_t *
get_authenticator (void **authenticator,
                   void **auth_baton,
                   apr_uint64_t method,
                   void *callback_baton,
                   apr_pool_t *pool)
{
  svn_client_auth_baton_t *cb = 
    (svn_client_auth_baton_t *) callback_baton;

  /* At the moment, the callback_baton *is* the baton needed by the
     authenticator objects.  This may change. */
  *auth_baton = callback_baton;

  /* Return a specific authenticator vtable. */
  switch (method)
    {
    case SVN_RA_AUTH_USERNAME:
      {
        svn_ra_username_authenticator_t *ua = apr_pcalloc (pool, sizeof(*ua));
        ua->get_username = get_username;
        if (cb->do_store)
          ua->store_username = store_username;
        else
          ua->store_username = NULL;

        *authenticator = ua;
        break;
      }

    case SVN_RA_AUTH_SIMPLE_PASSWORD:
      {
        svn_ra_simple_password_authenticator_t *ua 
          = apr_pcalloc (pool, sizeof(*ua));
        ua->get_user_and_pass = get_user_and_pass;
        if (cb->do_store)
          ua->store_user_and_pass = store_user_and_pass;
        else
          ua->store_user_and_pass = NULL;

        *authenticator = ua;
        break;
      }

    default:
      {
        return svn_error_create (SVN_ERR_RA_UNKNOWN_AUTH, 0, NULL,
                                 pool, "Unknown authenticator requested.");
      }
    }
  
  return SVN_NO_ERROR;
}




svn_error_t *
svn_client__get_ra_callbacks (svn_ra_callbacks_t **callbacks,
                              void **callback_baton,
                              svn_client_auth_baton_t *auth_baton,
                              svn_stringbuf_t *path,
                              svn_boolean_t do_store,
                              apr_pool_t *pool)
{
  svn_ra_callbacks_t *cbtable = 
    (svn_ra_callbacks_t *) apr_pcalloc (pool, sizeof(*cbtable));
  
  cbtable->open_tmp_file = NULL;   /* ### implement in libsvn_wc ! */
  cbtable->close_tmp_file = NULL; /* ### implement in libsvn_wc ! */
  cbtable->get_authenticator = get_authenticator;

  /* Just copy the PATH and DO_STORE into the baton, so callbacks can
     see them later. */
  auth_baton->path = path;
  auth_baton->do_store = do_store;
  
  /* This is humorous; at present, we use the application-provided
     auth_baton as the baton for whole the callbacks-vtable!  This
     might not always be so.  For now, it's just easier that
     svn_client_auth_baton_t be shared by the application and client
     both, rather than wrapping one baton in another. */
  *callback_baton = auth_baton;

  *callbacks = cbtable;
  return SVN_NO_ERROR;
}




/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */


