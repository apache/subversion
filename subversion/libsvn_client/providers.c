/*
 * simple_wc_provider.c:  an authentication provider which gets/sets
 *                        username/password from the config-dir auth cache.
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


#include "svn_wc.h"
#include "svn_auth.h"
#include "svn_config.h"
#include "svn_client.h"


/* The keys that will be stored on disk */
#define SVN_CLIENT__AUTHFILE_USERNAME_KEY            "username"
#define SVN_CLIENT__AUTHFILE_PASSWORD_KEY            "password"


typedef struct
{
  /* the cred_kind being fetched (see svn_auth.h)*/
  const char *cred_kind;

  /* cache:  realmstring which identifies the credentials file */
  const char *realmstring;

  /* values retrieved from cache. */
  const char *username;
  const char *password;

} provider_baton_t;


/*** Common Helpers ***/

/* Fetch username, and optionally password, from @a parameters or from
 * disk cache, and store in @a *username and @a *password.  Try the
 * parameters first, then try fetching from the current directory as a
 * working copy.  @a password may be null (in which case not used),
 * but @a username may not be null.
 *
 * If there are no creds in @a parameters, and the current directory
 * either is not a working copy or does not contain auth data, then
 * set both creds to null and @a *got_creds to FALSE; else if able to
 * return either a username or password, set @a *got_creds to TRUE.
 *
 * If fetched creds from disk, set @a pb->username and @a pb->password
 * accordingly, so save_creds() can avoid writing out unchanged data
 * to disk.
 */
static svn_error_t *
get_creds (const char **username,
           const char **password,
           svn_boolean_t *got_creds,
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


  /* Set the default return values. */
  *got_creds = FALSE;
  *username = NULL;
  if (password)
    *password = NULL;
  
  /* Try to load simple credentials from a file on disk, based on the
     realmstring.  Don't throw an error, though:  if something went
     wrong reading the file, no big deal.  What really matters is that
     we failed to get the creds, so allow libsvn_auth to try the next
     provider. */
  svn_error_clear (svn_config_read_auth_data (&creds_hash, pb->cred_kind,
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
  if (password)
    *password
      = def_password ? def_password : spassword ? spassword->data : NULL;

  /* If we were asked for a password but didn't get one, then we
     didn't get creds; but if we weren't asked for a password, then a
     username is enough to say we got creds. */
  if (*username && (! password || *password))
    *got_creds = TRUE;

  return SVN_NO_ERROR;
}


static svn_error_t *
save_creds (svn_boolean_t *saved,
            provider_baton_t *pb,
            const char *username,
            const char *password,
            const char *config_dir,
            apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *creds_hash = NULL;

  *saved = FALSE;

  if (strcmp (pb->cred_kind, SVN_AUTH_CRED_SIMPLE) == 0)
    {
      /* If the creds are different from our baton cache, store in hash */
      if ((pb->username && (strcmp (username, pb->username) != 0))
          || (! pb->username)
          || (pb->password && (strcmp (password, pb->password) != 0))
          || (! pb->password))
        {
          creds_hash = apr_hash_make (pool);
          apr_hash_set (creds_hash, SVN_CLIENT__AUTHFILE_USERNAME_KEY,
                        APR_HASH_KEY_STRING,
                        svn_string_create (username, pool));
          apr_hash_set (creds_hash, SVN_CLIENT__AUTHFILE_PASSWORD_KEY,
                        APR_HASH_KEY_STRING,
                        svn_string_create (password, pool));

          /* ...and write to disk. */
          err = svn_config_write_auth_data (creds_hash, pb->cred_kind,
                                            pb->realmstring, config_dir, pool);
          *saved = err ? FALSE : TRUE;
          svn_error_clear (err);
        }
    }

  else if (strcmp (pb->cred_kind, SVN_AUTH_CRED_USERNAME) == 0)
    {
      /* If the creds are different from our baton cache, store in hash */
      if ((pb->username && (strcmp (username, pb->username) != 0))
          || (! pb->username))
        {
          creds_hash = apr_hash_make (pool);
          apr_hash_set (creds_hash, SVN_CLIENT__AUTHFILE_USERNAME_KEY,
                        APR_HASH_KEY_STRING,                        
                        svn_string_create (username, pool));

          /* ...and write to disk. */
          err = svn_config_write_auth_data (creds_hash, pb->cred_kind,
                                            pb->realmstring, config_dir, pool);
          *saved = err ? FALSE : TRUE;
          svn_error_clear (err);
        }
    }

  return SVN_NO_ERROR;
}



/*** Simple Auth (username/password) Provider ***/

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
  svn_boolean_t got_creds;

  if (realmstring)
    pb->realmstring = apr_pstrdup (pool, realmstring);

  SVN_ERR (get_creds (&username, &password, &got_creds, pb, parameters, pool));

  if (got_creds)
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
  const char *no_auth_cache;
  const char *config_dir;

  *saved = FALSE;

  no_auth_cache = apr_hash_get (parameters, 
                                SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                APR_HASH_KEY_STRING);
 
  config_dir = apr_hash_get (parameters,
                             SVN_AUTH_PARAM_CONFIG_DIR,
                             APR_HASH_KEY_STRING);
  
  if (no_auth_cache == NULL)
    SVN_ERR (save_creds (saved, pb, creds->username, creds->password,
                         config_dir, pool));

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
  pb->cred_kind = SVN_AUTH_CRED_SIMPLE;

  po->vtable = &simple_provider;
  po->provider_baton = pb;
  *provider = po;
}


/*** Username-only Provider ***/
static svn_error_t *
username_first_creds (void **credentials,
                      void **iter_baton,
                      void *provider_baton,
                      apr_hash_t *parameters,
                      const char *realmstring,
                      apr_pool_t *pool)
{
  provider_baton_t *pb = provider_baton;
  const char *username;
  svn_boolean_t got_creds;

  if (realmstring)
    pb->realmstring = apr_pstrdup (pool, realmstring);

  SVN_ERR (get_creds (&username, NULL, &got_creds, pb, parameters, pool));

  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof(*creds));
      creds->username = username;
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
username_save_creds (svn_boolean_t *saved,
                     void *credentials,
                     void *provider_baton,
                     apr_hash_t *parameters,
                     apr_pool_t *pool)
{
  svn_auth_cred_simple_t *creds = credentials;
  provider_baton_t *pb = provider_baton;
  const char *no_auth_cache;
  const char *config_dir;

  *saved = FALSE;

  no_auth_cache = apr_hash_get (parameters, 
                                SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                APR_HASH_KEY_STRING);

  config_dir = apr_hash_get (parameters,
                             SVN_AUTH_PARAM_CONFIG_DIR,
                             APR_HASH_KEY_STRING);
  
  if (no_auth_cache == NULL)
    SVN_ERR (save_creds (saved, pb, creds->username, NULL, config_dir, pool));

  return SVN_NO_ERROR;
}


static const svn_auth_provider_t username_provider = {
  SVN_AUTH_CRED_USERNAME,
  username_first_creds,
  NULL,
  username_save_creds
};


/* Public API */
void
svn_client_get_username_provider (svn_auth_provider_object_t **provider,
                                  apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));
  pb->cred_kind = SVN_AUTH_CRED_USERNAME;

  po->vtable = &username_provider;
  po->provider_baton = pb;
  *provider = po;
}
