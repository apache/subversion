/*
 * auth_example.c: simple demo of svn_auth.c / libsvn_auth API
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


/* A quick test that the libsvn_auth machinery actually works.
   Also an example of how to write an authentication provider (see svn_auth.h)

   On my FreeBSD system, I compile at the commandline with:

   cc -g -o auth_example auth_example.c \
   -I/usr/local/include/subversion-1  -I/usr/local/apache2/include \
   -L/usr/local/lib -L/usr/local/apache2/lib \
   -lapr-0 -lsvn_auth-1 -lsvn_subr-1 -rpath /usr/local/apache2/lib
*/

#include <apr_general.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_auth.h"


/* ------------------------------------------------------------- */
/* A simple provider */

svn_error_t *
prov1_first_creds (void **credentials,
                   void **iter_baton,
                   void *provider_baton,
                   apr_pool_t *pool)
{
  static int i = 0;
  static svn_auth_cred_simple_t creds = { "joe", "89e8txx29" };
  *credentials = &creds;
  *iter_baton = &i;
  return SVN_NO_ERROR;
}

svn_error_t *
prov1_next_creds (void **credentials,
                  void *iter_baton,
                  apr_pool_t *pool)
{
  int *i = (int *) iter_baton;
  static svn_auth_cred_simple_t creds;

  if (*i < 5)
    {
      creds.username = "mary";
      creds.password =  apr_psprintf (pool, "passwd-%d", *i);
      *credentials = &creds;
      (*i)++;
    }
  else
    {
      (*i) = 0;
      *credentials = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
prov1_save_creds (svn_boolean_t *saved,
                  void *credentials,
                  void *provider_baton,
                  apr_pool_t *pool)
{
  /* pretend the save happened. */
  *saved = TRUE;
  return SVN_NO_ERROR;
}

const svn_auth_provider_t provider1 = 
  {
    SVN_AUTH_CRED_SIMPLE,  /* username/passwd creds */
    prov1_first_creds,
    prov1_next_creds,
    prov1_save_creds
  };

/* ------------------------------------------------------------- */
/* Another provider, with only the 'next' function different. */


svn_error_t *
prov2_next_creds (void **credentials,
                  void *iter_baton,
                  apr_pool_t *pool)
{
  int *i = (int *) iter_baton;
  static svn_auth_cred_simple_t creds;

  if (*i < 3)
    {
      creds.username = "phyllis";
      creds.password =  apr_psprintf (pool, "cookie-%d", *i);
      *credentials = &creds;
      (*i)++;
    }
  else
    {
      *credentials = NULL;
    }

  return SVN_NO_ERROR;
}

const svn_auth_provider_t provider2 = 
  {
    SVN_AUTH_CRED_SIMPLE,  /* username/passwd creds */
    prov1_first_creds,
    prov2_next_creds,
    prov1_save_creds
  };



/* ------------------------------------------------------------- */
/* Now use the svn_auth.h API. */

int
main (int argc, const char * const *argv)
{
  svn_error_t *err = NULL;
  apr_pool_t *pool;
  svn_auth_baton_t *auth_baton;
  svn_auth_iterstate_t *state;
  svn_auth_cred_simple_t *creds;

  apr_initialize ();
  pool = svn_pool_create (NULL);

  /* Create the auth_baton and register providers in a certain order. */
  err = svn_auth_open (&auth_baton, pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);

  err = svn_auth_register_provider (auth_baton, 0 /* ignored */,
                                    &provider1, NULL /* no provider baton */,
                                    pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);

  err = svn_auth_register_provider (auth_baton, 0 /* ignored */,
                                    &provider2, NULL /* no provider baton */,
                                    pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);

  
  /* Query the baton for "simple" creds. */
  err = svn_auth_first_credentials ((void **) &creds,
                                    &state, SVN_AUTH_CRED_SIMPLE,
                                    auth_baton, pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);
  
  printf ("First creds back are %s, %s.\n", creds->username, creds->password); 

  /* Keep querying until there are no more creds left. */
  while (creds != NULL)
    {
      err = svn_auth_next_credentials ((void **) &creds, state, pool);
      if (err)
        svn_handle_error (err, stderr, TRUE);
      
      if (creds)
        printf ("Next creds back are %s, %s.\n",
                creds->username, creds->password); 
    }

  return 0;
}
