/*
 * simple_providers.c: providers for SVN_AUTH_CRED_SIMPLE
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



/* Get the username from the OS */
static const char *
get_os_username (apr_pool_t *pool)
{
  const char *username_utf8;
  char *username;
  apr_uid_t uid;
  apr_gid_t gid;

  if (apr_uid_current (&uid, &gid, pool) == APR_SUCCESS &&
      apr_uid_name_get (&username, uid, pool) == APR_SUCCESS)
    {
      svn_error_t *err;
      err = svn_utf_cstring_to_utf8 (&username_utf8, username, pool);
      svn_error_clear (err);
      if (! err)
        return username_utf8;
    }

  return NULL;
}



static svn_error_t *
simple_first_creds (void **credentials,
                    void **iter_baton,
                    void *provider_baton,
                    apr_hash_t *parameters,
                    const char *realmstring,
                    apr_pool_t *pool)
{
  const char *config_dir = apr_hash_get (parameters,
                                         SVN_AUTH_PARAM_CONFIG_DIR,
                                         APR_HASH_KEY_STRING);
  const char *username = apr_hash_get (parameters,
                                       SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                       APR_HASH_KEY_STRING);
  const char *password = apr_hash_get (parameters,
                                       SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                       APR_HASH_KEY_STRING);
  svn_boolean_t may_save = username || password;
  svn_error_t *err;

  /* If we don't have a usename and a password yet, we try the auth cache */
  if (! (username && password))
    {
      apr_hash_t *creds_hash = NULL;

      /* Try to load credentials from a file on disk, based on the
         realmstring.  Don't throw an error, though: if something went
         wrong reading the file, no big deal.  What really matters is that
         we failed to get the creds, so allow the auth system to try the
         next provider. */
      err = svn_config_read_auth_data (&creds_hash, SVN_AUTH_CRED_SIMPLE,
                                       realmstring, config_dir, pool);
      svn_error_clear (err);
      if (! err && creds_hash)
        {
          svn_string_t *str;
          if (! username)
            {
              str = apr_hash_get (creds_hash,
                                  SVN_CLIENT__AUTHFILE_USERNAME_KEY,
                                  APR_HASH_KEY_STRING);
              if (str && str->data)
                username = str->data;
            }

          if (! password)
            {
              str = apr_hash_get (creds_hash,
                                  SVN_CLIENT__AUTHFILE_PASSWORD_KEY,
                                  APR_HASH_KEY_STRING);
              if (str && str->data)
                password = str->data;
            }
        }
    }

  /* Ask the OS for the username if we have a password but no
     username. */
  if (password && ! username)
    username = get_os_username (pool);

  if (username && password)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof(*creds));
      creds->username = username;
      creds->password = password;
      creds->may_save = may_save;
      *credentials = creds;
    }
  else
    *credentials = NULL;

  *iter_baton = NULL;

  return SVN_NO_ERROR;
}


static svn_error_t *
simple_save_creds (svn_boolean_t *saved,
                   void *credentials,
                   void *provider_baton,
                   apr_hash_t *parameters,
                   const char *realmstring,
                   apr_pool_t *pool)
{
  svn_auth_cred_simple_t *creds = credentials;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *err;
  const char *dont_store_passwords =
    apr_hash_get (parameters,
                  SVN_AUTH_PARAM_DONT_STORE_PASSWORDS,
                  APR_HASH_KEY_STRING);

  *saved = FALSE;

  if (! creds->may_save)
    return SVN_NO_ERROR;

  config_dir = apr_hash_get (parameters,
                             SVN_AUTH_PARAM_CONFIG_DIR,
                             APR_HASH_KEY_STRING);

  /* Put the credentials in a hash and save it to disk */
  creds_hash = apr_hash_make (pool);
  apr_hash_set (creds_hash, SVN_CLIENT__AUTHFILE_USERNAME_KEY,
                APR_HASH_KEY_STRING,
                svn_string_create (creds->username, pool));
  if (! dont_store_passwords)
    apr_hash_set (creds_hash, SVN_CLIENT__AUTHFILE_PASSWORD_KEY,
                  APR_HASH_KEY_STRING,
                  svn_string_create (creds->password, pool));
  err = svn_config_write_auth_data (creds_hash, SVN_AUTH_CRED_SIMPLE,
                                    realmstring, config_dir, pool);
  svn_error_clear (err);
  *saved = ! err;

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

  po->vtable = &simple_provider;
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
                         svn_boolean_t may_save,
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

      /* No default username?  Try the auth cache. */
      if (! def_username)
        {
          const char *config_dir = apr_hash_get (parameters,
                                                 SVN_AUTH_PARAM_CONFIG_DIR,
                                                 APR_HASH_KEY_STRING);
          apr_hash_t *creds_hash = NULL;
          svn_string_t *str;
          svn_error_t *err;

          err = svn_config_read_auth_data (&creds_hash, SVN_AUTH_CRED_SIMPLE,
                                           realmstring, config_dir, pool);
          svn_error_clear (err);
          if (! err && creds_hash)
            {
              str = apr_hash_get (creds_hash,
                                  SVN_CLIENT__AUTHFILE_USERNAME_KEY,
                                  APR_HASH_KEY_STRING);
              if (str && str->data)
                def_username = str->data;
            }
        }

      /* Still no default username?  Try the UID. */
      if (! def_username)
        def_username = get_os_username (pool);

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
      (*cred_p)->may_save = TRUE;
    }
  else
    {
      SVN_ERR (pb->prompt_func (cred_p, pb->prompt_baton, realmstring,
                                def_username, may_save, pool));
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
  const char *no_auth_cache = apr_hash_get (parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                            APR_HASH_KEY_STRING);

  SVN_ERR (prompt_for_simple_creds ((svn_auth_cred_simple_t **) credentials_p,
                                    pb, parameters, realmstring, TRUE,
                                    ! no_auth_cache, pool));

  ibaton->retries = 0;
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a maximum of ib->pb->retry_limit. */
static svn_error_t *
simple_prompt_next_creds (void **credentials_p,
                          void *iter_baton,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          apr_pool_t *pool)
{
  simple_prompt_iter_baton_t *ib = iter_baton;
  simple_prompt_provider_baton_t *pb = provider_baton;
  const char *no_auth_cache = apr_hash_get (parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                            APR_HASH_KEY_STRING);

  if (ib->retries >= pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (prompt_for_simple_creds ((svn_auth_cred_simple_t **) credentials_p,
                                    pb, parameters, realmstring, FALSE,
                                    ! no_auth_cache, pool));

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
