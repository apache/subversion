/*
 * auth.c: authentication support functions for Subversion
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


#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_auth.h"

/* The good way to think of this machinery is as a set of tables.

   - Each type of credentials selects a single table.

   - In a given table, each row is a 'provider' capable of returning
     the same type of credentials.  Each column represents a
     provider's repeated attempts to provide credentials.

   When the caller asks for a particular type of credentials, the
   machinery in this file walks over the appropriate table.  It starts
   with the first provider (first row), and calls first_credentials()
   to get the first set of credentials (first column).  If the caller
   is unhappy with the credentials, then each subsequent call to
   next_credentials() traverses the row from left to right.  If the
   provider returns error at any point, then we go to the next provider
   (row).  We continue this way until every provider fails, or
   until the client is happy with the returned credentials.

   Note that the caller cannot see the table traversal, and thus has
   no idea when we switch providers.
*/


/* A provider object. */
typedef struct
{
  const svn_auth_provider_t *vtable;
  void *provider_baton;

} provider_t;

/* This effectively defines a single table.  Every provider in this
   array returns the same kind of credentials. */
typedef struct
{
  apr_array_header_t *providers; /* (ordered) array of provider_t */
  
} provider_set_t;


/* The auth baton contains all of the tables. */
struct svn_auth_baton_t
{
  apr_hash_t *tables;  /* maps cred_kind -> provider_set */
  apr_pool_t *pool;    /* the pool I'm allocated in. */

};

/* Abstracted iteration baton */
struct svn_auth_iterstate_t
{
  provider_set_t *table;       /* the table being searched */
  int provider_idx;            /* the current provider (row) */
  void *provider_iter_baton;   /* the provider's own iteration context */

};


svn_error_t * 
svn_auth_open (svn_auth_baton_t **auth_baton,
               apr_pool_t *pool)
{
  svn_auth_baton_t *ab;

  ab = apr_pcalloc (pool, sizeof (*ab));
  ab->tables = apr_hash_make (pool);
  ab->pool = pool;

  *auth_baton = ab;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_register_provider (svn_auth_baton_t *auth_baton,
                            int order,
                            const svn_auth_provider_t *vtable,
                            void *provider_baton,
                            apr_pool_t *pool)
{
  /* ### ignoring the order argument for now, because it would be
     complex to implement, and I can't see why it's worth it yet.
     can't the caller just register providers in order?

     gjs says: if two subcomponents each register three providers
     with the auth baton, then how do they interleave their providers?
     Specifically, the WC will be registering a provider to get/set
     auth information from the admin directory. The encapsulation
     creates the difficulty with manual ordering. Thus, the order
     should probably stay, but implemented right away? *shrug*
     Note that ARRAY->elts could be passed to qsort().  */
  
  provider_t *provider;
  provider_set_t *table;
  
  /* Create the provider */
  provider = apr_pcalloc (auth_baton->pool, sizeof(*provider));
  provider->vtable = vtable;
  provider->provider_baton = provider_baton;
  
  /* Add it to the appropriate table in the auth_baton */
  table = apr_hash_get (auth_baton->tables,
                        vtable->cred_kind, APR_HASH_KEY_STRING);
  if (! table)
    {
      table = apr_pcalloc (auth_baton->pool, sizeof(*table));
      table->providers = apr_array_make (auth_baton->pool, 1,
                                         sizeof (provider_t *));
      apr_hash_set (auth_baton->tables, vtable->cred_kind, APR_HASH_KEY_STRING,
                    table);
    }  
  *(provider_t **)apr_array_push (table->providers) = provider;
  
  /* ### hmmm, we never used the passed in pool.  maybe we don't need it? */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_first_credentials (void **credentials,
                            svn_auth_iterstate_t **state,
                            const char *cred_kind,
                            svn_auth_baton_t *auth_baton,
                            apr_pool_t *pool)
{
  int i;
  provider_set_t *table;
  provider_t *provider = NULL;
  void *creds = NULL;
  void *iter_baton = NULL;
  svn_auth_iterstate_t *iterstate;

  /* Get the appropriate table of providers for CRED_KIND. */
  table = apr_hash_get (auth_baton->tables, cred_kind, APR_HASH_KEY_STRING);
  if (! table)
    return svn_error_createf (SVN_ERR_AUTH_NO_PROVIDER, NULL,
                              "No provider registered for '%s' credentials.",
                              cred_kind);

  /* Find a provider that can give "first" credentials. */
  for (i = 0; i < table->providers->nelts; i++)
    {
      provider = APR_ARRAY_IDX(table->providers, i, provider_t *);
      SVN_ERR (provider->vtable->first_credentials 
               (&creds, &iter_baton, provider->provider_baton, pool));

      if (creds != NULL)
        break;
    }

  if (! creds)
    *state = NULL;
  else
    {
      /* Build an abstract iteration state. */
      iterstate = apr_pcalloc (pool, sizeof(*iterstate));
      iterstate->table = table;
      iterstate->provider_idx = i;
      iterstate->provider_iter_baton = iter_baton;
      *state = iterstate;
    }

  *credentials = creds;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_next_credentials (void **credentials,
                           svn_auth_iterstate_t *state,
                           apr_pool_t *pool)
{
  provider_t *provider;
  provider_set_t *table = state->table;
  void *creds = NULL;

  /* Continue traversing the table from where we left off. */
  for (/* no init */;
       state->provider_idx < table->providers->nelts;
       state->provider_idx++)
    {
      provider = APR_ARRAY_IDX(table->providers,
                               state->provider_idx,
                               provider_t *);
      if (provider->vtable->next_credentials)
        SVN_ERR (provider->vtable->next_credentials 
                 (&creds, state->provider_iter_baton, pool));

      if (creds != NULL)
        break;
    }

  *credentials = creds;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_save_credentials (const char *cred_kind,
                           void *credentials,
                           svn_auth_baton_t *auth_baton,
                           apr_pool_t *pool)
{
  int i;
  provider_set_t *table;
  provider_t *provider;
  svn_boolean_t save_succeeded = FALSE;

  /* Get the appropriate table of providers for CRED_KIND. */
  table = apr_hash_get (auth_baton->tables, cred_kind, APR_HASH_KEY_STRING);
  if (! table)
    return svn_error_createf (SVN_ERR_AUTH_NO_PROVIDER, NULL,
                              "No provider registered for '%s' credentials.",
                              cred_kind);

  /* Find a provider that can save the credentials. */
  for (i = 0; i < table->providers->nelts; i++)
    {
      provider = APR_ARRAY_IDX(table->providers, i, provider_t *);
      if (provider->vtable->save_credentials)
        SVN_ERR (provider->vtable->save_credentials 
                 (&save_succeeded, credentials,
                  provider->provider_baton, pool));

      if (save_succeeded)
        break;
    }

  /* If all providers failed to save, throw an error. */
  if (! save_succeeded)                  
    return svn_error_createf (SVN_ERR_AUTH_PROVIDERS_EXHAUSTED, NULL,
                              "%d provider(s) failed to save "
                              "'%s' credentials.", i, cred_kind);

  return SVN_NO_ERROR;
}
