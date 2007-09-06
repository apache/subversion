/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_client_private.h
 * @brief Subversion-internal client APIs.
 */

#include <apr_pools.h>
#include <apr_tables.h>
#include "svn_types.h"


#ifndef SVN_CLIENT_PRIVATE_H
#define SVN_CLIENT_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Retrieve the copy source of @a path_or_url at @a *revision, and
 * store it in @a *copyfrom_path and @a *copyfrom_rev. If @a
 * path_or_url is not a copy (or doesn't exist), set @a *copyfrom_path
 * and @a *copyfrom_rev to @c NULL and @c SVN_INVALID_REVNUM
 * (respectively).  Use @a pool for all allocations.
 */
svn_error_t *
svn_client__get_copy_source(const char *path_or_url,
                            const svn_opt_revision_t *revision,
                            const char **copyfrom_path,
                            svn_revnum_t *copyfrom_rev,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
