/*
 * simple_wc_provider.c:  an authentication provider which gets/sets
 *                        username/password from the wc auth cache.
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


/* Since this provider is solely responsible for reading/writing the
   files in .svn/auth/, then it gets to name the files as well.  */
#define SVN_WC__AUTHFILE_USERNAME            "username"
#define SVN_WC__AUTHFILE_PASSWORD            "password"


typedef struct
{
  /* the wc directory and access baton we're attempting to read/write from */
  const char *base_dir;
  svn_wc_adm_access_t *base_access;

  /* values retrieved from cache. */
  const char *username;
  const char *password;

} provider_baton_t;


/*** Common Helpers ***/

static svn_error_t *
get_creds (const char **username,
           const char **password,
           svn_boolean_t *got_creds,
           provider_baton_t *pb,
           apr_hash_t *parameters,
           apr_pool_t *pool)
{
  svn_stringbuf_t *susername = NULL, *spassword = NULL;
  const char *def_username = apr_hash_get (parameters, 
                                           SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                           APR_HASH_KEY_STRING);
  const char *def_password = apr_hash_get (parameters, 
                                           SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                           APR_HASH_KEY_STRING);
  pb->base_dir = apr_hash_get (parameters, SVN_AUTH_PARAM_SIMPLE_WC_WCDIR,
                               APR_HASH_KEY_STRING);
  pb->base_access = apr_hash_get (parameters,
                                  SVN_AUTH_PARAM_SIMPLE_WC_ACCESS,
                                  APR_HASH_KEY_STRING);

  /* Set the default return values. */
  *got_creds = FALSE;
  if (username)
    *username = NULL;
  if (password)
    *password = NULL;

  if (pb->base_dir)
    {
      svn_error_t *err1 = NULL, *err2 = NULL;

      /* Try to read the cache file data. */
      if (! def_username)
        err1 = svn_wc_get_auth_file (pb->base_dir, 
                                     SVN_WC__AUTHFILE_USERNAME,
                                     &susername, pool);
      if (! def_password)
        err2 = svn_wc_get_auth_file (pb->base_dir, 
                                     SVN_WC__AUTHFILE_PASSWORD,
                                     &spassword, pool);      
      if (err1 || err2)
        {
          /* for now, let's not try to distinguish "real" errors from
             situations where the files may simply not be present.  what
             really matters is that we failed to get the creds, so allow
             libsvn_auth to try the next provider.  */
          return SVN_NO_ERROR;
        }
    }
  
  /* If we read values from the cache, we want to remember those. */
  if (susername && susername->data)
    pb->username = susername->data;
  if (spassword && spassword->data)
    pb->password = spassword->data;
      
  if (username)
    *username
      = def_username ? def_username : susername ? susername->data : NULL;
  if (password)
    *password
      = def_password ? def_password : spassword ? spassword->data : NULL;
  *got_creds = TRUE;
  return SVN_NO_ERROR;
}


static svn_error_t *
save_creds (svn_boolean_t *saved,
            provider_baton_t *pb,
            const char *username,
            const char *password,
            apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_error_t *err;
  int wc_format;

  *saved = FALSE;

  /* Repository queries (at the moment HEAD to number, but in future date
     to number and maybe others) prior to a checkout will attempt to store
     auth info before the working copy exists.  */
  err = svn_wc_check_wc (pb->base_dir, &wc_format, pool);
  if (err || ! wc_format)
    {
      if (err && err->apr_err == APR_ENOENT)
        {
          svn_error_clear (err);
          err = SVN_NO_ERROR;
        }
      return err;
    }

  /* ### Fragile!  For a checkout we have no access baton before the checkout
     starts, so base_access is NULL.  However checkout closes its batons
     before storing auth info so we can open a new baton here.  We don't
     need a write-lock because storing auth data doesn't use log files. */

  if (! pb->base_access)
    SVN_ERR (svn_wc_adm_open (&adm_access, NULL, pb->base_dir, 
                              FALSE, TRUE, pool));
  else
    adm_access = pb->base_access;

  /* Do a recursive store of username and password if the new values
     are different than what we read from the cache, or if we read
     nothing from the cache at all. */
  if (username && 
      ((pb->username && (strcmp (username, pb->username) != 0))
       || (! pb->username)))
    SVN_ERR (svn_wc_set_auth_file (adm_access, TRUE,
                                   SVN_WC__AUTHFILE_USERNAME, 
                                   svn_stringbuf_create (username, pool),
                                   pool));
  if (password && 
      ((pb->password && (strcmp (password, pb->password) != 0))
       || (! pb->password)))
    SVN_ERR (svn_wc_set_auth_file (adm_access, TRUE,
                                   SVN_WC__AUTHFILE_PASSWORD,
                                   svn_stringbuf_create (password, pool),
                                   pool));

  *saved = TRUE;

  if (! pb->base_access)
    SVN_ERR (svn_wc_adm_close (adm_access));
  
  return SVN_NO_ERROR;
}



/*** Simple Auth (username/password) Provider ***/

static svn_error_t *
simple_first_creds (void **credentials,
                    void **iter_baton,
                    void *provider_baton,
                    apr_hash_t *parameters,
                    apr_pool_t *pool)
{
  provider_baton_t *pb = provider_baton;
  const char *username, *password;
  svn_boolean_t got_creds;

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

  *saved = FALSE;

  no_auth_cache = apr_hash_get (parameters, 
                                SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                APR_HASH_KEY_STRING);

  if (pb->base_dir
      && (no_auth_cache == NULL))
    SVN_ERR (save_creds (saved, pb, creds->username, creds->password, pool));
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
svn_wc_get_simple_provider (const svn_auth_provider_t **provider,
                            void **provider_baton,
                            apr_pool_t *pool)
{
  *provider = &simple_provider;
  *provider_baton = apr_pcalloc (pool, sizeof (**provider));;
}


/*** Username-only Provider ***/
static svn_error_t *
username_first_creds (void **credentials,
                      void **iter_baton,
                      void *provider_baton,
                      apr_hash_t *parameters,
                      apr_pool_t *pool)
{
  provider_baton_t *pb = provider_baton;
  const char *username;
  svn_boolean_t got_creds;

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

  *saved = FALSE;

  no_auth_cache = apr_hash_get (parameters, 
                                SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                APR_HASH_KEY_STRING);

  if (pb->base_dir
      && (no_auth_cache == NULL))
    SVN_ERR (save_creds (saved, pb, creds->username, NULL, pool));
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
svn_wc_get_username_provider (const svn_auth_provider_t **provider,
                              void **provider_baton,
                              apr_pool_t *pool)
{
  *provider = &username_provider;
  *provider_baton = apr_pcalloc (pool, sizeof (**provider));;
}
