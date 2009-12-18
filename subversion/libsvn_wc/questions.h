/*
 * questions.h :  asking questions about working copies
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#ifndef SVN_LIBSVN_WC_QUESTIONS_H
#define SVN_LIBSVN_WC_QUESTIONS_H

#include <apr_pools.h>
#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Return an SVN_ERR_WC_UNSUPPORTED_FORMAT error if the working copy
 * format WC_FORMAT is unsupported.  PATH is only used in the error
 * message.
 *
 * Use POOL for any temporary allocation.
 */
svn_error_t *
svn_wc__check_format(int wc_format, const char *path, apr_pool_t *pool);


/* Set *EQUAL_P to true if PATH's timestamp is the same as the one recorded
 * in its `entries' file, else to set to false. ADM_ACCESS must be an access
 * baton for PATH.
 *
 * Use POOL for any temporary allocation.
 */
svn_error_t *
svn_wc__timestamps_equal_p(svn_boolean_t *equal_p,
                           const char *path,
                           svn_wc_adm_access_t *adm_access,
                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_QUESTIONS_H */
