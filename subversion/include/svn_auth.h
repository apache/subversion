/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_auth.h
 * @brief Support for user authentication
 */

#ifndef SVN_AUTH_H
#define SVN_AUTH_H

#include <apr_pools.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*  Overview of the svn authentication system:

    We define an authentication "provider" as a module that is able to
    return a specific set of credentials. (e.g. username/password,
    certificate, etc.)  Each provider implements a vtable that

      1. can fetch initial credentials
      2. can retry the fetch (or try to fetch something different)
      3. can store the credentials for future use

    For any given type of credentials, there can exist any number of
    separate providers -- each provider has a different method of
    fetching. (i.e. from a disk store, by prompting the user, etc.)

    The application begins by creating an auth baton object, and
    "registers" some number of providers with the auth baton, in a
    specific order.  (For example, it may first register a
    username/password provider that looks in disk store, then register
    a username/password provider that prompts the user.)

    Later on, when any svn library is challenged, it asks the auth
    baton for the specific credentials.  If the initial credentials
    fail to authenticate, the caller keeps requesting new credentials.
    Under the hood, libsvn_auth effectively "walks" over each provider
    (in order of registry), one at a time, until all the providers
    have exhausted all their retry options.

    This system allows an application to flexibly define
    authentication behaviors (by changing registration order), and
    very easily write new authentication providers.
*/


typedef struct svn_auth_baton_t svn_auth_baton_t;
typedef struct svn_auth_iterstate_t svn_auth_iterstate_t;


/* The main authentication provider vtable. */
typedef struct
{
  /* A string that describes the kind of credentials this provider
     understands. */
  const char *cred_kind;
  
  /* Set *CREDENTIALS to a set of valid credentials.  If no
     credentials are available, return an error describing why (in
     which case *CREDENTIALS are undefined.)  Set *ITER_BATON to
     context that allows a subsequent call to next_credentials(), in
     case the first credentials fail to authenticate.  PROVIDER_BATON
     is general context for the vtable. */
  svn_error_t * (*first_credentials) (void **credentials,
                                      void **iter_baton,
                                      void *provider_baton,
                                      apr_pool_t *pool);

  /* Set *CREDENTIALS to another set of valid credentials, (using
     ITER_BATON as the context from previous call to first_credentials
     or next_credentials).  If no more credentials are available,
     return an error describing why. */
  svn_error_t * (*next_credentials) (void **credentials,
                                     void *iter_baton,
                                     apr_pool_t *pool);
  
  /* Store CREDENTIALS for future use.  PROVIDER_BATON is general
     context for the vtable.  Return error if unable to save. */
  svn_error_t * (*save_credentials) (void *credentials,
                                     void *provider_baton,
                                     apr_pool_t *pool);
  
} svn_auth_provider_t;

/** Specific types of credentials **/

/* A type of credentials:  a simple username/password pair. */
#define SVN_AUTH_CRED_SIMPLE "svn:simple"
typedef struct
{
  const char *username;
  const char *password;
  
} svn_auth_cred_simple_t;

/* A type of credentials:  just a username. */
#define SVN_AUTH_CRED_USERNAME "svn:username"
typedef struct
{
  const char *username;

} svn_auth_cred_username_t;


/** Public Interface **/

/* Return an authentication object in *AUTH_BATON (allocated in POOL)
   that represents a particular instance of the svn authentication
   system.  *AUTH_BATON will remember POOL, and use it to store
   registered providers. */
svn_error_t * svn_auth_open(svn_auth_baton_t **auth_baton,
                            apr_pool_t *pool);

/* Register an authentication provider (defined by
   VTABLE/PROVIDER_BATON) with AUTH_BATON, in the order specified by
   ORDER.  Use POOL for any temporary allocation. */
svn_error_t * svn_auth_register_provider(svn_auth_baton_t *auth_baton,
                                         int order,
                                         const svn_auth_provider_t *vtable,
                                         void *provider_baton,
                                         apr_pool_t *pool);

/* Ask AUTH_BATON to set *CREDENTIALS to a set of credentials defined
   by CRED_KIND.  If no credentials are available, return error(s)
   explaining why (in which case *CREDENTIALS are undefined.)
   Otherwise, return an iteration state in *STATE, so that the caller
   can call svn_auth_next_credentials(), in case the first set of
   credentials fails to authenticate.

   Use POOL to allocate *STATE, and for temporary allocation.  Note
   that there is no guarantee about where *CREDENTIALS will be
   allocated: it might be in POOL, or it might be in AUTH_BATON->POOL,
   depending on the provider.  So safe callers should duplicate the
   credentials to safe place if they plan to free POOL.
*/
svn_error_t * svn_auth_first_credentials(void **credentials,
                                         svn_auth_iterstate_t **state,
                                         const char *cred_kind,
                                         svn_auth_baton_t *auth_baton,
                                         apr_pool_t *pool);

/* Use STATE to fetch a different set of *CREDENTIALS, as a follow-up
   to svn_auth_first_credentials().  If no more credentials are
   available, return error(s) explaining why (in which case
   *CREDENTIALS are undefined.)

   Use POOL for temporary allocation.  Note that there is no guarantee
   about where *CREDENTIALS will be allocated: it might be in POOL, or
   it might be in AUTH_BATON->POOL, depending on the provider.  So
   safe callers should duplicate the credentials to safe place if they
   plan to free POOL.
*/
svn_error_t * svn_auth_next_credentials(void **credentials,
                                        svn_auth_iterstate_t *state,
                                        apr_pool_t *pool);

/* Ask AUTH_BATON to store CREDENTIALS (of type CRED_KIND) for future
   use.  Presumably these credentials authenticated successfully.  Use
   POOL for temporary allocation. */
svn_error_t * svn_auth_save_credentials(const char *cred_kind,
                                        void *credentials,
                                        svn_auth_baton_t *auth_baton,
                                        apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_H */
