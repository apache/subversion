/*
 * auth.c:  routines that drive "authenticator" objects received from RA.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_auth.h"
#include "svn_ra.h"
#include "svn_wc.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "client.h"


/*-----------------------------------------------------------------------*/


svn_error_t *
svn_client__dir_if_wc (const char **dir_p,
                       const char *dir,
                       apr_pool_t *pool)
{
  int wc_format;
  
  SVN_ERR (svn_wc_check_wc (dir, &wc_format, pool));
  
  if (wc_format == 0)
    *dir_p = NULL;
  else
    *dir_p = dir;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__default_auth_dir (const char **auth_dir_p,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    {
      SVN_ERR (svn_client__dir_if_wc (auth_dir_p, path, pool));

      /* Handle unversioned dir in a versioned parent. */
      if (! *auth_dir_p)
        goto try_parent;
    }
  else if ((kind == svn_node_file) || (kind == svn_node_none))
    {
    try_parent:
      svn_path_split (path, auth_dir_p, NULL, pool);
      SVN_ERR (svn_client__dir_if_wc (auth_dir_p, *auth_dir_p, pool));
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         "unknown node kind for `%s'", path);
    }
  
  return SVN_NO_ERROR;
}



typedef struct
{
  /* a callback function/baton that prompts the user */
  svn_client_prompt_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;

} prompt_provider_baton_t;


typedef struct
{
  /* The original provider baton */
  prompt_provider_baton_t *pb;

  /* how many times we've reprompted */
  int retries;

} prompt_iter_baton_t;



/*** Helper Functions ***/

static svn_error_t *
get_creds (const char **username,
           const char **password,
           svn_boolean_t *got_creds,
           prompt_provider_baton_t *pb,
           apr_hash_t *parameters,
           svn_boolean_t first_time,
           apr_pool_t *pool)
{
  const char *prompt_username = NULL, *prompt_password = NULL;
  const char *def_username = NULL, *def_password = NULL;
  
  /* Setup default return values. */
  *got_creds = FALSE;
  if (username)
    *username = NULL;
  if (password)
    *password = NULL;

  /* If we're allowed to check for default usernames and passwords, do
     so. */
  if (first_time)
    {
      def_username = apr_hash_get (parameters, 
                                   SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                   APR_HASH_KEY_STRING);

      /* No default username?  Try the UID. */
      if (! def_username)
        {
          char *un;
          apr_uid_t uid;
          apr_gid_t gid;
          apr_status_t status;
          
          if ((status = apr_uid_current (&uid, &gid, pool)))
            return svn_error_create (status, NULL, "Error getting UID");
          if ((status = apr_uid_name_get (&un, uid, pool)))
            return svn_error_create (status, NULL, "Error getting username");
          SVN_ERR (svn_utf_cstring_to_utf8 ((const char **)&def_username,
                                            un, NULL, pool));
        }

      def_password = apr_hash_get (parameters, 
                                   SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                   APR_HASH_KEY_STRING);
    }    

  /* Get the username. */
  if (def_username)
    {
      prompt_username = def_username;
    }
  else if (username)
    {
      SVN_ERR (pb->prompt_func (&prompt_username, "username: ",
                                FALSE, /* screen echo ok */
                                pb->prompt_baton, pool));
    }

  /* If we have no username, we can go no further. */
  if (! prompt_username)
    return SVN_NO_ERROR;

  /* Get the password. */
  if (def_password)
    {
      prompt_password = def_password;
    }
  else if (password)
    {
      const char *prompt = apr_psprintf (pool, "%s's password: ", 
                                         prompt_username);
      SVN_ERR (pb->prompt_func (&prompt_password, prompt,
                                TRUE, /* don't echo to screen */
                                pb->prompt_baton, pool));
    }

  if (username)
    *username = prompt_username;
  if (password)
    *password = prompt_password;
  *got_creds = TRUE;
  return SVN_NO_ERROR;
}



/*** Simple Prompt Provider ***/

/* Our first attempt will use any default username/password passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
simple_prompt_first_creds (void **credentials,
                           void **iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           apr_pool_t *pool)
{
  prompt_provider_baton_t *pb = provider_baton;
  prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof (*ibaton));
  const char *username, *password;
  svn_boolean_t got_creds;

  SVN_ERR (get_creds (&username, &password, &got_creds, pb,
                      parameters, TRUE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      creds->password = password;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

  ibaton->retries = 0;
  ibaton->pb = pb;
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a maximum of ib->pb->retry_limit. */
static svn_error_t *
simple_prompt_next_creds (void **credentials,
                          void *iter_baton,
                          apr_hash_t *parameters,
                          apr_pool_t *pool)
{
  prompt_iter_baton_t *ib = iter_baton;
  const char *username, *password;
  svn_boolean_t got_creds;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (get_creds (&username, &password, &got_creds, ib->pb,
                      parameters, FALSE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      creds->password = password;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

  return SVN_NO_ERROR;
}


/* Public API */
void
svn_client_get_simple_prompt_provider (const svn_auth_provider_t **provider,
                                       void **provider_baton,
                                       svn_client_prompt_t prompt_func,
                                       void *prompt_baton,
                                        int retry_limit,
                                       apr_pool_t *pool)
{
  prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));
  svn_auth_provider_t *prov = apr_palloc (pool, sizeof (*prov));

  prov->cred_kind = SVN_AUTH_CRED_SIMPLE;
  prov->first_credentials = simple_prompt_first_creds;
  prov->next_credentials = simple_prompt_next_creds;
  prov->save_credentials = NULL;

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  *provider = prov;
  *provider_baton = pb;
}



/*** Username Prompt Provider ***/

/* Our first attempt will use any default username passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
username_prompt_first_creds (void **credentials,
                             void **iter_baton,
                             void *provider_baton,
                             apr_hash_t *parameters,
                             apr_pool_t *pool)
{
  prompt_provider_baton_t *pb = provider_baton;
  prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof (*ibaton));
  const char *username;
  svn_boolean_t got_creds;

  SVN_ERR (get_creds (&username, NULL, &got_creds, pb,
                      parameters, TRUE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

  ibaton->retries = 0;
  ibaton->pb = pb;
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default username
   value, and simply re-prompt for the username, up to a maximum of
   ib->pb->retry_limit. */
static svn_error_t *
username_prompt_next_creds (void **credentials,
                            void *iter_baton,
                            apr_hash_t *parameters,
                            apr_pool_t *pool)
{
  prompt_iter_baton_t *ib = iter_baton;
  const char *username;
  svn_boolean_t got_creds;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (get_creds (&username, NULL, &got_creds, ib->pb,
                      parameters, FALSE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

  return SVN_NO_ERROR;
}


/* Public API */
void
svn_client_get_username_prompt_provider (const svn_auth_provider_t **provider,
                                         void **provider_baton,
                                         svn_client_prompt_t prompt_func,
                                         void *prompt_baton,
                                         int retry_limit,
                                         apr_pool_t *pool)
{
  prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));
  svn_auth_provider_t *prov = apr_palloc (pool, sizeof (*prov));

  prov->cred_kind = SVN_AUTH_CRED_USERNAME;
  prov->first_credentials = username_prompt_first_creds;
  prov->next_credentials = username_prompt_next_creds;
  prov->save_credentials = NULL;

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  *provider = prov;
  *provider_baton = pb;
}
