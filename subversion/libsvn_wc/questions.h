/*
 * questions.h :  asking questions about working copies
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
 */


#ifndef SVN_LIBSVN_WC_QUESTIONS_H
#define SVN_LIBSVN_WC_QUESTIONS_H

#include <apr_pools.h>
#include "svn_types.h"
#include "svn_string.h"
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


/* Set *EQUAL_P to true if PATH's TIMESTAMP_KIND timestamp is the same
 * as the one recorded in its `entries' file, else to set to false.
 *
 * Use POOL for any temporary allocation.
 */
svn_error_t *
svn_wc__timestamps_equal_p (svn_boolean_t *equal_p,
                            svn_stringbuf_t *path,
                            const enum svn_wc__timestamp_kind timestamp_kind,
                            apr_pool_t *pool);


/* Set *SAME to non-zero if file1 and file2 have the same contents,
   else set it to zero. 

   Note: This probably belongs in the svn_io library, however, it
   shares some private helper functions with other wc-specific
   routines.  Moving it to svn_io would not be impossible, merely
   non-trivial.  So far, it hasn't been worth it. */
svn_error_t *svn_wc__files_contents_same_p (svn_boolean_t *same,
                                            svn_stringbuf_t *file1,
                                            svn_stringbuf_t *file2,
                                            apr_pool_t *pool);


/* Set *MODIFIED_P to true if VERSIONED_FILE is modified with respect
 * to BASE_FILE, or false if it is not.  The comparison compensates
 * for VERSIONED_FILE's eol and keyword properties, but leaves
 * BASE_FILE alone (as though BASE_FILE were a text-base file, which
 * it usually is, only sometimes we're calling this on incoming
 * temporary text-bases).
 * 
 * If an error is returned, the effect on *MODIFIED_P is undefined.
 * 
 * Use POOL for temporary allocation.
 */
svn_error_t *svn_wc__versioned_file_modcheck (svn_boolean_t *modified_p,
                                              svn_stringbuf_t *versioned_file,
                                              svn_stringbuf_t *base_file,
                                              apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_QUESTIONS_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
