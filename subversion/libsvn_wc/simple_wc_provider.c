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
#define SVN_AUTH_SIMPLE_WC_USERNAME            "username"
#define SVN_AUTH_SIMPLE_WC_PASSWORD            "password"


typedef struct
{
  /* the wc directory we're attempting to read/write from */
  const char *base_dir;
  svn_wc_adm_access_t *base_access;
  
} simple_wc_provider_baton_t;


static svn_error_t *
simple_wc_first_creds (void **credentials,
                       void **iter_baton,
                       void *provider_baton,
                       apr_pool_t *pool)
{
  simple_wc_provider_baton_t *pb
    = (simple_wc_provider_baton_t *) provider_baton;
  svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof(*creds));
  svn_error_t *err;
  svn_stringbuf_t *susername, *spassword;

  err = svn_wc_get_auth_file (pb->base_dir, SVN_AUTH_SIMPLE_WC_USERNAME,
                              &susername, pool);
  err = svn_wc_get_auth_file (pb->base_dir, SVN_AUTH_SIMPLE_WC_PASSWORD,
                              &spassword, pool);  
  if (err)
    {
      /* for now, let's not try to distinguish "real" errors from
         situations where the files may simply not be present.  what
         really matters is that we failed to get the creds, so allow
         libsvn_auth to try the next provider.  */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }

  creds->username = susername->data;
  creds->password = spassword->data;
  *credentials = creds;
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


/* Most of this code was stolen right out of
   libsvn_client/auth.c:store_auth_info().  */
static svn_error_t *
simple_wc_save_creds (svn_boolean_t *saved,
                      void *credentials,
                      void *provider_baton,
                      apr_pool_t *pool)
{
  svn_auth_cred_simple_t *creds 
    = (svn_auth_cred_simple_t *) credentials;
  simple_wc_provider_baton_t *pb
    = (simple_wc_provider_baton_t *) provider_baton;

  svn_wc_adm_access_t *adm_access;
  svn_error_t *err;
  int wc_format;

  /* Repository queries (at the moment HEAD to number, but in future date
     to number and maybe others) prior to a checkout will attempt to store
     auth info before the working copy exists.  */
  err = svn_wc_check_wc (pb->base_dir, &wc_format, pool);
  if (err || ! wc_format)
    {
      if (err && err->apr_err == APR_ENOENT)
        {
          svn_error_clear (err);
          *saved = FALSE;
          err = SVN_NO_ERROR;
        }
      return err;
    }

  /* ### Fragile!  For a checkout we have no access baton before the checkout
     starts, so base_access is NULL.  However checkout closes its batons
     before storing auth info so we can open a new baton here.  We don't
     need a write-lock because storing auth data doesn't use log files. */

  if (! pb->base_access)
    SVN_ERR (svn_wc_adm_open (&adm_access, NULL, pb->base_dir, FALSE, TRUE,
                              pool));
  else
    adm_access = pb->base_access;

  /* Do a recursive store of username and password. */
  SVN_ERR (svn_wc_set_auth_file (adm_access, TRUE,
                                 SVN_AUTH_SIMPLE_WC_USERNAME, 
                                 svn_stringbuf_create (creds->username, pool),
                                 pool));
  SVN_ERR (svn_wc_set_auth_file (adm_access, TRUE,
                                 SVN_AUTH_SIMPLE_WC_PASSWORD,
                                 svn_stringbuf_create (creds->password, pool),
                                 pool));

  if (! pb->base_access)
    SVN_ERR (svn_wc_adm_close (adm_access));

  *saved = TRUE;
  return SVN_NO_ERROR;
}



/* The provider. */
static const svn_auth_provider_t simple_wc_provider = 
  {
    SVN_AUTH_CRED_SIMPLE,  /* username/passwd creds */
    simple_wc_first_creds,
    NULL,                  /* do, or do not.  there is no retry. */
    simple_wc_save_creds
  };


/* Public API */
void
svn_wc_get_simple_wc_provider (const svn_auth_provider_t **provider,
                               void **provider_baton,
                               const char *wc_dir,
                               svn_wc_adm_access_t *wc_dir_access,
                               apr_pool_t *pool)
{
  simple_wc_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));
  pb->base_dir = apr_pstrdup (pool, wc_dir);
  pb->base_access = wc_dir_access;

  *provider = &simple_wc_provider;
  *provider_baton = pb;
}
