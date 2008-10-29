/*
 * opt.h: share svn_opt__* functions
 *
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
 */

#ifndef SVN_LIBSVN_SUBR_OPT_H
#define SVN_LIBSVN_SUBR_OPT_H

#include "svn_opt.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Print version info for PGM_NAME.  If QUIET is  true, print in
 * brief.  Else if QUIET is not true, print the version more
 * verbosely, and if FOOTER is non-null, print it following the
 * version information.
 *
 * Use POOL for temporary allocations.
 */
svn_error_t *
svn_opt__print_version_info(const char *pgm_name,
                            const char *footer,
                            svn_boolean_t quiet,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_OPT_H */
