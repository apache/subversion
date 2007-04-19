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
 * @file svn_mergeinfo_private.h
 * @brief The Subversion Working Copy Library - Internal routines
 *
 * Requires:  
 *            - A working copy
 * 
 * Provides: 
 *            - Ability to manipulate working copy's versioned data.
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:   
 *            - Clients.
 */

#ifndef SVN_MERGEINFO_PRIVATE_H
#define SVN_MERGEINFO_PRIVATE_H

#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Take a hash of mergeinfo in @a mergeinput, and convert it back to
 * a text format mergeinfo in @a output.  If @a input contains no
 * elements, return the empty string.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo__to_string(svn_string_t **output, apr_hash_t *mergeinput,
                         apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_PRIVATE_H */
