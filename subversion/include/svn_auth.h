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
 * @brief Interface to Subversion authentication system
 */

#ifndef SVN_AUTH_H
#define SVN_AUTH_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Overview of the svn authentication system.
 *    
 * We define an authentication "provider" as a module that is able to
 * return a specific set of credentials. (e.g. username/password,
 * certificate, etc.)  Each provider implements a vtable that
 *
 * - can fetch initial credentials
 * - can retry the fetch (or try to fetch something different)
 * - can store the credentials for future use
 *
 * For any given type of credentials, there can exist any number of
 * separate providers -- each provider has a different method of
 * fetching. (i.e. from a disk store, by prompting the user, etc.)
 *
 * The application begins by creating an auth baton object, and
 * "registers" some number of providers with the auth baton, in a
 * specific order.  (For example, it may first register a
 * username/password provider that looks in disk store, then register
 * a username/password provider that prompts the user.)
 *
 * Later on, when any svn library is challenged, it asks the auth
 * baton for the specific credentials.  If the initial credentials
 * fail to authenticate, the caller keeps requesting new credentials.
 * Under the hood, libsvn_auth effectively "walks" over each provider
 * (in order of registry), one at a time, until all the providers have
 * exhausted all their retry options.
 *
 * This system allows an application to flexibly define authentication
 * behaviors (by changing registration order), and very easily write
 * new authentication providers.
 *
 * @defgroup auth_fns authentication functions
 * @{
 */


/** The type of a Subversion authentication object */
typedef struct svn_auth_baton_t svn_auth_baton_t;

/** The type of a Subversion authentication-iteration object */
typedef struct svn_auth_iterstate_t svn_auth_iterstate_t;


/** The main authentication "provider" vtable. */
typedef struct
{
  /** The kind of credentials this provider knows how to retrieve. */
  const char *cred_kind;
  
  /** Get an initial set of credentials.
   *
   * Set @a *credentials to a set of valid credentials, or NULL if no
   * credentials are available.  Set @a *iter_baton to context that
   * allows a subsequent call to @c next_credentials, in case the
   * first credentials fail to authenticate.  @a provider_baton is
   * general context for the vtable.
   */
  svn_error_t * (*first_credentials) (void **credentials,
                                      void **iter_baton,
                                      void *provider_baton,
                                      apr_pool_t *pool);

  /** Get a different set of credentials.
   *
   * Set @a *credentials to another set of valid credentials, (using
   * @a iter_baton as the context from previous call to first_credentials
   * or next_credentials).  If no more credentials are available, set
   * @a **credenitals to NULL.  If the provider only has one set of
   * credentials, this function pointer should simply be NULL.
   */
  svn_error_t * (*next_credentials) (void **credentials,
                                     void *iter_baton,
                                     apr_pool_t *pool);
  
  /** Save credentials.
   *
   * Store @a credentials for future use.  @a provider_baton is
   * general context for the vtable.  Set @a *saved to true if the
   * save happened, or false if not.  The provider is not required to
   * save; if it refuses or is unable to save for non-fatal reasons,
   * return false.  If the provider never saves data, then this
   * function pointer should simply be NULL.
   */
  svn_error_t * (*save_credentials) (svn_boolean_t *saved,
                                     void *credentials,
                                     void *provider_baton,
                                     apr_pool_t *pool);
  
} svn_auth_provider_t;


/** Specific types of credentials **/

/** A simple username/password pair. */
#define SVN_AUTH_CRED_SIMPLE "svn:simple"
typedef struct
{
  const char *username;
  const char *password;
  
} svn_auth_cred_simple_t;

/** Just a username. */
#define SVN_AUTH_CRED_USERNAME "svn:username"
typedef struct
{
  const char *username;

} svn_auth_cred_username_t;




/** Initialize an authentication system.
 *
 * Return an authentication object in @a *auth_baton (allocated in @a
 * pool) that represents a particular instance of the svn
 * authentication system.  @a *auth_baton will remember @a pool, and
 * use it to store registered providers.
 */
svn_error_t * svn_auth_open(svn_auth_baton_t **auth_baton,
                            apr_pool_t *pool);

/** Register an authentication provider.
 *
 * Register an authentication provider (defined by @a vtable and @a
 * provider_baton) with @a auth_baton, in the order specified by
 * @a order.  Use @a pool for any temporary allocation.
 */
svn_error_t * svn_auth_register_provider(svn_auth_baton_t *auth_baton,
                                         int order,
                                         const svn_auth_provider_t *vtable,
                                         void *provider_baton,
                                         apr_pool_t *pool);

/** Get an initial set of credentials.
 *
 * Ask @a auth_baton to set @a *credentials to a set of credentials
 * defined by @a cred_kind, or NULL if no credentials are available.
 * Otherwise, return an iteration state in @a *state, so that the
 * caller can call @c svn_auth_next_credentials, in case the first set
 * of credentials fails to authenticate.
 *
 * Use @a pool to allocate @a *state, and for temporary allocation.
 * Note that there is no guarantee about where @a *credentials will be
 * allocated: it might be in @a pool, or it might be in @a
 * auth_baton->pool, depending on the provider.  So safe callers
 * should duplicate the credentials to safe place if they plan to free
 * @a pool.
 */
svn_error_t * svn_auth_first_credentials(void **credentials,
                                         svn_auth_iterstate_t **state,
                                         const char *cred_kind,
                                         svn_auth_baton_t *auth_baton,
                                         apr_pool_t *pool);

/** Get another set of credentials, assuming previous ones failed to
 * authenticate.
 *
 * Use @a state to fetch a different set of @a *credentials, as a
 * follow-up to @c svn_auth_first_credentials or @c
 * svn_auth_next_credentials.  If no more credentials are available,
 * set @a *credentials to NULL.
 *
 * Use @a pool for temporary allocation.  Note that there is no
 * guarantee about where @a *credentials will be allocated: it might
 * be in @a pool, or it might be in @a auth_baton->pool, depending on
 * the provider.  So safe callers should duplicate the credentials to
 * safe place if they plan to free @a pool.
 */
svn_error_t * svn_auth_next_credentials(void **credentials,
                                        svn_auth_iterstate_t *state,
                                        apr_pool_t *pool);

/** Save a set of credentials.
 *
 * Ask @a auth_baton to store @a credentials (of type @a cred_kind)
 * for future use.  Presumably these credentials authenticated
 * successfully.  Use @a pool for temporary allocation.  If no
 * provider is able to store the credentials, return error.
 */
svn_error_t * svn_auth_save_credentials(const char *cred_kind,
                                        void *credentials,
                                        svn_auth_baton_t *auth_baton,
                                        apr_pool_t *pool);

/** @} */


/** General authentication providers */

/** Set @a *provider and @ *provider_baton to an authentication
    provider of type @c svn_auth_cred_simple_t that gets/sets
    information from a working copy directory @a wc_dir.  If an access
    baton for @a wc_dir is already open and available, pass it in @a
    wc_dir_access, else pass NULL. */
void svn_auth_get_simple_wc_provider (const svn_auth_provider_t **provider,
                                      void **provider_baton,
                                      const char *wc_dir,
                                      svn_wc_adm_access_t *wc_dir_access,
                                      apr_pool_t *pool);

/** Set @a *provider and @ *provider_baton to an authentication
    provider of type @c svn_auth_cred_simple_t that gets information
    by prompting the user with @a prompt_func and @a prompt_baton. If
    either @a default_username or @a default_password is non-NULL, the
    argument will be returned when @c svn_auth_first_credentials is
    called. */
void svn_auth_get_simple_prompt_provider (const svn_auth_provider_t **provider,
                                          void **provider_baton,
                                          svn_client_prompt_t prompt_func,
                                          void *prompt_baton,
                                          const char *default_username,
                                          const char *default_password,
                                          apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_H */
