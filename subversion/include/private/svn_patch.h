/*
 * svn_patch.h: svnpatch related functions
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

#ifndef SVN_PATCH_H
#define SVN_PATCH_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Output -- Writing */

/* Append a command into @a target in a printf-like fashion.
 * @see svn_ra_svn_write_tuple() for further details with the format. */
svn_error_t *
svn_patch__write_cmd(svn_stream_t *target,
                     apr_pool_t *pool,
                     const char *cmdname,
                     const char *fmt,
                     ...);

/* Input -- Reading */

svn_error_t *
svn_patch__parse_tuple(apr_array_header_t *list,
                       apr_pool_t *pool,
                       const char *fmt,
                       ...);

svn_error_t *
svn_patch__read_tuple(svn_stream_t *from,
                      apr_pool_t *pool,
                      const char *fmt,
                      ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_PATCH_H */
