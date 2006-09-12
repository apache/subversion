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


/* Indicates which kind of timestamp to pay attention to.
   See svn_wc__timestamps_equal_p(). */
enum svn_wc__timestamp_kind
{
  svn_wc__text_time = 1,
  svn_wc__prop_time
};


/* Return an SVN_ERR_WC_UNSUPPORTED_FORMAT error if the working copy
 * format WC_FORMAT is unsupported.  PATH is only used in the error
 * message.
 *
 * Use POOL for any temporary allocation.
 */
svn_error_t *
svn_wc__check_format(int wc_format, const char *path, apr_pool_t *pool);


/* Set *EQUAL_P to true if PATH's TIMESTAMP_KIND timestamp is the same as
 * the one recorded in its `entries' file, else to set to false. ADM_ACCESS
 * must be an access baton for PATH.
 *
 * Use POOL for any temporary allocation.
 */
svn_error_t *
svn_wc__timestamps_equal_p(svn_boolean_t *equal_p,
                           const char *path,
                           svn_wc_adm_access_t *adm_access,
                           enum svn_wc__timestamp_kind timestamp_kind,
                           apr_pool_t *pool);


/* Set *MODIFIED_P to true if VERSIONED_FILE is modified with respect
 * to BASE_FILE, or false if it is not.  The comparison compensates
 * for VERSIONED_FILE's eol and keyword properties, but leaves
 * BASE_FILE alone (as though BASE_FILE were a text-base file, which
 * it usually is, only sometimes we're calling this on incoming
 * temporary text-bases).  ADM_ACCESS must be an access baton for
 * VERSIONED_FILE.  If COMPARE_TEXTBASES is false, a clean copy of the
 * versioned file is compared to VERSIONED_FILE.
 *
 * If an error is returned, the effect on *MODIFIED_P is undefined.
 *
 * Use POOL for temporary allocation.
 */
svn_error_t *svn_wc__versioned_file_modcheck(svn_boolean_t *modified_p,
                                             const char *versioned_file,
                                             svn_wc_adm_access_t *adm_access,
                                             const char *base_file,
                                             svn_boolean_t compare_textbases,
                                             apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_QUESTIONS_H */
