/* proplist.h :  prototypes for PROPLIST skel-manipulation function.
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

#ifndef SVN_LIBSVN_FS_PROPLIST_H
#define SVN_LIBSVN_FS_PROPLIST_H

#include "svn_fs.h"
#include "skel.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* General PROPLIST skel manipulation functions. */


/* Get the value of the property NAME in PROPLIST, storing it in
   *VALUE_P.  Do all necessary allocations in POOL.  If NAME is not
   found in PROPLIST, set *VALUE_P to NULL.  */
svn_error_t *svn_fs__get_prop (svn_string_t **value_p,
                               skel_t *proplist,
                               const char *name,
                               apr_pool_t *pool);

/* Set *PROP_HASH to a hash table mapping const char * names to
   svn_stringbuf_t * values, based on PROPLIST.  The hash table and
   its name/value pairs are all allocated in POOL.  */
svn_error_t *svn_fs__make_prop_hash (apr_hash_t **prop_hash,
                                     skel_t *proplist,
                                     apr_pool_t *pool);

/* Set the value of the property NAME in PROPLIST to VALUE.  If NAME
   is not found in PROPLIST, add it to the list (with value VALUE).
   If VALUE is NULL, remove the property from the list altogether.  Do
   all necessary allocations in POOL.  */
svn_error_t *svn_fs__set_prop (skel_t *proplist,
                               const char *name,
                               const svn_string_t *value,
                               apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_PROPLIST_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
