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
   provider returns NULL at any point, then we go to the next provider
   (row).  We continue this way until every provider is used up, or
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
  const char *cred_kind;      /* the table being traversed */
  int provider_idx;           /* the provider (row) being searched */
  void *provider_iter_baton;  /* the provider's own iteration context */

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
svn_auth_register_provider (svn_auth_baton_t *baton,
                            int order,
                            const svn_auth_provider_t *vtable,
                            void *provider_baton,
                            apr_pool_t *pool)
{
  abort();
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_first_credentials (void **credentials,
                            svn_auth_iterstate_t **state,
                            const char *cred_kind,
                            svn_auth_baton_t *baton,
                            apr_pool_t *pool)
{
  abort();
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_next_credentials (void **credentials,
                           svn_auth_iterstate_t *state,
                           apr_pool_t *pool)
{
  abort();
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth_save_credentials (const char *cred_kind,
                           void *credentials,
                           svn_auth_baton_t *baton,
                           apr_pool_t *pool)
{
  abort();
  return SVN_NO_ERROR;
}
