/*
 * auth.c:  routines that drive "authenticator" objects received from RA.
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

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_wc.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "client.h"


/*-----------------------------------------------------------------------*/

/* Callback routines that RA libraries use to pull or store auth info. */


/* Set *USERNAME to the username to use for authentication.  
 * BATON is of type `svn_client__callback_baton_t'.
 *
 * If FORCE_PROMPT is true, then prompt the user unless
 * BATON->auth_baton->prompt_callback is null.  Otherwise, try to
 * obtain the information from the working copy, otherwise prompt (but
 * again, don't prompt if BATON->auth_baton->prompt_callback is null.)
 *
 * Allocate *USERNAME in POOL.
 */
static svn_error_t *
get_username (char **username,
              void *baton,
              svn_boolean_t force_prompt,
              apr_pool_t *pool)
{
  svn_stringbuf_t *uname;
  svn_client__callback_baton_t *cb = baton;
  svn_client_auth_baton_t *ab = cb->auth_baton;

  if (force_prompt && ab->prompt_callback)
    {
      char *prompt = apr_psprintf (pool, "username: ");
      SVN_ERR (ab->prompt_callback (username, prompt,
                                    FALSE, /* screen echo ok */
                                    ab->prompt_baton, pool));
      
      ab->got_new_auth_info = TRUE;

      /* Store a copy of the username in the auth_baton too. */
      ab->username = apr_pstrdup (pool, *username);

      return SVN_NO_ERROR;
    }
  else if (ab->username)
    {
      /* The auth_baton already has the value, probably from argv[]. */
      *username = apr_pstrdup (pool, ab->username);
      ab->got_new_auth_info = TRUE;
    }

  /* Else, try to get it from file cached in working copy. */
  else  
    {
      /* If there is a base_dir, and we don't have any problems
         getting the cached username from it, that's the result we'll
         go with.  */
      if ((cb->base_dir)
          && (! svn_wc_get_auth_file (cb->base_dir, 
                                      SVN_CLIENT_AUTH_USERNAME,
                                      &uname, pool)))
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
              svn_error_create(status, 0, NULL, pool,
                               "Error getting UID of process.");
          
          status = apr_get_username (&un, uid, pool);
          if (status)
            return svn_error_create(status, 0, NULL, pool,
                                    "Error in UID->username.");

          /* ### Be nice to avoid this cast... */
          SVN_ERR (svn_utf_cstring_to_utf8 ((const char **)username,
                                            un, NULL, pool));
        }

      /* Store a copy of the username in the auth_baton too. */
      ab->username = apr_pstrdup (pool, *username);
    }

  return SVN_NO_ERROR;
}


/* Set *PASSWORD to the authentication password for USERNAME.
 * BATON is of type `svn_client__callback_baton_t'.
 *
 * If FORCE_PROMPT is true, then prompt the user unless
 * BATON->auth_baton->prompt_callback is null.  Otherwise, try to
 * obtain the information from the working copy, otherwise prompt (but
 * again, don't prompt if BATON->auth_baton->prompt_callback is null.)
 *
 * Allocate *PASSWORD in POOL.
 */
static svn_error_t *
get_password (char **password,
              char *username,
              void *baton,
              svn_boolean_t force_prompt,
              apr_pool_t *pool)
{
  svn_stringbuf_t *pword;
  char *prompt;
  svn_client__callback_baton_t *cb = baton;
  svn_client_auth_baton_t *ab = cb->auth_baton;

  if (strlen(username) > 0)
    prompt = apr_psprintf (pool, "%s's password: ", username);
  else
    prompt = apr_psprintf (pool, "password: ");
  
  if (force_prompt && ab->prompt_callback)
    {
      SVN_ERR (ab->prompt_callback (password, prompt,
                                    TRUE, /* don't echo to the screen */
                                    ab->prompt_baton, pool));
      
      ab->got_new_auth_info = TRUE;

      /* Store a copy of the password in the auth_baton too. */
      ab->password = apr_pstrdup (pool, *password);

      return SVN_NO_ERROR;
    }
  else if (ab->password)
    {
      /* The auth_baton already has the value, probably from argv[]. */
      *password = apr_pstrdup (pool, ab->password);
      ab->got_new_auth_info = TRUE;
    }

  /* Else, try to get it from file cached in working copy. */
  else  
    {
      /* If there is a base_dir, and we don't have any problems
         getting the cached password from it, that's the result we'll
         go with.  */
      if ((cb->base_dir)
          && (! svn_wc_get_auth_file (cb->base_dir, 
                                      SVN_CLIENT_AUTH_PASSWORD,
                                      &pword, pool)))
        *password = pword->data;
      
      else if (ab->prompt_callback)
        {
          /* No file cache?  Then prompt the user. */
          SVN_ERR (ab->prompt_callback (password, prompt,
                                        TRUE, /* don't echo to the screen */
                                        ab->prompt_baton, pool));

          ab->got_new_auth_info = TRUE;
        }
      else
        *password = apr_pstrdup (pool, "");
      
      /* Store a copy of the password in the auth_baton too. */
      ab->password = password;
    }
  
  return SVN_NO_ERROR;
}



/* This matches the get_user_and_pass() prototype in
   `svn_ra_simple_password_authenticator_t'. */
static svn_error_t *
get_user_and_pass (char **username,
                   char **password,
                   void *baton,
                   svn_boolean_t force_prompt,
                   apr_pool_t *pool)
{
  SVN_ERR (get_username (username, baton, force_prompt, pool));
  SVN_ERR (get_password (password, *username, baton, force_prompt, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
store_auth_info (const char *filename,
                 const char *data,
                 svn_client__callback_baton_t *cb)
{
  svn_wc_adm_access_t *adm_access;

  /* ### Fragile!  For a checkout we have no access baton before the checkout
     starts, so base_access is NULL.  However checkout closes it's batons
     before storing auth info so we can open a new baton here. */

  if (! cb->base_access)
    SVN_ERR (svn_wc_adm_open (&adm_access, NULL, cb->base_dir, TRUE, TRUE,
                              cb->pool));
  else
    adm_access = cb->base_access;

  /* Do a recursive store. */
  SVN_ERR (svn_wc_set_auth_file (adm_access, TRUE, filename, 
                                 svn_stringbuf_create (data, cb->pool),
                                 cb->pool));

  if (! cb->base_access)
    SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}


static svn_error_t *
maybe_store_username (const char *username, void *baton)
{
  svn_client__callback_baton_t *cb = baton;
  
  if (cb->auth_baton->store_auth_info)
    return store_auth_info (SVN_CLIENT_AUTH_USERNAME, username, cb);
  else
    return SVN_NO_ERROR;
}


static svn_error_t *
maybe_store_password (const char *password, void *baton)
{
  svn_client__callback_baton_t *cb = baton;
      
  if (cb->auth_baton->store_auth_info)
    {
      /* There's a separate config option for preventing passwords
         from being stored, so check it. */
      struct svn_config_t *cfg;
      const char *val;

      SVN_ERR (svn_config_read_config (&cfg, cb->pool));
      svn_config_get (cfg, &val, "auth", "store_password", "yes");
      
      /* ### Oh, are we really case-sensitive? */
      if (strcmp (val, "yes") == 0)
        return store_auth_info (SVN_CLIENT_AUTH_PASSWORD, password, cb);
    }

  return SVN_NO_ERROR;
}


/* This matches the store_user_and_pass() prototype in
   `svn_ra_simple_password_authenticator_t'. */
static svn_error_t *
store_user_and_pass (void *baton)
{
  svn_client__callback_baton_t *cb = baton;
  
  if (cb->auth_baton->username)
    SVN_ERR (maybe_store_username (cb->auth_baton->username, cb));

  if (cb->auth_baton->password)
    SVN_ERR (maybe_store_password (cb->auth_baton->password, cb));
  
  return SVN_NO_ERROR;
}


svn_error_t * svn_client__get_authenticator (void **authenticator,
                                             void **auth_baton,
                                             enum svn_ra_auth_method method,
                                             void *callback_baton,
                                             apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = callback_baton;

  /* At the moment, the callback_baton *is* the baton needed by the
     authenticator objects.  This may change. */
  *auth_baton = callback_baton;

  /* Return a specific authenticator vtable. */
  switch (method)
    {
    case svn_ra_auth_username:
      {
        svn_ra_username_authenticator_t *ua = apr_pcalloc (pool, sizeof(*ua));

        ua->get_username = get_username;
        if (cb->do_store)
          ua->store_username = maybe_store_username;
        else
          ua->store_username = NULL;

        *authenticator = ua;
        break;
      }

    case svn_ra_auth_simple_password:
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



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */


