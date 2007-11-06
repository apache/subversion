/*
 * svn_repos_private.h: Private declarations for repos functionality
 * used internally by non-libsvn_repos* modules.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_REPOS_PRIVATE_H
#define SVN_REPOS_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Convert @a capabilities, a hash table mapping 'const char *' keys to
 * "yes" or "no" values, to a list of all keys whose value is "yes".
 * Return the list, allocated in @a pool, and use @a pool for all
 * temporary allocation.
 */
apr_array_header_t *
svn_repos__capabilities_as_list(apr_hash_t *capabilities, apr_pool_t *pool);

/* Set the client-reported capabilities of @a repos to @a capabilities,
 * which must be allocated in memory at least as long-lived as @a repos.
 */
void
svn_repos__set_capabilities(svn_repos_t *repos,
                            apr_array_header_t *capabilities);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_REPOS_PRIVATE_H */
