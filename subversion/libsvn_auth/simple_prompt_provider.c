/*
 * simple_prompt_provider.c:  an authentication provider which gets
 *                            username/password by prompting for each.
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


#include "svn_client.h"
#include "svn_auth.h"



typedef struct
{
  /* a callback function/baton that prompts the user */
  svn_auth_prompt_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;

  /* a default username, to try before prompting.  can be NULL. */
  const char *default_username;

  /* a default password, to try before prompting.  can be NULL. */
  const char *default_password;
  
} simple_prompt_provider_baton_t;


typedef struct
{
  /* The original provider baton */
  simple_prompt_provider_baton_t *pb;

  /* how many times we've reprompted */
  int retries;

} simple_prompt_iter_baton_t;


/* Our first attempt will use any default username/password passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
simple_prompt_first_creds (void **credentials,
                           void **iter_baton,
                           void *provider_baton,
                           apr_pool_t *pool)
{
  simple_prompt_provider_baton_t *pb = provider_baton;
  svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof(*creds));
  simple_prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof(*ibaton));
  const char *username, *password;

  if (pb->default_username == NULL)
    {
      SVN_ERR (pb->prompt_func (&username, "username: ",
                                FALSE, /* screen echo ok */
                                pb->prompt_baton, pool));
    }
  else
    {
      username = pb->default_username;
    }

  if (pb->default_password == NULL)
    {
      const char *prompt = apr_psprintf (pool, "%s's password: ", username);
      SVN_ERR (pb->prompt_func (&password, prompt,
                                TRUE, /* don't echo to screen */
                                pb->prompt_baton, pool));
    }
  else
    {
      password = pb->default_password;
    }

  creds->username = username;
  creds->password = password;
  *credentials = creds;

  ibaton->retries = 0;
  ibaton->pb = pb;
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a mai */
static svn_error_t *
simple_prompt_next_creds (void **credentials,
                          void *iter_baton,
                          apr_pool_t *pool)
{
  simple_prompt_iter_baton_t *ib = iter_baton;
  svn_auth_cred_simple_t *creds;
  const char *prompt;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  creds = apr_pcalloc (pool, sizeof(*creds));

  SVN_ERR (ib->pb->prompt_func (&creds->username, "username: ",
                                FALSE, /* screen echo ok */
                                ib->pb->prompt_baton, pool));

  prompt = apr_psprintf (pool, "%s's password: ", creds->username);
  SVN_ERR (ib->pb->prompt_func (&creds->password, prompt,
                                TRUE, /* don't echo to screen */
                                ib->pb->prompt_baton, pool));
  
  *credentials = creds;

  return SVN_NO_ERROR;
}



/* The provider. */
static const svn_auth_provider_t simple_prompt_provider = 
  {
    SVN_AUTH_CRED_SIMPLE,  /* username/passwd creds */
    simple_prompt_first_creds,
    simple_prompt_next_creds,
    NULL                  /* this provider can't save creds. */
  };


/* Public API */
void
svn_auth_get_simple_prompt_provider (const svn_auth_provider_t **provider,
                                     void **provider_baton,
                                     svn_auth_prompt_t prompt_func,
                                     void *prompt_baton,
                                     int retry_limit,
                                     const char *default_username,
                                     const char *default_password,
                                     apr_pool_t *pool)
{
  simple_prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  if (default_username)
    pb->default_username = apr_pstrdup (pool, default_username);
  if (default_password)
    pb->default_password = apr_pstrdup (pool, default_password);

  *provider = &simple_prompt_provider;
  *provider_baton = pb;
}
