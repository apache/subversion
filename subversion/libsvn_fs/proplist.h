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
   found in PROPLIST, set *VALUE_P to NULL.

/* Set a local value of property NAME to VALUE for the file or
   directory PATH.

   ### todo (issue #406): name could be const char *, value_p
   svn_string_t instead of svn_stringbuf_t.  */
svn_error_t *svn_fs__get_prop (svn_stringbuf_t **value_p,
                               skel_t *proplist,
                               const svn_string_t *name,
                               apr_pool_t *pool);

/* Set *PROP_HASH to a hash table mapping char * names to
   svn_stringbuf_t * values, based on PROPLIST.  The hash table and
   its name/value pairs are all allocated in POOL.  

   ### todo (issue #406): first of all, the hash should be mapping
   names to svn_string_t's, not svn_stringbuf_t.  Second, the fact
   that the keys are char *'s is inconsistent with other interfaces by
   which we set property names, which all take an svn_string_t or
   svn_stringbuf_t right now (usually the former).  Probably using
   const char * is best -- I mean, who really wants binary property
   names? -- but we need to be consistent about it.  This change would
   affect a lot of functions, not just here.  */
svn_error_t *svn_fs__make_prop_hash (apr_hash_t **prop_hash,
                                     skel_t *proplist,
                                     apr_pool_t *pool);

/* Set the value of the property NAME in PROPLIST to VALUE.  If NAME
   is not found in PROPLIST, add it to the list (with value VALUE).
   If VALUE is NULL, remove the property from the list altogether.  Do
   all necessary allocations in POOL.
   
   ### todo (issue #406): could be const char *name.  */
svn_error_t *svn_fs__set_prop (skel_t *proplist,
                               const svn_string_t *name,
                               const svn_string_t *value,
                               apr_pool_t *pool);


#endif /* SVN_LIBSVN_FS_PROPLIST_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
