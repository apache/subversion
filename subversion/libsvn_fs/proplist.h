/* proplist.h :  prototypes for PROPLIST skel-manipulation function.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_PROPLIST_H
#define SVN_LIBSVN_FS_PROPLIST_H

#include "svn_fs.h"
#include "skel.h"


/* General PROPLIST skel manipulation functions. */


/* Get the value of the property NAME in PROPLIST, storing it in
   *VALUE_P.  Do all necessary allocations in POOL.  If NAME is not
   found in PROPLIST, set *VALUE_P to NULL.  */
svn_error_t *svn_fs__get_prop (svn_stringbuf_t **value_p,
                               skel_t *proplist,
                               svn_stringbuf_t *name,
                               apr_pool_t *pool);

/* Create and return an APR hash *PROP_HASH from the properties and
   values found in PROPLIST, allocating the hash in POOL.  */
svn_error_t *svn_fs__make_prop_hash (apr_hash_t **prop_hash,
                                     skel_t *proplist,
                                     apr_pool_t *pool);

/* Set the value of the property NAME in PROPLIST to VALUE.  If NAME
   is not found in PROPLIST, add it to the list (with value VALUE).
   If VALUE is NULL, remove the property from the list altogether.  Do
   all necessary allocations in POOL.  */
svn_error_t *svn_fs__set_prop (skel_t *proplist,
                               svn_stringbuf_t *name,
                               svn_stringbuf_t *value,
                               apr_pool_t *pool);


#endif /* SVN_LIBSVN_FS_PROPLIST_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
