/*
 * simple_providers.c: providers for SVN_AUTH_CRED_SIMPLE
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

#include <apr_pools.h>
#include "svn_client.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_config.h"


/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* The keys that will be stored on disk */
#define SVN_CLIENT__AUTHFILE_USERNAME_KEY            "username"
#define SVN_CLIENT__AUTHFILE_PASSWORD_KEY            "password"


typedef struct
{
  /* cache:  realmstring which identifies the credentials file */
  const char *realmstring;

  /* values retrieved from cache. */
  const char *username;
  const char *password;
} provider_baton_t;



/* Fetch username and password from @a parameters or from disk cache,
 * and store in @a *username and @a *password.  Try the parameters
 * first, then try fetching from the auth system.
 *
 * If fetched creds from disk, set @a pb->username and @a pb->password
 * accordingly, so save_creds() can avoid writing out unchanged data
 * to disk.
 *
 * Return TRUE if both username and password were found, else FALSE.
 */
static svn_boolean_t
get_creds (const char **username,
           const char **password,
           provider_baton_t *pb,
           apr_hash_t *parameters,
           apr_pool_t *pool)
{
  apr_hash_t *creds_hash = NULL;
  svn_string_t *susername = NULL, *spassword = NULL;
  const char *def_username = apr_hash_get (parameters, 
                                           SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                           APR_HASH_KEY_STRING);
  const char *def_password = apr_hash_get (parameters, 
                                           SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                           APR_HASH_KEY_STRING);
  const char *config_dir;

  config_dir = apr_hash_get (parameters,
                             SVN_AUTH_PARAM_CONFIG_DIR,
                             APR_HASH_KEY_STRING);

  /* Try to load credentials from a file on disk, based on the
     realmstring.  Don't throw an error, though: if something went
     wrong reading the file, no big deal.  What really matters is that
     we failed to get the creds, so allow the auth system to try the
     next provider. */
  svn_error_clear (svn_config_read_auth_data (&creds_hash,
                                              SVN_AUTH_CRED_SIMPLE,
                                              pb->realmstring, config_dir,
                                              pool));
  if (creds_hash != NULL)
    {
      if (! def_username)
        susername = apr_hash_get (creds_hash,
                                  SVN_CLIENT__AUTHFILE_USERNAME_KEY,
                                  APR_HASH_KEY_STRING);

      if (! def_password)
        spassword = apr_hash_get (creds_hash,
                                  SVN_CLIENT__AUTHFILE_PASSWORD_KEY,
                                  APR_HASH_KEY_STRING);
    }

  /* If we read values from disk, we want to remember those, so
     we can avoid writing unchanged values back out again (not a
     correctness point, just about efficiency). */
  if (susername && susername->data)
    pb->username = susername->data;
  if (spassword && spassword->data)
    pb->password = spassword->data;
      
  *username = def_username ? def_username : susername ? susername->data : NULL;
  *password = def_password ? def_password : spassword ? spassword->data : NULL;

  return *username && *password;
}


static svn_boolean_t
save_creds (provider_baton_t *pb,
            const char *username,
            const char *password,
            const char *config_dir,
            apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *creds_hash = NULL;

  /* If the creds are already in our baton cache, do not store again */
  if (pb->username && strcmp (username, pb->username) == 0 &&
      pb->password && strcmp (password, pb->password) == 0)
    {
      return TRUE;
    }

  creds_hash = apr_hash_make (pool);
  apr_hash_set (creds_hash, SVN_CLIENT__AUTHFILE_USERNAME_KEY,
                APR_HASH_KEY_STRING,
                svn_string_create (username, pool));
  apr_hash_set (creds_hash, SVN_CLIENT__AUTHFILE_PASSWORD_KEY,
                APR_HASH_KEY_STRING,
                svn_string_create (password, pool));

  /* ...and write to disk. */
  err = svn_config_write_auth_data (creds_hash, SVN_AUTH_CRED_SIMPLE,
                                    pb->realmstring, config_dir, pool);
  svn_error_clear (err);
  return !err;
}


static svn_error_t *
simple_first_creds (void **credentials,
                    void **iter_baton,
                    void *provider_baton,
                    apr_hash_t *parameters,
                    const char *realmstring,
                    apr_pool_t *pool)
{
  provider_baton_t *pb = provider_baton;
  const char *username, *password;

  if (realmstring)
    pb->realmstring = apr_pstrdup (pool, realmstring);

  if (get_creds (&username, &password, pb, parameters, pool))
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof(*creds));
      creds->username = username;
      creds->password = password;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
simple_save_creds (svn_boolean_t *saved,
                   void *credentials,
                   void *provider_baton,
                   apr_hash_t *parameters,
                   apr_pool_t *pool)
{
  svn_auth_cred_simple_t *creds = credentials;
  provider_baton_t *pb = provider_baton;
  const char *config_dir;

  config_dir = apr_hash_get (parameters,
                             SVN_AUTH_PARAM_CONFIG_DIR,
                             APR_HASH_KEY_STRING);
  
  *saved = save_creds (pb, creds->username, creds->password, config_dir, pool);
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_first_creds,
  NULL,
  simple_save_creds
};


/* Public API */
void
svn_client_get_simple_provider (svn_auth_provider_object_t **provider,
                                apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));

  po->vtable = &simple_provider;
  po->provider_baton = pb;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for username/password prompting. */
typedef struct
{
  svn_auth_simple_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;
} simple_prompt_provider_baton_t;


/* Iteration baton type for username/password prompting. */
typedef struct
{
  /* The original provider baton */
  simple_prompt_provider_baton_t *pb;

  /* The original realmstring */
  const char *realmstring;

  /* how many times we've reprompted */
  int retries;
} simple_prompt_iter_baton_t;



/*** Helper Functions ***/
static svn_error_t *
prompt_for_simple_creds (svn_auth_cred_simple_t **cred_p,
                         simple_prompt_provider_baton_t *pb,
                         apr_hash_t *parameters,
                         const char *realmstring,
                         svn_boolean_t first_time,
                         apr_pool_t *pool)
{
  const char *def_username = NULL, *def_password = NULL;

  *cred_p = NULL;

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
         
          if (! apr_uid_current (&uid, &gid, pool)
              && ! apr_uid_name_get (&un, uid, pool))
            SVN_ERR (svn_utf_cstring_to_utf8 (&def_username, un, pool));
        }

      def_password = apr_hash_get (parameters, 
                                   SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                   APR_HASH_KEY_STRING);
    }    

  /* If we have defaults, just build the cred here and return it.
   *
   * ### I do wonder why this is here instead of in a separate
   * ### 'defaults' provider that would run before the prompt
   * ### provider... Hmmm.
   */
  if (def_username && def_password)
    {
      *cred_p = apr_palloc (pool, sizeof (**cred_p));
      (*cred_p)->username = apr_pstrdup (pool, def_username);
      (*cred_p)->password = apr_pstrdup (pool, def_password);
    }
  else
    {
      SVN_ERR (pb->prompt_func (cred_p, pb->prompt_baton,
                                realmstring, def_username, pool));
    }

  return SVN_NO_ERROR;
}


/* Our first attempt will use any default username/password passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
simple_prompt_first_creds (void **credentials_p,
                           void **iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  simple_prompt_provider_baton_t *pb = provider_baton;
  simple_prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof (*ibaton));

  SVN_ERR (prompt_for_simple_creds ((svn_auth_cred_simple_t **) credentials_p,
                                    pb, parameters, realmstring, TRUE, pool));

  ibaton->retries = 0;
  ibaton->pb = pb;
  ibaton->realmstring = apr_pstrdup (pool, realmstring);
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a maximum of ib->pb->retry_limit. */
static svn_error_t *
simple_prompt_next_creds (void **credentials_p,
                          void *iter_baton,
                          apr_hash_t *parameters,
                          apr_pool_t *pool)
{
  simple_prompt_iter_baton_t *ib = iter_baton;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (prompt_for_simple_creds ((svn_auth_cred_simple_t **) credentials_p,
                                    ib->pb, parameters, ib->realmstring, FALSE,
                                    pool));

  return SVN_NO_ERROR;
}


static const svn_auth_provider_t simple_prompt_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_prompt_first_creds,
  simple_prompt_next_creds,
  NULL,
};


/* Public API */
void
svn_client_get_simple_prompt_provider (
  svn_auth_provider_object_t **provider,
  svn_auth_simple_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  simple_prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &simple_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
