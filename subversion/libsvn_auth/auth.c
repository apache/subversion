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
#include "svn_sorts.h"

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



/* This effectively defines a single table.  Every provider in this
   array returns the same kind of credentials. */
typedef struct
{
  /* ordered array of svn_auth_provider_object_t */
  apr_array_header_t *providers; 
  
} provider_set_t;


/* The main auth baton. */
struct svn_auth_baton_t
{
  /* a collection of tables.  maps cred_kind -> provider_set */
  apr_hash_t *tables;

  /* the pool I'm allocated in. */
  apr_pool_t *pool;

  /* run-time parameters needed by providers. */
  apr_hash_t *parameters;

  /* cache of the last set of credentials returned of each kind.
     maps const char *cred_kind ==> void *.  */
  apr_hash_t *last_creds;

};

/* Abstracted iteration baton */
struct svn_auth_iterstate_t
{
  const char *cred_kind;       /* what kind of creds are we mining? */
  provider_set_t *table;       /* the table being searched */
  int provider_idx;            /* the current provider (row) */
  svn_boolean_t got_first;     /* did we get the provider's first creds? */
  void *provider_iter_baton;   /* the provider's own iteration context */
  svn_auth_baton_t *ab;        /* the original auth_baton. */
};



void
svn_auth_open (svn_auth_baton_t **auth_baton,
               apr_array_header_t *providers,
               apr_pool_t *pool)
{
  svn_auth_baton_t *ab;
  svn_auth_provider_object_t *provider;
  int i;

  /* Build the auth_baton. */
  ab = apr_pcalloc (pool, sizeof (*ab));
  ab->tables = apr_hash_make (pool);
  ab->parameters = apr_hash_make (pool);
  ab->last_creds = apr_hash_make (pool);
  ab->pool = pool;

  /* Register each provider in order.  Providers of different
     credentials will be automatically sorted into different tables by
     register_provider(). */
  for (i = 0; i < providers->nelts; i++)
    {      
      provider_set_t *table;
      provider = APR_ARRAY_IDX(providers, i, svn_auth_provider_object_t *);

      /* Add it to the appropriate table in the auth_baton */
      table = apr_hash_get (ab->tables,
                            provider->vtable->cred_kind, APR_HASH_KEY_STRING);
      if (! table)
        {
          table = apr_pcalloc (pool, sizeof(*table));
          table->providers 
            = apr_array_make (pool, 1, sizeof (svn_auth_provider_object_t *));

          apr_hash_set (ab->tables,
                        provider->vtable->cred_kind, APR_HASH_KEY_STRING,
                        table);
        }  
      *(svn_auth_provider_object_t **)apr_array_push (table->providers) 
        = provider;
    }

  *auth_baton = ab;
}



void 
svn_auth_set_parameter (svn_auth_baton_t *auth_baton,
                        const char *name,
                        const void *value)
{
  apr_hash_set (auth_baton->parameters, name, APR_HASH_KEY_STRING, value);
}

const void * 
svn_auth_get_parameter (svn_auth_baton_t *auth_baton,
                        const char *name)
{
  return apr_hash_get (auth_baton->parameters, name, APR_HASH_KEY_STRING);
}



svn_error_t *
svn_auth_first_credentials (void **credentials,
                            svn_auth_iterstate_t **state,
                            const char *cred_kind,
                            svn_auth_baton_t *auth_baton,
                            apr_pool_t *pool)
{
  int i = 0;
  provider_set_t *table;
  svn_auth_provider_object_t *provider = NULL;
  void *creds = NULL;
  void *iter_baton = NULL;
  svn_boolean_t got_first = FALSE;
  svn_auth_iterstate_t *iterstate;

  /* Get the appropriate table of providers for CRED_KIND. */
  table = apr_hash_get (auth_baton->tables, cred_kind, APR_HASH_KEY_STRING);
  if (! table)
    return svn_error_createf (SVN_ERR_AUTH_NO_PROVIDER, NULL,
                              "No provider registered for '%s' credentials.",
                              cred_kind);

  /* First, see if we have cached creds in the auth_baton. */
  creds = apr_hash_get (auth_baton->last_creds,
                        cred_kind, APR_HASH_KEY_STRING);
  if (creds)
    {
      got_first = FALSE;
    }
  /* If not, find a provider that can give "first" credentials. */
  else
    {
      for (i = 0; i < table->providers->nelts; i++)
        {
          provider = APR_ARRAY_IDX(table->providers, i,
                                   svn_auth_provider_object_t *);
          SVN_ERR (provider->vtable->first_credentials 
                   (&creds, &iter_baton, provider->provider_baton,
                    auth_baton->parameters, auth_baton->pool));
          
          if (creds != NULL)
            {
              got_first = TRUE;
              break;
            }
        }
    }

  if (! creds)
    *state = NULL;
  else
    {
      /* Build an abstract iteration state. */
      iterstate = apr_pcalloc (pool, sizeof(*iterstate));
      iterstate->cred_kind = apr_pstrdup (auth_baton->pool, cred_kind);
      iterstate->table = table;
      iterstate->provider_idx = i;
      iterstate->got_first = got_first;
      iterstate->provider_iter_baton = iter_baton;
      iterstate->ab = auth_baton;
      *state = iterstate;

      /* Have the auth_baton remember the latest creds as well */
      apr_hash_set (auth_baton->last_creds,
                    cred_kind, APR_HASH_KEY_STRING, creds);
    }

  *credentials = creds;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_next_credentials (void **credentials,
                           svn_auth_iterstate_t *state)
{
  svn_auth_provider_object_t *provider;
  provider_set_t *table = state->table;
  void *creds = NULL;

  /* Continue traversing the table from where we left off. */
  for (/* no init */;
       state->provider_idx < table->providers->nelts;
       state->provider_idx++)
    {
      provider = APR_ARRAY_IDX(table->providers,
                               state->provider_idx,
                               svn_auth_provider_object_t *);
      if (! state->got_first)
        {
          SVN_ERR (provider->vtable->first_credentials 
                   (&creds, &(state->provider_iter_baton),
                    provider->provider_baton, state->ab->parameters,
                    state->ab->pool));
          state->got_first = TRUE;
        }
      else
        {
          if (provider->vtable->next_credentials)
            SVN_ERR (provider->vtable->next_credentials 
                     (&creds, state->provider_iter_baton,
                      state->ab->parameters, state->ab->pool));
        }

      if (creds != NULL)
        {
          /* Have the auth_baton remember the latest creds. */
          apr_hash_set (state->ab->last_creds,
                        state->cred_kind, APR_HASH_KEY_STRING, creds);
          break;
        }

      state->got_first = FALSE;
    }

  *credentials = creds;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_save_credentials (svn_auth_iterstate_t *state,
                           apr_pool_t *pool)
{
  int i;
  svn_auth_provider_object_t *provider;
  svn_boolean_t save_succeeded = FALSE;
  void *last_creds;

  if (! state)
    return SVN_NO_ERROR;

  last_creds = apr_hash_get (state->ab->last_creds,
                             state->cred_kind, APR_HASH_KEY_STRING);
  if (! last_creds)
    return SVN_NO_ERROR;

  /* First, try to save the creds using the provider that produced them. */
  provider = APR_ARRAY_IDX (state->table->providers, 
                            state->provider_idx, 
                            svn_auth_provider_object_t *);
  if (provider->vtable->save_credentials)
    SVN_ERR (provider->vtable->save_credentials (&save_succeeded, 
                                                 last_creds,
                                                 provider->provider_baton,
                                                 state->ab->parameters,
                                                 pool));
  if (save_succeeded)
    return SVN_NO_ERROR;

  /* Otherwise, loop from the top of the list, asking every provider
     to attempt a save.  ### todo: someday optimize so we don't
     necessarily start from the top of the list. */
  for (i = 0; i < state->table->providers->nelts; i++)
    {
      provider = APR_ARRAY_IDX(state->table->providers, i,
                               svn_auth_provider_object_t *);
      if (provider->vtable->save_credentials)
        SVN_ERR (provider->vtable->save_credentials 
                 (&save_succeeded, last_creds,
                  provider->provider_baton, state->ab->parameters, pool));

      if (save_succeeded)
        break;
    }

  /* ### notice that at the moment, if no provider can save, there's
     no way the caller will know. */

  return SVN_NO_ERROR;
}
