/**
 * @copyright
 * svn_auth.h: authentication support for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
 * @{
 */

#ifndef SVN_AUTH_H
#define SVN_AUTH_H

#include <apr_pools.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct svn_auth_baton_t svn_auth_baton_t;
typedef struct svn_auth_iterstate_t svn_auth_iterstate_t;



typedef struct
{
    const char *cred_kind;

    svn_error_t * (*first_credentials) (void **credentials,
                                        void **iter_baton,
                                        void *provider_baton,
                                        apr_pool_t *pool);

    svn_error_t * (*next_credentials) (void **credentials,
                                       void *iter_baton,
                                       apr_pool_t *pool);

    svn_error_t * (*save_credentials) (void *credentials,
                                       void *provider_baton,
                                       apr_pool_t *pool);

} svn_auth_provider_t;

#define SVN_AUTH_CRED_SIMPLE "svn:simple"
typedef struct
{
    const char *username;
    const char *password;

} svn_auth_cred_simple_t;


#define SVN_AUTH_CRED_USERNAME "svn:username"
typedef struct
{
    const char *username;

} svn_auth_cred_username_t;


svn_error_t * svn_auth_open(svn_auth_baton_t **baton,
                            apr_pool_t *pool);

svn_error_t * svn_auth_register_provider(svn_auth_baton_t *baton,
                                         int order,
                                         const svn_auth_provider_t *vtable,
                                         void *provider_baton,
                                         apr_pool_t *pool);

svn_error_t * svn_auth_first_credentials(void **credentials,
                                         svn_auth_iterstate_t **state,
                                         const char *cred_kind,
                                         svn_auth_baton_t *baton,
                                         apr_pool_t *pool);

svn_error_t * svn_auth_next_credentials(void **credentials,
                                        svn_auth_iterstate_t *state,
                                        apr_pool_t *pool);

svn_error_t * svn_auth_save_credentials(const char *cred_kind,
                                        void *credentials,
                                        svn_auth_baton_t *baton,
                                        apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */
/** @} */

#endif /* SVN_AUTH_H */
