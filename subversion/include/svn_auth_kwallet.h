/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @file svn_auth_kwallet.h
 * @brief Subversion's authentication system - KWallet
 */

#ifndef SVN_AUTH_KWALLET_H
#define SVN_AUTH_KWALLET_H

#include <apr_pools.h>

#include "svn_auth.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Create and return @a *provider, an authentication provider of type @c
 * svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_client_get_simple_provider(), except that the
 * password is stored in KWallet.
 *
 * @since New in 1.6
 * @note This function actually works only on systems with libsvn_auth_kwallet
 * and KWallet installed.
 */
svn_auth_simple_provider_func_t
svn_auth_get_kwallet_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_KWALLET_H */
